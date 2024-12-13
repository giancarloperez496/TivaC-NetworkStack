/******************************************************************************
 * File:        dhcp.c
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

#include "timer.h"
#include "eeprom.h"
#include "uart0.h"
#include "arp.h"
#include "dhcp.h"
#include <stdio.h>

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

//=============================================================================
// GLOBALS
//=============================================================================

uint32_t xid = 0xEFBEADDE;
uint32_t leaseSeconds = 0;
uint32_t leaseT1 = 0;
uint32_t leaseT2 = 0;

bool discoverNeeded = false;
bool requestNeeded = false;
bool releaseNeeded = false;
bool declineNeeded = false;
bool ipConflictDetectionMode = false;

//possibly make a struct to save all offered addresses
uint8_t dhcpOfferedIpAdd[4];
uint8_t dhcpServerIpAdd[4];

uint8_t dhcpState = DHCP_DISABLED;
bool    dhcpEnabled = true;

// 7 Timers
uint8_t t1PeriodicTimer;
uint8_t t1HitTimer;
uint8_t t2PeriodicTimer;
uint8_t t2HitTimer;
uint8_t leaseEndTimer;
uint8_t arpTestTimer;
uint8_t releaseTimer;
uint8_t newAddTimer;
uint8_t leaseTickTimer;

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

void setDhcpState(uint8_t state) {
    dhcpState = state;
}

uint8_t getDhcpState() {
    return dhcpState;
}

// New address functions
// Manually requested at start-up
// Discover messages sent every 15 seconds

void requestDhcpNewAddress() {
    discoverNeeded = true;
}

void callbackDhcpGetNewAddressTimer(void* c) {
    requestDhcpNewAddress();
}

// Renew functions

void callbackDhcpT1PeriodicTimer(void* c) {
    requestNeeded = true;
}

void callbackDhcpT1HitTimer(void* c) {
    putsUart0("DHCP: T1 Hit reached\n");
    setDhcpState(DHCP_RENEWING);
    t1PeriodicTimer = startPeriodicTimer(callbackDhcpT1PeriodicTimer, 15, NULL);
}

// Rebind functions

void rebindDhcp() {
    putsUart0("DHCP: Rebinding IP, finding any DHCP server...\n");
    requestNeeded = true;
}

void callbackDhcpT2PeriodicTimer(void* c) {
    rebindDhcp();
    //stay in REBINDING
}

void callbackDhcpT2HitTimer(void* c) {
    stopTimer(t1PeriodicTimer);
    setDhcpState(DHCP_REBINDING);
    t2PeriodicTimer = startPeriodicTimer(callbackDhcpT2PeriodicTimer, 15, NULL);
}

// End of lease timer
void callbackDhcpLeaseEndTimer(void* c) {
    releaseDhcp();
}

void dhcpLeaseTick(void* c) {
    leaseSeconds--;
}

// Release functions
void releaseDhcp() {
    putsUart0("DHCP: IP Address Released\n");
    stopTimer(t1HitTimer);
    stopTimer(t1PeriodicTimer);
    stopTimer(t2HitTimer);
    stopTimer(t2PeriodicTimer);
    stopTimer(leaseEndTimer);
    stopTimer(leaseTickTimer);
    if (getDhcpState() != DHCP_INIT) {
        releaseNeeded = true;
    }
    setIpAddress(EMPTY_IP_ADDRESS);
    setIpSubnetMask(EMPTY_IP_ADDRESS);
    setIpGatewayAddress(EMPTY_IP_ADDRESS);
    setIpDnsAddress(EMPTY_IP_ADDRESS);
    setIpTimeServerAddress(EMPTY_IP_ADDRESS);
    setIpMqttBrokerAddress(EMPTY_IP_ADDRESS);
    setDhcpState(DHCP_INIT);
    discoverNeeded = true;
}

void renewIp() {
    discoverNeeded = true;
    setDhcpState(DHCP_INIT);
}

void renewDhcp() {
    //putsUart0("going to BOUND, starting T1, T2, leaseTimer...\n");
    putsUart0("DHCP: IP Address acquired\n");
    setDhcpState(DHCP_BOUND);
    setIpAddress(dhcpOfferedIpAdd);
    t1HitTimer = startOneshotTimer(callbackDhcpT1HitTimer, leaseT1, NULL);
    t2HitTimer = startOneshotTimer(callbackDhcpT2HitTimer, leaseT2, NULL);
    leaseEndTimer = startOneshotTimer(callbackDhcpLeaseEndTimer, leaseSeconds, NULL);
    leaseTickTimer = startPeriodicTimer(dhcpLeaseTick, 1, NULL);
}

// IP conflict detection
void callbackDhcpIpConflictWindow(void* c) {
    renewDhcp();
}

void requestDhcpIpConflictTest(etherHeader* ether) {
    ipHeader* iph = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = iph->size * 4;
    udpHeader* udp = (udpHeader*)((uint8_t*)iph + ipHeaderLength);
    dhcpFrame* dhcp = (dhcpFrame*)((uint8_t*)udp->data);
    uint8_t ip[4] = {0, 0, 0, 0};
    sendArpRequest(ether, ip, dhcp->yiaddr);
    putsUart0("DHCP: Sending ARP...\n");
    setDhcpState(DHCP_TESTING_IP);
    arpTestTimer = startOneshotTimer(callbackDhcpIpConflictWindow, 10, NULL); //was 5 seconds
}

bool isDhcpIpConflictDetectionMode() {
    return ipConflictDetectionMode;
}

// Lease functions

uint32_t getDhcpLeaseSeconds() {
    return leaseSeconds;
}

// Determines whether packet is DHCP
// Must be a UDP packet
bool isDhcpResponse(etherHeader* ether) {
    bool ok;
    if (!isUdp(ether)) {
        return false;
    }
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    ok = (udp->destPort == htons(DHCP_DEST_PORT_S));
    return ok;
}

dhcpFrame* getDhcpFrame(etherHeader* ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    dhcpFrame* dhcp = (dhcpFrame*)((uint8_t*)udp->data);
    return dhcp;
}

void addDhcpOption(uint8_t* options_ptr, uint8_t option_type, uint8_t len, uint8_t data[], uint8_t* options_length) {
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
    if (option_type != DHCP_OPTION_END_MARK) {
        options_ptr[i++] = len;
        for (j = 0; j < len; j++) {
            options_ptr[i++] = data[j];
        }
        if (options_length) {
            *options_length += (2 + len);
        }
        last = options_ptr + 2 + len;
    }
    else {
        if (options_length) {
            *options_length += 1;
        }
        last = options_ptr + 1;
    }
}

uint8_t* getDhcpOption(etherHeader *ether, uint8_t option) {
    dhcpFrame* dhcp = getDhcpFrame(ether);
    int i = 0;
    while (dhcp->options[i] != DHCP_OPTION_END_MARK) {
        if (dhcp->options[i] == option) {
            return dhcp->options + i;
        }
        i += 2 + dhcp->options[i+1];
    }
    return 0;
}

// Send DHCP message
void sendDhcpMessage(etherHeader* ether, uint8_t type) {
    uint8_t dhcpBuffer[576];
    dhcpFrame* dhcp = (dhcpFrame*)dhcpBuffer;
    socket s;
    uint8_t i;
    uint8_t localHwAddress[6];
    uint8_t localIpAddress[4];
    uint8_t optionData[DHCP_MAX_OPTION_LENGTH];
    uint8_t state = getDhcpState();
    getEtherMacAddress(localHwAddress);
    getIpAddress(localIpAddress);

    s.localPort = DHCP_SOURCE_PORT_C;
    s.remotePort = DHCP_DEST_PORT_C;
    copyMacAddress(s.remoteHwAddress, BROADCAST_MAC_ADDRESS);
    switch (state) {
    case DHCP_REQUESTING:
    case DHCP_TESTING_IP:
    case DHCP_BOUND:
    case DHCP_RENEWING:
        copyIpAddress(s.remoteIpAddress, dhcpServerIpAdd);
        break;
    case DHCP_SELECTING:
    case DHCP_INIT:
    case DHCP_REBINDING:
        if (type == DHCPRELEASE) {
            copyIpAddress(s.remoteIpAddress, dhcpServerIpAdd);
        }
        else {
            copyIpAddress(s.remoteIpAddress, BROADCAST_IP_ADDRESS);
        }
        break;
    }
    dhcp->op = 0x01;
    dhcp->htype = 0x01;
    dhcp->hlen = 0x06;
    dhcp->hops = 0x00;
    dhcp->xid = xid;
    dhcp->secs = htons(0x0000);
    dhcp->flags = htons(0x8000);
    copyIpAddress(dhcp->ciaddr, (type == DHCPRELEASE) ? dhcpOfferedIpAdd : EMPTY_IP_ADDRESS);
    copyIpAddress(dhcp->yiaddr, EMPTY_IP_ADDRESS);
    copyIpAddress(dhcp->siaddr, EMPTY_IP_ADDRESS);
    copyIpAddress(dhcp->giaddr, EMPTY_IP_ADDRESS);
    copyMacAddress(dhcp->chaddr, localHwAddress);
    for (i = 0; i < 10 + 192 - 1; i++) {
        dhcp->chaddr[HW_ADD_LENGTH + i] = 0; //padding
    }
    dhcp->magicCookie = MAGIC_COOKIE;//0x63825363;
    uint8_t options_length = 0;

    //DHCP Message Type - 53
    i = 0;
    optionData[i++] = type;
    addDhcpOption(dhcp->options, DHCP_OPTION_DHCP_TYPE, i, optionData, &options_length);

    //Client Identifier - 61
    i = 0;
    optionData[i++] = DHCP_OPTION_CLIENT_ID_ETHERNET;
    copyMacAddress(optionData+i, localHwAddress);
    i += HW_ADD_LENGTH;
    addDhcpOption(NULL, DHCP_OPTION_CLIENT_ID, i, optionData, &options_length);

    switch (type) {
    case DHCPDISCOVER:
        //Parameter Request List - 55
        i = 0;
        optionData[i++] = DHCP_OPTION_PARAMETER_LIST_SUBNET_MASK;
        optionData[i++] = DHCP_OPTION_PARAMETER_LIST_ROUTER;
        optionData[i++] = DHCP_OPTION_PARAMETER_LIST_DNS;
        optionData[i++] = DHCP_OPTION_PARAMETER_LIST_DOMAIN_NAME;
        addDhcpOption(NULL, DHCP_OPTION_PARAMETER_LIST, i, optionData, &options_length);
        break;
    case DHCPREQUEST:
        //Server ID - 54
        i = 0;
        copyIpAddress(optionData, dhcpServerIpAdd);
        i += IP_ADD_LENGTH;
        addDhcpOption(NULL, DHCP_OPTION_SERVER_ID, i, optionData, &options_length);
        //Requested IP - 50
        i = 0;
        copyIpAddress(optionData, dhcpOfferedIpAdd);
        i += IP_ADD_LENGTH;
        addDhcpOption(NULL, DHCP_OPTION_REQUESTED_IP, i, optionData, &options_length);
    }
    //End Options - 255
    addDhcpOption(NULL, DHCP_OPTION_END_MARK, 0, 0, &options_length);
    uint16_t dhcp_length = sizeof(dhcpFrame) + (options_length);
    sendUdpMessage(ether, &s, dhcpBuffer, dhcp_length);
}

bool isDhcpOffer(etherHeader* ether, uint8_t ipOfferedAdd[]) {
    if (!isDhcpResponse(ether)) {
        return false;
    }
    ipHeader* ip = getIpHeader(ether);
    udpHeader* udp = getUdpHeader(ether);
    dhcpFrame* dhcp = getDhcpFrame(ether);
    uint8_t* option = getDhcpOption(ether, DHCP_OPTION_DHCP_TYPE);
    bool ok;
    ok = (option[2] == DHCPOFFER) & (udp->sourcePort == DHCP_SOURCE_PORT_S);
    uint8_t localHwAddress[6];
    getEtherMacAddress(localHwAddress);
    int i;
    for (i = 0; i < HW_ADD_LENGTH; i++) {
        ok = (dhcp->chaddr[i] == localHwAddress[i]); //if this returns false the offer is not to me
    }
    if (ok) {
        copyIpAddress(dhcpOfferedIpAdd, dhcp->yiaddr);
    }
    return ok;
}

// Determines whether packet is DHCP ACK response to DHCP request
// Must be a UDP packet
bool isDhcpAck(etherHeader* ether) {
    if (!isDhcpResponse(ether)) {
        return false;
    }
    bool ok;
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    dhcpFrame* dhcp = (dhcpFrame*)((uint8_t*)udp->data);
    uint8_t* option = getDhcpOption(ether, DHCP_OPTION_DHCP_TYPE);
    uint8_t localHwAddress[6];
    getEtherMacAddress(localHwAddress);
    ok = (option[2] == DHCPACK) & (udp->sourcePort == htons(DHCP_SOURCE_PORT_S));
    int i;
    for (i = 0; i < HW_ADD_LENGTH; i++) {
        ok = (dhcp->chaddr[i] == localHwAddress[i]);
    }
    return ok;
}


void handleDhcpAck(etherHeader *ether) {
    uint8_t garb[4] = {0, 0, 0, 0};
    uint8_t* sn_ptr = getDhcpOption(ether, DHCP_OPTION_SUBNET_MASK) + 2;
    uint8_t* gw_ptr = getDhcpOption(ether, DHCP_OPTION_DEFAULT_GATEWAY) + 2;
    uint8_t* dns_ptr = getDhcpOption(ether, DHCP_OPTION_DNS_SERVER) + 2;
    uint8_t* time_ptr = getDhcpOption(ether, DHCP_OPTION_TIME_SERVER) + 2;
    //uint8_t* mqtt_ptr = getDhcpOption(ether, OPTION_DNS_SERVER) + 2;
    setIpSubnetMask(sn_ptr != (uint8_t*)(NULL+2) ? sn_ptr : garb);
    setIpGatewayAddress(gw_ptr != (uint8_t*)(NULL+2) ? gw_ptr : garb);
    setIpDnsAddress(dns_ptr != (uint8_t*)(NULL+2) ? dns_ptr : garb);
    setIpTimeServerAddress(time_ptr != (uint8_t*)(NULL+2) ? time_ptr : garb);
    //setIpMqttBrokerAddress(sn_ptr != (uint8_t*)(NULL+2) ? sn_ptr : garb);
    uint8_t* ls_ptr = getDhcpOption(ether, DHCP_OPTION_LEASE_TIME) + 2;
    uint8_t* t1_ptr = getDhcpOption(ether, DHCP_OPTION_RENEW_TIME) + 2;
    uint8_t* t2_ptr = getDhcpOption(ether, DHCP_OPTION_REBIND_TIME) + 2;
    leaseSeconds = htonl(*(uint32_t*)ls_ptr);
    if (leaseT1 != (NULL + 2)) {
        leaseT1 = htonl(*(uint32_t*)(t1_ptr));
    }
    else {
        leaseT1 = leaseSeconds/2;
    }
    if (!leaseT2 != (NULL + 2)) {
        leaseT2 = htonl(*(uint32_t*)(t2_ptr));
    }
    else {
        leaseT2 = 3*leaseSeconds/4;
    }
    uint8_t state = getDhcpState();
    switch (state) {
    case DHCP_RENEWING:
        stopTimer(t1PeriodicTimer);
        stopTimer(t2HitTimer);
        stopTimer(leaseEndTimer);
        stopTimer(leaseTickTimer);
        renewDhcp();
        break;
    case DHCP_REBINDING:
        stopTimer(t2PeriodicTimer);
        stopTimer(leaseEndTimer);
        stopTimer(leaseTickTimer);
        renewDhcp();
        break;
    case DHCP_REQUESTING:
        stopTimer(releaseTimer);
        if (isDhcpIpConflictDetectionMode()) {
            requestDhcpIpConflictTest(ether);
        }
        else {
            renewDhcp();
        }
        break;
    }
}

// Message requests

bool isDhcpDiscoverNeeded() {
    return discoverNeeded;
}

bool isDhcpRequestNeeded()
{
    return requestNeeded;
}

bool isDhcpDeclineNeeded() {
    return declineNeeded;
}

bool isDhcpReleaseNeeded() {
    return releaseNeeded;
}

void sendDhcpPendingMessages(etherHeader* ether) {
    uint8_t state = getDhcpState();
    switch(state) {
    case DHCP_DISABLED:
        return;
    case DHCP_INIT:
        if (isDhcpReleaseNeeded()) {
            releaseNeeded = false;
            sendDhcpMessage(ether, DHCPRELEASE);
        }
        if (isDhcpDiscoverNeeded()) {
            discoverNeeded = false;
            sendDhcpMessage(ether, DHCPDISCOVER);
            putsUart0("DHCP: Sending DISCOVER...\n");
            setDhcpState(DHCP_SELECTING);
            releaseTimer = startOneshotTimer(callbackDhcpLeaseEndTimer, 15, NULL); //calls releaseDhcp();
        }
        break;
    case DHCP_SELECTING:
        if (isDhcpRequestNeeded()) {
            requestNeeded = false;
            sendDhcpMessage(ether, DHCPREQUEST);
            setDhcpState(DHCP_REQUESTING);
        }
        break;
    case DHCP_REQUESTING:

        break;
    case DHCP_TESTING_IP:
        if (isDhcpDeclineNeeded()) {
            discoverNeeded = true;
            sendDhcpMessage(ether, DHCPDECLINE);
            setDhcpState(DHCP_INIT);
        }
        if (isDhcpDiscoverNeeded()) {
            discoverNeeded = false;
            stopTimer(newAddTimer);
            sendDhcpMessage(ether, DHCPDISCOVER);
            setDhcpState(DHCP_INIT);
        }
        break;
    case DHCP_BOUND:
        if (isDhcpReleaseNeeded()) {
            releaseNeeded = false;
            sendDhcpMessage(ether, DHCPRELEASE);
        }
        break;
    case DHCP_RENEWING:
        if (isDhcpRequestNeeded()) {
            requestNeeded = false;
            sendDhcpMessage(ether, DHCPREQUEST);
        }
        break;
    case DHCP_REBINDING:
        if (isDhcpRequestNeeded()) {
            requestNeeded = false;
            //putsUart0("Looking for any DHCP server... sending request to 255.255.255.255\n");
            putsUart0("DHCP: Rebinding IP, broadcasting REQUEST\n");
            int i;
            uint8_t* options = getDhcpOption(ether, DHCP_OPTION_SERVER_ID);
            for (i = 0; i < IP_ADD_LENGTH; i++) {
                options[2+i] = 255;
            }
            sendDhcpMessage(ether, DHCPREQUEST);
        }
        break;
    }
}

void processDhcpResponse(etherHeader *ether) {
    dhcpFrame* dhcp = getDhcpFrame(ether);
    uint8_t state = getDhcpState();
    switch (state) {
    case DHCP_DISABLED:
        break;
    case DHCP_INIT:
        break;
    case DHCP_SELECTING:
        if (isDhcpOffer(ether, dhcpOfferedIpAdd)) {
            uint8_t* sid = getDhcpOption(ether, DHCP_OPTION_SERVER_ID) + 2;
            int i;
            for (i = 0; i < IP_ADD_LENGTH; i++) {
                dhcpServerIpAdd[i] = sid[i];
            }
            requestNeeded = true;
        }
        break;
    case DHCP_REQUESTING:
        if (isDhcpAck(ether)) {
            handleDhcpAck(ether);
        }
        break;
    case DHCP_TESTING_IP:
        break;
    case DHCP_BOUND:
        break;
    case DHCP_RENEWING:
        if (isDhcpAck(ether)) {
            handleDhcpAck(ether);
        }
        break;
    case DHCP_REBINDING:
        if (isDhcpAck(ether)) {
            handleDhcpAck(ether);
        }
        break;
    }
}

void processDhcpArpResponse(etherHeader* ether) {
    arpPacket* arp = getArpPacket(ether);
    if (getDhcpState() == DHCP_TESTING_IP) {
        if (isIpEqual(arp->sourceIp, dhcpOfferedIpAdd)) {
            //conflicting IP
            stopTimer(arpTestTimer);
            declineNeeded = true;
            newAddTimer = startPeriodicTimer(callbackDhcpGetNewAddressTimer, 15, NULL);
        }
    }
}

void enableDhcp() {
    setDhcpState(DHCP_INIT);
    dhcpEnabled = true;
    discoverNeeded = true;
}

void disableDhcp() {
    setDhcpState(DHCP_DISABLED);
    uint8_t unboundIp[4] = {0, 0, 0, 0};
    setIpAddress(unboundIp);
    setIpSubnetMask(unboundIp);
    setIpGatewayAddress(unboundIp);
    setIpDnsAddress(unboundIp);
    setIpTimeServerAddress(unboundIp);
    setIpMqttBrokerAddress(unboundIp);
    stopTimer(t1HitTimer);
    stopTimer(t1PeriodicTimer);
    stopTimer(t2HitTimer);
    stopTimer(t2PeriodicTimer);
    stopTimer(leaseEndTimer);
    stopTimer(leaseTickTimer);
    releaseNeeded = true;
    dhcpEnabled = false;
}

bool isDhcpEnabled()
{
    return dhcpEnabled;
}

