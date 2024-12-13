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
#include "timer.h"
#include <stdio.h>
#include <stdint.h>

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

#define ARP_RETRY_SECONDS 1
#define MAX_ARP_ATTEMPTS 3
#define MAX_ARP_REQUESTS 5

//=============================================================================
// GLOBALS
//=============================================================================

arp_entry_t arpTable[MAX_ARP_ENTRIES];
uint8_t arpTableSize = 0;
arpRequest arpReqs[MAX_ARP_REQUESTS];
uint8_t arpReqsSize = 0;

//=============================================================================
// STATIC FUNCTIONS
//=============================================================================

static void arpTimeoutCallback(void* c) {
    arpRequest* arpReq = (arpRequest*)c;
    arpReq->attempts++;
    if (arpReq->attempts == MAX_ARP_ATTEMPTS) {
        arpReq->attempts = 0;
        stopTimer(arpReq->arpTimer);
        arpRespContext resp;
        resp.success = 0;
        resp.ctxt = arpReq->ctxt;
        if (arpReq->callback) {
            arpReq->callback(resp); //mac address invalid
        }
    }
    else {
        //Nth arp attempt...
        uint8_t ether[MAX_PACKET_SIZE];
        uint8_t localIp[IP_ADD_LENGTH];
        getIpAddress(localIp);
        sendArpRequest((etherHeader*)ether, localIp, arpReq->ipAdd); // 192.168.1.75 -> 192.168.1.163
    }
}

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

void clearArpTable() {
    uint8_t i;
    for (i = 0; i < MAX_ARP_ENTRIES; i++) {
        arpTable[i].valid = 0;
        arpTableSize = 0;
    }
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

arpPacket* getArpPacket(etherHeader* ether) {
    arpPacket* arp = (arpPacket*)ether->data;
    return arp;
}

/*
SAMPLEE FUNCTION FOR RESOLVEMACADDRESS CALLBACK
void fnNeedsMAC(ip) {
    socket* s = newSocket(); //points to global, OK to use
    resolveMacAddress(ip, arpResCallback, s);
}

void arpResolutionCallback(arpRespContext resp) {
    //when we get the MAC address
    uint8_t mac[6];
    copyMacAddress(mac, resp.responseMacAddress);
    if (resp.success) {
        //if MAC was retrieved
        //finish fnNeedsMAC function
    }
    else {
        //if timed out
        //error
    }
}
 */

//ctxt must consist between loops, essentially must be a global
void resolveMacAddress(uint8_t ipAdd[4], _arp_callback_t cb, void* ctxt) {
    uint8_t localIp[4];
    uint8_t subnetMask[4];
    uint8_t gateway[4];
    uint8_t remoteMac[6];
    getIpAddress(localIp);
    getIpSubnetMask(subnetMask);
    getIpGatewayAddress(gateway);
    bool isIpLocal = isIpInSubnet(localIp, ipAdd, subnetMask);
    uint8_t arpEntryExists = lookupArpEntry(isIpLocal ? ipAdd : gateway, remoteMac);
    if (arpEntryExists) {
        arpRespContext resp;
        resp.success = 1;
        copyMacAddress(resp.responseMacAddress, remoteMac); //return MAC if exists in table
        resp.ctxt = ctxt;
        cb(resp);
    }
    else {
        uint8_t ether[MAX_PACKET_SIZE];
        arpRequest req;
        copyIpAddress(req.ipAdd, isIpLocal ? ipAdd : gateway); //if local use local IP otherwise target is gateway, this will make line 178 work. arp does not care aobut the external IP just the requested one
        req.callback = cb;
        req.attempts = 0;
        req.arpTimer = startPeriodicTimer(arpTimeoutCallback, ARP_RETRY_SECONDS, &arpReqs[arpReqsSize]); //wait 3 seconds for arp
        req.ctxt = ctxt;
        arpReqs[arpReqsSize++] = req; //add req to list
        sendArpRequest((etherHeader*)ether, localIp, isIpLocal ? ipAdd : gateway); //if IP is in same subnet, send ARP for that IP, else send ARP to gateway
    }
}

void processArpResponse(etherHeader* ether) {
    uint8_t i, j;
    arpPacket* arp = getArpPacket(ether);
    if (!lookupArpEntry(arp->sourceIp, NULL)) { //if entry DNE, add it
        addArpEntry(arp->sourceIp, arp->sourceAddress);
    }
    for (i = 0; i < MAX_ARP_REQUESTS; i++) {
        if (isIpEqual(arpReqs[i].ipAdd, arp->sourceIp)) { //if this response is a response to one of our requests, call the request callback
            //found the arp request, stop timer;
            stopTimer(arpReqs[i].arpTimer);
            //respond with success code and resolved MAC along w context
            arpRespContext resp;
            resp.success = 1;
            copyMacAddress(resp.responseMacAddress, arp->sourceAddress);
            resp.ctxt = arpReqs[i].ctxt;
            arpReqs[i].callback(resp); //application should also check??? if target ip is external, response mac address will be router;
            //remove the request from the list
            for (j = i; j < arpReqsSize - 1; j++) {
                arpReqs[j] = arpReqs[j + 1];
            }
            arpReqsSize--;
            break;
        }
    }
}

// Determines whether packet is ARP response
bool isArpResponse(etherHeader *ether) {
    arpPacket* arp = getArpPacket(ether);
    bool ok;
    ok = (ether->frameType == htons(TYPE_ARP));
    if (ok)
        ok = (arp->op == htons(2));
    return ok;
}

// Sends an ARP response given the request data
void sendArpResponse(etherHeader *ether) {
    arpPacket *arp = (arpPacket*)ether->data;
    uint8_t i, tmp;
    uint8_t localHwAddress[HW_ADD_LENGTH];

    // set op to response
    arp->op = htons(2);
    // swap source and destination fields
    getEtherMacAddress(localHwAddress);
    for (i = 0; i < HW_ADD_LENGTH; i++) {
        arp->destAddress[i] = arp->sourceAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = arp->sourceAddress[i] = localHwAddress[i];
    }
    for (i = 0; i < IP_ADD_LENGTH; i++) {
        tmp = arp->destIp[i];
        arp->destIp[i] = arp->sourceIp[i];
        arp->sourceIp[i] = tmp;
    }
    // send packet
    putEtherPacket(ether, sizeof(etherHeader) + sizeof(arpPacket));
}

// Determines whether packet is ARP
bool isArpRequest(etherHeader* ether) {
    arpPacket *arp = (arpPacket*)ether->data;
    bool ok;
    uint8_t i = 0;
    uint8_t localIpAddress[IP_ADD_LENGTH];
    ok = (ether->frameType == htons(TYPE_ARP));
    getIpAddress(localIpAddress);
    while (ok && (i < IP_ADD_LENGTH)) {
        ok = (arp->destIp[i] == localIpAddress[i]);
        i++;
    }
    if (ok)
        ok = (arp->op == htons(1));
    return ok;
}

// Sends an ARP request
void sendArpRequest(etherHeader *ether, uint8_t ipFrom[], uint8_t ipTo[]) {
    arpPacket *arp = (arpPacket*)ether->data;
    uint8_t i;
    uint8_t localHwAddress[HW_ADD_LENGTH];

    // fill ethernet frame
    getEtherMacAddress(localHwAddress);
    for (i = 0; i < HW_ADD_LENGTH; i++) {
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
    for (i = 0; i < HW_ADD_LENGTH; i++) {
        arp->sourceAddress[i] = localHwAddress[i];
        arp->destAddress[i] = 0xFF;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++) {
        arp->sourceIp[i] = ipFrom[i];
        arp->destIp[i] = ipTo[i];
    }
    // send packet
    putEtherPacket(ether, sizeof(etherHeader) + sizeof(arpPacket));
}
