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


// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

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

//implement FIN_WAIT timeout and other necessary ones
void tcpTimeoutCallback(void* context) {
    static uint8_t attempts = 0;
    socket* s = (socket*)context;
    if (s) {
        s->assocTimer = INVALID_TIMER;
        switch (s->state) {
        case TCP_CLOSED:
            //arping timed out
            handleSocketError(s, SOCKET_ERROR_ARP_TIMEOUT);
            setTcpState(s, TCP_CLOSED);
            break;
        case TCP_SYN_SENT:
            //synack timed out
            attempts++;
            if (attempts == 2) {
                putsUart0("Reached max number of attempts to connect to server\n");
                attempts = 0;
                handleSocketError(s, SOCKET_ERROR_TCP_SYN_ACK_TIMEOUT);
                setTcpState(s, TCP_CLOSED);
            }
            else {
                //putsUart0("Retrying connection to server...\n");
                //s->retransmitting = 1; //used to not incremen SEQ
                s->sequenceNumber--;
                pendTcpResponse(s, SYN);
                setTcpState(s, TCP_SYN_SENT);
            }
            break;
        }
        //s->state = TCP_CLOSED;
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
        completeTcpConCallback(ether, s);
    }
    else {
        sendArpRequest(ether, localIp, isIpLocal ? s->remoteIpAddress : gateway); //if IP is in same subnet, send ARP for that IP, else send ARP to gateway
        uint8_t arpTimer = startOneshotTimer(tcpTimeoutCallback, 30, s); //wait 30 seconds for arp
        s->assocTimer = arpTimer; //assign timer id to socket or local port (multiple ways to do this)
    }
}

void completeTcpConCallback(etherHeader* ether, socket* s) {
    uint32_t ISN = random32();
    s->sequenceNumber = ISN;
    s->acknowledgementNumber = 0;
    setTcpState(s, TCP_SYN_SENT);
    pendTcpResponse(s, SYN);
}

void closeTcpConnection(etherHeader* ether, socket* s) {
    switch (s->state) {
    case TCP_ESTABLISHED: //if we're calling socketCloseTcp() while established
        pendTcpResponse(s, FIN | ACK);
        setTcpState(s, TCP_FIN_WAIT_1);
        break;
    case TCP_CLOSE_WAIT:
        pendTcpResponse(s, FIN | ACK);
        setTcpState(s, TCP_LAST_ACK);
        break;
    }
}

//potentially make a message Queue to send multiple flags in one loop e.g.
//tcp.c sets ACK Flag
//mqtt.c or application sets FIN ACK when ready, will overwrite the single ACK

bool fin = 0;
// Looping function
void sendTcpPendingMessages(etherHeader* ether) {
    uint32_t i;
    socket* sockets = getSockets();
    for (i = 0; i < MAX_SOCKETS; i++) {
        socket* s = &sockets[i]; //192.168.1.118:50115 -> 192.168.1.16:8080
        if (s->valid) {
            if (s->flags) {
                uint16_t flags = s->flags;
                sendTcpResponse(ether, s, flags);
                //switch statement is for actions to happen once sent
                switch (s->state) {
                case TCP_SYN_SENT:
                    s->assocTimer = startOneshotTimer(tcpTimeoutCallback, 3, s);
                    break;
                case TCP_ESTABLISHED:
                    if ((flags & ACK) && fin) {
                        setTcpState(s, TCP_CLOSE_WAIT); //set state once ACK sent
                        fin = 0;
                    }
                    break;
                }

                updateSeqNum(s, ether);
                s->flags = 0; //reset flags
                //s->retransmitting = 0; //reset rtx flag
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

uint16_t getTcpDataLength(etherHeader* ether) {
    ipHeader* ip = getIpHeader(ether);
    tcpHeader* tcp = getTcpHeader(ether);
    uint16_t payload_length = ntohs(ip->length) - (ip->size * 4) - ((ntohs(tcp->offsetFields) >> 12) * 4);
    return payload_length;
}

uint8_t isTcpDataAvailable(etherHeader* ether) {
    //ipHeader* ip = (ipHeader*)ether->data;
    //tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);

}

void pendTcpResponse(socket* s, uint8_t flags) {
    s->flags = flags;
}

void tcpTimeWaitCallback(void* context) {
    socket* s = (socket*)context;
    setTcpState(s, TCP_CLOSED);
    deleteSocket(s);
}

//used when sending
void updateSeqNum(socket* s, etherHeader* ether) {
    //if (!s->retransmitting) {
    if (isTcpSyn(ether) || isTcpFin(ether)) {
        s->sequenceNumber += 1;
    }
    else if (isTcpPsh(ether)) {
        uint16_t len = getTcpDataLength(ether);
        s->sequenceNumber += len;
    }
    //}
}

//used when receiving
void updateAckNum(socket* s, etherHeader* ether) {
    tcpHeader* tcp = getTcpHeader(ether);
    uint8_t state = getTcpState(s);
    if (state != TCP_ESTABLISHED) {
        if (isTcpSyn(ether) || isTcpFin(ether)) {
            s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + 1;
        }
    }
    else {
        if (isTcpPsh(ether)) {
            uint16_t len = getTcpDataLength(ether);
            s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + len;
        }
        else if (isTcpFin(ether)) {
            s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + 1;
        }
        else if (isTcpAck(ether)) {
            //s->acknowledgementNumber = ntohl(tcp->acknowledgementNumber);
        }
    }
}

void processTcpResponse(etherHeader* ether) {
    tcpHeader* tcp = getTcpHeader(ether);
    uint16_t localPort = ntohs(tcp->destPort);
    uint16_t remotePort = ntohs(tcp->sourcePort);
    socket* s = getSocketFromLocalPort(localPort); //192.168.1.118:50115 -> 192.168.1.16:8080
    if (s) {
        updateAckNum(s, ether);
        switch (s->state) {
        case TCP_CLOSED:
            break;
        case TCP_SYN_SENT:
            if (isTcpSyn(ether) && isTcpAck(ether)) {
                stopTimer(s->assocTimer);
                pendTcpResponse(s, ACK); //s->flags = ACK;
                setTcpState(s, TCP_ESTABLISHED); //s->state = TCP_ESTABLISHED;
            }
            break;
        case TCP_ESTABLISHED:
            if (isTcpPsh(ether)) {
                //s->rx_buffer
                //s->acknowledgementNumber = ntohl(tcp->sequenceNumber) + DATA_LENGTH;
                //s->flags = ACK;
                pendTcpResponse(s, ACK);
            }
            else if (isTcpFin(ether)) {
                pendTcpResponse(s, ACK); //s->flags = FIN | ACK;
                fin = 1;
                //setTcpState(s, TCP_CLOSE_WAIT);
            }
            else if (isTcpAck(ether)) {

            }
            break;
        case TCP_CLOSE_WAIT:
            //invoked by socketCloseTcp() from application
            //pendTcpResponse(FIN | ACK);
            break;
        case TCP_LAST_ACK:
            if (isTcpAck(ether)) {
                setTcpState(s, TCP_CLOSED);
                deleteSocket(s);
            }
            break;
        case TCP_FIN_WAIT_1:
            if (isTcpFin(ether)) {
                pendTcpResponse(s, ACK);
                setTcpState(s, TCP_CLOSING);
            }
            if (isTcpAck(ether)) {
                setTcpState(s, TCP_FIN_WAIT_2);
            }
            break;
        case TCP_CLOSING:
            if (isTcpAck(ether)) {
                setTcpState(s, TCP_TIME_WAIT);
                startOneshotTimer(tcpTimeWaitCallback, 10, s);
            }
            break;
        case TCP_FIN_WAIT_2:
            if (isTcpFin(ether)) {
                pendTcpResponse(s, ACK);
                setTcpState(s, TCP_TIME_WAIT);
                startOneshotTimer(tcpTimeWaitCallback, 10, s);
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
        if (isIpEqual(s->remoteIpAddress, arp->sourceIp)) {//checking socket by IP since ARP doesnt have port #
            if (s->state == TCP_CLOSED) {
                //problem w this is that if multiple sockets point to same IP, it will only find the first one.
                //maybe make a way to track which sockets are ARPing (queue? idk)
                stopTimer(s->assocTimer); //MAYBE MAKE DIFF TIMERS OR CALLBACKS FOR SOCKETS IF NEEDED!
                copyMacAddress(s->remoteHwAddress, arp->sourceAddress);
                completeTcpConCallback(ether, s);
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

//len is the value shown in wireshark
void addTcpOption(uint8_t* options_ptr, uint8_t option_type, uint8_t len, uint8_t data[], uint8_t* options_length) {
    static uint8_t* last;
    if (options_ptr == NULL) {
        options_ptr = last;
    }
    if (options_ptr == NULL) {
        return;
    }
    uint32_t i = 0;
    uint32_t j;
    options_ptr[i++] = option_type;
    if (option_type != TCP_OPTION_NO_OP) {
        options_ptr[i++] = len;
        for (j = 0; j < len-2; j++) {
            options_ptr[i++] = data[j];
        }
        if (options_length) {
            *options_length += len;
        }
        last = options_ptr + len;
    }
    else {
        if (options_length) {
            *options_length += 1;
        }
        last = options_ptr + 1;
    }
}

void sendTcpResponse(etherHeader* ether, socket* s, uint16_t flags){
    uint32_t sum;
    uint16_t tmp16;
    uint16_t tcpLength;
    uint8_t i;
    uint8_t options_length = 0;
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
    // TCP Options
    if (flags & SYN) {
        uint8_t optionData[TCP_MAX_OPTION_LENGTH];
        // Max Segment Size - 2
        i = 0;
        optionData[i++] = (uint8_t)(MAX_SEGMENT_SIZE >> 8);
        optionData[i++] = (uint8_t)(MAX_SEGMENT_SIZE & 0xFF);
        addTcpOption(tcp->data, TCP_OPTION_MAX_SEGMENT_SIZE, 4, optionData, &options_length);
        // No Op - 1
        addTcpOption(NULL, TCP_OPTION_NO_OP, 0, 0, &options_length);
        // No Op - 1
        addTcpOption(NULL, TCP_OPTION_NO_OP, 0, 0, &options_length);
        // SACK Permitted - 4
        addTcpOption(NULL, TCP_OPTION_SACK_PERMITTED, 2, 0, &options_length);
    }
    // Length & Checksum Calculation
    tcpLength = sizeof(tcpHeader) + options_length;
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
    tcp->windowSize = htons(WINDOW_SIZE);
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
