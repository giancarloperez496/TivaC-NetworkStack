// MQTT Library (framework only)
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

#ifndef MQTT_H_
#define MQTT_H_

#include "tcp.h"
#include <stdint.h>
#include <stdbool.h>


#define MQTT_CONNECT 1
#define MQTT_CONNACK 2
#define MQTT_PUBLISH 3
#define MQTT_PUBACK 4
#define MQTT_PUBREC 5
#define MQTT_PUBREL 6
#define MQTT_PUBCOMP 7
#define MQTT_SUBSCRIBE 8
#define MQTT_SUBACK 9
#define MQTT_UNSUBSCRIBE 10
#define MQTT_UNSUBACK 11
#define MQTT_PINGREQ 12
#define MQTT_PINGRESP 13
#define MQTT_DISCONNECT 14
#define MQTT_AUTH 15

#define MQTT_STATE_DISCONNECTED 0
#define MQTT_STATE_CONNECTING 1
#define MQTT_STATE_CONNECTED 2
#define MQTT_STATE_DISCONNECTING 3

#define QOS_0 0
#define QOS_1 1
#define QOS_2 2
#define QOS_3 3
#define MQTT_FLAG_USERNAME 0x80
#define MQTT_FLAG_PASSWORD 0x40
#define MQTT_FLAG_WILLRETAIN 0x20
#define MQTT_FLAG_QOS_0 0x00
#define MQTT_FLAG_QOS_1 0x08
#define MQTT_FLAG_QOS_2 0x10
#define MQTT_FLAG_QOS_3 0x18
#define MQTT_FLAG_WILL 0x04
#define MQTT_FLAG_CLEANSESSION 0x02

#define MQTT_OK 0
#define MQTT_CONNECT_ERROR_INVALID_SOCKET 1
#define MQTT_CONNECT_ERROR_INVALID_STATE 2

#define MQTT_PORT 1883

 //Bit 7 : Username     Bit 6 : Password     Bit 5 : Will Retain
    // Bit 4:3 QoS Lvl     Bit 2 : Will Flag       Bit 1 : Clean Session
//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

#define MAX_TOPICS 8
#define MAX_TOPIC_LENGTH 30
#define QOS 1

#define MAX_MQTT_DATA_SIZE 50

#define MQTT_CLIENT_TCP_CONNECTED   0b00000001
#define MQTT_CLIENT_TX_READY        0b00000010
#define MQTT_CLIENT_RX_READY        0b00000100
#define MQTT_CLIENT_RUNNING         0b00001000

#define MQTT_CLIENT_STATE_DISCONNECTED 0
#define MQTT_CLIENT_STATE_INIT 1
#define MQTT_CLIENT_STATE_TCP_CONNECTING 2
#define MQTT_CLIENT_STATE_TCP_CONNECTED 3
#define MQTT_CLIENT_STATE_MQTT_CONNECTING 4
#define MQTT_CLIENT_STATE_MQTT_WAITFORCONNACK 5
#define MQTT_CLIENT_STATE_MQTT_CONNECTED 6
#define MQTT_CLIENT_STATE_MQTT_

#define MQTT_CLOSING 255;

typedef struct _mqttHeader {// 20 or more bytes
    uint8_t flags; //4bits packet type - 4 bits flags
    uint8_t data[0];
} mqttHeader;

typedef struct _mqttClient {
    socket* socket;
    uint8_t state;
    uint8_t tx_buf[MAX_MQTT_DATA_SIZE];
    uint16_t tx_size;
    uint8_t rx_buf[MAX_MQTT_DATA_SIZE];
    uint16_t rx_size;
    char mqttTopics[MAX_TOPICS][MAX_TOPIC_LENGTH];
    uint16_t topicCount;
    uint8_t flags;
} mqttClient;

typedef struct mqttError {
    uint8_t errorCode;
    socket* sk;
    char errorMsg[SOCKET_ERROR_MAX_MSG_LEN];
} mqttError;

#define MQTT_ERROR_CONNECT_TIMEOUT 1
/*
 * 1 - isTcpConnected
 * 2 - isTxReady
 * 3 - isRxReady
 * 4 - isRunning
 */

uint32_t getStrLength(char* str);
void setMqttSocket(socket*);
socket* getMqttSocket();
void initMqtt();
void runMqttClient();
void processMqttData(etherHeader* ether);
void processMqttPublish(mqttHeader* mqtt, uint8_t* topicIndex, char* command);
void connectMqtt();
void disconnectMqtt();
void mqttRetryConnection();
void mqttErrorCallback(void* context);
uint8_t encodeLength(uint8_t* out, uint32_t i);
void publishMqtt(char strTopic[], char strData[]);
void subscribeMqtt(char strTopic[]);
void unsubscribeMqtt(char strTopic[]);
void setMqttState( uint8_t state);
void sendMqttMessage(uint8_t msg);
uint8_t getMqttState();
uint8_t getMqttQos();

#endif

