/* ipxwrapper - Winsock functions
 * Copyright (C) 2008 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include <windows.h>
#include <winsock2.h>
#include <wsipx.h>
#include <mswsock.h>
#include <nspapi.h>

#include "winstuff.h"

#include "ipxwrapper.h"
#include "common.h"
#include "interface.h"

INT APIENTRY EnumProtocolsA(LPINT protocols, LPVOID buf, LPDWORD bsptr) {
	int bufsize = *bsptr, rval, i, want_ipx = 0;
	
	PROTOCOL_INFO *pinfo = buf;
	
	rval = r_EnumProtocolsA(protocols, buf, bsptr);
	if(rval == -1) {
		return -1;
	}
	
	if(!protocols) {
		want_ipx = 1;
	}else{
		for(i = 0; protocols[i]; i++) {
			if(protocols[i] == NSPROTO_IPX) {
				want_ipx = 1;
				break;
			}
		}
	}
	
	if(want_ipx) {
		for(i = 0; i < rval; i++) {
			if(pinfo[i].iProtocol == NSPROTO_IPX) {
				break;
			}
		}
		
		if(i == rval) {
			*bsptr += sizeof(PROTOCOL_INFO);
			rval++;
		}
		
		if(*bsptr > bufsize) {
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			return -1;
		}
		
		pinfo[i].dwServiceFlags = 5641;
		pinfo[i].iAddressFamily = AF_IPX;
		pinfo[i].iMaxSockAddr = 16;
		pinfo[i].iMinSockAddr = 14;
		pinfo[i].iSocketType = SOCK_DGRAM;
		pinfo[i].iProtocol = NSPROTO_IPX;
		pinfo[i].dwMessageSize = 576;
		pinfo[i].lpProtocol = "IPX";
	}
	
	return rval;
}

INT APIENTRY EnumProtocolsW(LPINT protocols, LPVOID buf, LPDWORD bsptr) {
	int bufsize = *bsptr, rval, i, want_ipx = 0;
	
	PROTOCOL_INFO *pinfo = buf;
	
	rval = r_EnumProtocolsW(protocols, buf, bsptr);
	if(rval == -1) {
		return -1;
	}
	
	if(!protocols) {
		want_ipx = 1;
	}else{
		for(i = 0; protocols[i]; i++) {
			if(protocols[i] == NSPROTO_IPX) {
				want_ipx = 1;
				break;
			}
		}
	}
	
	if(want_ipx) {
		for(i = 0; i < rval; i++) {
			if(pinfo[i].iProtocol == NSPROTO_IPX) {
				break;
			}
		}
		
		if(i == rval) {
			*bsptr += sizeof(PROTOCOL_INFO);
			rval++;
		}
		
		if(*bsptr > bufsize) {
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			return -1;
		}
		
		pinfo[i].dwServiceFlags = 5641;
		pinfo[i].iAddressFamily = AF_IPX;
		pinfo[i].iMaxSockAddr = 16;
		pinfo[i].iMinSockAddr = 14;
		pinfo[i].iSocketType = SOCK_DGRAM;
		pinfo[i].iProtocol = NSPROTO_IPX;
		pinfo[i].dwMessageSize = 576;
		pinfo[i].lpProtocol = (char*)L"IPX";
	}
	
	return rval;
}

SOCKET WSAAPI socket(int af, int type, int protocol) {
	log_printf("socket(%d, %d, %d)", af, type, protocol);
	
	if(af == AF_IPX) {
		ipx_socket *nsock = malloc(sizeof(ipx_socket));
		if(!nsock) {
			RETURN_WSA(ERROR_OUTOFMEMORY, -1);
		}
		
		nsock->fd = r_socket(AF_INET, SOCK_DGRAM, 0);
		if(nsock->fd == -1) {
			log_printf("Creating fake socket failed: %s", w32_error(WSAGetLastError()));
			
			free(nsock);
			RETURN(-1);
		}
		
		nsock->flags = IPX_SEND | IPX_RECV;
		nsock->s_ptype = (protocol ? NSPROTO_IPX - protocol : 0);
		
		lock_mutex();
		
		nsock->next = sockets;
		sockets = nsock;
		
		log_printf("IPX socket created (fd = %d)", nsock->fd);
		
		RETURN(nsock->fd);
	}else{
		return r_socket(af, type, protocol);
	}
}

int WSAAPI closesocket(SOCKET fd) {
	int ret = r_closesocket(fd);
	
	ipx_socket *ptr = get_socket(fd);
	ipx_socket *pptr = sockets;
	
	if(!ptr) {
		/* Not an IPX socket */
		return ret;
	}
	
	if(ret == SOCKET_ERROR) {
		log_printf("closesocket(%d) failed: %s", fd, w32_error(WSAGetLastError()));
		RETURN(SOCKET_ERROR);
	}
	
	log_printf("IPX socket closed (fd = %d)", fd);
	
	if(ptr == sockets) {
		sockets = ptr->next;
		free(ptr);
	}else{
		while(ptr && pptr->next) {
			if(ptr == pptr->next) {
				pptr->next = ptr->next;
				free(ptr);
			}
			
			pptr = pptr->next;
		}
	}
	
	RETURN(0);
}

int WSAAPI bind(SOCKET fd, const struct sockaddr *addr, int addrlen) {
	ipx_socket *ptr = get_socket(fd);
	
	if(ptr) {
		struct sockaddr_ipx *ipxaddr = (struct sockaddr_ipx*)addr;
		char net_s[12], node_s[18];
		
		NET_TO_STRING(net_s, ipxaddr->sa_netnum);
		NODE_TO_STRING(node_s, ipxaddr->sa_nodenum);
		
		log_printf("bind(%d, net=%s node=%s socket=%hu)", fd, net_s, node_s, ntohs(ipxaddr->sa_socket));
		
		if(ptr->flags & IPX_BOUND) {
			log_printf("bind failed: socket already bound");
			RETURN_WSA(WSAEINVAL, -1);
		}
		
		/* Network number 00:00:00:00 is specified as the "current" network, this code
		 * treats it as a wildcard when used for the network OR node numbers.
		 *
		 * According to MSDN 6, IPX socket numbers are unique to systems rather than
		 * interfaces and as such, the same socket number cannot be bound to more than
		 * one interface, my code lacks any "catch all" address like INADDR_ANY as I have
		 * not found any mentions of an equivalent address for IPX. This means that a
		 * given socket number may only be used on one interface.
		 *
		 * If you know the above information about IPX socket numbers to be incorrect,
		 * PLEASE email me with corrections!
		*/
		
		unsigned char z6[] = {0,0,0,0,0,0};
		struct ipx_interface *nic = nics;
		
		while(nic) {
			if(
				(memcmp(ipxaddr->sa_netnum, nic->ipx_net, 4) == 0 || memcmp(ipxaddr->sa_netnum, z6, 4) == 0) &&
				(memcmp(ipxaddr->sa_nodenum, nic->ipx_node, 6) == 0 || memcmp(ipxaddr->sa_nodenum, z6, 6) == 0)
			) {
				break;
			}
			
			nic = nic->next;
		}
		
		if(!nic) {
			log_printf("bind failed: no such address");
			RETURN_WSA(WSAEADDRNOTAVAIL, -1);
		}
		
		ptr->nic = nic;
		
		if(ipxaddr->sa_socket == 0) {
			/* Automatic socket allocations start at 1024, I have no idea if
			 * this is normal IPX behaviour, but IP does it and it doesn't seem
			 * to interfere with any IPX software I've tested.
			*/
			
			uint16_t s = 1024;
			ipx_socket *socket = sockets;
			
			while(socket) {
				if(ntohs(socket->socket) == s && socket->flags & IPX_BOUND) {
					if(s == 65535) {
						log_printf("bind failed: out of sockets?!");
						RETURN_WSA(WSAEADDRNOTAVAIL, -1);
					}
					
					s++;
					socket = sockets;
					
					continue;
				}
				
				socket = socket->next;
			}
			
			ptr->socket = htons(s);
		}else{
			/* Test if any bound socket is using the requested socket number. */
			
			ipx_socket *socket = sockets;
			
			while(socket) {
				if(socket->socket == ipxaddr->sa_socket && socket->flags & IPX_BOUND) {
					log_printf("bind failed: requested socket in use");
					RETURN_WSA(WSAEADDRINUSE, -1);
				}
				
				socket = socket->next;
			}
			
			ptr->socket = ipxaddr->sa_socket;
		}
		
		NET_TO_STRING(net_s, nic->ipx_net);
		NODE_TO_STRING(node_s, nic->ipx_node);
		
		log_printf("bind address: net=%s node=%s socket=%hu", net_s, node_s, ntohs(ptr->socket));
		
		/* TODO: Bind fake socket in socket() call rather than here?
		 *
		 * I think I put the bind() call for it here so that the fd given to the
		 * program would be in the expected un-bound state, although I'm not sure
		 * if there are any winsock calls it could ONLY make on such a socket.
		*/
		
		struct sockaddr_in bind_addr;
		bind_addr.sin_family = AF_INET;
		bind_addr.sin_port = 0;
		bind_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		
		int rval = r_bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr));
		
		if(rval == 0) {
			ptr->flags |= IPX_BOUND;
		}else{
			log_printf("Binding fake socket failed: %s", w32_error(WSAGetLastError()));
		}
		
		RETURN(rval);
	}else{
		RETURN(r_bind(fd, addr, addrlen));
	}
}

/* Bind extra address of a socket, does not check if address is already in use
 * Attempts to bind socket 0 will really bind socket 0
*/
int ipx_ex_bind(SOCKET fd, const struct sockaddr_ipx *ipxaddr) {
	ipx_socket *ptr = get_socket(fd);
	
	if(!ipxaddr) {
		/* Call with NULL address to remove extra bind */
		log_printf("ipx_ex_bind(%d, NULL)", fd);
		
		ptr->flags &= ~IPX_EX_BOUND;
		RETURN(0);
	}
	
	char net_s[12], node_s[18];
	
	NET_TO_STRING(net_s, ipxaddr->sa_netnum);
	NODE_TO_STRING(node_s, ipxaddr->sa_nodenum);
	
	log_printf("ipx_ex_bind(%d, net=%s node=%s socket=%hu)", fd, net_s, node_s, ntohs(ipxaddr->sa_socket));
	
	if(!(ptr->flags & IPX_BOUND)) {
		log_printf("ipx_ex_bind: Socket is not bound");
		RETURN_WSA(WSAEINVAL, -1);
	}
	
	unsigned char z6[] = {0,0,0,0,0,0};
	struct ipx_interface *nic = nics;
	
	while(nic) {
		if(
			(memcmp(ipxaddr->sa_netnum, nic->ipx_net, 4) == 0 || memcmp(ipxaddr->sa_netnum, z6, 4) == 0) &&
			(memcmp(ipxaddr->sa_nodenum, nic->ipx_node, 6) == 0 || memcmp(ipxaddr->sa_nodenum, z6, 6) == 0)
		) {
			break;
		}
		
		nic = nic->next;
	}
	
	if(!nic) {
		log_printf("ipx_ex_bind: no such address");
		RETURN_WSA(WSAEADDRNOTAVAIL, -1);
	}
	
	NET_TO_STRING(net_s, nic->ipx_net);
	NODE_TO_STRING(node_s, nic->ipx_node);
	
	log_printf("bind address: net=%s node=%s socket=%hu", net_s, node_s, ntohs(ipxaddr->sa_socket));
	
	ptr->ex_nic = nic;
	ptr->ex_socket = ipxaddr->sa_socket;
	ptr->flags |= IPX_EX_BOUND;
	
	RETURN(0);
}

int WSAAPI getsockname(SOCKET fd, struct sockaddr *addr, int *addrlen) {
	struct sockaddr_ipx *ipxaddr = (struct sockaddr_ipx*)addr;
	ipx_socket *ptr = get_socket(fd);
	
	if(ptr) {
		if(ptr->flags & IPX_BOUND) {
			if(*addrlen < sizeof(struct sockaddr_ipx)) {
				*addrlen = sizeof(struct sockaddr_ipx);
				RETURN_WSA(WSAEFAULT, -1);
			}
			
			ipxaddr->sa_family = AF_IPX;
			memcpy(ipxaddr->sa_netnum, ptr->nic->ipx_net, 4);
			memcpy(ipxaddr->sa_nodenum, ptr->nic->ipx_node, 6);
			ipxaddr->sa_socket = ptr->socket;
			
			*addrlen = sizeof(struct sockaddr_ipx);
			
			RETURN(0);
		}else{
			RETURN_WSA(WSAEINVAL, -1);
		}
	}else{
		RETURN(r_getsockname(fd, addr, addrlen));
	}
}

/* Recieve a packet from an IPX socket
 * addr must be NULL or a region of memory big enough for a sockaddr_ipx
 *
 * The mutex should be locked before calling and will be released before returning
 * The size of the packet will be returned on success, even if it was truncated
*/
static int recv_packet(ipx_socket *sockptr, char *buf, int bufsize, int flags, struct sockaddr_ipx *addr) {
	SOCKET fd = sockptr->fd;
	int is_bound = sockptr->flags & IPX_BOUND;
	
	unlock_mutex();
	
	if(!is_bound) {
		WSASetLastError(WSAEINVAL);
		return -1;
	}
	
	struct ipx_packet *packet = malloc(PACKET_BUF_SIZE);
	if(!packet) {
		WSASetLastError(ERROR_OUTOFMEMORY);
		return -1;
	}
	
	int rval = r_recv(fd, (char*)packet, PACKET_BUF_SIZE, flags);
	if(rval == -1) {
		free(packet);
		return -1;
	}
	
	if(addr) {
		addr->sa_family = AF_IPX;
		memcpy(addr->sa_netnum, packet->src_net, 4);
		memcpy(addr->sa_nodenum, packet->src_node, 6);
		addr->sa_socket = packet->src_socket;
	}
	
	memcpy(buf, packet->data, packet->size <= bufsize ? packet->size : bufsize);
	rval = packet->size;
	free(packet);
	
	return rval;
}

int WSAAPI recvfrom(SOCKET fd, char *buf, int len, int flags, struct sockaddr *addr, int *addrlen) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(addr && addrlen && *addrlen < sizeof(struct sockaddr_ipx)) {
			unlock_mutex();
			
			WSASetLastError(WSAEFAULT);
			return -1;
		}
		
		int rval = recv_packet(sockptr, buf, len, flags, (struct sockaddr_ipx*)addr);
		
		/* The value pointed to by addrlen is only set if the recv call was
		 * successful, may not be correct.
		*/
		if(rval >= 0 && addr && addrlen) {
			*addrlen = sizeof(struct sockaddr_ipx);
		}
		
		if(rval > len) {
			WSASetLastError(WSAEMSGSIZE);
			return -1;
		}
		
		return rval;
	}else{
		return r_recvfrom(fd, buf, len, flags, addr, addrlen);
	}
}

int WSAAPI recv(SOCKET fd, char *buf, int len, int flags) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		int rval = recv_packet(sockptr, buf, len, flags, NULL);
		
		if(rval > len) {
			WSASetLastError(WSAEMSGSIZE);
			return -1;
		}
		
		return rval;
	}else{
		return r_recv(fd, buf, len, flags);
	}
}

int PASCAL WSARecvEx(SOCKET fd, char *buf, int len, int *flags) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		int rval = recv_packet(sockptr, buf, len, 0, NULL);
		
		if(rval > len) {
			*flags = MSG_PARTIAL;
			
			/* Wording of MSDN is unclear on what should be returned when
			 * an incomplete message is read, I think it should return the
			 * amount of data copied to the buffer.
			*/
			rval = len;
		}else if(rval != -1) {
			*flags = 0;
		}
		
		return rval;
	}else{
		return r_WSARecvEx(fd, buf, len, flags);
	}
}

#define CHECK_OPTLEN(size) \
	if(*optlen < size) {\
		*optlen = size;\
		RETURN_WSA(WSAEFAULT, -1);\
	}\
	*optlen = size;

int WSAAPI getsockopt(SOCKET fd, int level, int optname, char FAR *optval, int FAR *optlen) {
	int* intval = (int*)optval;
	
	ipx_socket *ptr = get_socket(fd);
	
	if(ptr) {
		if(level == NSPROTO_IPX) {
			if(optname == IPX_PTYPE) {
				CHECK_OPTLEN(sizeof(int));
				*intval = ptr->s_ptype;
				
				RETURN(0);
			}
			
			if(optname == IPX_FILTERPTYPE) {
				CHECK_OPTLEN(sizeof(int));
				*intval = ptr->f_ptype;
				
				RETURN(0);
			}
			
			if(optname == IPX_MAXSIZE) {
				CHECK_OPTLEN(sizeof(int));
				*intval = MAX_PACKET_SIZE;
				
				RETURN(0);
			}
			
			if(optname == IPX_ADDRESS) {
				CHECK_OPTLEN(sizeof(IPX_ADDRESS_DATA));
				
				IPX_ADDRESS_DATA *ipxdata = (IPX_ADDRESS_DATA*)optval;
				
				struct ipx_interface *nic = nics;
				int i = 0;
				
				while(nic && i < ipxdata->adapternum) {
					nic = nic->next;
					i++;
				}
				
				if(!nic) {
					WSASetLastError(ERROR_NO_DATA);
					return -1;
				}
				
				memcpy(ipxdata->netnum, nic->ipx_net, 4);
				memcpy(ipxdata->nodenum, nic->ipx_node, 6);
				
				/* TODO: LAN/WAN detection, link speed detection */
				ipxdata->wan = FALSE;
				ipxdata->status = FALSE;
				ipxdata->maxpkt = MAX_PACKET_SIZE;
				ipxdata->linkspeed = 100000; /* 10MBps */
				
				RETURN(0);
			}
			
			/* NOTE: IPX_MAX_ADAPTER_NUM implies it may be the maximum index
			 * for referencing an IPX interface. This behaviour makes no sense
			 * and a code example in MSDN implies it should be the number of
			 * IPX interfaces, this code follows the latter.
			*/
			if(optname == IPX_MAX_ADAPTER_NUM) {
				CHECK_OPTLEN(sizeof(int));
				
				*intval = 0;
				
				struct ipx_interface *nic = nics;
				while(nic) {
					(*intval)++;
					nic = nic->next;
				}
				
				RETURN(0);
			}
			
			RETURN_WSA(WSAENOPROTOOPT, -1);
		}
	}
	
	RETURN(r_getsockopt(fd, level, optname, optval, optlen));
}

int WSAAPI setsockopt(SOCKET fd, int level, int optname, const char FAR *optval, int optlen) {
	int *intval = (int*)optval;
	BOOL *bval = (BOOL*)optval;
	
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(level == NSPROTO_IPX) {
			if(optname == IPX_PTYPE) {
				sockptr->s_ptype = *intval;
				RETURN(0);
			}
			
			if(optname == IPX_FILTERPTYPE) {
				sockptr->f_ptype = *intval;
				sockptr->flags |= IPX_FILTER;
				
				RETURN(0);
			}
			
			if(optname == IPX_STOPFILTERPTYPE) {
				sockptr->flags &= ~IPX_FILTER;
				RETURN(0);
			}
			
			RETURN_WSA(WSAENOPROTOOPT, -1);
		}
		
		if(level == SOL_SOCKET) {
			if(optname == SO_BROADCAST) {
				if(*bval == TRUE) {
					sockptr->flags |= IPX_BROADCAST;
				}else{
					sockptr->flags &= ~IPX_BROADCAST;
				}
				
				RETURN(0);
			}
		}
	}
	
	RETURN(r_setsockopt(fd, level, optname, optval, optlen));
}

int WSAAPI sendto(SOCKET fd, const char *buf, int len, int flags, const struct sockaddr *addr, int addrlen) {
	struct sockaddr_ipx *ipxaddr = (struct sockaddr_ipx*)addr;
	
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(!addr || addrlen < sizeof(struct sockaddr_ipx)) {
			RETURN_WSA(WSAEDESTADDRREQ, -1);
		}
		
		if(!(sockptr->flags & IPX_SEND)) {
			RETURN_WSA(WSAESHUTDOWN, -1);
		}
		
		if(!(sockptr->flags & IPX_BOUND)) {
			log_printf("sendto() on unbound socket, attempting implicit bind");
			
			struct sockaddr_ipx bind_addr;
			
			bind_addr.sa_family = AF_IPX;
			memcpy(bind_addr.sa_netnum, ipxaddr->sa_netnum, 4);
			memset(bind_addr.sa_nodenum, 0, 6);
			bind_addr.sa_socket = 0;
			
			if(bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1) {
				RETURN(-1);
			}
		}
		
		if(len > MAX_PACKET_SIZE) {
			RETURN_WSA(WSAEMSGSIZE, -1);
		}
		
		int psize = sizeof(ipx_packet)+len-1;
		
		ipx_packet *packet = malloc(psize);
		if(!packet) {
			RETURN_WSA(ERROR_OUTOFMEMORY, -1);
		}
		
		packet->ptype = sockptr->s_ptype;
		
		memcpy(packet->dest_net, ipxaddr->sa_netnum, 4);
		memcpy(packet->dest_node, ipxaddr->sa_nodenum, 6);
		packet->dest_socket = ipxaddr->sa_socket;
		
		unsigned char z6[] = {0,0,0,0,0,0};
		
		if(memcmp(packet->dest_net, z6, 4) == 0) {
			memcpy(packet->dest_net, sockptr->nic->ipx_net, 4);
		}
		
		memcpy(packet->src_net, sockptr->nic->ipx_net, 4);
		memcpy(packet->src_node, sockptr->nic->ipx_node, 6);
		packet->src_socket = sockptr->socket;
		
		packet->size = htons(len);
		memcpy(packet->data, buf, len);
		
		ipx_host *host = find_host(packet->dest_net, packet->dest_node);
		
		struct sockaddr_in saddr;
		saddr.sin_family = AF_INET;
		saddr.sin_port = htons(global_conf.udp_port);
		saddr.sin_addr.s_addr = (host ? host->ipaddr : (global_conf.bcast_all ? INADDR_BROADCAST : sockptr->nic->bcast));
		
		int sval = r_sendto(net_fd, (char*)packet, psize, 0, (struct sockaddr*)&saddr, sizeof(saddr));
		if(sval == -1) {
			len = -1;
		}
		
		free(packet);
		RETURN(len);
	}else{
		RETURN(r_sendto(fd, buf, len, flags, addr, addrlen));
	}
}

int PASCAL shutdown(SOCKET fd, int cmd) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr) {
		if(cmd == SD_SEND || cmd == SD_BOTH) {
			sockptr->flags &= ~IPX_SEND;
		}
		
		if(cmd == SD_RECEIVE || cmd == SD_BOTH) {
			sockptr->flags &= ~IPX_RECV;
		}
		
		RETURN(0);
	}else{
		RETURN(r_shutdown(fd, cmd));
	}
}

int PASCAL ioctlsocket(SOCKET fd, long cmd, u_long *argp) {
	ipx_socket *sockptr = get_socket(fd);
	
	if(sockptr && cmd == FIONREAD) {
		ipx_packet packet;
		fd_set fdset;
		struct timeval tv = {0,0};
		
		FD_ZERO(&fdset);
		FD_SET(sockptr->fd, &fdset);
		
		int r = select(1, &fdset, NULL, NULL, &tv);
		if(r == -1) {
			RETURN(-1);
		}else if(r == 0) {
			*(unsigned long*)argp = 0;
			RETURN(0);
		}
		
		r = r_recv(sockptr->fd, (char*)&packet, sizeof(packet), MSG_PEEK);
		if(r == -1 && WSAGetLastError() != WSAEMSGSIZE) {
			RETURN(-1);
		}
		
		*(unsigned long*)argp = packet.size;
		RETURN(0);
	}else{
		RETURN(r_ioctlsocket(fd, cmd, argp));
	}
}
