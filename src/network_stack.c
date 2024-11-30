#include "network_stack.h"
#include "wait.h"
#include "eeprom.h"
#include "gpio.h"
#include "uart0.h"
#include "eth0.h"
#include "arp.h"
#include "icmp.h"
#include "dhcp.h"
#include "socket.h"
#include "udp.h"
#include "tcp.h"
#include "mqtt.h"
#include <string.h>

uint8_t buffer[MAX_PACKET_SIZE];
uint8_t* udpData;
socket s;
char out[100];

bool isNetworkReady() {
    uint8_t ip[4], gw[4], sn[4];
    getIpAddress(ip);
    getIpGatewayAddress(gw);
    getIpSubnetMask(sn);
    return isIpValid(ip) && isIpValid(gw) && isIpValid(sn);
}

void netstat() {
    putsUart0("\nTCP Info\n------------------------------------------------------------\n");
    int i;
    socket* sockets = getSockets();
    for (i = 0; i < MAX_SOCKETS; i++) {
        socket t = sockets[i];
        if (t.valid) {
            uint8_t ip[4];
            getIpAddress(ip);
            char* s;
            switch(t.state) {
            case TCP_CLOSED:
                s = "CLOSED";
                break;
            case TCP_LISTEN:
                s = "LISTEN";
                break;
            case TCP_SYN_RECEIVED:
                s = "SYN_RECEIVED";
                break;
            case TCP_SYN_SENT:
                s = "SYN_SENT";
                break;
            case TCP_ESTABLISHED:
                s = "ESTABLISHED";
                break;
            case TCP_FIN_WAIT_1:
                s = "FIN_WAIT_1";
                break;
            case TCP_FIN_WAIT_2:
                s = "FIN_WAIT_2";
                break;
            case TCP_CLOSING:
                s = "CLOSING";
                break;
            case TCP_CLOSE_WAIT:
                s = "CLOSE_WAIT";
                break;
            case TCP_LAST_ACK:
                s = "LAST_ACK";
                break;
            case TCP_TIME_WAIT:
                s = "TIME_WAIT";
                break;
            }
            snprintf(out, 100, "TCP | %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d - %s\n", ip[0], ip[1], ip[2], ip[3], t.localPort, t.remoteIpAddress[0], t.remoteIpAddress[1], t.remoteIpAddress[2], t.remoteIpAddress[3], t.remotePort, s);
            putsUart0(out);
        }
    }
    putsUart0("------------------------------------------------------------\n");
}

void readConfiguration() {
    uint32_t temp;
    uint8_t* ip;

    if (readEeprom(EEPROM_DHCP) == EEPROM_ERASED) {
        enableDhcp();
    }
    else {
        disableDhcp();
        temp = readEeprom(EEPROM_IP);
        if (temp != EEPROM_ERASED)
        {
            ip = (uint8_t*)&temp;
            setIpAddress(ip);
        }
        temp = readEeprom(EEPROM_SUBNET_MASK);
        if (temp != EEPROM_ERASED)
        {
            ip = (uint8_t*)&temp;
            setIpSubnetMask(ip);
        }
        temp = readEeprom(EEPROM_GATEWAY);
        if (temp != EEPROM_ERASED)
        {
            ip = (uint8_t*)&temp;
            setIpGatewayAddress(ip);
        }
        temp = readEeprom(EEPROM_DNS);
        if (temp != EEPROM_ERASED)
        {
            ip = (uint8_t*)&temp;
            setIpDnsAddress(ip);
        }
        temp = readEeprom(EEPROM_TIME);
        if (temp != EEPROM_ERASED)
        {
            ip = (uint8_t*)&temp;
            setIpTimeServerAddress(ip);
        }
        temp = readEeprom(EEPROM_MQTT);
        if (temp != EEPROM_ERASED)
        {
            ip = (uint8_t*)&temp;
            setIpMqttBrokerAddress(ip);
        }
    }
}

void processDhcpData(etherHeader* data) {
    if (isIp(data)) {
        if (!isIpUnicast(data)) {
            if (isUdp(data)) {
                if (isDhcpResponse(data)) {
                    processDhcpResponse(data);
                }
            }
        }
    }
}

void processTcpData(etherHeader* data) {
    socket s;
    if (isIp(data)) {
        if (isIpUnicast(data)) {
            if (isTcp(data)) {
                if (isTcpPortOpen(data)) {
                    processTcpResponse(data);
                    //Layer 5-7 logic
                    if (isMqttResponse(data)) {
                        processMqttData(data);
                    }
                }
                else {
                    sendTcpResponse(data, &s, RST | ACK);
                }
            }
        }
    }
}

void processUdpData(etherHeader* data) {
    socket s;
    if (isIp(data)) {
        if (isIpUnicast(data)) {
            if (isUdp(data)) {
                //Layer 5-7 logic
                udpData = getUdpData(data);
                if (strcmp((char*)udpData, "on") == 0)
                    setPinValue(GREEN_LED, 1);
                if (strcmp((char*)udpData, "off") == 0)
                    setPinValue(GREEN_LED, 0);
                getSocketInfoFromUdpPacket(data, &s);
                sendUdpMessage(data, s, (uint8_t*)"Received", 9);
            }
        }
    }
}

void processIcmpData(etherHeader* data) {
    if (isIp(data)) {
        if (isIpUnicast(data)) {
            if (isPingRequest(data)) {
                sendPingResponse(data);
            }
        }
    }
}

void processArpData(etherHeader* data) {
    if (isArpRequest(data)) {
        sendArpResponse(data);
    }
    if (isArpResponse(data)) {
        arpPacket* arp = getArpPacket(data);
        addArpEntry(arp->sourceIp, arp->sourceAddress);
        processDhcpArpResponse(data);
        processTcpArpResponse(data);
    }
}



void runNetworkStack() {
    static etherHeader* data = (etherHeader*)buffer;
    if (isDhcpEnabled()) {
        sendDhcpPendingMessages(data); //for DHCP state machine
    }
    sendTcpPendingMessages(data); //for TCP state machine
    if (isEtherDataAvailable()) {
        if (isEtherOverflow()) {
            setPinValue(RED_LED, 1);
            waitMicrosecond(100000);
            setPinValue(RED_LED, 0);
        }
        getEtherPacket(data, MAX_PACKET_SIZE);

        processTcpData(data);
        processArpData(data);
        processIcmpData(data);
        processUdpData(data);
        if (isDhcpEnabled()) {
            processDhcpData(data);
        }
    }
}




