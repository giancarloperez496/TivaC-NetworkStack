/******************************************************************************
 * File:        tcp.c
 *
 * Author:      Giancarlo Perez
 *
 * Created:     12/7/24
 *
 * Description: -
 ******************************************************************************/

#ifndef TCP_H_
#define TCP_H_

//=============================================================================
// INCLUDES
//=============================================================================

#include "ip.h"
#include "socket.h"
#include <stdint.h>
#include <stdbool.h>

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

/* TCP States */
#define TCP_CLOSED 0
#define TCP_LISTEN 1
#define TCP_SYN_RECEIVED 2
#define TCP_SYN_SENT 3
#define TCP_ESTABLISHED 4
#define TCP_FIN_WAIT_1 5
#define TCP_FIN_WAIT_2 6
#define TCP_CLOSING 7
#define TCP_CLOSE_WAIT 8
#define TCP_LAST_ACK 9
#define TCP_TIME_WAIT 10

/* TCP Offset/Flags */
#define FIN 0b00000001
#define SYN 0b00000010
#define RST 0b00000100
#define PSH 0b00001000
#define ACK 0b00010000
#define URG 0b00100000
#define ECE 0b01000000
#define CWR 0b10000000
#define NS  0x100
#define OFS_SHIFT 12

/* TCP Options */
#define TCP_OPTION_NO_OP 1
#define TCP_OPTION_MAX_SEGMENT_SIZE 2
#define TCP_OPTION_WINDOW_SCALE 3
#define TCP_OPTION_SACK_PERMITTED 4

/* Constants */
#define MAX_SEGMENT_SIZE 1460

/* Config */
#define WINDOW_SIZE 1284
#define MAX_TCP_PORTS 4
#define TCP_MAX_OPTION_LENGTH 50
#define TCP_MAX_SYN_ATTEMPTS 3
#define TCP_ARP_TIMEOUT 10
#define TCP_SYN_TIMEOUT 2
//=============================================================================
// TYPEDEFS AND GLOBALS
//=============================================================================

//uint8_t tcpState[MAX_TCP_PORTS];
//uint8_t tcpFsmFlags[MAX_SOCKETS];

typedef struct _tcpHeader {// 20 or more bytes
  uint16_t sourcePort;
  uint16_t destPort;
  uint32_t sequenceNumber;
  uint32_t acknowledgementNumber;
  uint16_t offsetFields;
  uint16_t windowSize;
  uint16_t checksum;
  uint16_t urgentPointer;
  uint8_t  data[0];
} tcpHeader;

//=============================================================================
// FUNCTION PROTOTYPES
//=============================================================================

inline void setTcpState(socket* s, uint8_t state);
inline uint8_t getTcpState(socket* s);
inline tcpHeader* getTcpHeader(etherHeader* ether);
inline uint8_t* getTcpData(etherHeader* ether);
inline uint16_t getTcpDataLength(etherHeader* ether);
bool isTcp(etherHeader *ether);
/*bool isTcpSyn(etherHeader *ether);
bool isTcpAck(etherHeader *ether);
bool isTcpFin(etherHeader* ether);
bool isTcpPsh(etherHeader* ether);
bool isTcpRst(etherHeader* ether);*/
bool isTcpPortOpen(etherHeader *ether);
void openTcpConnection(etherHeader* ether, socket* s);
void closeTcpConnection(etherHeader* ether, socket* s);
void sendTcpPendingMessages(etherHeader *ether);
void processTcpResponse(etherHeader* ether);
//void processTcpArpResponse(etherHeader *ether);
//uint8_t isTcpDataAvailable(etherHeader* ether);
//inline void pendTcpResponse(socket* s, uint8_t flags);
void sendTcpResponse(etherHeader *ether, socket* s, uint16_t flags);
void sendTcpMessage(etherHeader *ether, socket* s, uint16_t flags, uint8_t data[], uint16_t dataSize);

#endif

