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


#include "socket.h"
#include "ip.h"
#include "arp.h"
#include "udp.h"
#include "tcp.h"
#include "timer.h"
#include <stdio.h>


// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

//using all as TCP sockets
uint8_t socketCount = 0;
socket sockets[MAX_SOCKETS];

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void initSockets(void) {
    uint8_t i;
    for (i = 0; i < MAX_SOCKETS; i++) {
        sockets[i].state = TCP_CLOSED;
        sockets[i].assocTimer = INVALID_TIMER;
    }
}

socket* newSocket(void) {
    uint8_t i = 0;
    socket* s = NULL;
    bool foundUnused = false;
    while (i < MAX_SOCKETS && !foundUnused) {
        foundUnused = !sockets[i].valid;
        if (foundUnused) {
            sockets[i].valid = 1;
            s = &sockets[i];
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
    uint8_t buffer[MAX_PACKET_SIZE];
    etherHeader* ether = (etherHeader*)buffer;

    getIpAddress(s->localIpAddress);
    s->localPort = (random32() & 0x3FFF) + 49152;

    copyIpAddress(s->remoteIpAddress, serverIp);
    s->remotePort = port;

    openTcpConnection(ether, s);
}

void socketSendTcp(socket* s, uint8_t* data, uint16_t length) {
    uint8_t buffer[MAX_PACKET_SIZE];
    etherHeader* ether = (etherHeader*)buffer;
    //updateSeqNum(s, ether)
    //memcpy(s->tx_buffer, data, length);
    //s->tx_size = length;
    sendTcpMessage(ether, s, PSH | ACK, data, length);
    s->sequenceNumber += length;
}
//void sendTcpMessage(etherHeader* ether, socket* s, uint16_t flags, uint8_t data[], uint16_t dataSize)

/*uint16_t socketRecvTcp(socket* s, uint8_t* buf) {

    return 1;
}*/

void socketCloseTcp(socket* s) {
    uint8_t buffer[MAX_PACKET_SIZE];
    etherHeader* ether = (etherHeader*)buffer;
    closeTcpConnection(ether, s);
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
