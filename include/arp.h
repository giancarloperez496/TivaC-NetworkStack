// ARP Library
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

#ifndef ARP_H_
#define ARP_H_

#include "eth0.h"
#include "ip.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct _arpPacket { // 28 bytes
  uint16_t hardwareType;
  uint16_t protocolType;
  uint8_t hardwareSize;
  uint8_t protocolSize;
  uint16_t op;
  uint8_t sourceAddress[6];
  uint8_t sourceIp[4];
  uint8_t destAddress[6];
  uint8_t destIp[4];
} arpPacket;

typedef struct {
    uint8_t ipAddress[4];  // Key: IP address
    uint8_t macAddress[6]; // Value: MAC address
    uint8_t valid;
} arp_entry_t;

#define MAX_ARP_ENTRIES 20

extern arp_entry_t arpTable[MAX_ARP_ENTRIES];
//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void addArpEntry(uint8_t ipAddress[], uint8_t macAddress[]);
uint8_t lookupArpEntry(uint8_t ipAddress[], uint8_t macAddressToWrite[]);
bool isArpRequest(etherHeader* ether);
bool isArpResponse(etherHeader *ether);
arpPacket* getArpPacket(etherHeader* ether);
void sendArpResponse(etherHeader *ether);
void sendArpRequest(etherHeader *ether, uint8_t ipFrom[], uint8_t ipTo[]);

#endif

