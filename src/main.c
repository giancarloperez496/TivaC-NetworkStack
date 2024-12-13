/******************************************************************************
 * File:        main.c
 *
 * Author:      Giancarlo Perez
 *
 * Created:     12/7/24
 *
 * Description: Network Stack Main File
 ******************************************************************************/

// Target Platform: EK-TM4C123GXL w/ ENC28J60
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// ENC28J60 Ethernet controller on SPI0
//   MOSI (SSI0Tx) on PA5
//   MISO (SSI0Rx) on PA4
//   SCLK (SSI0Clk) on PA2
//   ~CS (SW controlled) on PA3
//   WOL on PB3
//   RESET on PB2
//   INT on PC6


//=============================================================================
// INCLUDES
//=============================================================================

/* Standard Libraries */
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
/* Drivers */
#include "tm4c123gh6pm.h"
#include "clock.h"
#include "gpio.h"
#include "uart0.h"
/* Libraries */
#include "wait.h"
#include "mqtt_client.h"
#include "strlib.h"
/* Applications */
#include "network_stack.h"
#include "shell.h"
#include "sensors.h"

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

// Initialize Hardware
void initHw() {
    // Initialize system clock to 40 MHz
    initSystemClockTo40Mhz();
    initSysTimer1ms();
    // Enable clocks
    enablePort(PORTF);
    _delay_cycles(3);
    // Configure LED and pushbutton pins
    selectPinPushPullOutput(RED_LED);
    selectPinPushPullOutput(GREEN_LED);
    selectPinPushPullOutput(BLUE_LED);
    selectPinDigitalInput(PUSH_BUTTON);
    enablePinPullup(PUSH_BUTTON);
}

//Application Logic

void handlePublish(const mqttData* data) {
    putsUart0("Application: Handling Publish\n");
    if (str_equal(data->topic, "TEST")) {
        //handle data
    }
    else if (str_equal(data->topic, "TOPIC")) {
        //handle data
    }
}

//=============================================================================
// MAIN FUNCTION
//=============================================================================

//topics and behavior will be defind and set in here
int main(void) {
    // Init controller
    initHw();
    // Setup UART0
    initUart0();
    setUart0BaudRate(115200, 40e6);

    // Init timer
    initTimer();

    // Init sockets
    initSockets();

    // Init ethernet interface (eth0)
    putsUart0("\nStarting eth0\n");
    initEther(ETHER_UNICAST | ETHER_BROADCAST | ETHER_HALFDUPLEX);
    setEtherMacAddress(0x02, 0x03, 0x04, 0x05, 0x06, 0xF6);

    // Init EEPROM
    initEeprom();
    readConfiguration();

    //Init MQTT Client
    initMqttClient();
    setHandlePublishCallback(handlePublish);


    while (true) {
        processShell();
        runNetworkStack();
        runMqttClient();
    }
}

