/* ipxwrapper - Library functions
 * Copyright (C) 2008-2019 Daniel Collins <solemnwarning@solemnwarning.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#define WINSOCK_API_LINKAGE

#include <winsock2.h>
#include <windows.h>
#include <wsipx.h>
#include <nspapi.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#include "ipxwrapper.h"
#include "common.h"
#include "funcprof.h"
#include "interface.h"
#include "router.h"
#include "addrcache.h"

extern const char *version_string;
extern const char *compile_time;

struct ipaddr_list {
	uint32_t ipaddr;
	struct ipaddr_list *next;
};

ipx_socket *sockets = NULL;
main_config_t main_config;

static CRITICAL_SECTION sockets_cs;

typedef ULONGLONG WINAPI (*GetTickCount64_t)(void);
static HMODULE kernel32 = NULL;

struct FuncStats ipxwrapper_fstats[] = {
	#define FPROF_DECL(func) { #func },
	#include "ipxwrapper_prof_defs.h"
	#undef FPROF_DECL
};

const unsigned int ipxwrapper_fstats_size = sizeof(ipxwrapper_fstats) / sizeof(*ipxwrapper_fstats);

static void init_cs(CRITICAL_SECTION *cs)
{
	if(!InitializeCriticalSectionAndSpinCount(cs, 0x80000000))
	{
		log_printf(LOG_ERROR, "Failed to initialise critical section: %s", w32_error(GetLastError()));
		abort();
	}
}

static HANDLE prof_thread_handle = NULL;
static HANDLE prof_thread_exit = NULL;

static DWORD WINAPI prof_thread_main(LPVOID lpParameter)
{
	static const int PROF_INTERVAL_MS = 10000;
	
	while(WaitForSingleObject(prof_thread_exit, PROF_INTERVAL_MS) == WAIT_TIMEOUT)
	{
		fprof_report(STUBS_DLL_NAME, stub_fstats, NUM_STUBS);
		fprof_report("ipxwrapper.dll", ipxwrapper_fstats, ipxwrapper_fstats_size);
	}
	
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if(fdwReason == DLL_PROCESS_ATTACH)
	{
		fprof_init(stub_fstats, NUM_STUBS);
		fprof_init(ipxwrapper_fstats, ipxwrapper_fstats_size);
		
		log_open("ipxwrapper.log");
		
		log_printf(LOG_INFO, "IPXWrapper %s", version_string);
		log_printf(LOG_INFO, "Compiled at %s", compile_time);
		
		if(!getenv("SystemRoot"))
		{
			log_printf(LOG_WARNING, "SystemRoot is not set in the environment");
			
			char env[268] = "SystemRoot=";
			GetSystemWindowsDirectory(env+11, 256);
			
			log_printf(LOG_INFO, "Setting SystemRoot to '%s'", env+11);
			_putenv(env);
		}
		
		main_config = get_main_config();
		min_log_level = main_config.log_level;
		ipx_use_pcap  = main_config.use_pcap;
		
		if(main_config.fw_except)
		{
			log_printf(LOG_INFO, "Adding exception to Windows Firewall");
			add_self_to_firewall();
		}
		
		addr_cache_init();
		
		ipx_interfaces_init();
		
		init_cs(&sockets_cs);
		
		WSADATA wsdata;
		int err = WSAStartup(MAKEWORD(1,1), &wsdata);
		if(err)
		{
			log_printf(LOG_ERROR, "Failed to initialize winsock: %s", w32_error(err));
			return FALSE;
		}
		
		router_init();
		
		prof_thread_exit = CreateEvent(NULL, FALSE, FALSE, NULL);
		if(prof_thread_exit != NULL)
		{
			prof_thread_handle = CreateThread(
				NULL,               /* lpThreadAttributes */
				0,                  /* dwStackSize */
				&prof_thread_main,  /* lpStartAddress */
				NULL,               /* lpParameter */
				0,                  /* dwCreationFlags */
				NULL);              /* lpThreadId */
			
			if(prof_thread_handle == NULL)
			{
				log_printf(LOG_ERROR,
					"Unable to create prof_thread_main thread: %s",
					w32_error(GetLastError()));
			}
		}
		else{
			log_printf(LOG_ERROR,
				"Unable to create prof_thread_exit event object: %s",
				w32_error(GetLastError()));
		}
	}
	else if(fdwReason == DLL_PROCESS_DETACH)
	{
		/* When the "lpvReserved" parameter is non-NULL, the process is terminating rather
		 * than the DLL being unloaded dynamically and any threads will have been terminated
		 * at unknown points, meaning any global data may be in an inconsistent state and we
		 * cannot (safely) clean up. MSDN states we should do nothing.
		*/
		if(lpvReserved != NULL)
		{
			return TRUE;
		}
		
		if(prof_thread_exit != NULL)
		{
			SetEvent(prof_thread_exit);
			
			if(prof_thread_handle != NULL)
			{
				WaitForSingleObject(prof_thread_handle, INFINITE);
				
				CloseHandle(prof_thread_handle);
				prof_thread_handle = NULL;
			}
			
			CloseHandle(prof_thread_exit);
			prof_thread_exit = NULL;
		}
		
		router_cleanup();
		
		WSACleanup();
		
		DeleteCriticalSection(&sockets_cs);
		
		ipx_interfaces_cleanup();
		
		addr_cache_cleanup();
		
		unload_dlls();
		
		fprof_report(STUBS_DLL_NAME, stub_fstats, NUM_STUBS);
		fprof_report("ipxwrapper.dll", ipxwrapper_fstats, ipxwrapper_fstats_size);
		
		log_close();
		
		if(kernel32)
		{
			FreeLibrary(kernel32);
			kernel32 = NULL;
		}
		
		fprof_cleanup(ipxwrapper_fstats, ipxwrapper_fstats_size);
		fprof_cleanup(stub_fstats, NUM_STUBS);
	}
	
	return TRUE;
}

/* Lock the sockets table and search for one by file descriptor.
 * 
 * Returns an ipx_socket pointer on success, unlocks the sockets table and
 * returns NULL if no match is found.
*/
ipx_socket *get_socket(SOCKET sockfd)
{
	lock_sockets();
	
	ipx_socket *sock;
	HASH_FIND_INT(sockets, &sockfd, sock);
	
	if(!sock)
	{
		unlock_sockets();
	}
	
	return sock;
}

/* Lock the mutex */
void lock_sockets(void)
{
	FPROF_RECORD_SCOPE(&(ipxwrapper_fstats[IPXWRAPPER_FSTATS_lock_sockets]));
	EnterCriticalSection(&sockets_cs);
}

/* Unlock the mutex */
void unlock_sockets(void)
{
	LeaveCriticalSection(&sockets_cs);
}

uint64_t get_ticks(void)
{
	static GetTickCount64_t GetTickCount64 = NULL;
	
	if(!kernel32 && (kernel32 = LoadLibrary("kernel32.dll")))
	{
		GetTickCount64 = (GetTickCount64_t)(GetProcAddress(kernel32, "GetTickCount64"));
	}
	
	if(GetTickCount64)
	{
		return GetTickCount64();
	}
	else{
		return GetTickCount();
	}
}
