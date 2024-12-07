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
#include <stdio.h>

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

//=============================================================================
// GLOBALS
//=============================================================================

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

// Sends a ping request
void sendPingRequest(etherHeader* ether, uint8_t ipAdd[]) {
    //need to ARP for mac and need a timer for response
    //if arp fails, dest host unreachable
    /*uint16_t icmp_size;
    ipHeader* ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    icmpHeader* icmp = (icmpHeader*)((uint8_t*)ip + ipHeaderLength);
    getEtherMacAddress(localHwAddress);
    getIpAddress(localIpAddress);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = 255; //set dest IP to 255.255.255.255
        ether->sourceAddress[i] = localHwAddress[i];
    }
    ether->frameType = htons(TYPE_IP);
    // IP header
    ipHeader* ip = (ipHeader*)ether->data;
    ip->rev = 0x4;
    ip->size = 0x5;
    ip->typeOfService = 0;
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = PROTOCOL_UDP;
    ip->headerChecksum = 0;
    icmp->type = 8;
    icmp->code = 0;
    icmp->check = 0;
    icmp->data = 0xEFBE;
    icmp->seq_no = 0;
    icmp->data = "abcdefghijklmnopqrstuvwxyzabcdef";
    icmp->check = 0;
    icmp_size = ntohs(ip->length) - ipHeaderLength;
    sumIpWords(icmp, icmp_size, &sum);
    icmp->check = getIpChecksum(sum);
    putEtherPacket(ether, sizeof(etherHeader) + ntohs(ip->length));*/
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

