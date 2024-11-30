// TCP Library
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

#include <stdio.h>
#include <string.h>
#include "arp.h"
#include "tcp.h"
#include "timer.h"
#include "socket.h"
#include "uart0.h"

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

//instance = idx of port #?
//e.g
/*
 * tcpPorts = {
 *    instance = port
 *           0 = 60000
 *           1 = 49032
 *           2 = 52403
 *           3 = 63235
 * }
 *
 *
 * tcpFsmFlags = {
 *      socket = flags
 *           0 = 00000001 - FIN needed
 *           1 = 00000010 - SYN needed
 *           2 = 00000100 -
 *           3 = 00001000
 * }
 *
 * sockets = {
 *           0 = {r_ip, r_mac, r_port, l_port, seq, ack, state}
 *           1 = {192.168.1.50, FF:FF:FF:FF:FF:FF, 53, 65031, 0, 0, CLOSED}
 * }
 *
 */

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

/*typedef struct {
    uint8_t timerId;
    socket* s;
} timerSocketMap;

timerSocketMap timerMap[MAX_TIMERS];

socket* getSocketFromTimer(uint8_t id) {
    uint8_t i;
    for (i = 0; i < MAX_TIMERS; i++) {
        if (timerMap[i].timerId == id) {
            return timerMap[i].s;
        }
    }
}

*/

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

#define WINDOW_SIZE 1460
#define MAX_INITSOCKS 5

typedef void(* _fn)(void);

uint8_t isExternal;

char out[100];
//TODO: test TCP message

// Set TCP state
void setTcpState(socket* s, uint8_t state) {
    if (s) {
        s->state = state;
    }
}

// Get TCP state
uint8_t getTcpState(socket* s) {
    if (s) {
        return s->state;
    }
    return TCP_CLOSED;
}

tcpHeader* getTcpHeader(etherHeader* ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);
    return tcp;
}

// Determines whether packet is TCP packet
// Must be an IP packet
bool isTcp(etherHeader* ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);
    bool ok;
    uint16_t tmp16;
    uint16_t tcpLength = ip->length - ip->size*4;
    uint32_t sum = 0;
    ok = (ip->protocol == PROTOCOL_TCP);
    if (ok) {
        uint16_t tcpLengthHton = htons(tcpLength);
        sum = 0;
        sumIpWords(ip->sourceIp, 8, &sum);
        tmp16 = ip->protocol;
        sum += (tmp16 & 0xff) << 8;
        sumIpWords(&tcpLengthHton, 2, &sum);
        tcp->checksum = 0;
        sumIpWords(tcp, tcpLength, &sum);
        tcp->checksum = getIpChecksum(sum);
    }
    return ok;
}

bool isTcpSyn(etherHeader* ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);
    if (!isTcp(ether)) {
        return false;
    }
    return htons(tcp->offsetFields) & SYN;
}

bool isTcpFin(etherHeader* ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);
    return isTcp(ether) && (htons(tcp->offsetFields) & FIN);
}

bool isTcpAck(etherHeader* ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);
    if (!isTcp(ether)) {
        return false;
    }
    return htons(tcp->offsetFields) & ACK;
}

bool isTcpPsh(etherHeader* ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);
    if (!isTcp(ether)) {
        return false;
    }
    return htons(tcp->offsetFields) & PSH;
}

//socket* initSockQ[5] = {0};
//uint8_t initSockQSize = 0;

/*void enqueue_socket(socket* s) {
    if (initSockQSize < 5) {
        initSockQ[initSockQSize++] = s;
    }
}

socket* dequeue_socket(uint8_t idx) {
    if (idx >= initSockQSize) {
        return NULL;
    }
    uint32_t i;
    socket* retSock = initSockQ[idx];
    for (i = idx; i < initSockQSize - 1; i++) {
        initSockQ[i] = initSockQ[i+1];
    }
    initSockQSize--;
    return retSock;
}*/


/*void resetConCallback(void* context) { //make this function take a parameter if needed

    snprintf(out, 100, "Could not reach %d.%d.%d.%d (timed out)\n", initSock->remoteIpAddress[0], initSock->remoteIpAddress[1], initSock->remoteIpAddress[2], initSock->remoteIpAddress[3]);
    putsUart0(out);
}*/

void handleSocketError(socket* s, uint8_t errorCode) {
    socketError err;
    err.errorCode = errorCode;
    switch (errorCode) {
    case SOCKET_ERROR_ARP_TIMEOUT:
        snprintf(err.errorMsg, SOCKET_ERROR_MAX_MSG_LEN, "Could not reach %d.%d.%d.%d (timed out)\n", s->remoteIpAddress[0], s->remoteIpAddress[1], s->remoteIpAddress[2], s->remoteIpAddress[3]);
        break;
    case SOCKET_ERROR_TCP_SYN_ACK_TIMEOUT:
        snprintf(err.errorMsg, SOCKET_ERROR_MAX_MSG_LEN, "No response from %d.%d.%d.%d (timed out)\n", s->remoteIpAddress[0], s->remoteIpAddress[1], s->remoteIpAddress[2], s->remoteIpAddress[3]);
        break;
    }
    err.sk = s;
    if (s->errorCallback) {
        s->errorCallback(&err);
    }
}

void tcpTimeoutCallback(void* context) {
    socket* s = (socket*)context;
    if (s) {
        s->assocTimer = INVALID_TIMER;
        switch (s->state) {
        case TCP_CLOSED:
            //arping timed out
            handleSocketError(s, SOCKET_ERROR_ARP_TIMEOUT);
            break;
        case TCP_SYN_SENT:
            //synack timed out
            handleSocketError(s, SOCKET_ERROR_TCP_SYN_ACK_TIMEOUT);
            break;
        }
        s->state = TCP_CLOSED;
    }

}

void openTcpConnection(etherHeader* ether, socket* s) {
    uint8_t localIp[4];
    uint8_t subnetMask[4];
    uint8_t gateway[4];
    uint8_t remoteMac[6];
    getIpAddress(localIp);
    getIpSubnetMask(subnetMask);
    getIpGatewayAddress(gateway);
    bool isIpLocal = isIpInSubnet(localIp, s->remoteIpAddress, subnetMask);
    uint8_t arpEntryExists = lookupArpEntry(s->remoteIpAddress, remoteMac);
    if (arpEntryExists) {
        copyMacAddress(s->remoteHwAddress, remoteMac);
        completeTcpConnection(ether, s);
    }
    else {
        sendArpRequest(ether, localIp, isIpLocal ? s->remoteIpAddress : gateway); //if IP is in same subnet, send ARP for that IP, else send ARP to gateway
        uint8_t arpTimer = startOneshotTimer(tcpTimeoutCallback, 30, s); //wait 30 seconds for arp
        s->assocTimer = arpTimer; //assign timer id to socket or local port (multiple ways to do this)
    }
}

void completeTcpConnection(etherHeader* ether, socket* s) {
    /*uint8_t  remoteIpAddress[4]; X
    uint8_t  remoteHwAddress[6]; X
    uint16_t remotePort; X
    uint16_t localPort; X
    //TCP
    uint32_t sequenceNumber; X
    uint32_t acknowledgementNumber; X
    uint8_t  state; X
    uint8_t* rx_buffer;
    uint16_t rx_size;
    uint8_t* tx_buffer;
    uint16_t tx_size;
    uint16_t flags; X
    uint8_t assocTimer;
    uint8_t  valid;*/
    uint32_t ISN = random32();
    s->sequenceNumber = ISN;
    s->acknowledgementNumber = 0;
    setTcpState(TCP_SYN_SENT);
    pendTcpResponse(SYN);

}

void closeTcpConnection(etherHeader* ether, socket* s) {
    sendTcpResponse(ether, s, FIN | ACK);
    s->state = TCP_FIN_WAIT_1;
}

void sendTcpPendingMessages(etherHeader* ether) {
    uint32_t i;
    socket* sockets = getSockets();
    for (i = 0; i < MAX_SOCKETS; i++) {
        socket* s = &sockets[i]; //192.168.1.118:50115 -> 192.168.1.16:8080
        if (s->valid && s->flags) {
            uint16_t flags = s->flags;
            sendTcpResponse(ether, s, flags);
            s->flags = 0; //reset flags
            switch (s->state) {
            case TCP_SYN_SENT:
                s->assocTimer = startOneshotTimer(tcpTimeoutCallback, 15, s);
                /*if (s->flags & ACK) {
                    setTcpState(TCP_ESTABLISHED);
                }*/
                break;
            case TCP_ESTABLISHED:

                break;
            }
        }
    }
}

void handleTcpData(etherHeader* ether, socket* s) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);
    uint16_t payload_length = ntohs(ip->length) - (ip->size * 4) - ((ntohs(tcp->offsetFields) >> 12) * 4);

}

uint8_t* getTcpData(etherHeader* ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);
    uint8_t* payload = tcp->data;
    return payload;
}

uint8_t isTcpDataAvailable(etherHeader* ether) {
    //ipHeader* ip = (ipHeader*)ether->data;
    //tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);

}

void pendTcpResponse(socket* s, uint8_t flags) {
    s->flags = flags;
}

void adjustTcpAckNum(socket* s, uint8_t flags, uint32_t dataLength) {
    if (s) {

    }
}

void processTcpResponse(etherHeader* ether) {
    tcpHeader* tcp = getTcpHeader(ether);
    uint16_t localPort = ntohs(tcp->destPort);
    uint16_t remotePort = ntohs(tcp->sourcePort);
    socket* s = getSocketFromLocalPort(localPort); //192.168.1.118:50115 -> 192.168.1.16:8080
    if (s) {
        switch (s->state) {
        case TCP_CLOSED:
            break;
        case TCP_SYN_SENT:
            if (isTcpSyn(ether) && isTcpAck(ether)) {
                stopTimer(s->assocTimer);
                s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + 1;
                s->sequenceNumber += 1;
                pendTcpResponse(s, ACK); //s->flags = ACK;
                setTcpState(s, TCP_ESTABLISHED); //s->state = TCP_ESTABLISHED;
            }
            break;
        case TCP_ESTABLISHED:
            if (isTcpPsh(ether)) {
                //s->rx_buffer
                //s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + DATA_LENGTH;
                //s->flags = ACK;
            }
            if (isTcpFin(ether)) {
                s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + 1;
                s->sequenceNumber += 1;
                pendTcpResponse(s, FIN | ACK); //s->flags = FIN | ACK;
                setTcpState(s, TCP_CLOSE_WAIT);
            }
            break;
        }
    }
}

void processTcpArpResponse(etherHeader* ether) {
    uint32_t i;
    arpPacket* arp = getArpPacket(ether);
    socket* sockets = getSockets();
    for (i = 0; i < MAX_SOCKETS; i++) {
        socket* s = &sockets[i];
        if (isIpEqual(s->remoteIpAddress, arp->sourceIp)) {
            if (s->state == TCP_CLOSED) {
                //checking socket by IP,
                //problem w this is that if multiple sockets point to same IP, it will only find the first one.
                //maybe make a way to track which sockets are ARPing (queue? idk)
                stopTimer(s->assocTimer); //MAYBE MAKE DIFF TIMERS OR CALLBACKS FOR SOCKETS IF NEEDED!
                copyMacAddress(s->remoteHwAddress, arp->sourceAddress);
                completeTcpConnection(ether, s);
                break;
            }
        }
    }
}

bool isTcpPortOpen(etherHeader* ether) {
    tcpHeader* tcp = getTcpHeader(ether);
    uint16_t port = ntohs(tcp->destPort);
    int i;
    socket* sockets = getSockets();
    for (i = 0; i < MAX_SOCKETS; i++) {
        socket* s = &sockets[i];
        if (s->localPort == port && s->state != TCP_CLOSED) {
            return true;
        }
    }
    return false;
}

void sendTcpResponse(etherHeader* ether, socket* s, uint16_t flags){
    uint8_t i;
    uint32_t sum;
    uint16_t tmp16;
    uint16_t tcpLength;
    uint8_t localHwAddress[6];
    uint8_t localIpAddress[4];
    getEtherMacAddress(localHwAddress);
    getIpAddress(localIpAddress);
    // Ether frame
    copyMacAddress(ether->destAddress, s->remoteHwAddress);
    copyMacAddress(ether->sourceAddress, localHwAddress);
    ether->frameType = htons(TYPE_IP);
    // IP header
    ipHeader* ip = (ipHeader*)ether->data;
    ip->rev = 0x4;
    ip->size = 0x5;
    ip->typeOfService = 0;
    ip->id = 0x5555;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = PROTOCOL_TCP;
    ip->headerChecksum = 0;
    copyIpAddress(ip->destIp, s->remoteIpAddress);
    copyIpAddress(ip->sourceIp, localIpAddress);
    uint8_t ipHeaderLength = ip->size * 4;
    // TCP header
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + (ip->size * 4));
    tcp->sourcePort = htons(s->localPort);
    tcp->destPort = htons(s->remotePort);
    tcp->sequenceNumber = htonl(s->sequenceNumber);
    tcp->acknowledgementNumber = htonl(s->acknowledgementNumber);
    tcp->windowSize = htons(WINDOW_SIZE);
    tcp->urgentPointer = 0;
    i = 0;
    uint16_t mss = 1280;
    tcp->data[i++] = 2;
    tcp->data[i++] = 4;
    tcp->data[i++] = (uint8_t)(mss >> 8);
    tcp->data[i++] = (uint8_t)(mss & 0xF);
    tcpLength = sizeof(tcpHeader) + i;
    tcp->offsetFields = htons(((((uint16_t)(tcpLength/4)) & 0xF) << 12) | flags);
    ip->length = htons(ipHeaderLength + tcpLength);

    calcIpChecksum(ip);
    uint16_t tcpLengthHton = htons(tcpLength);
    sum = 0;
    sumIpWords(ip->sourceIp, 8, &sum);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    sumIpWords(&tcpLengthHton, 2, &sum);
    tcp->checksum = 0;
    sumIpWords(tcp, tcpLength, &sum);
    tcp->checksum = getIpChecksum(sum);
    putEtherPacket(ether, sizeof(etherHeader) + ipHeaderLength + tcpLength);
}

void sendTcpMessage(etherHeader* ether, socket* s, uint16_t flags, uint8_t data[], uint16_t dataSize) {
    uint8_t i;
    uint32_t sum;
    uint16_t tmp16;
    uint16_t tcpLength;
    uint8_t* copyData;
    uint8_t localHwAddress[6];
    uint8_t localIpAddress[4];
    // Ether
    getEtherMacAddress(localHwAddress);
    getIpAddress(localIpAddress);
    copyMacAddress(ether->destAddress, s->remoteHwAddress);
    copyMacAddress(ether->sourceAddress, localHwAddress);
    ether->frameType = htons(TYPE_IP);
    // IP header
    ipHeader* ip = (ipHeader*)ether->data;
    ip->rev = 0x4;
    ip->size = 0x5;
    ip->typeOfService = 0;
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = PROTOCOL_TCP;
    ip->headerChecksum = 0;
    copyIpAddress(ip->destIp, s->remoteIpAddress);
    copyIpAddress(ip->sourceIp, localIpAddress);
    uint8_t ipHeaderLength = ip->size * 4;
    // TCP header
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + (ip->size * 4));
    tcp->sourcePort = htons(s->localPort);
    tcp->destPort = htons(s->remotePort);
    tcp->sequenceNumber = htonl(s->sequenceNumber);
    tcp->acknowledgementNumber = htonl(s->acknowledgementNumber);
    tcp->windowSize = htons(2048);
    tcp->urgentPointer = 0;
    tcpLength = sizeof(tcpHeader) + dataSize;
    tcp->offsetFields = htons(((((uint16_t)(sizeof(tcpHeader)/4)) & 0xF) << 12) | flags);
    ip->length = htons(ipHeaderLength + tcpLength);

    calcIpChecksum(ip);
    copyData = tcp->data;
    for (i = 0; i < dataSize; i++) {
        copyData[i] = data[i];
    }
    uint16_t tcpLengthHton = htons(tcpLength);
    sum = 0;
    sumIpWords(ip->sourceIp, 8, &sum);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    sumIpWords(&tcpLengthHton, 2, &sum);
    tcp->checksum = 0;
    sumIpWords(tcp, tcpLength, &sum);
    tcp->checksum = getIpChecksum(sum);
    putEtherPacket(ether, sizeof(etherHeader) + ipHeaderLength + tcpLength);
}
