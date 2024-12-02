// Socket Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: -
// Target uC:       -
// System Clock:    -

// Hardware configuration:
// -

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef SOCKET_H_
#define SOCKET_H_

#include "ip.h"
#include "timer.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_SOCKETS 10

// UDP/TCP socket
typedef struct _socket {
    //uint8_t socket_id;
    uint8_t  localIpAddress[4];
    uint16_t localPort;
    uint8_t  remoteIpAddress[4];
    uint16_t remotePort;
    uint8_t  remoteHwAddress[6];
    //TCP
    uint32_t sequenceNumber;
    uint32_t acknowledgementNumber;
    uint8_t  state;
    uint8_t* rx_buffer;
    uint16_t rx_size;
    uint8_t* tx_buffer;
    uint16_t tx_size;
    uint16_t flags;
    uint8_t assocTimer;
    uint8_t  valid;
    _tim_callback_t errorCallback;
} socket;

#define SOCKET_ERROR_MAX_MSG_LEN 60
#define SOCKET_ERROR_NO_ERROR 0
#define SOCKET_ERROR_ARP_TIMEOUT 1
#define SOCKET_ERROR_TCP_SYN_ACK_TIMEOUT 2

typedef struct socketError {
    uint8_t errorCode;
    socket* sk;
    char errorMsg[SOCKET_ERROR_MAX_MSG_LEN];
} socketError;


//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void initSockets();
socket* newSocket();
void deleteSocket(socket *s);
void initSocket(socket* s, uint8_t remoteIp[], uint16_t remotePort);
socket* getSockets();
void socketConnectTcp(socket* s, uint8_t ip[4], uint16_t port);
void socketSendTcp(socket* s, uint8_t* data, uint16_t length);
uint16_t socketRecvTcp(socket* s, uint8_t* buf);
void socketCloseTcp(socket* s);
uint32_t getSocketId(socket* s);
socket* getSocketFromLocalPort(uint16_t l_port);
void getSocketInfoFromArpResponse(etherHeader* ether, socket* s);
void getSocketInfoFromArpResponse(etherHeader* ether, socket* s);
void getSocketInfoFromUdpPacket(etherHeader* ether, socket* s);
void getSocketInfoFromTcpPacket(etherHeader* ether, socket* s);

#endif

