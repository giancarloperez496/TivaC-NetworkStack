/******************************************************************************
 * File:        udp.h
 *
 * Author:      Giancarlo Perez
 *
 * Created:     12/7/24
 *
 * Description: -
 ******************************************************************************/

#ifndef UDP_H_
#define UDP_H_

//=============================================================================
// INCLUDES
//=============================================================================

#include "socket.h"
#include "ip.h"
#include <stdint.h>
#include <stdbool.h>

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

//=============================================================================
// TYPEDEFS AND GLOBALS
//=============================================================================

typedef struct _udpHeader {// 8 bytes
  uint16_t sourcePort;
  uint16_t destPort;
  uint16_t length;
  uint16_t check;
  uint8_t  data[0];
} udpHeader;

//=============================================================================
// FUNCTION PROTOTYPES
//=============================================================================

bool isUdp(etherHeader *ether);
inline udpHeader* getUdpHeader(etherHeader* ether);
inline uint8_t* getUdpData(etherHeader *ether);
void sendUdpMessage(etherHeader* ether, socket* s, uint8_t data[], uint16_t dataSize);

#endif

