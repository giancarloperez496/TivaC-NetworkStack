#include "shell.h"
#include "tm4c123gh6pm.h"
#include "uart0.h"
#include "eeprom.h"
#include "eth0.h"
#include "arp.h"
#include "ip.h"
#include "dhcp.h"
#include "mqtt_client.h"
#include "strlib.h"
#include <inttypes.h>
#include <network_stack.h>
#include <stdio.h>

char strInput[MAX_CHARS+1];
uint8_t count = 0;

extern arp_entry_t arpTable[MAX_ARP_ENTRIES];

uint8_t asciiToUint8(const char str[]) {
    uint8_t data;
    if (str[0] == '0' && to_lower(str[1]) == 'x')
        sscanf(str, "%hhx", &data);
    else
        sscanf(str, "%hhu", &data);
    return data;
}

void ipconfig() {
    uint8_t i;
    char str[20];
    uint8_t mac[6];
    uint8_t ip[4];
    getEtherMacAddress(mac);
    putsUart0("\nIP Configuration\n------------------------------------------------------------\n");
    putsUart0("  MAC:   ");
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        snprintf(str, sizeof(str), "%02X", mac[i]);
        putsUart0(str);
        if (i < HW_ADD_LENGTH-1)
            putcUart0(':');
    }
    putcUart0('\n');
    getIpAddress(ip);
    putsUart0("  IP:    ");
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        snprintf(str, sizeof(str), "%"PRIu8, ip[i]);
        putsUart0(str);
        if (i < IP_ADD_LENGTH-1)
            putcUart0('.');
    }
    if (isDhcpEnabled())
        putsUart0(" (dhcp)");
    else
        putsUart0(" (static)");
    putcUart0('\n');
    getIpSubnetMask(ip);
    putsUart0("  SN:    ");
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        snprintf(str, sizeof(str), "%"PRIu8, ip[i]);
        putsUart0(str);
        if (i < IP_ADD_LENGTH-1)
            putcUart0('.');
    }
    putcUart0('\n');
    getIpGatewayAddress(ip);
    putsUart0("  GW:    ");
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        snprintf(str, sizeof(str), "%"PRIu8, ip[i]);
        putsUart0(str);
        if (i < IP_ADD_LENGTH-1)
            putcUart0('.');
    }
    putcUart0('\n');
    getIpDnsAddress(ip);
    putsUart0("  DNS:   ");
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        snprintf(str, sizeof(str), "%"PRIu8, ip[i]);
        putsUart0(str);
        if (i < IP_ADD_LENGTH-1)
            putcUart0('.');
    }
    putcUart0('\n');
    getIpTimeServerAddress(ip);
    putsUart0("  NTP:   ");
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        snprintf(str, sizeof(str), "%"PRIu8, ip[i]);
        putsUart0(str);
        if (i < IP_ADD_LENGTH-1)
            putcUart0('.');
    }
    putcUart0('\n');
    getIpMqttBrokerAddress(ip);
    putsUart0("  MQTT:  ");
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        snprintf(str, sizeof(str), "%"PRIu8, ip[i]);
        putsUart0(str);
        if (i < IP_ADD_LENGTH-1)
            putcUart0('.');
    }
    putcUart0('\n');
    if (isDhcpEnabled())
    {
        putsUart0("  Lease: ");
        uint32_t s, m, h, d;
        s = getDhcpLeaseSeconds();
        d = s / (24*60*60);
        s -= d * (24*60*60);
        h = s / (60*60);
        s -= h * (60*60);
        m = s / 60;
        snprintf(str, sizeof(str), "%"PRIu32"d:%02"PRIu32"h:%02"PRIu32"m\n", d, h, m);
        putsUart0(str);
    }
    if (isEtherLinkUp())
        putsUart0("  Link is up\n");
    else
        putsUart0("  Link is down\n");
    putsUart0("------------------------------------------------------------\n\n");
}

void processShell() {
    bool end;
    char c;
    uint8_t i;
    uint8_t ip[IP_ADD_LENGTH];
    uint32_t* p32;
    char* topic,* data,* token;
    if (kbhitUart0()) {
        c = getcUart0();
        end = (c == 13) || (count == MAX_CHARS);
        if (!end) {
            if ((c == 8 || c == 127) && count > 0)
                count--;
            if (c >= ' ' && c < 127)
                strInput[count++] = c;
        }
        else {
            strInput[count] = '\0';
            count = 0;
            token = str_tokenize(strInput, " ");
            if (str_equal(token, "dhcp")) {
                token = str_tokenize(NULL, " ");
                if (str_equal(token, "renew")) {
                    renewDhcp();
                }
                else if (str_equal(token, "release")) {
                    releaseDhcp();
                }
                else if (str_equal(token, "on")) {
                    enableDhcp();
                    writeEeprom(EEPROM_DHCP, EEPROM_ERASED);
                }
                else if (str_equal(token, "off")) {
                    disableDhcp();
                    writeEeprom(EEPROM_DHCP, 0);
                }
                else
                    putsUart0("Error in dhcp argument\r");
            }
            if (str_equal(token, "mqtt")) {
                token = str_tokenize(NULL, " ");
                if (str_equal(token, "connect")) {
                    //setMqttClientState(&mq, MQTT_CLIENT_STATE_INIT);
                    connectMqtt();

                    //uint8_t retcode = connectMqtt(ether);
                    //connectMqtt();
                    /*switch (retcode) {
                    case MQTT_OK:
                        break;
                    case MQTT_CONNECT_ERROR_INVALID_SOCKET:
                        putsUart0("Error connecting to MQTT Broker: Invalid socket\n");
                        break;
                    case MQTT_CONNECT_ERROR_INVALID_STATE:
                        putsUart0("Error connecting to MQTT Broker: Invalid state\n");
                        break;
                    }*/
                }
                if (str_equal(token, "disconnect")) {
                    disconnectMqtt();
                }
                if (str_equal(token, "publish")) {
                    topic = str_tokenize(NULL, " ");
                    data = str_tokenize(NULL, " ");
                    if (topic != NULL && data != NULL)
                        publishMqtt(topic, data);
                }
                if (str_equal(token, "subscribe")) {
                    topic = str_tokenize(NULL, " ");
                    if (topic != NULL)
                        subscribeMqtt(topic);
                }
                if (str_equal(token, "unsubscribe")) {
                    topic = str_tokenize(NULL, " ");
                    if (topic != NULL)
                        unsubscribeMqtt(topic);
                }
            }
            if (str_equal(token, "ipconfig")) {
                ipconfig();
            }
            if (str_equal(token, "netstat")) {
                netstat();
            }
            if (str_equal(token, "arp")) {
                displayArpTable();
            }
            if (str_equal(token, "ping")) {
                for (i = 0; i < IP_ADD_LENGTH; i++)
                {
                    token = str_tokenize(NULL, " .");
                    ip[i] = asciiToUint8(token);
                }
                ping(ip);
            }
            if (str_equal(token, "reboot")) {
                resetEther();
                NVIC_APINT_R = NVIC_APINT_VECTKEY | NVIC_APINT_SYSRESETREQ;
            }
            if (str_equal(token, "set")) {
                token = str_tokenize(NULL, " ");
                if (str_equal(token, "ip")) {
                    for (i = 0; i < IP_ADD_LENGTH; i++) {
                        token = str_tokenize(NULL, " .");
                        ip[i] = asciiToUint8(token);
                    }
                    setIpAddress(ip);
                    p32 = (uint32_t*)ip;
                    writeEeprom(EEPROM_IP, *p32);
                }
                if (str_equal(token, "sn")) {
                    for (i = 0; i < IP_ADD_LENGTH; i++) {
                        token = str_tokenize(NULL, " .");
                        ip[i] = asciiToUint8(token);
                    }
                    setIpSubnetMask(ip);
                    p32 = (uint32_t*)ip;
                    writeEeprom(EEPROM_SUBNET_MASK, *p32);
                }
                if (str_equal(token, "gw")) {
                    for (i = 0; i < IP_ADD_LENGTH; i++) {
                        token = str_tokenize(NULL, " .");
                        ip[i] = asciiToUint8(token);
                    }
                    setIpGatewayAddress(ip);
                    p32 = (uint32_t*)ip;
                    writeEeprom(EEPROM_GATEWAY, *p32);
                }
                if (str_equal(token, "dns")) {
                    for (i = 0; i < IP_ADD_LENGTH; i++) {
                        token = str_tokenize(NULL, " .");
                        ip[i] = asciiToUint8(token);
                    }
                    setIpDnsAddress(ip);
                    p32 = (uint32_t*)ip;
                    writeEeprom(EEPROM_DNS, *p32);
                }
                if (str_equal(token, "time")) {
                    for (i = 0; i < IP_ADD_LENGTH; i++) {
                        token = str_tokenize(NULL, " .");
                        ip[i] = asciiToUint8(token);
                    }
                    setIpTimeServerAddress(ip);
                    p32 = (uint32_t*)ip;
                    writeEeprom(EEPROM_TIME, *p32);
                }
                if (str_equal(token, "mqtt")) {
                    for (i = 0; i < IP_ADD_LENGTH; i++) {
                        token = str_tokenize(NULL, " .");
                        ip[i] = asciiToUint8(token);
                    }
                    setIpMqttBrokerAddress(ip);
                    p32 = (uint32_t*)ip;
                    writeEeprom(EEPROM_MQTT, *p32);
                }
            }
            if (str_equal(token, "help")) {
                putsUart0("Commands:\n");
                putsUart0("  dhcp on|off|renew|release\n");
                putsUart0("  mqtt ACTION [USER [PASSWORD]]\n");
                putsUart0("    where ACTION = {connect|disconnect|publish TOPIC DATA\n");
                putsUart0("                   |subscribe TOPIC|unsubscribe TOPIC}\n");
                putsUart0("  ip\n");
                putsUart0("  ping w.x.y.z\n");
                putsUart0("  reboot\n");
                putsUart0("  set ip|gw|dns|time|mqtt|sn w.x.y.z\n");
            }
        }
    }
}
