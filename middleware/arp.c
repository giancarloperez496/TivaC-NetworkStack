/******************************************************************************
 * File:        arp.c
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

#include "uart0.h"
#include "arp.h"
#include "ip.h"
#include <stdio.h>
#include <stdint.h>

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

//=============================================================================
// GLOBALS
//=============================================================================

arp_entry_t arpTable[MAX_ARP_ENTRIES];
uint8_t arpTableSize = 0;

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

void displayArpTable() {
    putsUart0("\nARP Cache\n------------------------------------------------------------\n");
    putsUart0(" IP Address                  MAC Address\n");
    uint8_t i;
    for (i = 0; i < MAX_ARP_ENTRIES; i++) {
        if (arpTable[i].valid) {
            uint8_t* ip = arpTable[i].ipAddress;
            uint8_t* mac = arpTable[i].macAddress;
            char ipStr[16];
            char macStr[18];
            snprintf(ipStr, 16, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            snprintf(macStr, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            snprintf(out, MAX_UART_OUT, " %-28s%-20s\n", ipStr, macStr);
            putsUart0(out);
        }
    }
    putsUart0("------------------------------------------------------------\n\n");
}

void addArpEntry(uint8_t ipAddress[], uint8_t macAddress[]) {
    //O(1) complexity
    arp_entry_t* entry = &arpTable[arpTableSize++];
    copyIpAddress(entry->ipAddress, ipAddress);
    copyMacAddress(entry->macAddress, macAddress);
    entry->valid = 1;
}

//writes to mac address, returns NULL If no entry
uint8_t lookupArpEntry(uint8_t ipAddress[], uint8_t macAddressToWrite[]) {
    uint8_t i;
    //O(1) best case
    //O(N) worst case
    for (i = 0; i < arpTableSize; i++) {
        if (isIpEqual(arpTable[i].ipAddress, ipAddress)) {
            //found given IP in ARP table
            if (macAddressToWrite) {
                copyMacAddress(macAddressToWrite, arpTable[i].macAddress);
            }
            return 1;
        }
    }
    return NULL;  // No entry found for the given IP address
}

// Determines whether packet is ARP
bool isArpRequest(etherHeader* ether) {
    arpPacket *arp = (arpPacket*)ether->data;
    bool ok;
    uint8_t i = 0;
    uint8_t localIpAddress[IP_ADD_LENGTH];
    ok = (ether->frameType == htons(TYPE_ARP));
    getIpAddress(localIpAddress);
    while (ok && (i < IP_ADD_LENGTH))
    {
        ok = (arp->destIp[i] == localIpAddress[i]);
        i++;
    }
    if (ok)
        ok = (arp->op == htons(1));
    return ok;
}

// Determines whether packet is ARP response
bool isArpResponse(etherHeader *ether)
{
    arpPacket *arp = (arpPacket*)ether->data;
    bool ok;
    ok = (ether->frameType == htons(TYPE_ARP));
    if (ok)
        ok = (arp->op == htons(2));
    return ok;
}

arpPacket* getArpPacket(etherHeader* ether) {
    arpPacket* arp = (arpPacket*)ether->data;
    return arp;
}

// Sends an ARP response given the request data
void sendArpResponse(etherHeader *ether)
{
    arpPacket *arp = (arpPacket*)ether->data;
    uint8_t i, tmp;
    uint8_t localHwAddress[HW_ADD_LENGTH];

    // set op to response
    arp->op = htons(2);
    // swap source and destination fields
    getEtherMacAddress(localHwAddress);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        arp->destAddress[i] = arp->sourceAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = arp->sourceAddress[i] = localHwAddress[i];
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        tmp = arp->destIp[i];
        arp->destIp[i] = arp->sourceIp[i];
        arp->sourceIp[i] = tmp;
    }
    // send packet
    putEtherPacket(ether, sizeof(etherHeader) + sizeof(arpPacket));
}

// Sends an ARP request
void sendArpRequest(etherHeader *ether, uint8_t ipFrom[], uint8_t ipTo[]) {
    arpPacket *arp = (arpPacket*)ether->data;
    uint8_t i;
    uint8_t localHwAddress[HW_ADD_LENGTH];

    // fill ethernet frame
    getEtherMacAddress(localHwAddress);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->sourceAddress[i] = localHwAddress[i];
        ether->destAddress[i] = 0xFF;
    }
    ether->frameType = htons(TYPE_ARP);
    // fill arp frame
    arp->hardwareType = htons(1);
    arp->protocolType = htons(TYPE_IP);
    arp->hardwareSize = HW_ADD_LENGTH;
    arp->protocolSize = IP_ADD_LENGTH;
    arp->op = htons(1);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        arp->sourceAddress[i] = localHwAddress[i];
        arp->destAddress[i] = 0xFF;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        arp->sourceIp[i] = ipFrom[i];
        arp->destIp[i] = ipTo[i];
    }
    // send packet
    putEtherPacket(ether, sizeof(etherHeader) + sizeof(arpPacket));
}
