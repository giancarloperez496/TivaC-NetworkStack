/******************************************************************************
 * File:        icmp.c
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

#include "icmp.h"
#include "clock.h"
#include "uart0.h"
#include "ip.h"
#include "arp.h"
#include "timer.h"
#include <stdio.h>

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

#define ICMP_ARP_RETRY 1
#define ICMP_ARP_TIMEOUT 3
#define ICMP_ECHO_TIMEOUT 2
#define ICMP_MAX_ARP_ATTEMPTS 3
#define ICMP_MAX_ECHO_ATTEMPTS 4

#define ICMP_DEFAULT_ECHO_DATA "abcdefghijklmnopqrstuvwxyzabcdef"

//=============================================================================
// GLOBALS
//=============================================================================

uint8_t arpTimer;
uint8_t arpingAdd[4];
uint8_t pinging;
uint8_t arping;
uint32_t pingstart;
uint8_t pings;

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

// Determines whether packet is ping request
// Must be an IP packet
bool isPingRequest(etherHeader *ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    icmpHeader *icmp = (icmpHeader*)((uint8_t*)ip + ipHeaderLength);
    return (ip->protocol == PROTOCOL_ICMP && icmp->type == 8);
}

uint8_t isPingResponse(etherHeader* ether, icmpEchoResponse* reply) {
    if (pinging) {
        ipHeader* ip = (ipHeader*)ether->data;
        uint8_t ipHeaderLength = ip->size * 4;
        icmpHeader* icmp = (icmpHeader*)((uint8_t*)ip + ipHeaderLength);
        if (ip->protocol == PROTOCOL_ICMP && icmp->type == 0) {
            pinging = 0;
            uint32_t responseTime = millis() - pingstart;
            pingstart = 0;
            copyIpAddress(reply->remoteIp, ip->sourceIp);
            reply->bytes = ntohs(ip->length) - ipHeaderLength - sizeof(icmpHeader);
            reply->ms = responseTime;
            reply->ttl = ip->ttl;
            return 1;
        }
    }
    return NULL;
}

void pingTimeoutCallback(void* c) {
    if (pinging) {
        if (arping) { //if timed out while arping
            putsUart0("Destination host unreachable.\n");
        }
        else { //if timed out while pinging
            putsUart0("Request timed out.\n");
        }
        pinging = 0;
    }
}

void arpTimeoutCallback(void* c) {
    etherHeader* ether = (etherHeader*)c;
    ipHeader* ip = (ipHeader*)ether->data;
    static uint8_t attempts = 0;
    attempts++;
    if (attempts == ICMP_MAX_ARP_ATTEMPTS) {
        //putsUart0("Failed to connect to server\n");
        attempts = 0;
        pingTimeoutCallback(c);
        arping = 0;
        stopTimer(arpTimer);

    }
    else {
        //snprintf(out, MAX_UART_OUT, "TCP: Retrying connection to server... (%d/%d)\n", s->connectAttempts+1, TCP_MAX_SYN_ATTEMPTS);
        //putsUart0(out);
        //s->sequenceNumber--; //undo increment of seq n for retransmission of syn
        //pendTcpResponse(s, SYN);
        //setTcpState(s, TCP_SYN_SENT);

        sendArpRequest(ether, localIp, ip->destIp);
    }
}

/*void pingCallback(void* c) {
    static uint8_t cnt = 0;
    uint8_t* ipAdd = (uint8_t*)c; //aray of 4
    uint8_t buffer[MAX_PACKET_SIZE];
    if (cnt++ < ICMP_MAX_ECHO_ATTEMPTS) {
        sendPingRequest((etherHeader*)buffer, ipAdd);
    }
    else {
        stopTimer(pings);
    }
}*/

void ping(uint8_t ipAdd[]) {
    //pingCallback(ipAdd);
    //pings = startPeriodicTimer(pingCallback, ICMP_ECHO_TIMEOUT, ipAdd);
    uint8_t buffer[MAX_PACKET_SIZE];
    sendPingRequest((etherHeader*)buffer, ipAdd);
}

void sendPingRequestCallback(etherHeader* ether, icmpEchoRequest* req) {
    //dest MAC and dest IP need to be set before this is called
    pingstart = millis(); //for timing response
    uint8_t pingTimeoutTimer = startOneshotTimer(pingTimeoutCallback, ICMP_ECHO_TIMEOUT-1, ether); //NOT for timing response
    ipHeader* ip = (ipHeader*)ether->data;
    uint32_t sum = 0;
    copyMacAddress(ether->destAddress, req->remoteMac);
    getEtherMacAddress(ether->sourceAddress);
    ether->frameType = htons(TYPE_IP);
    // IP header
    ip->rev = 0x4;
    ip->size = 0x5;
    ip->typeOfService = 0;
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = PROTOCOL_ICMP;
    ip->headerChecksum = 0;
    getIpAddress(ip->sourceIp);
    copyIpAddress(ip->destIp, req->remoteIp);
    uint8_t ipHeaderLength = ip->size * 4;
    icmpHeader* icmp = (icmpHeader*)((uint8_t*)ip + ipHeaderLength);
    icmp->type = 8;
    icmp->code = 0;
    icmp->check = 0;
    icmp->seq_no = 0;
    uint16_t data_size = req->dataSize;
    uint8_t i;
    for (i = 0; i < data_size; i++) {
        icmp->data[i] = req->data[i];
    }
    uint16_t icmp_size = sizeof(icmpHeader) + data_size;
    ip->length = htons(ipHeaderLength + icmp_size);
    //icmp->data = "abcdefghijklmnopqrstuvwxyzabcdef";
    icmp->check = 0;
    calcIpChecksum(ip);
    //icmp_size = ntohs(ip->length) - ipHeaderLength;
    sumIpWords(icmp, icmp_size, &sum);
    icmp->check = getIpChecksum(sum);
    putEtherPacket(ether, sizeof(etherHeader) + ntohs(ip->length));
}

// Sends a ping request
void sendPingRequest(etherHeader* ether, uint8_t ipAdd[]) {
    //need to ARP for mac and need a timer for response
    //if arp fails, dest host unreachable
    if (pinging) {
        return;
    }
    pinging = 1;
    uint8_t localIp[4];
    uint8_t subnetMask[4];
    uint8_t gateway[4];
    uint8_t remoteMac[6];
    getIpAddress(localIp);
    getIpSubnetMask(subnetMask);
    getIpGatewayAddress(gateway);
    bool isIpLocal = isIpInSubnet(localIp, ipAdd, subnetMask);
    uint8_t arpEntryExists = lookupArpEntry(ipAdd, remoteMac);
    if (arpEntryExists) {
        icmpEchoRequest req;
        copyMacAddress(req.remoteMac, remoteMac);
        copyIpAddress(req.remoteIp, ipAdd);
        req.dataSize = str_length(ICMP_DEFAULT_ECHO_DATA);
        str_copy(req.data, ICMP_DEFAULT_ECHO_DATA);
        sendPingRequestCallback(ether, &req);
        //send ping
    }
    else {
        arping = 1;
        copyIpAddress(arpingAdd, ipAdd);
        sendArpRequest(ether, localIp, isIpLocal ? ipAdd : gateway); //if IP is in same subnet, send ARP for that IP, else send ARP to gateway
        arpTimer = startPeriodicTimer(arpTimeoutCallback, ICMP_ARP_RETRY, ether); //wait 30 seconds for arp
        //s->assocTimer = arpTimer; //assign timer id to socket or local port (multiple ways to do this)
    }
}

void processIcmpArpResponse(etherHeader* ether) {
    ipHeader* ip = getIpHeader(ether);
    arpPacket* arp = getArpPacket(ether);
    if (isIpEqual(arp->sourceIp, arpingAdd)) {
        //arp response is in response to our ping
        arping = 0;
        stopTimer(arpTimer);
        //copyMacAddress(ether->destAddress, arp->sourceAddress);
        //copyIpAddress(ip->destIp, arpingAdd);
        icmpEchoRequest req;
        copyMacAddress(req.remoteMac, arp->sourceAddress);
        copyIpAddress(req.remoteIp, arpingAdd);
        req.dataSize = str_length(ICMP_DEFAULT_ECHO_DATA);
        str_copy(req.data, ICMP_DEFAULT_ECHO_DATA);
        sendPingRequestCallback(ether, &req);
    }
}

// Sends a ping response given the request data
void sendPingResponse(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    icmpHeader *icmp = (icmpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t i, tmp;
    uint16_t icmp_size;
    uint32_t sum = 0;
    // swap source and destination fields
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        tmp = ether->destAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = tmp;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        tmp = ip->destIp[i];
        ip->destIp[i] = ip ->sourceIp[i];
        ip->sourceIp[i] = tmp;
    }
    // this is a response
    icmp->type = 0;
    // calc icmp checksum
    icmp->check = 0;
    icmp_size = ntohs(ip->length) - ipHeaderLength;
    sumIpWords(icmp, icmp_size, &sum);
    icmp->check = getIpChecksum(sum);
    // send packet
    putEtherPacket(ether, sizeof(etherHeader) + ntohs(ip->length));
}

