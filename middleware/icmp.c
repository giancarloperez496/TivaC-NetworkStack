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

#define ICMP_ECHO_TIMEOUT 3
#define ICMP_MAX_ECHO_ATTEMPTS 4

#define ICMP_DEFAULT_ECHO_DATA "abcdefghijklmnopqrstuvwabcdefghi"
#define ICMP_DEFAULT_ECHO_SIZE 32


//=============================================================================
// GLOBALS
//=============================================================================

uint32_t pingStart;
uint8_t pinging;
uint8_t pingTimeoutTimer;
uint8_t pingingIp[4];

//=============================================================================
// STATIC FUNCTIONS
//=============================================================================

static void pingTimeoutCallback(void* c) {
    putsUart0("Request timed out.\n");
    pinging = 0;
}

static void icmpArpResCallback(arpRespContext resp) {
    //when we get the MAC address
    if (resp.success) {
        //if MAC was retrieved
        uint8_t buffer[MAX_PACKET_SIZE];
        etherHeader* ether = (etherHeader*)buffer;
        copyMacAddress(ether->destAddress, resp.responseMacAddress);
        sendPingRequest(ether, (uint8_t*)resp.ctxt); //context is the passed IP address
    }
    else {
        //if ARP timed out
        putsUart0("Destination host unreachable.\n");
        pinging = 0;
    }
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

//top level fn
void ping(uint8_t ipAdd[]) {
    if (!pinging) {
        if (isIpValid(ipAdd)) {
            pinging = 1;
            copyIpAddress(pingingIp, ipAdd);
            resolveMacAddress(pingingIp, icmpArpResCallback, pingingIp);
        }
        else {
            putsUart0("Invalid IP Address\n");
        }
    }
    else {
        //pinging in progress
    }
}

// Determines whether packet is ping request
// Must be an IP packet
bool isPingRequest(etherHeader *ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    icmpHeader *icmp = (icmpHeader*)((uint8_t*)ip + ipHeaderLength);
    return (ip->protocol == PROTOCOL_ICMP && icmp->type == 8);
}

uint8_t isPingResponse(etherHeader* ether, icmpEchoResponse* reply) {
    ipHeader* ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    icmpHeader* icmp = (icmpHeader*)((uint8_t*)ip + ipHeaderLength);
    if (ip->protocol == PROTOCOL_ICMP && icmp->type == 0) {
        uint32_t responseTime = millis() - pingStart;
        pinging = 0;
        stopTimer(pingTimeoutTimer);
        //pingstart = 0;
        copyIpAddress(reply->remoteIp, ip->sourceIp);
        reply->bytes = ntohs(ip->length) - ipHeaderLength - sizeof(icmpHeader);
        reply->ms = responseTime;
        reply->ttl = ip->ttl;
        return 1;
    }
    return NULL;
}

void sendPingRequest(etherHeader* ether, uint8_t ipAdd[]) {
    //dest MAC need to be set before this is called
    ipHeader* ip = getIpHeader(ether);
    uint32_t sum = 0;
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
    copyIpAddress(ip->destIp, ipAdd);
    uint8_t ipHeaderLength = ip->size * 4;
    icmpHeader* icmp = (icmpHeader*)((uint8_t*)ip + ipHeaderLength);
    icmp->type = 8;
    icmp->code = 0;
    icmp->check = 0;
    icmp->id = htons(0x0001);
    icmp->seq_no = 1;
    uint16_t data_size = ICMP_DEFAULT_ECHO_SIZE;
    uint8_t i;
    for (i = 0; i < data_size; i++) {
        icmp->data[i] = (ICMP_DEFAULT_ECHO_DATA)[i];
    }
    uint16_t icmp_size = sizeof(icmpHeader) + data_size;
    ip->length = htons(ipHeaderLength + icmp_size);
    //icmp->data = "abcdefghijklmnopqrstuvwxyzabcdef";
    icmp->check = 0;
    calcIpChecksum(ip);
    //icmp_size = ntohs(ip->length) - ipHeaderLength;
    sumIpWords(icmp, icmp_size, &sum);
    icmp->check = getIpChecksum(sum);
    pingStart = millis(); //for timing response
    pingTimeoutTimer = startOneshotTimer(pingTimeoutCallback, ICMP_ECHO_TIMEOUT, ether); //NOT for timing response
    putEtherPacket(ether, sizeof(etherHeader) + ntohs(ip->length));
}

// Sends a ping response given the request data
void sendPingResponse(etherHeader *ether) {
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    icmpHeader *icmp = (icmpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t i, tmp;
    uint16_t icmp_size;
    uint32_t sum = 0;
    // swap source and destination fields
    for (i = 0; i < HW_ADD_LENGTH; i++) {
        tmp = ether->destAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = tmp;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++) {
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

