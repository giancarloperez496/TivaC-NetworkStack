/******************************************************************************
 * File:        socket.c
 *
 * Author:      Giancarlo Perez
 *
 * Created:     12/7/24
 *
 * Description: -
 ******************************************************************************/

//=============================================================================
// INCLUDES
//=============================================================================

#include "socket.h"
#include "ip.h"
#include "arp.h"
#include "udp.h"
#include "tcp.h"
#include "timer.h"
#include <stdio.h>

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

//=============================================================================
// GLOBALS
//=============================================================================

//using all as TCP sockets
uint8_t socketCount = 0;
socket sockets[MAX_SOCKETS];

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

void initSockets(void) {
    uint8_t i;
    for (i = 0; i < MAX_SOCKETS; i++) {
        sockets[i].state = TCP_CLOSED;
        sockets[i].assocTimer = INVALID_TIMER;
        sockets[i].connectAttempts = 0;
    }
}

socket* newSocket(uint8_t type) {
    uint8_t i = 0;
    socket* s = NULL;
    bool foundUnused = false;
    while (i < MAX_SOCKETS && !foundUnused) {
        foundUnused = !sockets[i].valid;
        if (foundUnused) {
            sockets[i].valid = 1;
            s = &sockets[i];
            s->type = type;
            socketCount++;
        }
        i++;
    }
    return s;
}

void deleteSocket(socket* s) {
    uint8_t i = 0;
    bool foundMatch = false;
    while (i < MAX_SOCKETS && !foundMatch) {
        foundMatch = &sockets[i] == s;
        if (foundMatch) {
            sockets[i].valid = 0;
            socketCount--;
        }
        i++;
    }
}

/*void initSocket(socket* s, uint8_t remoteIp[], uint16_t remotePort) {
    uint32_t i;
    s->localPort = (random32() & 0x3FFF) + 49152;
    for (i = 0; i < IP_ADD_LENGTH; i++) {
        s->remoteIpAddress[i] = remoteIp[i];
    }
    s->remotePort = remotePort;
}*/

uint32_t getSocketId(socket* s) {
    uint32_t i;
    for (i = 0; i < socketCount; i++) {
        if (&sockets[i] == s) {
            return i;
        }
    }
    return 0xFFFFFFFF;
}

socket* getSockets() {
    return sockets;
}

void socketConnectUdp() {

}

void socketSendTo() {

}

void socketRecvFrom() {

}


void socketConnectTcp(socket* s, uint8_t serverIp[4], uint16_t port) {
    if (s->type == SOCKET_STREAM) {
        uint8_t buffer[MAX_PACKET_SIZE];
        etherHeader* ether = (etherHeader*)buffer;
        getIpAddress(s->localIpAddress);
        s->localPort = (random32() & 0x3FFF) + 49152;
        copyIpAddress(s->remoteIpAddress, serverIp);
        s->remotePort = port;
        openTcpConnection(ether, s);
    }
    else {
        //not a tcp socket
    }
}

void socketSendTcp(socket* s, uint8_t* data, uint16_t length) {
    if (s->type == SOCKET_STREAM) {
        uint8_t buffer[MAX_PACKET_SIZE];
        etherHeader* ether = (etherHeader*)buffer;
        //updateSeqNum(s, ether)
        //memcpy(s->tx_buffer, data, length);
        //s->tx_size = length;
        sendTcpMessage(ether, s, PSH | ACK, data, length);
        s->sequenceNumber += length;
    }
    else {
        //not a TCP socket
    }
}

/*uint16_t socketRecvTcp(socket* s, uint8_t* buf) {
    if (s->type == SOCKET_STREAM) {

    }
    else {

    }
    return 1;
}*/

void socketCloseTcp(socket* s) {
    if (s->type == SOCKET_STREAM) {
        uint8_t buffer[MAX_PACKET_SIZE];
        etherHeader* ether = (etherHeader*)buffer;
        closeTcpConnection(ether, s);
    }
    else {

    }
}

// Get socket from ephemeral port, may expand this in the future to get socket from all fields
socket* getSocketFromLocalPort(uint16_t l_port) {
    uint32_t i;
    for (i = 0; i < socketCount; i++) {
        if (sockets[i].localPort == l_port && sockets[i].valid) {
            return &sockets[i];
        }
    }
    return NULL;
}

// Get socket information from a received ARP response message
void getSocketInfoFromArpResponse(etherHeader* ether, socket* s) {
    arpPacket *arp = (arpPacket*)ether->data;
    uint8_t i;
    for (i = 0; i < HW_ADD_LENGTH; i++)
        s->remoteHwAddress[i] = arp->sourceAddress[i];
    for (i = 0; i < IP_ADD_LENGTH; i++)
        s->remoteIpAddress[i] = arp->sourceIp[i];
}

// Get socket information from a received UDP packet
void getSocketInfoFromUdpPacket(etherHeader* ether, socket* s) {
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t i;
    for (i = 0; i < HW_ADD_LENGTH; i++)
        s->remoteHwAddress[i] = ether->sourceAddress[i];
    for (i = 0; i < IP_ADD_LENGTH; i++)
        s->remoteIpAddress[i] = ip->sourceIp[i];
    s->remotePort = ntohs(udp->sourcePort);
    s->localPort = ntohs(udp->destPort);
}

// Get socket information from a received TCP packet
void getSocketInfoFromTcpPacket(etherHeader* ether, socket* s) {
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t i;
    for (i = 0; i < HW_ADD_LENGTH; i++)
        s->remoteHwAddress[i] = ether->sourceAddress[i];
    for (i = 0; i < IP_ADD_LENGTH; i++)
        s->remoteIpAddress[i] = ip->sourceIp[i];
    s->remotePort = ntohs(tcp->sourcePort);
    s->localPort = ntohs(tcp->destPort);
}

//this function shall be called anytime an error occurs and the application needs to be aware of it
void throwSocketError(socket* s, uint8_t errorCode) {
    socketError err;
    err.errorCode = errorCode;
    switch (errorCode) {
    case SOCKET_ERROR_ARP_TIMEOUT:
        snprintf(err.errorMsg, SOCKET_ERROR_MAX_MSG_LEN, "Could not reach %d.%d.%d.%d (timed out)", s->remoteIpAddress[0], s->remoteIpAddress[1], s->remoteIpAddress[2], s->remoteIpAddress[3]);
        break;
    case SOCKET_ERROR_TCP_SYN_ACK_TIMEOUT:
        snprintf(err.errorMsg, SOCKET_ERROR_MAX_MSG_LEN, "No response from %d.%d.%d.%d (timed out)", s->remoteIpAddress[0], s->remoteIpAddress[1], s->remoteIpAddress[2], s->remoteIpAddress[3]);
        break;
    case SOCKET_ERROR_CONNECTION_RESET:
        snprintf(err.errorMsg, SOCKET_ERROR_MAX_MSG_LEN, "Connection was reset by remote host (%d.%d.%d.%d:%d)", s->remoteIpAddress[0], s->remoteIpAddress[1], s->remoteIpAddress[2], s->remoteIpAddress[3], s->remotePort);
        break;
    }
    err.sk = s;
    if (s->errorCallback) {
        s->errorCallback(&err); // application responsible for deleting socket in the case of an error
    }
}
