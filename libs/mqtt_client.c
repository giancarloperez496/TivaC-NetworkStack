/******************************************************************************
 * File:        mqtt_client.c
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

#include "mqtt_client.h"
#include "mqtt.h"
#include "strlib.h"
#include "uart0.h"
#include <stdio.h>

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

#ifndef NULL
 #define NULL 0
#endif

/* Configurations */
#define MAX_CONNECT_RETRIES 2
#define MQTT_VERSION MQTT_VERSION_v3_1_1
#define USE_WILL 0
#define QOS 0

//=============================================================================
// GLOBALS
//=============================================================================

//if i want to make mqttclient a singleton of some sort, i can define it in here and just use it all as a global instead of function parameters, similar to dhcp
//we are assuming client is a singleton and only one instance can run on this device. functions shall not take parameters as they will modify this singleton

/* Globals */
static mqttClient client[1];
char out[MAX_UART_OUT];

//=============================================================================
// STATIC FUNCTIONS
//=============================================================================

/* Forward Declarations */
static void socketErrorCallback(void* context);
static void mqttRetryConnection();
static void mqttErrorCallback(void* context);
static void mqttConnectTimeout(void* context);
static void mqttKeepAliveCallback(void* context);

//Application level error handler, application can handle Layer 4 errors here
//Layer 4 calls this function
static void socketErrorCallback(void* context) {
    socketError* err = (socketError*)context;
    //if i want to retry TCP connections, i can call connectMqtt() again
    switch (err->errorCode) {
    //TCP layer errors
    case SOCKET_ERROR_ARP_TIMEOUT:
        setMqttState(MQTT_CLIENT_STATE_DISCONNECTED);
        break;
    case SOCKET_ERROR_TCP_SYN_ACK_TIMEOUT:
        setMqttState(MQTT_CLIENT_STATE_DISCONNECTED);
        break;
    case SOCKET_ERROR_CONNECTION_RESET:
        setMqttState(MQTT_CLIENT_STATE_DISCONNECTED);
        //reset config (topics, etc.)
        break;
    }
    deleteSocket(err->sk);
    putsUart0(err->errorMsg);
    putsUart0("\n\n");
    //err->sk should ALWAYS be the same as client->socket
}

static void mqttRetryConnection() {
    static uint8_t attempts = 0;
    uint8_t mqstate = getMqttState();
    if (mqstate == MQTT_CLIENT_STATE_MQTT_CONNECTING) {
        attempts++;
        if (attempts == MAX_CONNECT_RETRIES) {
            putsUart0("MQTT Client: Failed to connect to MQTT Broker\n\n");
            attempts = 0;
            disconnectMqtt();
        }
        else {
            snprintf(out, MAX_UART_OUT, "MQTT Client: Retrying connection to MQTT Broker... (%d/%d)\n", attempts, MAX_CONNECT_RETRIES);
            putsUart0(out);
            sendMqttConnect(client);
            client->timeoutTimer = startOneshotTimer(mqttConnectTimeout, 10, NULL);
        }
    }
    else {
        //timer should be stopped when this function is called and should not restart
    }
}

//Application error handler, application will handle Layer 7 errors here
//Layer 7 calls this function
static void mqttErrorCallback(void* context) {
    mqttError* err = (mqttError*)context;
    putsUart0(err->errorMsg);
    switch(err->errorCode) {
    //MQTT layer errors
    case MQTT_ERROR_CONNECT_TIMEOUT:
        mqttRetryConnection();
        break;
    }
}

static void mqttConnectTimeout(void* context) {
    //if state == connecting else ignore??
    mqttError err;
    err.errorCode = MQTT_ERROR_CONNECT_TIMEOUT;
    //call connectMqtt() again as needed
    str_copy(err.errorMsg, "Timed out waiting for MQTT Connect from broker.\n");
    mqttErrorCallback(&err);
}

static void mqttKeepAliveCallback(void* context) {
    putsUart0("MQTT Client: Sending PINGREQ\n");
    sendMqttPingReq(client);
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

//main client loop
void runMqttClient() {
    socket* mqttSocket = client->socket;
    uint8_t mqttState = getMqttState();
    uint8_t tcpState = getTcpState(mqttSocket);
    switch (mqttState) {
    case MQTT_CLIENT_STATE_DISCONNECTED:
        break;
    case MQTT_CLIENT_STATE_TCP_CONNECTING:
        //lowkey need a better way to check if the socket is ready, usually by callback or blocking fn or flag, this should work for this purpose tho
        /*
         * onTcpEstablished(socket) {
        */
        if (tcpState == TCP_ESTABLISHED) {
            putsUart0("MQTT Client: TCP Connection established\n");
            setMqttState(MQTT_CLIENT_STATE_TCP_CONNECTED);
        }
        break;
    case MQTT_CLIENT_STATE_TCP_CONNECTED:
        sendMqttConnect(client);
        setMqttState(MQTT_CLIENT_STATE_MQTT_CONNECTING);
        if (MAX_CONNECT_RETRIES > 0) {
            client->timeoutTimer = startOneshotTimer(mqttConnectTimeout, 10, NULL);
        }
        break;
    case MQTT_CLIENT_STATE_MQTT_CONNECTING:
        //handle CONNACK in processMqttData
        if (tcpState == TCP_CLOSE_WAIT) {
            stopTimer(client->timeoutTimer);
            //stopTimer(client->keepAliveTimer);
            disconnectMqtt();
        }
        break;
    case MQTT_CLIENT_STATE_MQTT_CONNECTED:

        if (tcpState == TCP_CLOSE_WAIT) {
            setMqttState(MQTT_CLIENT_STATE_DISCONNECTING);
            //stopTimer(client->keepAliveTimer);
            socketCloseTcp(client->socket);
            //disconnectMqtt();
        }
        break;
    case MQTT_CLIENT_STATE_DISCONNECTING:


        if (tcpState == TCP_CLOSED) {
            stopTimer(client->timeoutTimer);
            stopTimer(client->keepAliveTimer);
            setMqttState(MQTT_CLIENT_STATE_DISCONNECTED);
            putsUart0("MQTT Client: Disconnected from MQTT Broker\n");
            //deleteSocket(mqttSocket);
            //how do we know that the socket finished closing?
            //if !s.valid?
            //checking state may be iffy
            //callback?
        }
        break;
    }
}

inline void setMqttState(uint8_t state) {
    client->state = state;
}

inline uint8_t getMqttState() {
    return client->state;
}

inline uint8_t getMqttResponse(mqttHeader* mqtt) {
    return mqtt->flags >> 4;
}

inline void setHandlePublishCallback(mqtt_callback_t cb) {
    client->pubCallback = cb;
}

//use strlib eventually
void setMqttTopics(char** topics, uint32_t count) {
    uint32_t i, j;
    if (count <= MAX_TOPICS) {
        for (i = 0; i < count; i++) {
            for (j = 0; topics[i][j] != '\0' && j < MAX_TOPIC_LENGTH - 1; j++) {
                client->mqttTopics[i][j] = topics[i][j];
            }
            client->mqttTopics[i][j] = '\0';
        }
    }
}

void getMqttTopics(char** input, uint32_t* count) {
    uint32_t i;
    for (i = 0; i < MAX_TOPICS; i++) {
        input[i] = client->mqttTopics[i];
        if (client->mqttTopics[i][0] == '\0') {
            *count = i;
            break;
        }
        *count = i + 1;
    }
}

void initMqttClient() {
    setMqttState(MQTT_CLIENT_STATE_DISCONNECTED);
    mqttOptions* opt = &client->options;
    str_copy(client->clientId, "TM4C123GXL");
    opt->version = MQTT_VERSION;
    opt->qos = QOS; //in the future have this configured by command, along with all other options
    opt->keepAlive = DEFAULT_KEEPALIVE;
    opt->cleanSession = DEFAULT_CLEANSESSION;
    opt->willFlag = 0; //set will flag off by default, wil be able to be changed by commands in the future
    opt->willQos = QOS_0;
    client->pubCallback = NULL;
    //return client;
}

//connects to MQTT server stored in device on port 1883
//starts with TCP connection
//once TCP established, send MQTT connect and move to MQTT_CONNECTING

void connectMqtt() {
    uint8_t mqttState = getMqttState();
    uint8_t mqserv[4];
    switch (mqttState) {
    case MQTT_CLIENT_STATE_DISCONNECTED:
        client->socket = newSocket(SOCKET_STREAM);
        client->socket->errorCallback = socketErrorCallback;
        getIpMqttBrokerAddress(mqserv);
        snprintf(out, MAX_UART_OUT, "MQTT Client: Connecting to MQTT server %d.%d.%d.%d:%d\n", mqserv[0], mqserv[1], mqserv[2], mqserv[3], MQTT_PORT);
        putsUart0(out);
        socketConnectTcp(client->socket, mqserv, MQTT_PORT);
        setMqttState(MQTT_CLIENT_STATE_TCP_CONNECTING);
        break;
    default:
        break;
    }
}

void disconnectMqtt() {
    uint8_t mqttState = getMqttState();
    switch (mqttState) {
    case MQTT_CLIENT_STATE_DISCONNECTED:
        //do nothing, client already disconnected
        return;
    case MQTT_CLIENT_STATE_TCP_CONNECTING:
        //never got SYN ACK, should be handled by socketErrorCallback
        break;
    case MQTT_CLIENT_STATE_MQTT_CONNECTING:
        //never got CONNACK, client wants to close connection
        //sendMqttDisconnect();
        socketCloseTcp(client->socket);
        break;
    case MQTT_CLIENT_STATE_MQTT_CONNECTED:
        //got CONNACK, client wants to close connection
        sendMqttDisconnect(client);
        socketCloseTcp(client->socket);
        break;
    default:
        break;
    }
    setMqttState(MQTT_CLIENT_STATE_DISCONNECTING);
}

void publishMqtt(char strTopic[], char strData[]) {
    uint8_t mqttState = getMqttState();
    //restart keepalive timer
    restartTimer(client->keepAliveTimer);
    if (mqttState == MQTT_CLIENT_STATE_MQTT_CONNECTED) {
        sendMqttPublish(client, strTopic, strData);
    }
}

void subscribeMqtt(char strTopic[]) {
    uint8_t mqttState = getMqttState();
    //restart keepalive timer
    restartTimer(client->keepAliveTimer);
    if (mqttState == MQTT_CLIENT_STATE_MQTT_CONNECTED) {
        sendMqttSubscribe(client, strTopic);
    }
}

void unsubscribeMqtt(char strTopic[]) {
    uint8_t mqttState = getMqttState();
    restartTimer(client->keepAliveTimer);
    if (mqttState == MQTT_CLIENT_STATE_MQTT_CONNECTED) {
        sendMqttUnsubscribe(client, strTopic);
    }
}


void processMqttData(etherHeader* ether) {
    mqttHeader* mqtt = getMqttHeader(ether);
    mqttData data;
    uint8_t responseType = getMqttResponse(mqtt);
    uint16_t mqttState = getMqttState();
    uint8_t i;
    if (mqttState == MQTT_CLIENT_STATE_MQTT_CONNECTING) {
        switch (responseType) {
        case MQTT_CONNACK:
            stopTimer(client->timeoutTimer);
            client->keepAliveTimer = startPeriodicTimer(mqttKeepAliveCallback, client->options.keepAlive * 0.9, NULL); //check if no other messages were sent in this time frame, if so reset timer.
            setMqttState(MQTT_CLIENT_STATE_MQTT_CONNECTED);
            putsUart0("MQTT Client: Connected to MQTT Broker\n");
            break;
        }
    }
    else if (mqttState == MQTT_CLIENT_STATE_MQTT_CONNECTED) {
        //snprintf(out, MAX_UART_OUT, "Received MQTT data:\n %s\n", mqtt->data);
        //putsUart0(out);
        uint16_t dataLen;
        uint32_t lenlen;
        uint16_t topicLen;
        putsUart0("MQTT Client: ");
        switch (responseType) {
        case MQTT_PUBLISH:
            putsUart0("Received PUBLISH\n");
            lenlen = decodeLength(mqtt->data, &dataLen);
            topicLen = (mqtt->data[lenlen] << 8) | mqtt->data[lenlen + 1];
            for (i = 0; i < topicLen; i++) {
                data.topic[i] = mqtt->data[i+lenlen+2]; //copy topic from data into topic var
            }
            data.topic[topicLen] = '\0';
            data.data = mqtt->data + lenlen + 2 + topicLen;

            mqtt->data[dataLen+1] = '\0'; //Null-terminate data for printing
            snprintf(out, MAX_UART_OUT, " Received:\n  Topic: %s\n  Data: %s\n", data.topic, (char*)data.data);
            putsUart0(out);
            if (client->pubCallback){
                client->pubCallback(&data); //Pass data to application
            }
            break;
        case MQTT_PUBACK:
            putsUart0("Received PUBACK\n");
            break;
        case MQTT_SUBACK:
            putsUart0("Received SUBACK\n");
            break;
        case MQTT_UNSUBACK:
            putsUart0("Received UNSUBACK\n");
            break;
        case MQTT_PINGREQ:
            putsUart0("Received PINGREQ\n");
            //pingRespMqtt();
            break;
        case MQTT_PINGRESP:
            putsUart0("Received PINGRESP\n");
            break;
        }
    }
    else {
        //we only care about MQTT packets if we're connecting or connected
    }
}

