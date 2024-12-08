/******************************************************************************
 * File:        mqtt.c
 *
 * Author:      Giancarlo Perez
 *
 * Created:     12/2/24
 *
 * Description: -
 ******************************************************************************/


//=============================================================================
// INCLUDES
//=============================================================================

#include <stdio.h>
#include <string.h>
#include "mqtt.h"
#include "tcp.h"
#include "timer.h"
#include "uart0.h"
#include "strlib.h"


//=============================================================================
// DEFINES AND MACROS
//=============================================================================

#define lobyte(x) x & 0xFF;
#define hibyte(x) x >> 8;

//=============================================================================
// GLOBALS
//=============================================================================

uint8_t mqttBuffer[MAX_MQTT_PACKET_SIZE];

//=============================================================================
// STATIC FUNCTIONS
//=============================================================================

static uint8_t encodeLength(uint8_t* out, uint32_t i) {
    uint8_t byte;
    uint8_t lenlen = 0;;
    do {
        byte = i % 128;
        i /= 128;
        if (i > 0) {
            byte |= 0x80;
        }
        out[lenlen++] = byte;
    } while (i > 0);
    return lenlen;
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

uint32_t decodeLength(const uint8_t* data, uint16_t* dataLen) {
    size_t multiplier = 1;
    size_t value = 0;
    size_t lenlen = 0;
    do {
        if (lenlen >= 4) {
            return 0;
        }
        value += (data[lenlen] & 0x7F) * multiplier;
        multiplier *= 128;

        if ((data[lenlen] & 0x80) == 0) {
            *dataLen = value;
            return lenlen + 1;
        }
        lenlen++;
    } while (1);
}

mqttHeader* getMqttHeader(etherHeader* ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);
    uint32_t pllength = ntohs(ip->length) - (ip->size * 4) - ((ntohs(tcp->offsetFields) >> 12) * 4);
    uint8_t* tcpData = tcp->data;
    mqttHeader* mqtt = (mqttHeader*)tcpData;
    return mqtt;
}

bool isMqttResponse(etherHeader* ether) {
    bool ok;
    tcpHeader* tcp = getTcpHeader(ether);
    mqttHeader* mqtt = getMqttHeader(ether);
    ok = (htons(tcp->sourcePort) == MQTT_PORT);
    ok &= (mqtt->flags >= 0x10 && mqtt->flags <= 0xF0);
    return ok;
}

void sendMqttConnect(mqttClient* client) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    mqttOptions* opt = &client->options; //using a ptr to save space
    //in the future maybe make functions to add these fields, like in DHCP & TCP
    uint16_t i = 0; //uint8 should suffice
    // Header Flags:
    mqtt->flags = (MQTT_CONNECT << 4) | 0b0000;
    // Msg Len:
    mqtt->data[i++] = 0; //i = 0 is length byte
    // Protocol Name Length:
    mqtt->data[i++] = 4 >> 8;
    mqtt->data[i++] = 4 & 0xFF;
    // Protocol Name:
    mqtt->data[i++] = 'M';
    mqtt->data[i++] = 'Q';
    mqtt->data[i++] = 'T';
    mqtt->data[i++] = 'T';
    // Version:
    mqtt->data[i++] = opt->version;
    // Connect Flags:
    mqtt->data[i++] = (opt->willRetain << 5) | (opt->willQos << 3) | (opt->willFlag << 2) | (opt->cleanSession << 1);
    // Keep Alive:
    mqtt->data[i++] = opt->keepAlive >> 8;
    mqtt->data[i++] = opt->keepAlive & 0xFF;
    // Client ID Length:
    uint16_t cidLen = str_length(client->clientId);
    mqtt->data[i++] = cidLen >> 8;
    mqtt->data[i++] = cidLen & 0xFF;
    // Client ID:
    uint8_t o;
    for (o = 0; o < cidLen; o++) {
        mqtt->data[i++] = client->clientId[o];
    }
    //mqtt->data[i++] = 'G';
    //mqtt->data[i++] = 'P';
    uint8_t tmplen[4];
    uint8_t lenlen = encodeLength(tmplen, i);
    uint32_t p;
    for (p = 1; p < i + lenlen - 1; p++) {
        mqtt->data[p + lenlen - 1] = mqtt->data[p];
    }
    for (p = 0; p < lenlen; p++) {
        mqtt->data[p] = tmplen[p];
    }
    mqtt->data[0] = i-1;
    uint16_t dataLength = i + 1;
    socketSendTcp(client->socket, (uint8_t*)mqtt, dataLength);
}

void sendMqttConnack(mqttClient* client) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;

}

void sendMqttPublish(mqttClient* client, char strTopic[], char strData[]) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    mqttOptions* opt = &client->options;
    uint16_t i = 0;
    uint32_t o;
    // Header Flags:
    mqtt->flags = (MQTT_PUBLISH << 4) | (opt->qos << 1);
    // Msg Len:
    mqtt->data[i++] = 0;
    if (opt->qos > 0) {
        uint16_t packetId = 10;
        mqtt->data[i++] = packetId >> 8;
        mqtt->data[i++] = packetId & 0xFF;
    }
    uint16_t msgLen = str_length((char*)strData);
    uint16_t topicLen = str_length((char*)strTopic);
    // Topic Length:
    mqtt->data[i++] = topicLen >> 8;
    mqtt->data[i++] = topicLen & 0xFF;
    for (o = 0; o < topicLen; o++) {
        mqtt->data[i++] = strTopic[o];
    }
    for (o = 0; o < msgLen; o++) {
        mqtt->data[i++] = strData[o];
    }
    uint8_t tmplen[4];
    uint8_t lenlen = encodeLength(tmplen, i);
    uint32_t p;
    for (p = 1; p < i + lenlen - 1; p++) {
        mqtt->data[p + lenlen - 1] = mqtt->data[p];
    }
    for (p = 0; p < lenlen; p++) {
        mqtt->data[p] = tmplen[p];
    }
    mqtt->data[0] = i-1;
    uint16_t dataLength = i+1;
    socketSendTcp(client->socket, (uint8_t*)mqtt, dataLength);
}

void sendMqttPubAck(mqttClient* client) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;

}

void sendMqttPubRec(mqttClient* client) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;

}

void sendMqttPubComp(mqttClient* client) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;

}

void sendMqttSubscribe(mqttClient* client, char strTopic[]) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    mqttOptions* opt = &client->options;
    uint32_t i = 0;
    mqtt->flags = (MQTT_SUBSCRIBE << 4) | 0b0010;
    mqtt->data[i++] = 0;
    uint16_t packetId = random32() % 0xFFFF;
    mqtt->data[i++] = hibyte(packetId);
    mqtt->data[i++] = lobyte(packetId);
    size_t topicLen = str_length((char*)strTopic);
    mqtt->data[i++] = hibyte(topicLen);
    mqtt->data[i++] = lobyte(topicLen);
    uint32_t o;
    for (o = 0; o < topicLen; o++) {
        mqtt->data[i++] = strTopic[o];
    }
    mqtt->data[i++] = opt->qos;
    uint8_t tmplen[4];
    uint8_t lenlen = encodeLength(tmplen, i);
    uint32_t p;
    for (p = 1; p < i + lenlen - 1; p++) {
        mqtt->data[p + lenlen - 1] = mqtt->data[p];
    }
    for (p = 0; p < lenlen; p++) {
        mqtt->data[p] = tmplen[p];
    }
    mqtt->data[0] = i-1;
    uint16_t dataLength = i + 1;
    socketSendTcp(client->socket, (uint8_t*)mqtt, dataLength);
}

void sendMqttSubAck(mqttClient* client) {

}

void sendMqttUnsubscribe(mqttClient* client, char strTopic[]) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    //mqttOptions* opt = &client->options;
    uint32_t i = 0;
    mqtt->flags = (MQTT_UNSUBSCRIBE << 4) | 0b0010;
    mqtt->data[i++] = 0;
    uint16_t packetId = random32() % 0xFFFF;
    mqtt->data[i++] = hibyte(packetId);
    mqtt->data[i++] = lobyte(packetId);
    size_t topicLen = str_length((char*)strTopic);
    mqtt->data[i++] = hibyte(topicLen);
    mqtt->data[i++] = lobyte(topicLen);
    uint32_t o;
    for (o = 0; o < topicLen; o++) {
        mqtt->data[i++] = strTopic[o];
    }
    uint8_t tmplen[4];
    uint8_t lenlen = encodeLength(tmplen, i);
    uint32_t p;
    for (p = 1; p < i + lenlen - 1; p++) {
        mqtt->data[p + lenlen - 1] = mqtt->data[p];
    }
    for (p = 0; p < lenlen; p++) {
        mqtt->data[p] = tmplen[p];
    }
    mqtt->data[0] = i-1;
    uint16_t dataLength = i + 1;
    socketSendTcp(client->socket, (uint8_t*)mqtt, dataLength);
}

void sendMqttUnsubAck(mqttClient* client) {

}

void sendMqttPingReq(mqttClient* client) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    uint16_t i = 0;
    mqtt->flags = (MQTT_PINGREQ << 4) | 0b0000;
    mqtt->data[i++] = 0;
    uint16_t dataLength = i + 1;
    socketSendTcp(client->socket, (uint8_t*)mqtt, dataLength);
}

void sendMqttPingResp(mqttClient* client) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    uint16_t i = 0;
    mqtt->flags = (MQTT_PINGRESP << 4) | 0b0000;
    mqtt->data[i++] = 0;
    uint16_t dataLength = i + 1;
    socketSendTcp(client->socket, (uint8_t*)mqtt, dataLength);
}

void sendMqttDisconnect(mqttClient* client) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    uint8_t i = 0;
    mqtt->flags = (MQTT_DISCONNECT << 4) | 0b0000;    // disconnect flag
    mqtt->data[i++] = 0;              // no length
    uint16_t dataLength = i + 1;
    socketSendTcp(client->socket, (uint8_t*)mqtt, dataLength);
}

