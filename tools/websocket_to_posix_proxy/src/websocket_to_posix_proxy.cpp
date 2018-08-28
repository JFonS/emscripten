#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "posix_sockets.h"
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <netdb.h>

#include "websocket_to_posix_proxy.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

uint64_t ntoh64(uint64_t x)
{
  return ntohl(x>>32) | ((uint64_t)ntohl(x&0xFFFFFFFFu) << 32);
}

#define POSIX_SOCKET_MSG_SOCKET 1
#define POSIX_SOCKET_MSG_SOCKETPAIR 2
#define POSIX_SOCKET_MSG_SHUTDOWN 3
#define POSIX_SOCKET_MSG_BIND 4
#define POSIX_SOCKET_MSG_CONNECT 5
#define POSIX_SOCKET_MSG_LISTEN 6
#define POSIX_SOCKET_MSG_ACCEPT 7
#define POSIX_SOCKET_MSG_GETSOCKNAME 8
#define POSIX_SOCKET_MSG_GETPEERNAME 9
#define POSIX_SOCKET_MSG_SEND 10
#define POSIX_SOCKET_MSG_RECV 11
#define POSIX_SOCKET_MSG_SENDTO 12
#define POSIX_SOCKET_MSG_RECVFROM 13
#define POSIX_SOCKET_MSG_SENDMSG 14
#define POSIX_SOCKET_MSG_RECVMSG 15
#define POSIX_SOCKET_MSG_GETSOCKOPT 16
#define POSIX_SOCKET_MSG_SETSOCKOPT 17
#define POSIX_SOCKET_MSG_GETADDRINFO 18
#define POSIX_SOCKET_MSG_GETNAMEINFO 19

#define MAX_SOCKADDR_SIZE 256
#define MAX_OPTIONVALUE_SIZE 16

#define POSIX_SOCKET_DEBUG

struct SocketCallHeader
{
  int callId;
  int function;
};

void WebSocketMessageUnmaskPayload(uint8_t *payload, uint64_t payloadLength, uint32_t maskingKey)
{
  uint8_t maskingKey8[4];
  memcpy(maskingKey8, &maskingKey, 4);
  uint32_t *data_u32 = (uint32_t *)payload;
  uint32_t *end_u32 = (uint32_t *)((uintptr_t)(payload + (payloadLength & ~3u)));

  while(data_u32 < end_u32)
    *data_u32++ ^= maskingKey;

  uint8_t *end = payload + payloadLength;
  uint8_t *data = (uint8_t *)data_u32;
  while(data < end)
  {
    *data ^= maskingKey8[(data-payload) % 4];
    ++data;
  }
}

void SendWebSocketMessage(int client_fd, void *buf, uint64_t numBytes)
{
  uint8_t headerData[sizeof(WebSocketMessageHeader) + 8/*possible extended length*/] = {};
  WebSocketMessageHeader *header = (WebSocketMessageHeader *)headerData;
  header->opcode = 0x02;
  header->fin = 1;
  int headerBytes = 2;

  if (numBytes < 126)
    header->payloadLength = numBytes;
  else if (numBytes <= 65535)
  {
    header->payloadLength = 126;
    *(uint16_t*)(headerData+headerBytes) = htons((unsigned short)numBytes);
    headerBytes += 2;
  }
  else
  {
    header->payloadLength = 127;
    *(uint64_t*)(headerData+headerBytes) = hton64(numBytes);
    headerBytes += 8;
  }

#if 1
  printf("Sending %llu bytes message (%llu bytes of payload) to WebSocket\n", headerBytes + numBytes, numBytes);

  printf("Header:");
  for(int i = 0; i < headerBytes; ++i)
    printf(" %02X", headerData[i]);

  printf("\nPayload:");
  for(int i = 0; i < numBytes; ++i)
    printf(" %02X", ((unsigned char*)buf)[i]);
  printf("\n");
#endif

  send(client_fd, (const char*)headerData, headerBytes, 0); // header
  send(client_fd, (const char*)buf, (int)numBytes, 0); // payload
}

#define PRINT_ERRNO() do { \
  int Errno = errno; \
  printf("Call failed! errno: %s(%d)\n", strerror(Errno), Errno); \
  } while(0)

#define MUSL_PF_UNSPEC       0
#define MUSL_PF_LOCAL        1
#define MUSL_PF_UNIX         PF_LOCAL
#define MUSL_PF_FILE         PF_LOCAL
#define MUSL_PF_INET         2
#define MUSL_PF_AX25         3
#define MUSL_PF_IPX          4
#define MUSL_PF_APPLETALK    5
#define MUSL_PF_NETROM       6
#define MUSL_PF_BRIDGE       7
#define MUSL_PF_ATMPVC       8
#define MUSL_PF_X25          9
#define MUSL_PF_INET6        10
#define MUSL_PF_ROSE         11
#define MUSL_PF_DECnet       12
#define MUSL_PF_NETBEUI      13
#define MUSL_PF_SECURITY     14
#define MUSL_PF_KEY          15
#define MUSL_PF_NETLINK      16
#define MUSL_PF_ROUTE        PF_NETLINK
#define MUSL_PF_PACKET       17
#define MUSL_PF_ASH          18
#define MUSL_PF_ECONET       19
#define MUSL_PF_ATMSVC       20
#define MUSL_PF_RDS          21
#define MUSL_PF_SNA          22
#define MUSL_PF_IRDA         23
#define MUSL_PF_PPPOX        24
#define MUSL_PF_WANPIPE      25
#define MUSL_PF_LLC          26
#define MUSL_PF_IB           27
#define MUSL_PF_MPLS         28
#define MUSL_PF_CAN          29
#define MUSL_PF_TIPC         30
#define MUSL_PF_BLUETOOTH    31
#define MUSL_PF_IUCV         32
#define MUSL_PF_RXRPC        33
#define MUSL_PF_ISDN         34
#define MUSL_PF_PHONET       35
#define MUSL_PF_IEEE802154   36
#define MUSL_PF_CAIF         37
#define MUSL_PF_ALG          38
#define MUSL_PF_NFC          39
#define MUSL_PF_VSOCK        40
#define MUSL_PF_KCM          41
#define MUSL_PF_MAX          42

#define MUSL_AF_UNSPEC       MUSL_PF_UNSPEC
#define MUSL_AF_LOCAL        MUSL_PF_LOCAL
#define MUSL_AF_UNIX         MUSL_AF_LOCAL
#define MUSL_AF_FILE         MUSL_AF_LOCAL
#define MUSL_AF_INET         MUSL_PF_INET
#define MUSL_AF_AX25         MUSL_PF_AX25
#define MUSL_AF_IPX          MUSL_PF_IPX
#define MUSL_AF_APPLETALK    MUSL_PF_APPLETALK
#define MUSL_AF_NETROM       MUSL_PF_NETROM
#define MUSL_AF_BRIDGE       MUSL_PF_BRIDGE
#define MUSL_AF_ATMPVC       MUSL_PF_ATMPVC
#define MUSL_AF_X25          MUSL_PF_X25
#define MUSL_AF_INET6        MUSL_PF_INET6
#define MUSL_AF_ROSE         MUSL_PF_ROSE
#define MUSL_AF_DECnet       MUSL_PF_DECnet
#define MUSL_AF_NETBEUI      MUSL_PF_NETBEUI
#define MUSL_AF_SECURITY     MUSL_PF_SECURITY
#define MUSL_AF_KEY          MUSL_PF_KEY
#define MUSL_AF_NETLINK      MUSL_PF_NETLINK
#define MUSL_AF_ROUTE        MUSL_PF_ROUTE
#define MUSL_AF_PACKET       MUSL_PF_PACKET
#define MUSL_AF_ASH          MUSL_PF_ASH
#define MUSL_AF_ECONET       MUSL_PF_ECONET
#define MUSL_AF_ATMSVC       MUSL_PF_ATMSVC
#define MUSL_AF_RDS          MUSL_PF_RDS
#define MUSL_AF_SNA          MUSL_PF_SNA
#define MUSL_AF_IRDA         MUSL_PF_IRDA
#define MUSL_AF_PPPOX        MUSL_PF_PPPOX
#define MUSL_AF_WANPIPE      MUSL_PF_WANPIPE
#define MUSL_AF_LLC          MUSL_PF_LLC
#define MUSL_AF_IB           MUSL_PF_IB
#define MUSL_AF_MPLS         MUSL_PF_MPLS
#define MUSL_AF_CAN          MUSL_PF_CAN
#define MUSL_AF_TIPC         MUSL_PF_TIPC
#define MUSL_AF_BLUETOOTH    MUSL_PF_BLUETOOTH
#define MUSL_AF_IUCV         MUSL_PF_IUCV
#define MUSL_AF_RXRPC        MUSL_PF_RXRPC
#define MUSL_AF_ISDN         MUSL_PF_ISDN
#define MUSL_AF_PHONET       MUSL_PF_PHONET
#define MUSL_AF_IEEE802154   MUSL_PF_IEEE802154
#define MUSL_AF_CAIF         MUSL_PF_CAIF
#define MUSL_AF_ALG          MUSL_PF_ALG
#define MUSL_AF_NFC          MUSL_PF_NFC
#define MUSL_AF_VSOCK        MUSL_PF_VSOCK
#define MUSL_AF_KCM          MUSL_PF_KCM
#define MUSL_AF_MAX          MUSL_PF_MAX

static int Translate_Socket_Domain(int domain)
{
  switch(domain)
  {
//  case MUSL_PF_UNSPEC: return PF_UNSPEC;
//  case MUSL_PF_LOCAL: return PF_LOCAL;
//  case MUSL_PF_UNIX: return PF_UNIX;
//  case MUSL_PF_FILE: return PF_FILE;
//  case MUSL_PF_INET: return PF_INET;
//  case MUSL_PF_AX25: return PF_AX25;
//  case MUSL_PF_IPX: return PF_IPX;
//  case MUSL_PF_APPLETALK: return PF_APPLETALK;
//  case MUSL_PF_NETROM: return PF_NETROM;
//  case MUSL_PF_BRIDGE: return PF_BRIDGE;
//  case MUSL_PF_ATMPVC: return PF_ATMPVC;
//  case MUSL_PF_X25: return PF_X25;
//  case MUSL_PF_INET6: return PF_INET6;
//  case MUSL_PF_ROSE: return PF_ROSE;
//  case MUSL_PF_DECnet: return PF_DECnet;
//  case MUSL_PF_NETBEUI: return PF_NETBEUI;
//  case MUSL_PF_SECURITY: return PF_SECURITY;
//  case MUSL_PF_KEY: return PF_KEY;
//  case MUSL_PF_NETLINK: return PF_NETLINK;
//  case MUSL_PF_ROUTE: return PF_ROUTE;
//  case MUSL_PF_PACKET: return PF_PACKET;
//  case MUSL_PF_ASH: return PF_ASH;
//  case MUSL_PF_ECONET: return PF_ECONET;
//  case MUSL_PF_ATMSVC: return PF_ATMSVC;
//  case MUSL_PF_RDS: return PF_RDS;
//  case MUSL_PF_SNA: return PF_SNA;
//  case MUSL_PF_IRDA: return PF_IRDA;
//  case MUSL_PF_PPPOX: return PF_PPPOX;
//  case MUSL_PF_WANPIPE: return PF_WANPIPE;
//  case MUSL_PF_LLC: return PF_LLC;
//  case MUSL_PF_IB: return PF_IB;
//  case MUSL_PF_MPLS: return PF_MPLS;
//  case MUSL_PF_CAN: return PF_CAN;
//  case MUSL_PF_TIPC: return PF_TIPC;
//  case MUSL_PF_BLUETOOTH: return PF_BLUETOOTH;
//  case MUSL_PF_IUCV: return PF_IUCV;
//  case MUSL_PF_RXRPC: return PF_RXRPC;
//  case MUSL_PF_ISDN: return PF_ISDN;
//  case MUSL_PF_PHONET: return PF_PHONET;
//  case MUSL_PF_IEEE802154: return PF_IEEE802154;
//  case MUSL_PF_CAIF: return PF_CAIF;
//  case MUSL_PF_ALG: return PF_ALG;
//  case MUSL_PF_NFC: return PF_NFC;
//  case MUSL_PF_VSOCK: return PF_VSOCK;
//  case MUSL_PF_KCM: return PF_KCM;
//  case MUSL_PF_MAX: return PF_MAX;

  case MUSL_AF_UNSPEC: return AF_UNSPEC;
  case MUSL_AF_LOCAL: return AF_LOCAL;
//  case MUSL_AF_UNIX: return AF_UNIX;
//  case MUSL_AF_FILE: return AF_FILE;
  case MUSL_AF_INET: return AF_INET;
//  case MUSL_AF_AX25: return AF_AX25;
  case MUSL_AF_IPX: return AF_IPX;
  case MUSL_AF_APPLETALK: return AF_APPLETALK;
//  case MUSL_AF_NETROM: return AF_NETROM;
//  case MUSL_AF_BRIDGE: return AF_BRIDGE;
//  case MUSL_AF_ATMPVC: return AF_ATMPVC;
//  case MUSL_AF_X25: return AF_X25;
  case MUSL_AF_INET6: return AF_INET6;
//  case MUSL_AF_ROSE: return AF_ROSE;
  case MUSL_AF_DECnet: return AF_DECnet;
//  case MUSL_AF_NETBEUI: return AF_NETBEUI;
//  case MUSL_AF_SECURITY: return AF_SECURITY;
//  case MUSL_AF_KEY: return AF_KEY;
//  case MUSL_AF_NETLINK: return AF_NETLINK;
//  case MUSL_AF_ROUTE: return AF_ROUTE;
//  case MUSL_AF_PACKET: return AF_PACKET;
//  case MUSL_AF_ASH: return AF_ASH;
//  case MUSL_AF_ECONET: return AF_ECONET;
//  case MUSL_AF_ATMSVC: return AF_ATMSVC;
//  case MUSL_AF_RDS: return AF_RDS;
  case MUSL_AF_SNA: return AF_SNA;
//  case MUSL_AF_IRDA: return AF_IRDA;
//  case MUSL_AF_PPPOX: return AF_PPPOX;
//  case MUSL_AF_WANPIPE: return AF_WANPIPE;
//  case MUSL_AF_LLC: return AF_LLC;
//  case MUSL_AF_IB: return AF_IB;
//  case MUSL_AF_MPLS: return AF_MPLS;
//  case MUSL_AF_CAN: return AF_CAN;
//  case MUSL_AF_TIPC: return AF_TIPC;
//  case MUSL_AF_BLUETOOTH: return AF_BLUETOOTH;
//  case MUSL_AF_IUCV: return AF_IUCV;
//  case MUSL_AF_RXRPC: return AF_RXRPC;
  case MUSL_AF_ISDN: return AF_ISDN;
//  case MUSL_AF_PHONET: return AF_PHONET;
//  case MUSL_AF_IEEE802154: return AF_IEEE802154;
//  case MUSL_AF_CAIF: return AF_CAIF;
//  case MUSL_AF_ALG: return AF_ALG;
//  case MUSL_AF_NFC: return AF_NFC;
//  case MUSL_AF_VSOCK: return AF_VSOCK;
//  case MUSL_AF_KCM: return AF_KCM;
  case MUSL_AF_MAX: return AF_MAX;
  default:
    fprintf(stderr, "Uncrecognized Socket Domain %d!\n", domain);
    return domain;
  }
}

#define MUSL_SOCK_STREAM    1
#define MUSL_SOCK_DGRAM     2
#define MUSL_SOCK_RAW       3
#define MUSL_SOCK_RDM       4
#define MUSL_SOCK_SEQPACKET 5
#define MUSL_SOCK_DCCP      6
#define MUSL_SOCK_PACKET    10
#define MUSL_SOCK_CLOEXEC   02000000
#define MUSL_SOCK_NONBLOCK  04000

static int Translate_Socket_Type(int type)
{
  if ((type & MUSL_SOCK_CLOEXEC) != 0)
  {
    fprintf(stderr, "Unsupported MUSL SOCK_CLOEXEC passed!\n");
    type &= ~MUSL_SOCK_CLOEXEC;
  }
  if ((type & MUSL_SOCK_NONBLOCK) != 0)
  {
    fprintf(stderr, "Unsupported MUSL SOCK_NONBLOCK passed!\n");
    type &= ~MUSL_SOCK_NONBLOCK;
  }

  switch(type)
  {
  case MUSL_SOCK_STREAM: return SOCK_STREAM;
  case MUSL_SOCK_DGRAM: return SOCK_DGRAM;
  case MUSL_SOCK_RAW: return SOCK_RAW;
  case MUSL_SOCK_RDM: return SOCK_RDM;
  case MUSL_SOCK_SEQPACKET: return SOCK_SEQPACKET;
//  case MUSL_SOCK_DCCP: return SOCK_DCCP;
//  case MUSL_SOCK_PACKET: return SOCK_PACKET;
  default:
    fprintf(stderr, "Uncrecognized socket type %d!\n", type);
    return type;
  }
}

static int Translate_Socket_Protocol(int protocol)
{
  switch(protocol)
  {
    case 0: return 0;
    default:
    fprintf(stderr, "Unrecognized socket protocol %d!\n", protocol);
    return protocol;
  }
}

#define MUSL_SOL_SOCKET      1
#define MUSL_SOL_IP          0
#define MUSL_SOL_IPV6        41
#define MUSL_SOL_ICMPV6      58
#define MUSL_SOL_RAW         255
#define MUSL_SOL_DECNET      261
#define MUSL_SOL_X25         262
#define MUSL_SOL_PACKET      263
#define MUSL_SOL_ATM         264
#define MUSL_SOL_AAL         265
#define MUSL_SOL_IRDA        266
#define MUSL_SOL_NETBEUI     267
#define MUSL_SOL_LLC         268
#define MUSL_SOL_DCCP        269
#define MUSL_SOL_NETLINK     270
#define MUSL_SOL_TIPC        271
#define MUSL_SOL_RXRPC       272
#define MUSL_SOL_PPPOL2TP    273
#define MUSL_SOL_BLUETOOTH   274
#define MUSL_SOL_PNPIPE      275
#define MUSL_SOL_RDS         276
#define MUSL_SOL_IUCV        277
#define MUSL_SOL_CAIF        278
#define MUSL_SOL_ALG         279
#define MUSL_SOL_NFC         280
#define MUSL_SOL_KCM         281

static int Translate_Socket_Level(int level)
{
  switch(level)
  {
  case MUSL_SOL_SOCKET: return SOL_SOCKET;
//  case MUSL_SOL_IP: return SOL_IP;
//  case MUSL_SOL_IPV6: return SOL_IPV6;
//  case MUSL_SOL_ICMPV6: return SOL_ICMPV6;
//  case MUSL_SOL_RAW: return SOL_RAW;
//  case MUSL_SOL_DECNET: return SOL_DECNET;
//  case MUSL_SOL_X25: return SOL_X25;
//  case MUSL_SOL_PACKET: return SOL_PACKET;
//  case MUSL_SOL_ATM: return SOL_ATM;
//  case MUSL_SOL_AAL: return SOL_AAL;
//  case MUSL_SOL_IRDA: return SOL_IRDA;
//  case MUSL_SOL_NETBEUI: return SOL_NETBEUI;
//  case MUSL_SOL_LLC: return SOL_LLC;
//  case MUSL_SOL_DCCP: return SOL_DCCP;
//  case MUSL_SOL_NETLINK: return SOL_NETLINK;
//  case MUSL_SOL_TIPC: return SOL_TIPC;
//  case MUSL_SOL_RXRPC: return SOL_RXRPC;
//  case MUSL_SOL_PPPOL2TP: return SOL_PPPOL2TP;
//  case MUSL_SOL_BLUETOOTH: return SOL_BLUETOOTH;
//  case MUSL_SOL_PNPIPE: return SOL_PNPIPE;
//  case MUSL_SOL_RDS: return SOL_RDS;
//  case MUSL_SOL_IUCV: return SOL_IUCV;
//  case MUSL_SOL_CAIF: return SOL_CAIF;
//  case MUSL_SOL_ALG: return SOL_ALG;
//  case MUSL_SOL_NFC: return SOL_NFC;
//  case MUSL_SOL_KCM: return SOL_KCM;
  default:
    fprintf(stderr, "Uncrecognized socket level %d!\n", level);
    return level;
  }
}

#define MUSL_SO_DEBUG        1
#define MUSL_SO_REUSEADDR    2
#define MUSL_SO_TYPE         3
#define MUSL_SO_ERROR        4
#define MUSL_SO_DONTROUTE    5
#define MUSL_SO_BROADCAST    6
#define MUSL_SO_SNDBUF       7
#define MUSL_SO_RCVBUF       8
#define MUSL_SO_KEEPALIVE    9
#define MUSL_SO_OOBINLINE    10
#define MUSL_SO_NO_CHECK     11
#define MUSL_SO_PRIORITY     12
#define MUSL_SO_LINGER       13
#define MUSL_SO_BSDCOMPAT    14
#define MUSL_SO_REUSEPORT    15
#define MUSL_SO_PASSCRED     16
#define MUSL_SO_PEERCRED     17
#define MUSL_SO_RCVLOWAT     18
#define MUSL_SO_SNDLOWAT     19
#define MUSL_SO_RCVTIMEO     20
#define MUSL_SO_SNDTIMEO     21
#define MUSL_SO_ACCEPTCONN   30
#define MUSL_SO_SNDBUFFORCE  32
#define MUSL_SO_RCVBUFFORCE  33
#define MUSL_SO_PROTOCOL     38
#define MUSL_SO_DOMAIN       39
#define MUSL_SO_SECURITY_AUTHENTICATION              22
#define MUSL_SO_SECURITY_ENCRYPTION_TRANSPORT        23
#define MUSL_SO_SECURITY_ENCRYPTION_NETWORK          24
#define MUSL_SO_BINDTODEVICE 25
#define MUSL_SO_ATTACH_FILTER        26
#define MUSL_SO_DETACH_FILTER        27
#define MUSL_SO_PEERNAME             28
#define MUSL_SO_TIMESTAMP            29
#define MUSL_SO_PEERSEC              31
#define MUSL_SO_PASSSEC              34
#define MUSL_SO_TIMESTAMPNS          35
#define MUSL_SO_MARK                 36
#define MUSL_SO_TIMESTAMPING         37
#define MUSL_SO_RXQ_OVFL             40
#define MUSL_SO_WIFI_STATUS          41
#define MUSL_SO_PEEK_OFF             42
#define MUSL_SO_NOFCS                43
#define MUSL_SO_LOCK_FILTER          44
#define MUSL_SO_SELECT_ERR_QUEUE     45
#define MUSL_SO_BUSY_POLL            46
#define MUSL_SO_MAX_PACING_RATE      47
#define MUSL_SO_BPF_EXTENSIONS       48
#define MUSL_SO_INCOMING_CPU         49
#define MUSL_SO_ATTACH_BPF           50
#define MUSL_SO_ATTACH_REUSEPORT_CBPF 51
#define MUSL_SO_ATTACH_REUSEPORT_EBPF 52
#define MUSL_SO_CNX_ADVICE           53

static int Translate_SOL_SOCKET_option(int sockopt)
{
  switch(sockopt)
  {
  case MUSL_SO_DEBUG: return SO_DEBUG;
  case MUSL_SO_REUSEADDR: return SO_REUSEADDR;
  case MUSL_SO_TYPE: return SO_TYPE;
  case MUSL_SO_ERROR: return SO_ERROR;
  case MUSL_SO_DONTROUTE: return SO_DONTROUTE;
  case MUSL_SO_BROADCAST: return SO_BROADCAST;
  case MUSL_SO_SNDBUF: return SO_SNDBUF;
  case MUSL_SO_RCVBUF: return SO_RCVBUF;
  case MUSL_SO_KEEPALIVE: return SO_KEEPALIVE;
  case MUSL_SO_OOBINLINE: return SO_OOBINLINE;
//  case MUSL_SO_NO_CHECK: return SO_NO_CHECK;
//  case MUSL_SO_PRIORITY: return SO_PRIORITY;
  case MUSL_SO_LINGER: return SO_LINGER;
//  case MUSL_SO_BSDCOMPAT: return SO_BSDCOMPAT;
  case MUSL_SO_REUSEPORT: return SO_REUSEPORT;
//  case MUSL_SO_PASSCRED: return SO_PASSCRED;
//  case MUSL_SO_PEERCRED: return SO_PEERCRED;
  case MUSL_SO_RCVLOWAT: return SO_RCVLOWAT;
  case MUSL_SO_SNDLOWAT: return SO_SNDLOWAT;
  case MUSL_SO_RCVTIMEO: return SO_RCVTIMEO;
  case MUSL_SO_SNDTIMEO: return SO_SNDTIMEO;
  case MUSL_SO_ACCEPTCONN: return SO_ACCEPTCONN;
//  case MUSL_SO_SNDBUFFORCE: return SO_SNDBUFFORCE;
//  case MUSL_SO_RCVBUFFORCE: return SO_RCVBUFFORCE;
//  case MUSL_SO_PROTOCOL: return SO_PROTOCOL;
//  case MUSL_SO_DOMAIN: return SO_DOMAIN;
//  case MUSL_SO_SECURITY_AUTHENTICATION: return SO_SECURITY_AUTHENTICATION;
//  case MUSL_SO_SECURITY_ENCRYPTION_TRANSPORT: return SO_SECURITY_ENCRYPTION_TRANSPORT;
//  case MUSL_SO_SECURITY_ENCRYPTION_NETWORK: return SO_SECURITY_ENCRYPTION_NETWORK;
//  case MUSL_SO_BINDTODEVICE: return SO_BINDTODEVICE;
//  case MUSL_SO_ATTACH_FILTER: return SO_ATTACH_FILTER;
//  case MUSL_SO_DETACH_FILTER: return SO_DETACH_FILTER;
//  case MUSL_SO_PEERNAME: return SO_PEERNAME;
  case MUSL_SO_TIMESTAMP: return SO_TIMESTAMP;
//  case MUSL_SO_PEERSEC: return SO_PEERSEC;
//  case MUSL_SO_PASSSEC: return SO_PASSSEC;
//  case MUSL_SO_TIMESTAMPNS: return SO_TIMESTAMPNS;
//  case MUSL_SO_MARK: return SO_MARK;
//  case MUSL_SO_TIMESTAMPING: return SO_TIMESTAMPING;
//  case MUSL_SO_RXQ_OVFL: return SO_RXQ_OVFL;
//  case MUSL_SO_WIFI_STATUS: return SO_WIFI_STATUS;
//  case MUSL_SO_PEEK_OFF: return SO_PEEK_OFF;
//  case MUSL_SO_NOFCS: return SO_NOFCS;
//  case MUSL_SO_LOCK_FILTER: return SO_LOCK_FILTER;
//  case MUSL_SO_SELECT_ERR_QUEUE: return SO_SELECT_ERR_QUEUE;
//  case MUSL_SO_BUSY_POLL: return SO_BUSY_POLL;
//  case MUSL_SO_MAX_PACING_RATE: return SO_MAX_PACING_RATE;
//  case MUSL_SO_BPF_EXTENSIONS: return SO_BPF_EXTENSIONS;
//  case MUSL_SO_INCOMING_CPU: return SO_INCOMING_CPU;
//  case MUSL_SO_ATTACH_BPF: return SO_ATTACH_BPF;
//  case MUSL_SO_ATTACH_REUSEPORT_CBPF: return SO_ATTACH_REUSEPORT_CBPF;
//  case MUSL_SO_ATTACH_REUSEPORT_EBPF: return SO_ATTACH_REUSEPORT_EBPF;
//  case MUSL_SO_CNX_ADVICE: return SO_CNX_ADVICE;
  default:
    fprintf(stderr, "Unrecognized SOL_SOCKET option %d!\n", sockopt);
    return sockopt;
  }
}

void Socket(int client_fd, uint8_t *data, uint64_t numBytes) // int socket(int domain, int type, int protocol);
{
  struct MSG {
    SocketCallHeader header;
    int domain;
    int type;
    int protocol;
  };
  MSG *d = (MSG*)data;

  d->domain = Translate_Socket_Domain(d->domain);
  d->type = Translate_Socket_Type(d->type);
  d->protocol = Translate_Socket_Protocol(d->protocol);
  int ret = socket(d->domain, d->type, d->protocol);

#ifdef POSIX_SOCKET_DEBUG
  printf("socket(domain=%d,type=%d,protocol=%d)->%d\n", d->domain, d->type, d->protocol, ret);
  if (ret < 0) PRINT_ERRNO();
#endif

  struct {
    int callId;
    int ret;
    int errno_;
  } r;
  r.callId = d->header.callId;
  r.ret = ret;
  r.errno_ = (ret != 0) ? errno : 0;
  SendWebSocketMessage(client_fd, &r, sizeof(r));
}

void Socketpair(int client_fd, uint8_t *data, uint64_t numBytes) // int socketpair(int domain, int type, int protocol, int socket_vector[2]);
{
  struct MSG {
    SocketCallHeader header;
    int domain;
    int type;
    int protocol;
  };
  MSG *d = (MSG*)data;

  int socket_vector[2];

#ifdef _MSC_VER
  printf("TODO implement socketpair() on Windows\n");
  int ret = -1;
#else
  d->domain = Translate_Socket_Domain(d->domain);
  d->type = Translate_Socket_Type(d->type);
  d->protocol = Translate_Socket_Protocol(d->protocol);
  int ret = socketpair(d->domain, d->type, d->protocol, socket_vector);
#endif

#ifdef POSIX_SOCKET_DEBUG
  printf("socketpair(domain=%d,type=%d,protocol=%d, socket_vector=[%d,%d])->%d\n", d->domain, d->type, d->protocol, socket_vector[0], socket_vector[1], ret);
  if (ret != 0) PRINT_ERRNO();
#endif

  struct {
    int callId;
    int ret;
    int errno_;
    int sv[2];
  } r;
  r.callId = d->header.callId;
  r.ret = ret;
  r.errno_ = (ret != 0) ? errno : 0;
  r.sv[0] = socket_vector[0];
  r.sv[1] = socket_vector[1];
  SendWebSocketMessage(client_fd, &r, sizeof(r));
}

#define MUSL_SHUT_RD 0
#define MUSL_SHUT_WR 1
#define MUSL_SHUT_RDWR 2

static int Translate_Shutdown_How(int how)
{
  switch(how)
  {
  case MUSL_SHUT_RD: return SHUT_RD;
  case MUSL_SHUT_WR: return SHUT_WR;
  case MUSL_SHUT_RDWR: return SHUT_RDWR;
  default:
    fprintf(stderr, "Unrecognized shutdown() how option %d!\n", how);
    return how;
  }
}

void Shutdown(int client_fd, uint8_t *data, uint64_t numBytes) // int shutdown(int socket, int how);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    int how;
  };
  MSG *d = (MSG*)data;

  d->how = Translate_Shutdown_How(d->how);
  int ret = shutdown(d->socket, d->how);
#ifdef POSIX_SOCKET_DEBUG
  printf("shutdown(socket=%d,how=%d)->%d\n", d->socket, d->how, ret);
  if (ret != 0) PRINT_ERRNO();
#endif

  struct {
    int callId;
    int ret;
    int errno_;
  } r;
  r.callId = d->header.callId;
  r.ret = ret;
  r.errno_ = (ret != 0) ? errno : 0;
  SendWebSocketMessage(client_fd, &r, sizeof(r));
}

void Bind(int client_fd, uint8_t *data, uint64_t numBytes) // int bind(int socket, const struct sockaddr *address, socklen_t address_len);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    uint32_t/*socklen_t*/ address_len;
    uint8_t address[];
  };
  MSG *d = (MSG*)data;

  int ret = bind(d->socket, (sockaddr*)d->address, d->address_len);
#ifdef POSIX_SOCKET_DEBUG
  printf("bind(socket=%d,address=%p,address_len=%d)->%d\n", d->socket, d->address, d->address_len, ret);
  if (ret != 0) PRINT_ERRNO();
#endif

  struct {
    int callId;
    int ret;
    int errno_;
  } r;
  r.callId = d->header.callId;
  r.ret = ret;
  r.errno_ = (ret != 0) ? errno : 0;
  SendWebSocketMessage(client_fd, &r, sizeof(r));
}

void Connect(int client_fd, uint8_t *data, uint64_t numBytes) // int connect(int socket, const struct sockaddr *address, socklen_t address_len);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    uint32_t/*socklen_t*/ address_len;
    uint8_t address[];
  };
  MSG *d = (MSG*)data;

  int actualAddressLen = MIN(d->address_len, (uint32_t)numBytes - sizeof(MSG));

  int ret = connect(d->socket, (sockaddr*)d->address, actualAddressLen);
#ifdef POSIX_SOCKET_DEBUG
  printf("connect(socket=%d,address=%p,address_len=%d)->%d\n", d->socket, d->address, d->address_len, ret);
  if (ret != 0) PRINT_ERRNO();
#endif

  struct {
    int callId;
    int ret;
    int errno_;
  } r;
  r.callId = d->header.callId;
  r.ret = ret;
  r.errno_ = (ret != 0) ? errno : 0;
  SendWebSocketMessage(client_fd, &r, sizeof(r));
}

void Listen(int client_fd, uint8_t *data, uint64_t numBytes) // int listen(int socket, int backlog);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    int backlog;
  };
  MSG *d = (MSG*)data;

  int ret = listen(d->socket, d->backlog);
#ifdef POSIX_SOCKET_DEBUG
  printf("listen(socket=%d,backlog=%d)->%d\n", d->socket, d->backlog, ret);
  if (ret != 0) PRINT_ERRNO();
#endif

  struct {
    int callId;
    int ret;
    int errno_;
  } r;
  r.callId = d->header.callId;
  r.ret = ret;
  r.errno_ = (ret != 0) ? errno : 0;
  SendWebSocketMessage(client_fd, &r, sizeof(r));
}

void Accept(int client_fd, uint8_t *data, uint64_t numBytes) // int accept(int socket, struct sockaddr *address, socklen_t *address_len);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    uint32_t/*socklen_t*/ address_len;
  };
  MSG *d = (MSG*)data;

  uint8_t address[MAX_SOCKADDR_SIZE];

  socklen_t addressLen = (socklen_t)d->address_len;
  int ret = accept(d->socket, (sockaddr*)address, &addressLen);

#ifdef POSIX_SOCKET_DEBUG
  printf("accept(socket=%d,address=%p,address_len=%u)->%d\n", d->socket, address, d->address_len, ret);
  if (ret < 0) PRINT_ERRNO();
#endif

  struct Result {
    int callId;
    int ret;
    int errno_;
    int address_len;
    uint8_t address[];
  };

  int actualAddressLen = MIN(addressLen, (socklen_t)d->address_len);
  int resultSize = sizeof(Result) + actualAddressLen;
  Result *r = (Result*)malloc(resultSize);
  r->callId = d->header.callId;
  r->ret = ret;
  r->errno_ = (ret < 0) ? errno : 0;
  r->address_len = addressLen;
  memcpy(r->address, address, actualAddressLen);
  SendWebSocketMessage(client_fd, r, resultSize);
  free(r);
}

void Getsockname(int client_fd, uint8_t *data, uint64_t numBytes) // int getsockname(int socket, struct sockaddr *address, socklen_t *address_len);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    uint32_t/*socklen_t*/ address_len;
  };
  MSG *d = (MSG*)data;

  uint8_t address[MAX_SOCKADDR_SIZE];

  socklen_t addressLen = (socklen_t)d->address_len;
  int ret = getsockname(d->socket, (sockaddr*)address, &addressLen);

#ifdef POSIX_SOCKET_DEBUG
  printf("getsockname(socket=%d,address=%p,address_len=%u)->%d\n", d->socket, address, d->address_len, ret);
  if (ret != 0) PRINT_ERRNO();
#endif

  struct Result {
    int callId;
    int ret;
    int errno_;
    int address_len;
    uint8_t address[];
  };
  int actualAddressLen = MIN(addressLen, (socklen_t)d->address_len);
  int resultSize = sizeof(Result) + actualAddressLen;
  Result *r = (Result*)malloc(resultSize);
  r->callId = d->header.callId;
  r->ret = ret;
  r->errno_ = (ret != 0) ? errno : 0;
  r->address_len = addressLen;
  memcpy(r->address, address, actualAddressLen);
  SendWebSocketMessage(client_fd, r, resultSize);
  free(r);
}

void Getpeername(int client_fd, uint8_t *data, uint64_t numBytes) // int getpeername(int socket, struct sockaddr *address, socklen_t *address_len);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    uint32_t/*socklen_t*/ address_len;
  };
  MSG *d = (MSG*)data;

  uint8_t address[MAX_SOCKADDR_SIZE];

  socklen_t addressLen = (socklen_t)d->address_len;
  int ret = getpeername(d->socket, (sockaddr*)address, &addressLen);

#ifdef POSIX_SOCKET_DEBUG
  printf("getpeername(socket=%d,address=%p,address_len=%u)->%d\n", d->socket, address, d->address_len, ret);
  if (ret != 0) PRINT_ERRNO();
#endif

  struct Result {
    int callId;
    int ret;
    int errno_;
    int address_len;
    uint8_t address[];
  };
  int actualAddressLen = MIN(addressLen, (socklen_t)d->address_len);
  int resultSize = sizeof(Result) + actualAddressLen;
  Result *r = (Result*)malloc(resultSize);
  r->callId = d->header.callId;
  r->ret = ret;
  r->errno_ = (ret != 0) ? errno : 0;
  r->address_len = addressLen;
  memcpy(r->address, address, actualAddressLen);
  SendWebSocketMessage(client_fd, r, resultSize);
  free(r);
}

void Send(int client_fd, uint8_t *data, uint64_t numBytes) // ssize_t/int send(int socket, const void *message, size_t length, int flags);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    uint32_t/*size_t*/ length;
    int flags;
    uint8_t message[];
  };
  MSG *d = (MSG*)data;

  SEND_RET_TYPE ret = send(d->socket, (const char *)d->message, d->length, d->flags);

#ifdef POSIX_SOCKET_DEBUG
  printf("send(socket=%d,message=%p,length=%zd,flags=%d)->" SEND_FORMATTING_SPECIFIER "\n", d->socket, d->message, d->length, d->flags, ret);
  if (ret < 0) PRINT_ERRNO();
#endif

  struct {
    int callId;
    int/*ssize_t/int*/ ret;
    int errno_;
  } r;
  r.callId = d->header.callId;
  r.ret = (int)ret;
  r.errno_ = (ret != 0) ? errno : 0;
  SendWebSocketMessage(client_fd, &r, sizeof(r));
}

void Recv(int client_fd, uint8_t *data, uint64_t numBytes) // ssize_t/int recv(int socket, void *buffer, size_t length, int flags);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    uint32_t/*size_t*/ length;
    int flags;
  };
  MSG *d = (MSG*)data;

  uint8_t *buffer = (uint8_t*)malloc(d->length);
  SEND_RET_TYPE ret = recv(d->socket, (char *)buffer, d->length, d->flags);

#ifdef POSIX_SOCKET_DEBUG
  printf("recv(socket=%d,buffer=%p,length=%zd,flags=%d)->" SEND_FORMATTING_SPECIFIER "\n", d->socket, buffer, d->length, d->flags, ret);
  if (ret < 0) PRINT_ERRNO();
#endif

  int receivedBytes = MAX(ret, 0);

  struct Result {
    int callId;
    int/*ssize_t/int*/ ret;
    int errno_;
    uint8_t data[];
  };
  int resultSize = sizeof(Result) + receivedBytes;
  Result *r = (Result *)malloc(resultSize);
  r->callId = d->header.callId;
  r->ret = (int)ret;
  r->errno_ = (ret != 0) ? errno : 0;
  memcpy(r->data, buffer, receivedBytes);
  free(buffer);
  SendWebSocketMessage(client_fd, r, resultSize);
  free(r);
}

void Sendto(int client_fd, uint8_t *data, uint64_t numBytes) // ssize_t/int sendto(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    uint32_t/*size_t*/ length;
    int flags;
    uint32_t/*socklen_t*/ dest_len;
    uint8_t dest_addr[MAX_SOCKADDR_SIZE];
    uint8_t message[];
  };
  MSG *d = (MSG*)data;

  SEND_RET_TYPE ret = sendto(d->socket, (const char *)d->message, d->length, d->flags, (sockaddr*)d->dest_addr, d->dest_len);

#ifdef POSIX_SOCKET_DEBUG
  printf("sendto(socket=%d,message=%p,length=%zd,flags=%d,dest_addr=%p,dest_len=%d)->" SEND_FORMATTING_SPECIFIER "\n", d->socket, d->message, d->length, d->flags, d->dest_addr, d->dest_len, ret);
  if (ret < 0) PRINT_ERRNO();
#endif

  struct {
    int callId;
    int/*ssize_t/int*/ ret;
    int errno_;
  } r;
  r.callId = d->header.callId;
  r.ret = (int)ret;
  r.errno_ = (ret != 0) ? errno : 0;
  SendWebSocketMessage(client_fd, &r, sizeof(r));
}

void Recvfrom(int client_fd, uint8_t *data, uint64_t numBytes) // ssize_t/int recvfrom(int socket, void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    uint32_t/*size_t*/ length;
    int flags;
    uint32_t/*socklen_t*/ address_len;
  };
  MSG *d = (MSG*)data;

  uint8_t address[MAX_SOCKADDR_SIZE];
  uint8_t *buffer = (uint8_t *)malloc(d->length);

  socklen_t address_len = (socklen_t)d->address_len;
  int ret = recvfrom(d->socket, (char *)buffer, d->length, d->flags, (sockaddr*)address, &address_len);

#ifdef POSIX_SOCKET_DEBUG
  printf("recvfrom(socket=%d,buffer=%p,length=%zd,flags=%d,address=%p,address_len=%u)->%d\n", d->socket, buffer, d->length, d->flags, address, d->address_len, ret);
  if (ret < 0) PRINT_ERRNO();
#endif

  int receivedBytes = MAX(ret, 0);
  int actualAddressLen = MIN(address_len, (socklen_t)d->address_len);

  struct Result {
    int callId;
    int/*ssize_t/int*/ ret;
    int errno_;
    int data_len;
    int address_len; // N.B. this is the reported address length of the sender, that may be larger than what is actually serialized to this message.
    uint8_t data_and_address[];
  };
  int resultSize = sizeof(Result) + receivedBytes + actualAddressLen;
  Result *r = (Result *)malloc(resultSize);
  r->callId = d->header.callId;
  r->ret = (int)ret;
  r->errno_ = (ret != 0) ? errno : 0;
  r->data_len = receivedBytes;
  r->address_len = d->address_len; // How many bytes would have been needed to fit the whole sender address, not the actual size provided
  memcpy(r->data_and_address, buffer, receivedBytes);
  memcpy(r->data_and_address + receivedBytes, address, actualAddressLen);
  SendWebSocketMessage(client_fd, r, resultSize);
  free(r);
}

void Sendmsg(int client_fd, uint8_t *data, uint64_t numBytes) // ssize_t/int sendmsg(int socket, const struct msghdr *message, int flags);
{
	printf("TODO implement sendmsg()\n");
#ifdef POSIX_SOCKET_DEBUG
//  printf("sendmsg(socket=%d,message=%p,flags=%d)\n", d->socket, d->message, d->flags);
//  if (ret < 0) PRINT_ERRNO();
#endif

  // TODO
}

void Recvmsg(int client_fd, uint8_t *data, uint64_t numBytes) // ssize_t/int recvmsg(int socket, struct msghdr *message, int flags);
{
	printf("TODO implement recvmsg()\n");
#ifdef POSIX_SOCKET_DEBUG
//  printf("recvmsg(socket=%d,message=%p,flags=%d)\n", d->socket, d->message, d->flags);
//  if (ret < 0) PRINT_ERRNO();
#endif
}

void Getsockopt(int client_fd, uint8_t *data, uint64_t numBytes) // int getsockopt(int socket, int level, int option_name, void *option_value, socklen_t *option_len);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    int level;
    int option_name;
    uint32_t/*socklen_t*/ option_len;
  };
  MSG *d = (MSG*)data;

  uint8_t option_value[MAX_OPTIONVALUE_SIZE];

  d->level = Translate_Socket_Level(d->level);
  d->option_name = Translate_SOL_SOCKET_option(d->option_name);

  socklen_t option_len = (socklen_t)d->option_len;
  int ret = getsockopt(d->socket, d->level, d->option_name, (char*)option_value, &option_len);

#ifdef POSIX_SOCKET_DEBUG
  printf("getsockopt(socket=%d,level=%d,option_name=%d,option_value=%p,option_len=%u)->%d\n", d->socket, d->level, d->option_name, option_value, d->option_len, ret);
  if (ret != 0) PRINT_ERRNO();
#endif

  struct Result {
    int callId;
    int ret;
    int errno_;
    int option_len;
    uint8_t option_value[];
  };

  int actualOptionLen = MIN(option_len, (socklen_t)d->option_len);
  int resultSize = sizeof(Result) + actualOptionLen;
  Result *r = (Result*)malloc(resultSize);
  r->callId = d->header.callId;
  r->ret = ret;
  r->errno_ = (ret != 0) ? errno : 0;
  r->option_len = option_len;
  memcpy(r->option_value, option_value, actualOptionLen);
  SendWebSocketMessage(client_fd, r, resultSize);
  free(r);
}

void Setsockopt(int client_fd, uint8_t *data, uint64_t numBytes) // int setsockopt(int socket, int level, int option_name, const void *option_value, socklen_t option_len);
{
  struct MSG {
    SocketCallHeader header;
    int socket;
    int level;
    int option_name;
    int option_len;
    uint8_t option_value[];
  };
  MSG *d = (MSG*)data;
  int actualOptionLen = MIN(d->option_len, (int)(numBytes - sizeof(MSG)));

  d->level = Translate_Socket_Level(d->level);
  d->option_name = Translate_SOL_SOCKET_option(d->option_name);
  int ret = setsockopt(d->socket, d->level, d->option_name, (const char *)d->option_value, actualOptionLen);

#ifdef POSIX_SOCKET_DEBUG
  printf("setsockopt(socket=%d,level=%d,option_name=%d,option_value=%p,option_len=%d)->%d\n", d->socket, d->level, d->option_name, d->option_value, d->option_len, ret);
  if (ret != 0) PRINT_ERRNO();
#endif

  struct {
    int callId;
    int ret;
    int errno_;
  } r;
  r.callId = d->header.callId;
  r.ret = ret;
  r.errno_ = (ret != 0) ? errno : 0;
  SendWebSocketMessage(client_fd, &r, sizeof(r));
}

void Getaddrinfo(int client_fd, uint8_t *data, uint64_t numBytes) // int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
{

#define MAX_NODE_LEN 2048
#define MAX_SERVICE_LEN 128

  struct MSG {
    SocketCallHeader header;
    char node[MAX_NODE_LEN]; // Arbitrary max length limit
    char service[MAX_SERVICE_LEN]; // Arbitrary max length limit
    int hasHints;
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
  };
  MSG *d = (MSG*)data;

  addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_flags = d->ai_flags;
  hints.ai_family = d->ai_family;
  hints.ai_socktype = d->ai_socktype;
  hints.ai_protocol = d->ai_protocol;

  addrinfo *res = 0;
  int ret = getaddrinfo(d->node, d->service, d->hasHints ? &hints : 0, &res);

#ifdef POSIX_SOCKET_DEBUG
  printf("getaddrinfo(node=%s,service=%s,hasHints=%d,ai_flags=%d,ai_family=%d,ai_socktype=%d,ai_protocol=%d)->%d\n", d->node, d->service, d->hasHints, d->ai_flags, d->ai_family, d->ai_socktype, d->ai_protocol, ret);
  if (ret != 0) PRINT_ERRNO();
#endif

  char ai_canonname[MAX_NODE_LEN] = {};
  int ai_addrTotalLen = 0;
  int addrCount = 0;

  if (ret == 0)
  {
    if (res && res->ai_canonname)
    {
      if (strlen(res->ai_canonname) >= MAX_NODE_LEN) printf("Warning: Truncated res->ai_canonname to %d bytes (was %s)\n", MAX_NODE_LEN, res->ai_canonname);
      strncpy(ai_canonname, res->ai_canonname, MAX_NODE_LEN-1);
    }

    addrinfo *ai = res;
    while(ai)
    {
      ai_addrTotalLen += ai->ai_addrlen;
      ++addrCount;
      ai = ai->ai_next;
    }
  }

  struct ResAddrinfo
  {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    int/*socklen_t*/ ai_addrlen;
    uint8_t /*sockaddr **/ ai_addr[];
  };

  struct Result {
    int callId;
    int ret;
    int errno_;
    char ai_canonname[MAX_NODE_LEN];
    int addrCount;
    uint8_t /*ResAddrinfo[]*/ addr[];
  };

  int resultSize = sizeof(Result) + sizeof(ResAddrinfo)*addrCount + ai_addrTotalLen;
  Result *r = (Result*)malloc(resultSize);

  memset(r, 0, resultSize);
  r->callId = d->header.callId;
  r->ret = ret;
  r->errno_ = (ret != 0) ? errno : 0;
  strncpy(r->ai_canonname, ai_canonname, MAX_NODE_LEN-1);
  r->addrCount = addrCount;

  addrinfo *ai = res;
  int offset = 0;
  while(ai)
  {
    ResAddrinfo *o = (ResAddrinfo*)(r->addr + offset);
    o->ai_flags = ai->ai_flags;
    o->ai_family = ai->ai_family;
    o->ai_socktype = ai->ai_socktype;
    o->ai_protocol = ai->ai_protocol;
    o->ai_addrlen = ai->ai_addrlen;
    memcpy(o->ai_addr, ai->ai_addr, ai->ai_addrlen);
    offset += sizeof(ResAddrinfo) + ai->ai_addrlen;
    ai = ai->ai_next;
  }
  if (res) freeaddrinfo(res);

  SendWebSocketMessage(client_fd, r, resultSize);

  free(r);
}

void Getnameinfo(int client_fd, uint8_t *data, uint64_t numBytes) // int getnameinfo(const struct sockaddr *addr, socklen_t addrlen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags);
{
  fprintf(stderr, "TODO getnameinfo() unimplemented!\n");
}

void ProcessWebSocketMessage(int client_fd, uint8_t *payload, uint64_t numBytes)
{
  if (numBytes < sizeof(SocketCallHeader))
  {
    printf("Received too small sockets call message! size: %d bytes, expected at least %d bytes\n", (int)numBytes, (int)sizeof(SocketCallHeader));
    return;
  }
  SocketCallHeader *header = (SocketCallHeader*)payload;
  switch(header->function)
  {
    case POSIX_SOCKET_MSG_SOCKET: Socket(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_SOCKETPAIR: Socketpair(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_SHUTDOWN: Shutdown(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_BIND: Bind(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_CONNECT: Connect(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_LISTEN: Listen(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_ACCEPT: Accept(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_GETSOCKNAME: Getsockname(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_GETPEERNAME: Getpeername(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_SEND: Send(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_RECV: Recv(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_SENDTO: Sendto(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_RECVFROM: Recvfrom(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_SENDMSG: Sendmsg(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_RECVMSG: Recvmsg(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_GETSOCKOPT: Getsockopt(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_SETSOCKOPT: Setsockopt(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_GETADDRINFO: Getaddrinfo(client_fd, payload, numBytes); break;
    case POSIX_SOCKET_MSG_GETNAMEINFO: Getnameinfo(client_fd, payload, numBytes); break;
    default:
      printf("Unknown POSIX_SOCKET_MSG %u received!\n", header->function);
      break;
	}
}
