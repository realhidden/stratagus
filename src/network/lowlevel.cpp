//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \\   __\_  __ \__  \\   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/**@name lowlevel.cpp - The network lowlevel. */
//
//      (c) Copyright 2000-2007 by Lutz Sammer and Jimmy Salmon
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//      02111-1307, USA.
//

//@{

//----------------------------------------------------------------------------
//  Includes
//----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifndef _MSC_VER
#include <signal.h>
#endif

#include "stratagus.h"
#include "net_lowlevel.h"
#include "network.h"

//----------------------------------------------------------------------------
//  Declarations
//----------------------------------------------------------------------------

#define MAX_LOC_IP 10

#if defined(_MSC_VER) || defined(__MINGW32__)
typedef const char *setsockopttype;
typedef char *recvfrombuftype;
typedef char *recvbuftype;
typedef const char *sendtobuftype;
typedef const char *sendbuftype;
typedef int socklen_t;
#else
typedef const void *setsockopttype;
typedef void *recvfrombuftype;
typedef void *recvbuftype;
typedef const void *sendtobuftype;
typedef const void *sendbuftype;
#endif

//----------------------------------------------------------------------------
//  Variables
//----------------------------------------------------------------------------

int NetLastSocket;         /// Last socket
unsigned long NetLastHost; /// Last host number (net format)
int NetLastPort;           /// Last port number (net format)

unsigned long NetLocalAddrs[MAX_LOC_IP]; /// Local IP-Addrs of this host (net format)

//----------------------------------------------------------------------------
//  Low level functions
//----------------------------------------------------------------------------

#ifdef USE_WINSOCK // {

/**
**  Hardware dependend network init.
*/
int NetInit()
{
	WSADATA wsaData;

	// Start up the windows networking
	if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
		fprintf(stderr, "Couldn't initialize Winsock 2\n");
		return -1;
	}
	return 0;
}

/**
**  Hardware dependend network exit.
*/
void NetExit()
{
	// Clean up windows networking
	if (WSACleanup() == SOCKET_ERROR) {
		if (WSAGetLastError() == WSAEINPROGRESS) {
			WSACancelBlockingCall();
			WSACleanup();
		}
	}
}

/**
**  Close an UDP socket port.
**
**  @param sockfd  Socket fildes
*/
void NetCloseUDP(Socket sockfd)
{
	closesocket(sockfd);
}

/**
**  Close a TCP socket port.
**
**  @param sockfd  Socket fildes
*/
void NetCloseTCP(Socket sockfd)
{
	closesocket(sockfd);
}

#endif // } !USE_WINSOCK

#if !defined(USE_WINSOCK) // {

/**
**  Hardware dependend network init.
*/
int NetInit()
{
	return 0;
}

/**
**  Hardware dependend network exit.
*/
void NetExit()
{
}

/**
**  Close an UDP socket port.
**
**  @param sockfd  Socket fildes
*/
void NetCloseUDP(Socket sockfd)
{
	close(sockfd);
}

/**
**  Close a TCP socket port.
**
**  @param sockfd  Socket fildes
*/
void NetCloseTCP(Socket sockfd)
{
	close(sockfd);
}

#endif // } !USE_WINSOCK

/**
**  Set socket to non-blocking.
**
**  @param sockfd  Socket
**
**  @return 0 for success, -1 for error
*/
#ifdef USE_WINSOCK
int NetSetNonBlocking(Socket sockfd)
{
	unsigned long opt = 1;
	return ioctlsocket(sockfd, FIONBIO, &opt);
}
#else
int NetSetNonBlocking(Socket sockfd)
{
	int flags = fcntl(sockfd, F_GETFL, 0);
	return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}
#endif

/**
**  Resolve host in name or dotted quad notation.
**
**  @param host  Host name (f.e. 192.168.0.0 or stratagus.net)
*/
unsigned long NetResolveHost(const std::string &host)
{
	unsigned long addr;

	if (!host.empty()) {
		addr = inet_addr(host.c_str()); // try dot notation
		if (addr == INADDR_NONE) {
			struct hostent *he;

			he = 0;
			he = gethostbyname(host.c_str());
			if (he) {
				addr = 0;
				Assert(he->h_length == 4);
				memcpy(&addr, he->h_addr, he->h_length);
			}
		}
		return addr;
	}
	return INADDR_NONE;
}

/**
**  Get IP-addrs of local interfaces from Network file descriptor
**  and store them in the NetLocalAddrs array.
**
**  @param sock local socket.
**
**  @return number of IP-addrs found.
*/
#ifdef USE_WINSOCK // {
// ARI: MS documented this for winsock2, so I finally found it..
// I also found a way for winsock1.1 (= win95), but
// that one was too complex to start with.. -> trouble
// Lookout for INTRFC.EXE on the MS web site...
int NetSocketAddr(const Socket sock)
{
	INTERFACE_INFO localAddr[MAX_LOC_IP];  // Assume there will be no more than MAX_LOC_IP interfaces
	DWORD bytesReturned;
	SOCKADDR_IN *pAddrInet;
	u_long SetFlags;
	int nif;
	int wsError;
	int numLocalAddr;

	nif = 0;
	if (sock != static_cast<Socket>(-1)) {
		wsError = WSAIoctl(sock, SIO_GET_INTERFACE_LIST, NULL, 0, &localAddr,
						   sizeof(localAddr), &bytesReturned, NULL, NULL);
		if (wsError == SOCKET_ERROR) {
			DebugPrint("SIOCGIFCONF:WSAIoctl(SIO_GET_INTERFACE_LIST) - errno %d\n" _C_ WSAGetLastError());
		}

		// parse interface information
		numLocalAddr = (bytesReturned / sizeof(INTERFACE_INFO));
		for (int i = 0; i < numLocalAddr; ++i) {
			SetFlags = localAddr[i].iiFlags;
			if ((SetFlags & IFF_UP) == 0) {
				continue;
			}
			if ((SetFlags & IFF_LOOPBACK)) {
				continue;
			}
			pAddrInet = (SOCKADDR_IN *)&localAddr[i].iiAddress;
			NetLocalAddrs[nif] = pAddrInet->sin_addr.s_addr;
			++nif;
			if (nif == MAX_LOC_IP) {
				break;
			}
		}
	}
	return nif;
}
#else // } { !USE_WINSOCK
#ifdef unix // {
// ARI: I knew how to write this for a unix environment,
// but am quite certain that porting this can cause you
// trouble..
int NetSocketAddr(const Socket sock)
{
	char buf[4096];
	char *cp;
	char *cplim;
	struct ifconf ifc;
	struct ifreq ifreq;
	struct ifreq *ifr;
	struct sockaddr_in *sap;
	struct sockaddr_in sa;
	int i;
	int nif;

	nif = 0;
	if (sock != static_cast<Socket>(-1)) {
		ifc.ifc_len = sizeof(buf);
		ifc.ifc_buf = buf;
		if (ioctl(sock, SIOCGIFCONF, (char *)&ifc) < 0) {
			DebugPrint("SIOCGIFCONF - errno %d\n" _C_ errno);
			return 0;
		}
		// with some inspiration from routed..
		ifr = ifc.ifc_req;
		cplim = buf + ifc.ifc_len; // skip over if's with big ifr_addr's
		for (cp = buf; cp < cplim;
			 cp += sizeof(ifr->ifr_name) + sizeof(ifr->ifr_ifru)) {
			ifr = (struct ifreq *)cp;
			ifreq = *ifr;
			if (ioctl(sock, SIOCGIFFLAGS, (char *)&ifreq) < 0) {
				DebugPrint("%s: SIOCGIFFLAGS - errno %d\n" _C_
						   ifr->ifr_name _C_ errno);
				continue;
			}
			if ((ifreq.ifr_flags & IFF_UP) == 0 || ifr->ifr_addr.sa_family == AF_UNSPEC) {
				continue;
			}
			// argh, this'll have to change sometime
			if (ifr->ifr_addr.sa_family != AF_INET) {
				continue;
			}
			if (ifreq.ifr_flags & IFF_LOOPBACK) {
				continue;
			}
			sap = (struct sockaddr_in *)&ifr->ifr_addr;
			sa = *sap;
			NetLocalAddrs[nif] = sap->sin_addr.s_addr;
			if (ifreq.ifr_flags & IFF_POINTOPOINT) {
				if (ioctl(sock, SIOCGIFDSTADDR, (char *)&ifreq) < 0) {
					DebugPrint("%s: SIOCGIFDSTADDR - errno %d\n" _C_
							   ifr->ifr_name _C_ errno);
					// failed to obtain dst addr - ignore
					continue;
				}
				if (ifr->ifr_addr.sa_family == AF_UNSPEC) {
					continue;
				}
			}
			// avoid p-t-p links with common src
			if (nif) {
				for (i = 0; i < nif; ++i) {
					if (sa.sin_addr.s_addr == NetLocalAddrs[i]) {
						i = -1;
						break;
					}
				}
				if (i == -1) {
					continue;
				}
			}
			++nif;
			if (nif == MAX_LOC_IP) {
				break;
			}
		}
	}
	return nif;
}
#else // } !unix
// Beos?? Mac??
int NetSocketAddr(const Socket sock)
{
	NetLocalAddrs[0] = htonl(0x7f000001);
	return 1;
}
#endif
#endif // } !USE_WINSOCK

/**
**  Open an UDP Socket port.
**
**  @param port !=0 Port to bind in host notation.
**
**  @return If success the socket fildes, -1 otherwise.
*/
Socket NetOpenUDP(char *addr, int port)
{
	Socket sockfd;

	// open the socket
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd == INVALID_SOCKET) {
		return static_cast<Socket>(-1);
	}
	// bind local port
	if (port) {
		struct sockaddr_in sock_addr;

		memset(&sock_addr, 0, sizeof(sock_addr));
		sock_addr.sin_family = AF_INET;
		if (addr) {
			sock_addr.sin_addr.s_addr = inet_addr(addr);
		} else {
			sock_addr.sin_addr.s_addr = INADDR_ANY;
		}
		sock_addr.sin_port = htons(port);
		// Bind the socket for listening
		if (bind(sockfd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0) {
			fprintf(stderr, "Couldn't bind to local port\n");
			NetCloseUDP(sockfd);
			return static_cast<Socket>(-1);
		}
		NetLastHost = sock_addr.sin_addr.s_addr;
		NetLastPort = sock_addr.sin_port;
	}
	return sockfd;
}

/**
**  Open a TCP socket
**
**  @param port  Bind socket to a specific port number
**
**  @return If success the socket fildes, -1 otherwise
*/
Socket NetOpenTCP(char *addr, int port)
{
	Socket sockfd;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == INVALID_SOCKET) {
		return static_cast<Socket>(-1);
	}
	// bind local port
	if (port) {
		struct sockaddr_in sock_addr;
		int opt;

		memset(&sock_addr, 0, sizeof(sock_addr));
		sock_addr.sin_family = AF_INET;
		if (addr) {
			sock_addr.sin_addr.s_addr = inet_addr(addr);
		} else {
			sock_addr.sin_addr.s_addr = INADDR_ANY;
		}
		sock_addr.sin_port = htons(port);

		opt = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (setsockopttype)&opt, sizeof(opt));

		if (bind(sockfd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0) {
			fprintf(stderr, "Couldn't bind to local port\n");
			NetCloseTCP(sockfd);
			return static_cast<Socket>(-1);
		}
		NetLastHost = sock_addr.sin_addr.s_addr;
		NetLastPort = sock_addr.sin_port;
	}
	NetLastSocket = sockfd;
	return sockfd;
}

/**
**  Open a TCP connection
**
**  @param sockfd  An open socket to use
**  @param addr    Address returned from NetResolveHost
**  @param port    Port on remote host to connect to
**
**  @return 0 if success, -1 if failure
*/
int NetConnectTCP(Socket sockfd, unsigned long addr, int port)
{
	struct sockaddr_in sa;
#ifndef __BEOS__
	int opt;

	opt = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (setsockopttype)&opt, sizeof(opt));
	opt = 0;
	setsockopt(sockfd, SOL_SOCKET, SO_LINGER, (setsockopttype)&opt, sizeof(opt));
#endif

	if (addr == INADDR_NONE) {
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	memcpy(&sa.sin_addr, &addr, sizeof(addr));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);

	if (connect(sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		fprintf(stderr, "connect to %d.%d.%d.%d:%d failed\n",
				NIPQUAD(ntohl(addr)), port);
		return -1;
	}

	return sockfd;
}

/**
**  Wait for socket ready.
**
**  @param sockfd   Socket fildes to probe.
**  @param timeout  Timeout in 1/1000 seconds.
**
**  @return 1 if data is available, 0 if not, -1 if failure.
*/
int NetSocketReady(Socket sockfd, int timeout)
{
	int retval;
	struct timeval tv;
	fd_set mask;

	// Check the file descriptors for available data
	do {
		// Set up the mask of file descriptors
		FD_ZERO(&mask);
		FD_SET(sockfd, &mask);

		// Set up the timeout
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;

		// Data available?
		retval = select(sockfd + 1, &mask, NULL, NULL, &tv);
#ifdef USE_WINSOCK
	} while (retval == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
#else
	} while (retval == -1 && errno == EINTR);
#endif

	return retval;
}

/**
**  Wait for socket set ready.
**
**  @param set      Socket set to probe.
**  @param timeout  Timeout in 1/1000 seconds.
**
**  @return 1 if data is available, 0 if not, -1 if failure.
*/
int NetSocketSetReady(SocketSet *set, int timeout)
{
	int retval;
	struct timeval tv;
	fd_set mask;

	// Check the file descriptors for available data
	do {
		// Set up the mask of file descriptors
		FD_ZERO(&mask);
		for (size_t i = 0; i < set->Sockets.size(); ++i) {
			FD_SET(set->Sockets[i], &mask);
		}

		// Set up the timeout
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;

		// Data available?
		retval = select(set->MaxSockFD + 1, &mask, NULL, NULL, &tv);
#ifdef USE_WINSOCK
	} while (retval == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
#else
	} while (retval == -1 && errno == EINTR);
#endif

	for (size_t i = 0; i < set->Sockets.size(); ++i)
	{
		set->SocketReady[i] = FD_ISSET(set->Sockets[i], &mask);
	}

	return retval;
}

/**
**  Check if a socket in a socket set is ready.
**
**  @param set     Socket set
**  @param socket  Socket to check
**
**  @return        Non-zero if socket is ready
*/
int NetSocketSetSocketReady(SocketSet *set, Socket socket)
{
	for (size_t i = 0; i < set->Sockets.size(); ++i) {
		if (set->Sockets[i] == socket) {
			return set->SocketReady[i];
		}
	}
	DebugPrint("Socket not found in socket set\n");
	return 0;
}

/**
**  Receive from a UDP socket.
**
**  @param sockfd  Socket
**  @param buf     Receive message buffer.
**  @param len     Receive message buffer length.
**
**  @return Number of bytes placed in buffer, or -1 if failure.
*/
int NetRecvUDP(Socket sockfd, void *buf, int len)
{
	socklen_t n;
	int l;
	struct sockaddr_in sock_addr;

	n = sizeof(struct sockaddr_in);
	if ((l = recvfrom(sockfd, (recvfrombuftype)buf, len, 0, (struct sockaddr *)&sock_addr, &n)) < 0) {
		PrintFunction();
		fprintf(stdout, "Could not read from UDP socket\n");
		return -1;
	}

	// Packet check for validness is done higher up, we don't know who should be
	// sending us packets at this level

	NetLastHost = sock_addr.sin_addr.s_addr;
	NetLastPort = sock_addr.sin_port;

	return l;
}

/**
**  Receive from a TCP socket.
**
**  @param sockfd  Socket
**  @param buf     Receive message buffer.
**  @param len     Receive message buffer length.
**
**  @return Number of bytes placed in buffer or -1 if failure.
*/
int NetRecvTCP(Socket sockfd, void *buf, int len)
{
	int ret;

	NetLastSocket = sockfd;
	ret = recv(sockfd, (recvbuftype)buf, len, 0);
	if (ret > 0) {
		return ret;
	}
	if (ret == 0) {
		return -1;
	}
#ifdef USE_WINSOCK
	if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
	if (errno == EWOULDBLOCK || errno == EAGAIN) {
#endif
		return 0;
	}
	return ret;
}

/**
**  Send through a UPD socket to a host:port.
**
**  @param sockfd  Socket
**  @param host    Host to send to (network byte order).
**  @param port    Port of host to send to (network byte order).
**  @param buf     Send message buffer.
**  @param len     Send message buffer length.
**
**  @return Number of bytes sent.
*/
int NetSendUDP(Socket sockfd, unsigned long host, int port,
			   const void *buf, int len)
{
	int n;
	struct sockaddr_in sock_addr;

	n = sizeof(struct sockaddr_in);
	sock_addr.sin_addr.s_addr = host;
	sock_addr.sin_port = port;
	sock_addr.sin_family = AF_INET;

	// if (MyRand() % 7) { return 0; }

	return sendto(sockfd, (sendtobuftype)buf, len, 0, (struct sockaddr *)&sock_addr, n);
}

/**
**  Send through a TCP socket.
**
**  @param sockfd  Socket
**  @param buf     Send message buffer.
**  @param len     Send message buffer length.
**
**  @return Number of bytes sent.
*/
int NetSendTCP(Socket sockfd, const void *buf, int len)
{
	return send(sockfd, (sendbuftype)buf, len, 0);
}

/**
**  Listen for connections on a TCP socket.
**
**  @param sockfd  Socket
**
**  @return 0 for success, -1 for error
*/
int NetListenTCP(Socket sockfd)
{
	return listen(sockfd, PlayerMax);
}

/**
**  Accept a connection on a TCP socket.
**
**  @param sockfd  Socket
**
**  @return If success the new socket fildes, -1 otherwise.
*/
Socket NetAcceptTCP(Socket sockfd)
{
	struct sockaddr_in sa;
	socklen_t len;

	len = sizeof(struct sockaddr_in);
	NetLastSocket = accept(sockfd, (struct sockaddr *)&sa, &len);
	NetLastHost = sa.sin_addr.s_addr;
	NetLastPort = sa.sin_port;
	return NetLastSocket;
}

/**
**  Add a socket to a socket set
**
**  @param socket  Socket to add to the socket set
*/
void SocketSet::AddSocket(Socket socket)
{
	Sockets.push_back(socket);
	SocketReady.push_back(0);
	if (socket > MaxSockFD) {
		MaxSockFD = socket;
	}
}

/**
**  Delete a socket from a socket set
**
**  @param socket  Socket to delete from the socket set
*/
void SocketSet::DelSocket(Socket socket)
{
	std::vector<Socket>::iterator i;
	std::vector<int>::iterator j;

	for (i = Sockets.begin(), j = SocketReady.begin(); i != Sockets.end(); ++i, ++j) {
		if (*i == socket) {
			Sockets.erase(i);
			SocketReady.erase(j);
			break;
		}
	}
	if (socket == MaxSockFD) {
		MaxSockFD = 0;
		for (i = Sockets.begin(); i != Sockets.end(); ++i) {
			if (*i > MaxSockFD) {
				MaxSockFD = *i;
			}
		}
	}
}

//@}
