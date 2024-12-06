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

/* Configurations */
#define QOS 1
#define MAX_CONNECT_RETRIES 2

//=============================================================================
// TYPEDEFS AND STRUCTURES
//=============================================================================

/*
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
*/

//if i want to make mqttclient a singleton of some sort, i can define it in here and just use it all as a global instead of function parameters, similar to dhcp
//we are assuming client is a singleton and only one instance can run on this device. functions shall not take parameters as they will modify this singleton

/* Globals */
static mqttClient client[1];
uint8_t mqttBuffer[MAX_MQTT_PACKET_SIZE];
char out[MAX_UART_OUT];

/* Forward Declarations */
static uint8_t encodeLength(uint8_t* out, uint32_t i);
static void mqttKeepAliveCallback(void* context);
static void mqttConnectTimeout(void* context);
static void socketErrorCallback(void* context);
static void mqttErrorCallback(void* context);
static void mqttRetryConnection();

//=============================================================================
// MAIN FUNCTION
//=============================================================================

//main client loop
void runMqttClient() { //extern these maybe?
    socket* mqttSocket = client->socket;
    uint8_t mqttState = getMqttState();
    uint8_t tcpState = getTcpState(mqttSocket);
    //handleTransition();
    switch (mqttState) {
    case MQTT_CLIENT_STATE_DISCONNECTED:
        break;
    case MQTT_CLIENT_STATE_TCP_CONNECTING:
        //lowkey need a better way to check if the socket is ready, usually by callback or blocking fn or flag, this should work for this purpose tho
        /*
         * onTcpEstablished(socket) {
        */
        if (tcpState == TCP_ESTABLISHED) {
            putsUart0("TCP Connection established\n");
            setMqttState(MQTT_CLIENT_STATE_TCP_CONNECTED);
        }
        break;
    case MQTT_CLIENT_STATE_TCP_CONNECTED:
        //sendMqttMessage(MQTT_CONNECT);
        sendMqttConnect(DEFAULT_KEEPALIVE, DEFAULT_CLEANSESSION, DEFAULT_WILLTOPIC, DEFAULT_WILLMSG, DEFAULT_WILLQOS, DEFAULT_WILLRETAIN);
        setMqttState(MQTT_CLIENT_STATE_MQTT_CONNECTING);
        if (MAX_CONNECT_RETRIES > 0) {
            client->timeoutTimer = startOneshotTimer(mqttConnectTimeout, 10, NULL);
        }
        break;
    case MQTT_CLIENT_STATE_MQTT_CONNECTING:
        //handle CONNACK in processMqttData
        if (tcpState == TCP_CLOSE_WAIT) {
            disconnectMqtt();
        }
        break;
    case MQTT_CLIENT_STATE_MQTT_CONNECTED:
        stopTimer(client->timeoutTimer);
        client->keepAliveTimer = startPeriodicTimer(mqttKeepAliveCallback, client->keepAlive * 0.9, NULL); //check if no other messages were sent in this time frame, if so reset timer.
        if (tcpState == TCP_CLOSE_WAIT) {
            socketCloseTcp(client->socket);
            //disconnectMqtt();
        }
        break;
    case MQTT_CLIENT_STATE_DISCONNECTING:


        if (tcpState == TCP_CLOSED) {
            stopTimer(client->timeoutTimer);
            setMqttState(MQTT_CLIENT_STATE_DISCONNECTED);
            putsUart0("Disconnected from MQTT Broker\n");
            //deleteSocket(mqttSocket);
            //how do we know that the socket finished closing?
            //if !s.valid?
            //checking state may be iffy
            //callback?
        }
        break;
    }
}

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

static void mqttKeepAliveCallback(void* context) {
    putsUart0("Sending MQTT Ping request");
    //sendMqttPingReq();
}

static void mqttConnectTimeout(void* context) {
    //if state == connecting else ignore??
    mqttError err;
    err.errorCode = MQTT_ERROR_CONNECT_TIMEOUT;
    //call connectMqtt() again as needed
    str_copy(err.errorMsg, "Timed out waiting for MQTT Connect from broker.\n");
    mqttErrorCallback(&err);
}

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
    }
    deleteSocket(err->sk);
    putsUart0(err->errorMsg);
    //err->sk should ALWAYS be the same as client->socket
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

static void mqttRetryConnection() {
    static uint8_t attempts = 0;
    uint8_t mqstate = getMqttState();
    if (mqstate == MQTT_CLIENT_STATE_MQTT_CONNECTING) {
        attempts++;
        if (attempts == MAX_CONNECT_RETRIES) {
            putsUart0("Reached max number of attempts to connect to broker; resetting connection...\n");
            attempts = 0;
            disconnectMqtt();
        }
        else {
            putsUart0("Retrying connection to MQTT Broker...\n");
            sendMqttConnect(DEFAULT_KEEPALIVE, DEFAULT_CLEANSESSION, DEFAULT_WILLTOPIC, DEFAULT_WILLMSG, DEFAULT_WILLQOS, DEFAULT_WILLRETAIN);
            client->timeoutTimer = startOneshotTimer(mqttConnectTimeout, 10, NULL);
        }
    }
    else {
        //timer should be stopped when this function is called and should not restart
    }
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/* Setters & Getters */
mqttHeader* getMqttHeader(etherHeader* ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);
    uint32_t pllength = ntohs(ip->length) - (ip->size * 4) - ((ntohs(tcp->offsetFields) >> 12) * 4);
    uint8_t* tcpData = tcp->data;
    mqttHeader* mqtt = (mqttHeader*)tcpData;
    return mqtt;
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


/* MQTT Specific Functions */
void initMqtt() {
    setMqttState(MQTT_CLIENT_STATE_DISCONNECTED);
    mqttOptions* opt = &client->options;
    str_copy(client->clientId, "TM4C123GXL");
    opt->version = MQTT_VERSION;
    opt->qos = DEFAULT_QOS; //in the future have this configured by command, along with all other options
    opt->keepAlive = DEFAULT_KEEPALIVE;
    opt->cleanSession = DEFAULT_CLEANSESSION;
    opt->willFlag = 0; //set will flag off by default, wil be able to be changed by commands in the future
    opt->willQos = QOS_0;
}


//connects to MQTT server stored in device on port 1883
//starts with TCP connection
//once TCP established, send MQTT connect and move to MQTT_CONNECTING
void connectMqtt() {
    uint8_t mqttState = getMqttState();
    uint8_t mqserv[4];
    switch (mqttState) {
    case MQTT_CLIENT_STATE_DISCONNECTED:
        client->socket = newSocket();
        client->socket->errorCallback = socketErrorCallback;
        getIpMqttBrokerAddress(mqserv);
        snprintf(out, 50, "Connecting to MQTT server %d.%d.%d.%d:%d\n", mqserv[0], mqserv[1], mqserv[2], mqserv[3], MQTT_PORT);
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
        sendMqttDisconnect();
        socketCloseTcp(client->socket);
        break;
    default:
        break;
    }
    setMqttState(MQTT_CLIENT_STATE_DISCONNECTING);
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


bool isMqttResponse(etherHeader* ether) {
    bool ok;
    tcpHeader* tcp = getTcpHeader(ether);
    ok = (htons(tcp->sourcePort) == MQTT_PORT);
    return ok;
}



/*void pingRespMqtt() {
    uint32_t i = 0;
    etherHeader* ether = (etherHeader*)etherbuffer;
    socket* s = getMqttSocket();
    if (s) {
        mqttHeader* mqtt = (mqttHeader*)mqttbuffer;
        mqtt->flags = (MQTT_PINGRESP << 4) | 0b0000;
        mqtt->data[i++] = 0;
        uint16_t dataLength;
        uint8_t state = getMqttState();
        switch (state) {
        case MQTT_STATE_DISCONNECTED:
            dataLength = i+1;
            sendTcpMessage(ether, s, PSH | ACK, (uint8_t*)mqtt, dataLength);
            setMqttState(MQTT_STATE_CONNECTING);
            break;
        default:
            break;
        }
    }
}*/



// returns return code
//void connectMqtt() {

    /*etherHeader* ether = (etherHeader*)etherbuffer;
    if (s) {
        uint32_t i = 0;
        mqttHeader* mqtt = (mqttHeader*)mqttbuffer;
        mqtt->flags = (MQTT_CONNECT << 4) | 0b0000;
        mqtt->data[i++] = 0;
        mqtt->data[i++] = 4 >> 8;
        mqtt->data[i++] = 4 & 0xFF;
        mqtt->data[i++] = 'M';
        mqtt->data[i++] = 'Q';
        mqtt->data[i++] = 'T';
        mqtt->data[i++] = 'T';
        mqtt->data[i++] = 0x04;
        mqtt->data[i] = 0;
        mqtt->data[i++] |= MQTT_FLAG_CLEANSESSION | (QOS << 3);
        mqtt->data[i++] = 60 >> 8;
        mqtt->data[i++] = 60 & 0xFF;
        mqtt->data[i++] = 2 >> 8;
        mqtt->data[i++] = 2 & 0xFF;
        mqtt->data[i++] = 'G';
        mqtt->data[i++] = 'P';
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
        uint16_t dataLength;
        uint8_t state = getMqttState();
        switch (state) {
        case MQTT_STATE_DISCONNECTED:
            dataLength = i + 1;
            sendTcpMessage(ether, s, PSH | ACK, (uint8_t*)mqtt, dataLength);
            setMqttState(MQTT_STATE_CONNECTING);
            break;
            //return MQTT_OK;
        default:
            //return MQTT_CONNECT_ERROR_INVALID_STATE;
            break;
        }
    }
    else {
        //return MQTT_CONNECT_ERROR_INVALID_SOCKET;
    }*/
//}

//DISCONNECT CODE
    /*etherHeader* ether = (etherHeader*)etherbuffer;
    socket* s = getMqttSocket();
    if (s) {
        uint32_t i = 0;
        mqttHeader* mqtt = (mqttHeader*)mqttbuffer;
        mqtt->flags = (MQTT_DISCONNECT << 4) | 0;
        mqtt->data[i++] = 0;
        uint16_t dataLength;
        uint8_t state = getMqttState();
        switch (state) {
        case MQTT_STATE_CONNECTED:
            dataLength = i+1;
            sendTcpMessage(ether, s, PSH | ACK, (uint8_t*)mqtt, dataLength);
            setMqttState(MQTT_STATE_DISCONNECTED);
            break;
        default:
            break;
        }
    }
    else {
        return;
    }*/

void publishMqtt(char strTopic[], char strData[]) {
    uint8_t mqttState = getMqttState();
    if (mqttState == MQTT_CLIENT_STATE_MQTT_CONNECTED) {
        sendMqttPublish(strTopic, strData);
    }
}

void processMqttPublish(etherHeader* ether, uint8_t* topicIndex, char* command) {
    mqttHeader* mqtt = getMqttHeader(ether);
    /*uint32_t varLength;
    uint8_t byte;
    uint8_t i = 0;
    uint32_t shift = 1;
    do {
        byte = mqtt->data[i];
        varLength += byte & 127 * shift;
        shift = shift * 128;
        i++;
    }
    while(byte & 128);
    char* topic;
    uint8_t topicLen = mqtt->data[ 3+i ] << 8 | mqtt->data[ 4+i ];
    strncpy(topic, (char*)(mqtt->data[ 5+i+topicLen+2 ]), topicLen);
    topic[ topicLen ] = '\0';
    uint32_t commandLen = varLength - 2 - topicLen;
    strncpy(command, (char*)(mqtt->data[ 5+i+topicLen+2 ]), commandLen);
    command[commandLen] = '\0';
    if(topicLen == strlen( mqttTopics[0])) {
        if( strcmp( command, mqttTopics[0] ) )          // "uta/ir/address"
            *topicIndex = 0;
        else if( strcmp( command, mqttTopics[5] ) )     // "uta/ir/channel"
            *topicIndex = 5;*/
}

void processMqttData(etherHeader* ether) {
    mqttHeader* mqtt = getMqttHeader(ether);
    uint8_t responseType = getMqttResponse(mqtt);
    uint16_t mqttState = getMqttState();
    if (mqttState == MQTT_CLIENT_STATE_MQTT_CONNECTING) {
        switch (responseType) {
        case MQTT_CONNACK:
            setMqttState(MQTT_CLIENT_STATE_MQTT_CONNECTED);
            putsUart0("Connected to MQTT Broker\n");
            break;
        }
    }
    else if (mqttState == MQTT_CLIENT_STATE_MQTT_CONNECTED) {
        snprintf(out, MAX_UART_OUT, "Received MQTT data:\n %s\n", mqtt->data);
        putsUart0(out);
        switch (responseType) {
        case MQTT_PUBLISH:

            break;
        case MQTT_PUBACK:

            break;
        case MQTT_SUBACK:

            break;
        case MQTT_UNSUBACK:

            break;
        case MQTT_PINGREQ:
            //pingRespMqtt();
            break;
        }
    }
    else {
        //we only care about packets if we're connecting or connected
    }
}


void subscribeMqtt(char strTopic[]) {
    uint8_t mqttState = getMqttState();
    if (mqttState == MQTT_CLIENT_STATE_MQTT_CONNECTED) {

    }
}

void unsubscribeMqtt(char strTopic[]) {
    uint8_t mqttState = getMqttState();
    if (mqttState == MQTT_CLIENT_STATE_MQTT_CONNECTED) {

    }
    /*etherHeader* ether = (etherHeader*)etherbuffer;
    socket* s = getMqttSocket();
    if (s) {
        uint32_t i = 0;
        mqttHeader* mqtt = (mqttHeader*)mqttbuffer;
        mqtt->flags = (MQTT_UNSUBSCRIBE << 4) | 0b0010;
        mqtt->data[i++] = 0;
        uint16_t packetId = 12;
        mqtt->data[i++] = packetId >> 8;
        mqtt->data[i++] = packetId & 0xFF;
        size_t topicLen = str_length((char*)strTopic);
        mqtt->data[i++] = topicLen >> 8;
        mqtt->data[i++] = topicLen & 0xFF;
        uint32_t o;
        for (o = 0; o < topicLen; o++) {
            mqtt->data[i++] = strTopic[o];
        }
        mqtt->data[i++] = 0x01;
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
        uint16_t dataLength;
        uint8_t state = getMqttState();
        switch (state) {
        case MQTT_STATE_CONNECTED:
            dataLength = i+1;
            sendTcpMessage(ether, s, PSH | ACK, (uint8_t*)mqtt, dataLength);
            break;
        }
    }*/
}

void sendMqttConnect() {
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

void sendMqttConnack() {
    uint8_t etherBuffer[MAX_PACKET_SIZE];
    etherHeader* ether = (etherHeader*)etherBuffer;
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;

}

void sendMqttPublish(char strTopic[], char strData[]) {
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

void sendMqttPubAck() {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;

}

void sendMqttPubRec() {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;

}

void sendMqttPubComp() {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;

}

void sendMqttSubscribe(char strTopic[]) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    mqttOptions* opt = &client->options;
    uint32_t i = 0;
    mqtt->flags = (MQTT_SUBSCRIBE << 4) | 0b0010;
    mqtt->data[i++] = 0;
    uint16_t packetId = 10;
    mqtt->data[i++] = packetId >> 8;
    mqtt->data[i++] = packetId & 0xFF;
    size_t topicLen = str_length((char*)strTopic);
    mqtt->data[i++] = topicLen >> 8;
    mqtt->data[i++] = topicLen & 0xFF;
    uint32_t o;
    for (o = 0; o < topicLen; o++) {
        mqtt->data[i++] = strTopic[o];
    }
    mqtt->data[i++] = 0x01;
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
    sendTcpMessage(ether, client->socket, PSH | ACK, (uint8_t*)mqtt, dataLength);
}

void sendMqttSubAck() {

}

void sendMqttUnsubscribe() {

}

void sendMqttUnsubAck() {

}

void sendMqttPingReq() {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    uint16_t i = 0;
    mqtt->flags = (MQTT_PINGREQ << 4) | 0b0000;
    mqtt->data[i++] = 0;
    uint16_t dataLength = i + 1;
    socketSendTcp(client->socket, (uint8_t*)mqtt, dataLength);
}

void sendMqttPingResp() {

}

void sendMqttDisconnect() {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    uint8_t i = 0;
    mqtt->flags = (MQTT_DISCONNECT << 4) | 0b0000;    // disconnect flag
    mqtt->data[i++] = 0;              // no length
    uint16_t dataLength = i + 1;
    socketSendTcp(client->socket, (uint8_t*)mqtt, dataLength);
    //sendTcpMessage(ether, client->socket, PSH | ACK, (uint8_t*)mqtt, dataLength);
}



/*
 * sendMqttMessage(MQTT_CONNECT, NULL, NULL, NULL);
 * sendMqttMessage(MQTT_CONNACK, NULL, NULL, NULL);
 * if NULL use ? to set defaults
 */
/*void sendMqttMessage(uint8_t msgType, mqttMsg m) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    uint16_t len = 4;
    mqtt->flags = (msg << 4) | 0b0010;
    switch (msg) {
    case MQTT_CONNECT:
        mqttConnect* msg = (mqttConnect*)(m.msgInfo);

        break;
    case MQTT_CONNACK:
        mqttConnect* msg = (mqttConnect*)(m.msgInfo);
        break;

    }
    socketSendTcp(client->socket, mqtt, len);
}*/

