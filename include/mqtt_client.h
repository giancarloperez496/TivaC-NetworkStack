/******************************************************************************
 * File:        mqtt.h
 *
 * Author:      Giancarlo Perez
 *
 * Created:     12/2/24
 *
 * Description: -
 ******************************************************************************/

#ifndef MQTT_CLIENT_H_
#define MQTT_CLIENT_H_

//=============================================================================
// INCLUDES
//=============================================================================

#include "mqtt.h"

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

//=============================================================================
// TYPEDEFS AND GLOBALS
//=============================================================================

//=============================================================================
// FUNCTION PROTOTYPES
//=============================================================================

void runMqttClient();
inline void setMqttState(uint8_t state);
inline uint8_t getMqttState();
inline uint8_t getMqttResponse(mqttHeader* mqtt);
inline void setHandlePublishCallback(mqtt_callback_t cb);
void setMqttTopics(char** topics, uint32_t count);
void getMqttTopics(char** input, uint32_t* count);
void initMqttClient();
void connectMqtt();
void disconnectMqtt();
void publishMqtt(char strTopic[], char strData[]);
void subscribeMqtt(char strTopic[]);
void unsubscribeMqtt(char strTopic[]);
void processMqttData(etherHeader* ether);

#endif
