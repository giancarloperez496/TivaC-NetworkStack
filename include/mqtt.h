/******************************************************************************
 * File:        mqtt.h
 *
 * Author:      Giancarlo Perez
 *
 * Created:     12/2/24
 *
 * Description: -
 ******************************************************************************/

#ifndef MQTT_H_
#define MQTT_H_

//=============================================================================
// INCLUDES
//=============================================================================

#include "tcp.h"
#include <stdint.h>
#include <stdbool.h>

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

/* MQTT Message Types */
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

/* MQTT QoS Types */
#define QOS_0 0
#define QOS_1 1
#define QOS_2 2
#define QOS_3 3

/* MQTT Flags */
#define MQTT_FLAG_USERNAME 0x80
#define MQTT_FLAG_PASSWORD 0x40
#define MQTT_FLAG_WILLRETAIN 0x20
#define MQTT_FLAG_QOS_0 0x00
#define MQTT_FLAG_QOS_1 0x08
#define MQTT_FLAG_QOS_2 0x10
#define MQTT_FLAG_QOS_3 0x18
#define MQTT_FLAG_WILL 0x04
#define MQTT_FLAG_CLEANSESSION 0x02

/* MQTT Client Error Types */
#define MQTT_OK 0
#define MQTT_ERROR_CONNECT_INVALID_SOCKET 1
#define MQTT_ERROR_CONNECT_INVALID_STATE 2
#define MQTT_ERROR_CONNECT_TIMEOUT 3


/* MQTT Client States */
#define MQTT_CLIENT_STATE_DISCONNECTED 0
#define MQTT_CLIENT_STATE_INIT 1
#define MQTT_CLIENT_STATE_TCP_CONNECTING 2
#define MQTT_CLIENT_STATE_TCP_CONNECTED 3
#define MQTT_CLIENT_STATE_MQTT_CONNECTING 4
#define MQTT_CLIENT_STATE_MQTT_CONNECTED 5
#define MQTT_CLIENT_STATE_DISCONNECTING 6

/* MQTT Default Send Parameters */
      //MQTT_CONNECT_
#define DEFAULT_KEEPALIVE 0
#define DEFAULT_CLEANSESSION 0
#define DEFAULT_WILLTOPIC NULL
#define DEFAULT_WILLMSG NULL
#define DEFAULT_WILLQOS 0
#define DEFAULT_WILLRETAIN 0


/* Constants */
#define MQTT_PORT 1883

#define MAX_CLIENT_ID_LENGTH 20
#define MAX_TOPICS 8
#define MAX_TOPIC_LENGTH 30
#define MAX_MQTT_DATA_SIZE 50
#define MAX_MQTT_PACKET_SIZE 256

//=============================================================================
// TYPEDEFS AND STRUCTURES
//=============================================================================

typedef struct _mqttHeader {// 20 or more bytes
    uint8_t flags; //4bits packet type - 4 bits flags
    uint8_t data[0];
} mqttHeader;

typedef struct _mqttClient {
    char clientId[MAX_CLIENT_ID_LENGTH];
    socket* socket;
    uint8_t state;
    uint8_t tx_buf[MAX_MQTT_DATA_SIZE];
    uint16_t tx_size;
    uint8_t rx_buf[MAX_MQTT_DATA_SIZE];
    uint16_t rx_size;
    char mqttTopics[MAX_TOPICS][MAX_TOPIC_LENGTH];
    uint16_t topicCount;
    uint8_t timeoutTimer;
    uint8_t flags;
} mqttClient;

typedef struct mqttError {
    uint8_t errorCode;
    socket* sk;
    char errorMsg[SOCKET_ERROR_MAX_MSG_LEN];
} mqttError;

//=============================================================================
// FUNCTION PROTOTYPES
//=============================================================================

mqttHeader* getMqttHeader(etherHeader* ether);
void setMqttState(uint8_t state);
uint8_t getMqttState();
void initMqtt();
void runMqttClient();
void processMqttData(etherHeader* ether);
void processMqttPublish(mqttHeader* mqtt, uint8_t* topicIndex, char* command);
void connectMqtt();
void disconnectMqtt();
void publishMqtt(char strTopic[], char strData[]);
void subscribeMqtt(char strTopic[]);
void unsubscribeMqtt(char strTopic[]);


//void sendMqttMessage(uint8_t msg);

void sendMqttConnect(uint16_t keepAlive, bool cleanSession, const char* willTopic, const char* willMsg, uint8_t willQoS, bool willRetain);
void sendMqttConnack();
void sendMqttPublish();
void sendMqttPubAck();
void sendMqttPubRec() ;
void sendMqttPubComp();
void sendMqttSubscribe();
void sendMqttSubAck();
void sendMqttUnsubscribe();
void sendMqttUnsubAck();
void sendMqttPingReq();
void sendMqttPingResp();
void sendMqttDisconnect();

#endif

