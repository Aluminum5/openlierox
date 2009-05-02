/////////////////////////////////////////
//
//             OpenLieroX
//
// code under LGPL, based on JasonBs work,
// enhanced by Dark Charlie and Albert Zeyer
//
//
/////////////////////////////////////////


// Common networking routines to help us
// Created 18/12/02
// Jason Boettcher

//#include <SDL_syswm.h>
#include "ThreadPool.h"
#ifndef WIN32
#include <signal.h>
#endif

#include "LieroX.h"
#include "Debug.h"
#include "Options.h"
#include "Error.h"
#include "Networking.h"
#include "StringUtils.h"
#include "SmartPointer.h"
#include "Timer.h"
#include "ThreadVar.h"
#include "MathLib.h"
#include "InputEvents.h"
#include "TaskManager.h"
#include "ReadWriteLock.h"


#ifdef _MSC_VER
#pragma warning(disable: 4786)
#endif

#include <map>

#include <nl.h>
// workaraound for bad named makros by nl.h
// macros are bad, esp the names (reserved/used by CBytestream)
// TODO: they seem to not work correctly!
// all use of it in CBytestream was removed
inline void nl_writeShort(char* x, int& y, NLushort z)		{ writeShort(x, y, z); }
inline void nl_writeLong(char* x, int& y, NLulong z)		{ writeLong(x, y, z); }
inline void nl_writeFloat(char* x, int& y, NLfloat z)		{ writeFloat(x, y, z); }
inline void nl_writeDouble(char* x, int& y, NLdouble z)		{ writeDouble(x, y, z); }
inline void nl_readShort(char* x, int& y, NLushort z)		{ readShort(x, y, z); }
inline void nl_readLong(char* x, int& y, NLulong z)			{ readLong(x, y, z); }
inline void nl_readFloat(char* x, int& y, NLfloat z)		{ readFloat(x, y, z); }
inline void nl_readDouble(char* x, int& y, NLdouble z)		{ readDouble(x, y, z); }
#undef writeByte
#undef writeShort
#undef writeFloat
#undef writeString
#undef readByte
#undef readShort
#undef readFloat
#undef readString

class NetAddrIniter {
public:
	void operator()(SmartPointer<NLaddress, NetAddrIniter>* addr) {
		NLaddress* addrPtr = new NLaddress;
		memset(addrPtr, 0, sizeof(NLaddress));
		*addr = addrPtr;
	}
};

typedef SmartPointer<NLaddress, NetAddrIniter> NetAddrSmartPtr;
DECLARE_INTERNDATA_CLASS( NetworkAddr, NetAddrSmartPtr );

DECLARE_INTERNDATA_CLASS__WITH_INIT( NetworkSocket, NLsocket, NL_INVALID );


static NLsocket& getNLsocket(NetworkSocket& socket) {
	return NetworkSocketData(socket);
}

static NLaddress* getNLaddr(NetworkAddr& addr) {
	return NetworkAddrData(addr).get();
}

static const NLaddress* getNLaddr(const NetworkAddr& addr) {
	return NetworkAddrData(addr).get();
}


// TODO: perhaps move these test-functions somewhere else?
// but it's good to keep them somewhere to easily test some code part
void test_NetworkSmartPointer() {
	for(int i = 0; i < 100; i++)
	{
		printf("creating SP\n");
		NetAddrSmartPtr sp;
		printf("destroying SP\n");
	}


	SDL_Delay(1000);

	printf("creating data-array\n");
	char* tmp = new char[2048];

	printf("freeing data-array\n");
	delete tmp;

//	exit(-1);
}


// --------------------------------------------------------------------------
// ------------- Net events

bool SdlNetEvent_Inited = false;
bool SdlNetEventThreadExit = false;
ThreadPoolItem* SdlNetEventThreads[3] = {NULL, NULL, NULL};
NLint SdlNetEventGroup = 0;
int SdlNetEventSocketCount = 0;

struct NetActivityData {
	uint type;
	NLsocket* socket;
	NetActivityData(uint t, NLsocket* s) : type(t), socket(s) {}
};
Event<NetActivityData> onNetActivity;

static bool isSocketInGroup(NLint group, NetworkSocket sock) {
	NLsocket sockets[NL_MAX_GROUP_SOCKETS];
	NLint len = NL_MAX_GROUP_SOCKETS;
	nlGroupGetSockets( group, sockets, &len );
	for( int f = 0; f < len; f++ )
		if( sockets[f] == getNLsocket(sock) )
			return true;

	return false;
}

static bool isSocketGroupEmpty(NLint group) {
	NLsocket oneSocket;
	NLint len = 1;
	// because we set len=1 we will either get one or none sockets here
	nlGroupGetSockets( group, &oneSocket, &len );
	return len == 0;
}


static int SdlNetEventThreadMain( void * param )
{
	int buffer_size = MAX(2, SdlNetEventSocketCount);
	NLsocket *sock_out = new NLsocket[buffer_size];
	// save event-type (NL_READ_STATUS, NL_WRITE_STATUS or NL_ERROR_STATUS)

	// When restarting, this can happen, we wait for options to initialize
	while (tLXOptions == NULL && !SdlNetEventThreadExit)
		SDL_Delay(20);

	if (SdlNetEventThreadExit)  {
		delete (uint *)param;
		delete[] sock_out;
		return 0;
	}

	TimeDiff max_frame_time = TimeDiff(MAX(0.01f, (tLXOptions->nMaxFPS > 0) ? 1.0f/(float)tLXOptions->nMaxFPS : 0.0f));
	AbsTime lastTime = GetTime();
	while( ! SdlNetEventThreadExit )
	{
		if( ! isSocketGroupEmpty( SdlNetEventGroup ) && SdlNetEventSocketCount ) // only when we have at least one socket
		{
			if( nlPollGroup( SdlNetEventGroup, *(uint*)param, sock_out, buffer_size, 0 ) > 0 ) // Wait max_frame_time
			{
				if(sock_out >= 0) {
					onNetActivity.pushToMainQueue( NetActivityData( (long) *(uint*)param, sock_out ) );
				}
				else
					warnings("net-event-system: invalid socket\n");
			} else {
				if (nlGetError() == NL_BUFFER_SIZE)  { // We should increase the buffer size
					delete[] sock_out;
					buffer_size = MAX(2, SdlNetEventSocketCount);
					sock_out = new NLsocket[buffer_size];
					continue; // Retry
				}
			}
		}

		AbsTime curTime = GetTime();
		if(curTime - lastTime < max_frame_time) {
			SDL_Delay( (Uint32)( ( max_frame_time - (curTime - lastTime) ).milliseconds() ) );
		}
		lastTime = curTime;
	}

	delete (uint*)param;
	delete[] sock_out;
	return 0;
};

static bool SdlNetEvent_Init()
{
	if( SdlNetEvent_Inited || !tLX )
		return false;
	SdlNetEvent_Inited = true;

	SdlNetEventGroup = nlGroupCreate();
	SdlNetEventThreads[0] = threadPool->start( &SdlNetEventThreadMain, new uint(NL_READ_STATUS), "NL_READ_STATUS watcher" );
	// TODO: this does not behave as expected
	// select() which is internally used by nlPollGroup will return true if the socket is *writeable*
	// which for UDP sockets means always (this has nothing to do with the actual writing to the socket)
	// See http://msdn2.microsoft.com/en-us/library/ms740141(VS.85).aspx or
	// http://support.sas.com/documentation/onlinedoc/sasc/doc750/html/lr2/select.htm for more details
	//SdlNetEventThreads[1] = threadPool->start( &SdlNetEventThreadMain, new uint(NL_WRITE_STATUS), "NL_WRITE_STATUS watcher" );
	SdlNetEventThreads[2] = threadPool->start( &SdlNetEventThreadMain, new uint(NL_ERROR_STATUS), "NL_ERROR_STATUS watcher" );

	return true;
};

static void SdlNetEvent_UnInit() {
	if( ! SdlNetEvent_Inited )
		return;

	SdlNetEventThreadExit = true;
	int status = 0;
	threadPool->wait( SdlNetEventThreads[0], &status );
	threadPool->wait( SdlNetEventThreads[1], &status );
	threadPool->wait( SdlNetEventThreads[2], &status );
	nlGroupDestroy(SdlNetEventGroup);

	SdlNetEvent_Inited = false;
};



void AddSocketToNotifierGroup( NetworkSocket sock )
{
	SdlNetEvent_Init();

	if( IsSocketStateValid(sock) && !isSocketInGroup(SdlNetEventGroup, sock) )  {
		nlGroupAddSocket( SdlNetEventGroup, getNLsocket(sock) );
		SdlNetEventSocketCount++;
	}
}

void RemoveSocketFromNotifierGroup( NetworkSocket sock )
{
	SdlNetEvent_Init();

	if (nlGroupDeleteSocket( SdlNetEventGroup, getNLsocket(sock) ) && SdlNetEventSocketCount > 0)
		SdlNetEventSocketCount--;
}

// ------------------------------------------------------------------------


#ifndef WIN32
static void sigpipe_handler(int i) {
	warnings << "got SIGPIPE, ignoring..." << endl;
	signal(SIGPIPE, sigpipe_handler);
}
#endif


/*
 *
 * HawkNL Network wrapper
 *
 */


bool bNetworkInited = false;
ReadWriteLock nlSystemUseChangeLock;


typedef std::map<std::string, std::pair< NLaddress, AbsTime > > dnsCacheT; // Second parameter is expiration time of DNS record
ThreadVar<dnsCacheT>* dnsCache = NULL;

void AddToDnsCache(const std::string& name, const NetworkAddr& addr, TimeDiff expireTime ) {
	ScopedReadLock lock(nlSystemUseChangeLock);
	if(dnsCache == NULL) return;
	ThreadVar<dnsCacheT>::Writer dns( *dnsCache );
	dns.get()[name] = std::make_pair( *getNLaddr(addr), tLX->currentTime + expireTime );
}

bool GetFromDnsCache(const std::string& name, NetworkAddr& addr) {
	ScopedReadLock lock(nlSystemUseChangeLock);
	if(dnsCache == NULL) return false;
	ThreadVar<dnsCacheT>::Writer dns( *dnsCache );
	dnsCacheT::iterator it = dns.get().find(name);
	if(it != dns.get().end()) {
		if( it->second.second < tLX->currentTime )
		{
			dns.get().erase(it);
			return false;
		}
		*getNLaddr(addr) = it->second.first;
		return true;
	} else
		return false;
}



/////////////////////
// Initializes network
bool InitNetworkSystem() {
	nlSystemUseChangeLock.startWriteAccess();
	bNetworkInited = false;

    if(!nlInit()) {
    	SystemError("nlInit failed");
		nlSystemUseChangeLock.endWriteAccess();
    	return false;
    }

    if(!nlSelectNetwork(NL_IP)) {
        SystemError("could not select IP-based network");
		nlSystemUseChangeLock.endWriteAccess();
		return false;
    }

	bNetworkInited = true;
	
	if(!SdlNetEvent_Init()) {
		SystemError("SdlNetEvent_Init failed");
		nlSystemUseChangeLock.endWriteAccess();
		return false;
	}

	dnsCache = new ThreadVar<dnsCacheT>();

#ifndef WIN32
	//sigignore(SIGPIPE);
	signal(SIGPIPE, sigpipe_handler);
#endif
	
	nlSystemUseChangeLock.endWriteAccess();
	return true;
}

//////////////////
// Shutdowns the network system
bool QuitNetworkSystem() {
	SdlNetEvent_UnInit();
	nlSystemUseChangeLock.startWriteAccess();
	nlShutdown();	
	bNetworkInited = false;
	delete dnsCache; dnsCache = NULL;
	nlSystemUseChangeLock.endWriteAccess();
	return true;
}









/*
Sadly, HawkNL lacks some support for specific things. For example we cannot get the real socket nr.

Atm., we are doing these:

nlUpdateState
nlPrepareClose

*/

// ---------------------------------------------------
// ----------- for state checking -------------------

// copied from sock.c from HawkNL

#include <memory.h>
#include <stdio.h>
#include <string.h>

#if defined (_WIN32_WCE)
#define EAGAIN          11
#define errno GetLastError()
#else
#include <errno.h>
#endif

#if defined WIN32 || defined WIN64 || defined (_WIN32_WCE)

#include "wsock.h"

#elif defined Macintosh

#include <Types.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/time.h>
#include <LowMem.h>
#define closesocket close
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define SOCKET int
#define sockerrno errno

 /* define INADDR_NONE if not already */
#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned long) -1)
#endif

#else

 /* Unix-style systems */
#ifdef SOLARIS
#include <sys/filio.h> /* for FIONBIO */
#endif

#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#define closesocket close
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define SOCKET int
#define sockerrno errno

 /* define INADDR_NONE if not already */
#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned long) -1)
#endif

/* SGI do not include socklen_t */
#if defined __sgi
typedef int socklen_t;
#endif

#endif /* WINDOWS_APP*/


// from nlinternal.h from HawkNL

/* number of buckets for average bytes/second */
#define NL_NUM_BUCKETS          8

/* number of packets stored for NL_LOOP_BACK */
#define NL_NUM_PACKETS          8
#define NL_MAX_ACCEPT           10


typedef struct
{
	NLlong      bytes;          /* bytes sent/received */
	NLlong      packets;        /* packets sent/received */
	NLlong      highest;        /* highest bytes/sec sent/received */
	NLlong      average;        /* average bytes/sec sent/received */
	time_t      stime;          /* the last time stats were updated */
	NLint       lastbucket;     /* the last bucket that was used */
	NLlong      curbytes;       /* current bytes sent/received */
	NLlong      bucket[NL_NUM_BUCKETS];/* buckets for sent/received counts */
	NLboolean   firstround;     /* is this the first round through the buckets? */
} nl_stats_t;

typedef struct
{
	/* info for NL_LOOP_BACK, NL_SERIAL, and NL_PARALLEL */
	NLbyte      *outpacket[NL_NUM_PACKETS];/* temp storage for packet data */
	NLbyte      *inpacket[NL_NUM_PACKETS];/* temp storage for packet data */
	NLint       outlen[NL_NUM_PACKETS];/* the length of each packet */
	NLint       inlen[NL_NUM_PACKETS];/* the length of each packet */
	NLint       nextoutused;    /* the next used packet */
	NLint       nextinused;     /* the next used packet */
	NLint       nextoutfree;    /* the next free packet */
	NLint       nextinfree;     /* the next free packet */
	NLsocket    accept[NL_MAX_ACCEPT];/* pending connects */
	NLsocket    consock;        /* the socket this socket is connected to */
} nl_extra_t;


/* the internal socket object */
typedef struct
{
	/* the current status of the socket */
	NLenum      driver;         /* the driver used with this socket */
	NLenum      type;           /* type of socket */
	NLboolean   inuse;          /* is in use */
	NLboolean   connecting;     /* a non-blocking TCP or UDP connection is in process */
	NLboolean   conerror;       /* an error occured on a UDP connect */
	NLboolean   connected;      /* is connected */
	NLboolean   reliable;       /* do we use reliable */
	NLboolean   blocking;       /* is set to blocking */
	NLboolean   listen;         /* can receive an incoming connection */
	NLint       realsocket;     /* the real socket number */
	NLushort    localport;      /* local port number */
	NLushort    remoteport;     /* remote port number */
	NLaddress   addressin;      /* address of remote system, same as the socket sockaddr_in structure */
	NLaddress   addressout;     /* the multicast address set by nlConnect or the remote address for unconnected UDP */
	NLmutex     readlock;       /* socket is locked to update data */
	NLmutex     writelock;      /* socket is locked to update data */

	/* the current read/write statistics for the socket */
	nl_stats_t  instats;        /* stats for received */
	nl_stats_t  outstats;       /* stats for sent */

	/* NL_RELIABLE_PACKETS info and storage */
	NLbyte      *outbuf;        /* temp storage for partially sent reliable packet data */
	NLint       outbuflen;      /* the length of outbuf */
	NLint       sendlen;        /* how much still needs to be sent */
	NLbyte      *inbuf;         /* temp storage for partially received reliable packet data */
	NLint       inbuflen;       /* the length of inbuf */
	NLint       reclen;         /* how much of the reliable packet we have received */
	NLboolean   readable;       /* a complete packet is in inbuf */
	NLboolean   message_end;    /* a message end error ocured but was not yet reported */
	NLboolean   packetsync;     /* is the reliable packet stream in sync */
	/* pointer to extra info needed for NL_LOOP_BACK, NL_SERIAL, and NL_PARALLEL */
	nl_extra_t   *ext;
} nl_socket_t;

typedef /*@only@*/ nl_socket_t *pnl_socket_t;

// -------------------------------------
// extern defs for HawkNL intern stuff
#ifdef _MSC_VER
extern "C"  {
	__declspec(dllimport) pnl_socket_t *nlSockets;
}
#else
#ifdef WIN32
extern "C" pnl_socket_t *nlSockets;	// For Dev-Cpp
#else
extern "C"  {
	NL_EXP pnl_socket_t *nlSockets;
}
#endif
#endif


#if defined(WIN32) && (! defined(_MSC_VER))
// TODO: why is that needed?
#define NL_EXP
#elif (_MSC_VER == 1400) //MSVC 2005 hax
// TODO: why is that needed?
#undef NL_EXP
#undef NL_APIENTRY
#define NL_EXP
#define NL_APIENTRY
#endif

extern "C" {
	NL_EXP void NL_APIENTRY nlSetError(NLenum err);
	
	// can we use those? (at least we have to)
	NLboolean nlLockSocket(NLsocket socket, NLint which);
	NLboolean nlIsValidSocket(NLsocket socket);
	void nlUnlockSocket(NLsocket socket, NLint which);
}


/* for nlLockSocket and nlUnlockSocket */
#define NL_READ                 0x0001
#define NL_WRITE                0x0002
#define NL_BOTH                 (NL_READ|NL_WRITE)




// WARNING: both these function assume that we selected the IP-network driver in HawkNL

// modified sock_Write of sock.c from HawkNL
// returns true if socket is connected and data could be send
static bool nlUpdateState(NLsocket socket)
{
	if(nlIsValidSocket(socket) != NL_TRUE) return false;
	
	struct Unlocker {
		NLsocket socket;
		~Unlocker() {
			nlUnlockSocket(socket, NL_BOTH);
		}
		Unlocker(NLsocket s) : socket(s) {}
	};
	
	if(nlLockSocket(socket, NL_BOTH) == NL_FALSE)
	{
		return false;
	}
	
	Unlocker unlocker(socket);
	
	nl_socket_t *sock = nlSockets[socket];
	NLint       count = 0;

	if((sock->type == NL_RELIABLE) || (sock->type == NL_RELIABLE_PACKETS)) /* TCP */
	{
		if(sock->connecting == NL_TRUE)
		{
			fd_set          fdset;
			struct timeval  t = {0,0};
			int             serrval = -1;
			socklen_t       serrsize = (socklen_t)sizeof(serrval);


			FD_ZERO(&fdset);
			FD_SET((SOCKET)sock->realsocket, &fdset);
			if(select(sock->realsocket + 1, NULL, &fdset, NULL, &t) == 1)
			{
				/* Check the socket status */
				(void)getsockopt( sock->realsocket, SOL_SOCKET, SO_ERROR, (char *)&serrval, &serrsize );
				if(serrval != 0)
				{
					if(serrval == ECONNREFUSED)
					{
						nlSetError(NL_CON_REFUSED);
					}
					else if(serrval == EINPROGRESS || serrval == EWOULDBLOCK)
					{
						nlSetError(NL_CON_PENDING);
					}
					return false;
				}
				/* the connect has completed */
				sock->connected = NL_TRUE;
				sock->connecting = NL_FALSE;
			}
			else
			{
				/* check for a failed connect */
				FD_ZERO(&fdset);
				FD_SET((SOCKET)sock->realsocket, &fdset);
				if(select(sock->realsocket + 1, NULL, NULL, &fdset, &t) == 1)
				{
					nlSetError(NL_CON_REFUSED);
				}
				else
				{
					nlSetError(NL_CON_PENDING);
				}
				return false;
			}
		}
		/* check for reliable packets */
		if(sock->type == NL_RELIABLE_PACKETS)
		{
			return true;
		}
		return true;
	}
	else /* unconnected UDP */
	{
		/* check for a non-blocking connection pending */
		if(sock->connecting == NL_TRUE)
		{
			nlSetError(NL_CON_PENDING);
			return false;
		}
		/* check for a connection error */
		if(sock->conerror == NL_TRUE)
		{
			nlSetError(NL_CON_REFUSED);
			return false;
		}
		if(sock->type == NL_BROADCAST)
		{
			((struct sockaddr_in *)&sock->addressin)->sin_addr.s_addr = INADDR_BROADCAST;
		}
		if(sock->type == NL_UDP_MULTICAST)
		{
			return true;
		}
		else if(sock->connected == NL_TRUE)
		{
			return true;
		}
		else
		{
			return true;
		}
	}
	if(count == SOCKET_ERROR)
	{
		return false;
	}
	return false;
}


static void nlPrepareClose(NLsocket socket) {
	if(nlIsValidSocket(socket) != NL_TRUE) return;
	
	struct Unlocker {
		NLsocket socket;
		~Unlocker() {
			nlUnlockSocket(socket, NL_BOTH);
		}
		Unlocker(NLsocket s) : socket(s) {}
	};
	
	if(nlLockSocket(socket, NL_BOTH) == NL_FALSE)
	{
		return;
	}
	
	Unlocker unlocker(socket);
	
	// code copied&modified from sock_Close
	// The advantage we have here is that we don't lock the whole socket array.
	// nlClose is doing this, so if we would hang in nlClose, we block the
	// *whole* HawkNL system (or at least actions like opening new sockets etc.)!
	
	nl_socket_t     *sock = nlSockets[socket];
	struct ip_mreq  mreq;
    
	if(sock->type == NL_UDP_MULTICAST)
	{
		/* leave the multicast group */
		mreq.imr_multiaddr.s_addr = ((struct sockaddr_in *)&sock->addressout)->sin_addr.s_addr;
		mreq.imr_interface.s_addr = INADDR_ANY; //bindaddress;
        
		(void)setsockopt((SOCKET)sock->realsocket, IPPROTO_IP, IP_DROP_MEMBERSHIP,
		 (char *)&mreq, (int)sizeof(mreq));
	}
	if(sock->type == NL_RELIABLE_PACKETS)
	{
		/* check for unsent data */
		if(sock->sendlen > 0)
		{
			int tries = 200;

			/* 200 * 50 ms = up to a 10 second delay to allow data to be sent */
			while(tries-- > 0 && sock->sendlen > 0)
			{
				SDL_Delay(50);
			}
		}
		
		// oh just fuck it
		sock->sendlen = 0;
	}
	if((sock->type == NL_RELIABLE_PACKETS || sock->type == NL_RELIABLE) && sock->listen == NL_FALSE)
	{
		struct linger l = {1, 10};

		(void)setsockopt((SOCKET)sock->realsocket, SOL_SOCKET, SO_LINGER, (const char *)&l, (int)sizeof(l));
	}
	(void)closesocket((SOCKET)sock->realsocket);
}





NetworkSocket OpenReliableSocket(unsigned short port) {
	NetworkSocket ret;
	getNLsocket(ret) = nlOpen(port, NL_RELIABLE);
	if (IsSocketStateValid(ret))
		AddSocketToNotifierGroup(ret);
#ifdef DEBUG
	else
		errors << "OpenReliableSocket: " << GetSocketErrorStr(nlGetError()) << endl;
#endif
	return ret;
}

NetworkSocket OpenUnreliableSocket(unsigned short port, bool events) {
	NetworkSocket ret;
	getNLsocket(ret) = nlOpen(port, NL_UNRELIABLE);
	if (!IsSocketStateValid(ret))  {
#ifdef DEBUG
		errors << "OpenUnreliableSocket: " << GetLastErrorStr() << endl;
#endif
		return ret;
	}
	if(events) AddSocketToNotifierGroup(ret);
	return ret;
}

NetworkSocket OpenBroadcastSocket(unsigned short port, bool events) {
	NetworkSocket ret;
	getNLsocket(ret) = nlOpen(port, NL_BROADCAST);
	if (!IsSocketStateValid(ret))  {
#ifdef DEBUG
		errors << "OpenBroadcastSocket: " << GetLastErrorStr() << endl;
#endif
		return ret;
	}
	if(events) AddSocketToNotifierGroup(ret);
	return ret;
}

bool ConnectSocket(NetworkSocket sock, const NetworkAddr& addr) {
	AddSocketToNotifierGroup(sock);
	return (nlConnect(getNLsocket(sock), getNLaddr(addr)) != NL_FALSE);
}

bool ListenSocket(NetworkSocket sock) {
	AddSocketToNotifierGroup(sock);
	return (nlListen(getNLsocket(sock)) != NL_FALSE);
}

bool CloseSocket(NetworkSocket& sock) {
	RemoveSocketFromNotifierGroup(sock);

	bool started = false;
	struct CloseSocketWorker : Task {
		NetworkSocket sock;
		bool* started;
		int handle() {
			nlSystemUseChangeLock.startReadAccess();
			*started = true;
			int ret = -1;
			if(bNetworkInited) { // only do that if we have the network system still up
				// this should already close the socket but not lock other parts in HawkNL
				nlPrepareClose(getNLsocket(sock));
				// hopefully this does not block anymore
				ret = (nlClose(getNLsocket(sock)) != NL_FALSE) ? 0 : -1;
			}
			nlSystemUseChangeLock.endReadAccess();
			return ret;
		}
	};
	CloseSocketWorker* worker = new CloseSocketWorker();
	worker->name = "close socket";
	worker->sock.swap(sock);
	worker->started = &started;
	taskManager->start(worker);
	while(!started) SDL_Delay(1);
	
	// sock is a new socket, we swapped them
	InvalidateSocketState(sock);
	return true;
}

int WriteSocket(NetworkSocket sock, const void* buffer, int nbytes) {
	NLint ret = nlWrite(getNLsocket(sock), buffer, nbytes);

#ifdef DEBUG
	// Error checking
	if (ret == NL_INVALID)  {
		errors << "WriteSocket: " << GetLastErrorStr() << endl;
		return NL_INVALID;
	}

	/*if (ret == 0) {  // HINT: this is not an error, it is one of the ways to find out if a socket is writeable
		errors << "WriteSocket: Could not send the packet, network buffers are full." << endl;
	}*/
#endif // DEBUG

	return ret;
}

int	WriteSocket(NetworkSocket sock, const std::string& buffer) {
	return WriteSocket(sock, buffer.data(), (int)buffer.size());
}

int ReadSocket(NetworkSocket sock, void* buffer, int nbytes) {
	NLint ret = nlRead(getNLsocket(sock), buffer, nbytes);

#ifdef DEBUG
	// Error checking
	if (ret == NL_INVALID)  {
		if (!IsMessageEndSocketErrorNr(GetSocketErrorNr()))
			errors << "ReadSocket: " << GetLastErrorStr() << endl;
		return NL_INVALID;
	}
#endif // DEBUG

	return ret;
}

bool IsSocketStateValid(NetworkSocket sock) {
	return (getNLsocket(sock) != NL_INVALID);
}






bool IsSocketReady(NetworkSocket sock)  {
	return IsSocketStateValid(sock) && nlUpdateState(getNLsocket(sock));
}

void InvalidateSocketState(NetworkSocket& sock) {
	NetworkSocketData(sock) = NL_INVALID;
}


// TODO: Remove the following functions because they are blocking.

/////////////////////
// Wait until the socket is writable
void WaitForSocketWrite(NetworkSocket sock, int timeout)
{
	NLint group = nlGroupCreate();
	nlGroupAddSocket(group, NetworkSocketData(sock));
	NLsocket s;
	nlPollGroup(group, NL_WRITE_STATUS, &s, 1, (NLint)timeout);
	nlGroupDestroy(group);
}

//////////////////////
// Wait until the socket contains some data to read
void WaitForSocketRead(NetworkSocket sock, int timeout)
{
	NLint group = nlGroupCreate();
	nlGroupAddSocket(group, NetworkSocketData(sock));
	NLsocket s;
	nlPollGroup(group, NL_READ_STATUS, &s, 1, (NLint)timeout);
	nlGroupDestroy(group);
}

/////////////////////
// Wait until the socket contains some data or is writeable
void WaitForSocketReadOrWrite(NetworkSocket sock, int timeout)
{
	NLint group = nlGroupCreate();
	nlGroupAddSocket(group, NetworkSocketData(sock));
	NLsocket s;

	if (timeout < 0)  {
		// Infinite timeout
		while (true)  {
			if (nlPollGroup(group, NL_READ_STATUS, &s, 1, 0))
				break;
			if (nlPollGroup(group, NL_WRITE_STATUS, &s, 1, 0))
				break;
			SDL_Delay(2);
		}
	} else if (timeout > 0)  {
		// Blocking, with a timeout
		Uint32 start = SDL_GetTicks();
		while (SDL_GetTicks() - start <= (Uint32)timeout)  {
			if (nlPollGroup(group, NL_READ_STATUS, &s, 1, 0))
				break;
			if (nlPollGroup(group, NL_WRITE_STATUS, &s, 1, 0))
				break;
			SDL_Delay(2);
		}
	}

	nlGroupDestroy(group);
}

int GetSocketErrorNr() {
	return nlGetError();
}

std::string GetSocketErrorStr(int errnr) {
	return GetLastErrorStr();
}

std::string GetLastErrorStr()  {
	if (nlGetError() != NL_SYSTEM_ERROR)
		return std::string(nlGetErrorStr(nlGetError()));
	else
		return std::string(nlGetSystemErrorStr(nlGetSystemError()));
}

bool IsMessageEndSocketErrorNr(int errnr) {
	return (errnr == NL_MESSAGE_END);
}

void ResetSocketError()  {
	if (!bNetworkInited)
		return;

	nlSetError(NL_NO_ERROR);
}

bool GetLocalNetAddr(NetworkSocket sock, NetworkAddr& addr) {
	if(getNLaddr(addr) == NULL)
		return false;
	else
		return (nlGetLocalAddr(getNLsocket(sock), getNLaddr(addr)) != NL_FALSE);
}

bool GetRemoteNetAddr(NetworkSocket sock, NetworkAddr& addr) {
	if(getNLaddr(addr) == NULL)
		return false;
	else
		return (nlGetRemoteAddr(getNLsocket(sock), getNLaddr(addr)) != NL_FALSE);
}

bool SetRemoteNetAddr(NetworkSocket sock, const NetworkAddr& addr) {
	if(getNLaddr(addr) == NULL)
		return false;
	else
		return (nlSetRemoteAddr(getNLsocket(sock), getNLaddr(addr)) != NL_FALSE);
}

bool IsNetAddrValid(const NetworkAddr& addr) {
	if(getNLaddr(addr))
		return (getNLaddr(addr)->valid != NL_FALSE);
	else
		return false;
}

bool SetNetAddrValid(NetworkAddr& addr, bool valid) {
	if(!getNLaddr(addr)) return false;
	getNLaddr(addr)->valid = valid ? NL_TRUE : NL_FALSE;
	return true;
}

void ResetNetAddr(NetworkAddr& addr) {
	if(!getNLaddr(addr)) return;
	// TODO: is this the best way?
	memset(getNLaddr(addr), 0, sizeof(NLaddress));
	SetNetAddrValid(addr, false);
}

static bool isStringValidIP(const std::string& str) {
	size_t p = 0;
	int numC = 0;
	int numLen = 0;
	while(p < str.size()) {
		if(str[p] == '.') {
			if(numC >= 3) return false;
			if(!numLen) return false;
			numC++; numLen = 0;
			p++; continue;
		}
		
		if(str[p] == ':') {
			if(numC < 3) return false;
			if(!numLen) return false;
			numC++; numLen = 0;
			p++; continue;
		}
		
		if(str[p] >= '0' && str[p] <= '9') {
			numLen++;
			if(numLen > 3) return false;
			p++; continue;
		}
		
		return false;
	}
	
	if(numC >= 3 && numLen > 0) return true;
	return false;
}

// accepts "%i.%i.%i.%i[:%l]" as input
bool StringToNetAddr(const std::string& string, NetworkAddr& addr) {
	if(getNLaddr(addr) == NULL) return false;
	
	if(!isStringValidIP(string)) {
		SetNetAddrValid(addr, false);
		return false;
	}
	
	return (nlStringToAddr(string.c_str(), getNLaddr(addr)) != NL_FALSE);
}

bool NetAddrToString(const NetworkAddr& addr, std::string& string) {
	// TODO: safty here for buffer
	char buf[256];
	NLchar* res = nlAddrToString(getNLaddr(addr), buf);
	if(res) {
		fix_markend(buf);
		string = buf;
		return true;
	} else
		return false;
}

unsigned short GetNetAddrPort(const NetworkAddr& addr) {
	if(getNLaddr(addr) == NULL)
		return 0;
	else
		return nlGetPortFromAddr(getNLaddr(addr));
}

bool SetNetAddrPort(NetworkAddr& addr, unsigned short port) {
	if(getNLaddr(addr) == NULL)
		return false;
	else
		return (nlSetAddrPort(getNLaddr(addr), port) != NL_FALSE);
}

bool AreNetAddrEqual(const NetworkAddr& addr1, const NetworkAddr& addr2) {
	if(getNLaddr(addr1) == getNLaddr(addr2))
		return true;
	else {
		return (nlAddrCompare(getNLaddr(addr1), getNLaddr(addr2)) != NL_FALSE);
	}
}




Event<> onDnsReady;

// copied from HawkNL sock.c and modified to not use nlStringToNetAddr
static bool GetAddrFromNameAsync_Internal(const NLchar* name, NLaddress* address) {
	struct hostent *hostentry;
    NLushort    port = 0;
    int			pos;
    NLbyte      temp[NL_MAX_STRING_LENGTH];
	
#ifdef _UNICODE
    /* convert from wide char string to multibyte char string */
    (void)wcstombs(temp, (const NLchar *)name, NL_MAX_STRING_LENGTH);
#else
    strncpy(temp, name, NL_MAX_STRING_LENGTH);
#endif
    temp[NL_MAX_STRING_LENGTH - 1] = (NLbyte)'\0';
    pos = (int)strcspn(temp, (const char *)":");
    if(pos > 0)
    {
        NLbyte      *p = &temp[pos+1];
		
        temp[pos] = (NLbyte)'\0';
        (void)sscanf(p, "%hu", &port);
    }
    hostentry = gethostbyname((const char *)temp);
	
    if(hostentry != NULL)
    {
        ((struct sockaddr_in *)address)->sin_family = AF_INET;
        ((struct sockaddr_in *)address)->sin_port = htons(port);
        ((struct sockaddr_in *)address)->sin_addr.s_addr = *(NLulong *)hostentry->h_addr_list[0];
        address->valid = NL_TRUE;
    }
    else
    {
        ((struct sockaddr_in *)address)->sin_family = AF_INET;
        ((struct sockaddr_in *)address)->sin_addr.s_addr = INADDR_NONE;
        ((struct sockaddr_in *)address)->sin_port = 0;
        nlSetError(NL_SYSTEM_ERROR);
        return false;
    }
    return true;
}

bool GetNetAddrFromNameAsync(const std::string& name, NetworkAddr& addr)
{
	// We don't use nlGetAddrFromNameAsync here because we have to use SmartPointers
	// The problem is, if you call this and then delete the memory of the network address
	// while the thread isn't ready, it will write after to deleted memory.

	if(getNLaddr(addr) == NULL)
		return false;

	if(name == "") {
		SetNetAddrValid(addr, false);
		return false;
	}
	
	if (StringToNetAddr(name, addr))  {
		return true;
	}
	ResetSocketError(); // Clear the bad address error
	
	if(GetFromDnsCache(name, addr)) {
		SetNetAddrValid(addr, true);
		return true;
	}

    getNLaddr(addr)->valid = NL_FALSE;

	struct GetAddrFromNameAsync_Executer : Task {
		std::string addr_name;
		NetAddrSmartPtr address;
		
		int handle() {
			if(GetAddrFromNameAsync_Internal(addr_name.c_str(), address.get())) {
				// TODO: we use default DNS record expire time of 1 hour, we should include some DNS client to make it in correct way
				AddToDnsCache(addr_name, NetworkAddr(address));
			}
			
			// TODO: handle failures here? there should be, but we only have the valid field
			// For now, we leave it at false, so the timeout handling will just occur.
			
			if(SdlNetEvent_Inited) {
				// push a net event
				onDnsReady.pushToMainQueue(EventData());
			}
			
			return 0;
		}
	};
	GetAddrFromNameAsync_Executer* data = new GetAddrFromNameAsync_Executer();
	if(data == NULL) return false;
	data->name = "GetNetAddrFromNameAsync for " + name;
    data->addr_name = name;
    data->address = NetworkAddrData(addr);

	taskManager->start(data, false);
    return true;
}


bool isDataAvailable(NetworkSocket sock) {
	NLint group = nlGroupCreate();
	nlGroupAddSocket( group, getNLsocket(sock) );
	NLsocket sock_out[2];
	int ret = nlPollGroup( group, NL_READ_STATUS, sock_out, 1, 0 );
	nlGroupDestroy(group);
	return ret > 0;
}

// In some cases, e.g. if the network is not connected, some IPs are
// not available. Most likely, without any network, you just have
// 127.* in the routing table.
bool IsNetAddrAvailable(const NetworkAddr& addr) {
	// TODO: implement...
	return true;
}

