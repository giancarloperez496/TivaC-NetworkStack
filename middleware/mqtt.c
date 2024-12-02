// MQTT Library (framework only)
// Giancarlo Perez

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: - EK-TM4C123GXL
// Target uC:       - TM4C123GH6PM
// System Clock:    - 40MHz

// Hardware configuration:
// -

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include "mqtt.h"
#include "tcp.h"
#include "timer.h"
#include "uart0.h"
#include "strlib.h"


// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

/*
typedef struct _mqttClient {
    socket* mqttSocket;
    uint8_t state;
    uint8_t tx_buf[MAX_MQTT_DATA_SIZE];
    uint16_t tx_size;
    uint8_t rx_buf[MAX_MQTT_DATA_SIZE];
    uint16_t rx_size;
    char mqttTopics[MAX_TOPICS][MAX_TOPIC_LENGTH];
    uint16_t topicCount;
    uint8_t flags;
} mqttClient;
/
 * flags
 * 1 - isTcpConnected   -MARK THIS WHILE TCP CONNECTION IS ESTABLISHED
 * 2 - isTxReady
 * 3 - isRxReady
 * 4 - isInitReady      -MARK THIS WHEN TCP CONNECTION NEEDS TO BE ESTABLISHED
 */

#define MAX_CONNECT_RETRIES 2

//we are assuming mqclient is a singleton and only one instance can run on this device. functions shall not take parameters as they will modify this singleton
mqttClient mqclient[1]; //# of MQTT instances, if i want to have more functions NEED to take a client param
uint8_t mqttBuffer[256];
char out[50];

//if i want to make mqttclient a singleton of some sort, i can define it in here and just use it all as a global instead of function parameters, similar to dhcp

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

/*mqttClient* getMqttClient() { //should not rly be a need for this function UNLESS Im running multiple MQTT instances
    return mqclient;
}*/

void setMqttState(uint8_t state) { //setMqttClientState()
    mqclient->state = state;
}

uint8_t getMqttState() {
    return mqclient->state;
}

uint8_t getMqttQos() {
    return QOS;
}

void initMqtt() {
    setMqttState(MQTT_CLIENT_STATE_DISCONNECTED);
}

uint8_t timeoutTimer;

/*void resetMqtt() {
    setMqttState(MQTT_CLIENT_STATE_DISCONNECTED);
    deleteSocket(mqclient.socket);
}*/

//main client loop
void runMqttClient() { //extern these maybe?
    socket* mqsocket = mqclient->socket;
    uint8_t mqttState = getMqttState();
    uint8_t tcpState = getTcpState(mqsocket);
    //handleTransition();
    switch (mqttState) {
    case MQTT_CLIENT_STATE_DISCONNECTED:
        break;
    case MQTT_CLIENT_STATE_TCP_CONNECTING:
        //lowkey need a better way to check if the socket is ready, either by callback or something?
        /*
         * onTcpEstablished(socket) {

        */
        if (tcpState == TCP_ESTABLISHED) {
            putsUart0("TCP Connection established\n");
            setMqttState(MQTT_CLIENT_STATE_TCP_CONNECTED);
        }
        break;
    case MQTT_CLIENT_STATE_TCP_CONNECTED:
        sendMqttMessage(MQTT_CONNECT);
        setMqttState(MQTT_CLIENT_STATE_MQTT_CONNECTING);
        if (MAX_CONNECT_RETRIES > 0) {
            //timeoutTimer = startOneshotTimer(mqttConnectTimeout, 10, NULL);
        }
        break;
    case MQTT_CLIENT_STATE_MQTT_CONNECTING:
        //handle CONNACK in callback or something
        //check if timeout? if timeout expired go back to TCP connected, basically what i said above, can either use startTimer or millis()
        if (tcpState == TCP_CLOSE_WAIT) {
            disconnectMqtt();
            //setMqttState(MQTT_CLIENT_STATE_DISCONNECTING);
            //socketCloseTcp(mqsocket);
        }
        break;
    case MQTT_CLIENT_STATE_MQTT_CONNECTED:
        if (tcpState == TCP_CLOSE_WAIT) {
            //setMqttState(MQTT_CLIENT_STATE_DISCONNECTING);
            //socketCloseTcp(mqsocket);
            disconnectMqtt();
        }
        break;
    case MQTT_CLIENT_STATE_DISCONNECTING:
        if (tcpState == TCP_CLOSED) {
            setMqttState(MQTT_CLIENT_STATE_DISCONNECTED);
            //deleteSocket(mqsocket);
            //how do we know that the socket finished closing?
            //if !s.valid?
            //checking state may be iffy
            //callback?
        }
        break;

    }
}

//Application level error handler, application can handle Layer 4 errors here
//Layer 4 calls this function
void socketErrorCallback(void* context) {
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
    //err->sk should ALWAYS be the same as mqclient->socket
}

//Application error handler, application will handle Layer 7 errors here
//Layer 7 calls this function
void mqttErrorCallback(void* context) {
    mqttError* err = (mqttError*)context;
    switch(err->errorCode) {
    //MQTT layer errors
    case MQTT_ERROR_CONNECT_TIMEOUT:
        mqttRetryConnection();
        break;
    }
    putsUart0(err->errorMsg);
}


void mqttConnectTimeout(void* context) {
    //if state == connecting else ignore??
    mqttError err;
    err.errorCode = MQTT_ERROR_CONNECT_TIMEOUT;
    //call connectMqtt() again as needed
    str_copy(err.errorMsg, "Timed out waiting for MQTT Connect from broker.\n");
    mqttErrorCallback(&err);
}

void mqttRetryConnection() {
    static uint8_t attempts = 0;
    uint8_t mqstate = getMqttState();
    if (mqstate == MQTT_CLIENT_STATE_MQTT_CONNECTING) {
        attempts++;
        if (attempts == MAX_CONNECT_RETRIES) {
            putsUart0("Reached max number of attempts to connect to broker; resetting connection...\n");
            attempts = 0;
            //socketCloseTcp(mqclient->socket);
            //setMqttState(MQTT_CLIENT_STATE_DISCONNECTED);
            disconnectMqtt();
        }
        else {
            //putsUart0("Retrying connection to MQTT Broker...\n");
            //timeoutTimer = startOneshotTimer(mqttConnectTimeout, 10, NULL);
        }
    }
    else {
        //timer should be stopped when this function is called and should not restart
    }
}

//connects to MQTT server stored in device on port 1883
//starts with TCP connection
//once TCP established, send MQTT connect and move to MQTT_CONNECTING
void connectMqtt() {
    uint8_t mqttState = getMqttState();
    uint8_t mqserv[4];
    switch (mqttState) {
    case MQTT_CLIENT_STATE_DISCONNECTED:
        mqclient->socket = newSocket();
        mqclient->socket->errorCallback = socketErrorCallback;
        getIpMqttBrokerAddress(mqserv);
        snprintf(out, 50, "Connecting to MQTT server %d.%d.%d.%d:%d\n", mqserv[0], mqserv[1], mqserv[2], mqserv[3], MQTT_PORT);
        putsUart0(out);
        socketConnectTcp(mqclient->socket, mqserv, MQTT_PORT);
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
        break;
    case MQTT_CLIENT_STATE_TCP_CONNECTING:
        //never got SYN ACK, should be handled by socketErrorCallback
        break;
    case MQTT_CLIENT_STATE_MQTT_CONNECTING:
        //never got CONNACK, client wants to close connection
        sendMqttMessage(MQTT_DISCONNECT);
        socketCloseTcp(mqclient->socket);
        break;
    case MQTT_CLIENT_STATE_MQTT_CONNECTED:
        //got CONNACK, client wants to close connection
        sendMqttMessage(MQTT_DISCONNECT);
        socketCloseTcp(mqclient->socket);
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
                mqclient->mqttTopics[i][j] = topics[i][j];
            }
            mqclient->mqttTopics[i][j] = '\0';
        }
    }
}

void getMqttTopics(char** input, uint32_t* count) {
    uint32_t i;
    for (i = 0; i < MAX_TOPICS; i++) {
        input[i] = mqclient->mqttTopics[i];
        if (mqclient->mqttTopics[i][0] == '\0') {
            *count = i;
            break;
        }
        *count = i + 1;
    }
}


bool isMqttResponse(etherHeader* ether) {
    bool ok;
    tcpHeader* tcp = getTcpHeader(ether);
    ok = (tcp->destPort == htons(MQTT_PORT));
    return ok;
}

mqttHeader* getMqttPacket(etherHeader* ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip + ip->size * 4);
    uint32_t pllength = ntohs(ip->length) - (ip->size * 4) - ((ntohs(tcp->offsetFields) >> 12) * 4);
    uint8_t* tcpData = tcp->data;
    mqttHeader* mqtt = (mqttHeader*)tcpData;
    return mqtt;
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

uint8_t encodeLength(uint8_t* out, uint32_t i) {
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
    sendMqttMessage();
    /*etherHeader* ether = (etherHeader*)etherbuffer;
    socket* s = getMqttSocket();
    if (s) {
        uint32_t i = 0;
        mqttHeader* mqtt = (mqttHeader*)mqttbuffer;
        mqtt->flags = (MQTT_PUBLISH << 4) | 0b0010;
        mqtt->data[i++] = 0;
        if (QOS > 0) {
            uint16_t packetId = 10;
            mqtt->data[i++] = packetId >> 8;
            mqtt->data[i++] = packetId & 0xFF;
        }
        size_t topicLen = str_length((char*)strTopic);
        mqtt->data[i++] = topicLen >> 8;
        mqtt->data[i++] = topicLen & 0xFF;
        uint32_t o;
        for (o = 0; o < topicLen; o++) {
            mqtt->data[i++] = strTopic[o];
        }
        mqtt->data[i++] = 14 >> 8;
        mqtt->data[i++] = 14 & 0xFF;
        size_t msgLen = getStrLength((char*)strData);
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
        uint16_t dataLength;
        uint8_t state = getMqttState();
        switch (state) {
        case MQTT_STATE_CONNECTED:
            dataLength = i+1;
            sendTcpMessage(ether, s, PSH | ACK, (uint8_t*)mqtt, dataLength);
            break;
        default:
            break;
        }
    }
    else {
        return;
    }*/
}

void processMqttPublish(mqttHeader* mqtt, uint8_t* topicIndex, char* command) {
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
            *topicIndex = 5;
    }
    else if( topicLen == strlen( mqttTopics[1] ) )
    {
        if( strcmp( command, mqttTopics[1] ) )          // "uta/ir/data"
            *topicIndex = 1;
    }
    else if( topicLen == strlen( mqttTopics[2] ) )
    {
        if( strcmp( command, mqttTopics[2] ) )          // "uta/ir/vol_up"
            *topicIndex = 2;
        else if( strcmp( command, mqttTopics[6] ) )     // "uta/ir/source"
            *topicIndex = 6;
    }
    else if( topicLen == strlen( mqttTopics[3] ) )
    {
        if( strcmp( command, mqttTopics[3] ) )          // "uta/ir/vol_down"
            *topicIndex = 3;
    }
    else if( topicLen == strlen( mqttTopics[4] ) )
    {
        if( strcmp( command, mqttTopics[4] ) )          // "uta/ir/power_toggle"
            *topicIndex = 4;
    }
    else
        *topicIndex = mqttConnection.topicCount;*/
}

void processMqttData(etherHeader* ether) {
    mqttHeader* mqtt = getMqttPacket(ether);
    uint8_t responseType = mqtt->flags >> 4;
    /*uint16_t mqttState = getMqttState();
    uint8_t qos = getMqttQos();
    if (mqttState == MQTT_STATE_CONNECTING) {
        switch (responseType) {
        case MQTT_CONNACK:
            mqttState = MQTT_STATE_CONNECTED;
            break;
        }
    }
    else if ( mqttState == MQTT_STATE_CONNECTED )
    {
        uint8_t topicIndex;
        char command[15];

        switch ( responseType ) {
            case MQTT_PUBLISH:
                processMqttPublish( mqtt, &topicIndex, command );
                switch(topicIndex) {
                    case 0: //topic 0
                        //topic0callback();
                        break;
                    case 1:                     // "uta/ir/data"
                        break;
                    case 2:                     // "uta/ir/vol_up"
                        break;
                    case 3:                     // "uta/ir/vol_down"
                        break;
                    case 4:                     // "uta/ir/power_toggle"
                        break;
                    case 5:                     // "uta/ir/channel"
                        break;
                    case 6:                     // "uta/ir/source"
                        break;
                    default:
                        break;
                }
                break;
            case MQTT_PUBACK:
                if (qos == 1) {

                }
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
    //if etherHeader == MQTT_CONNECT*/
}


void subscribeMqtt(char strTopic[]) {
    /*etherHeader* ether = (etherHeader*)etherbuffer;
    socket* s = getMqttSocket();
    if (s) {
        uint32_t i = 0;
        mqttHeader* mqtt = (mqttHeader*)mqttbuffer;
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

void unsubscribeMqtt(char strTopic[]) {
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

void sendMqttMessage(uint8_t msg) {
    mqttHeader* mqtt = (mqttHeader*)mqttBuffer;
    uint16_t len = 4;
    mqtt->flags = (msg << 4) | 0b0010;
    /*switch (msg) {
    case MQTT_CONNECT:

        break;
    case MQTT_CONNACK:

        break;

    }*/
    socketSendTcp(mqclient->socket, mqtt, len);
}
