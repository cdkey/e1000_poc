#ifndef _E1000_POC_H
#define _E1000_POC_H

#include <inttypes.h>

#ifndef RX_DESC_NUM
#define RX_DESC_NUM 32
#endif
#define E1000_RXD_STAT_DD 0x1

/* Legacy Receive Descriptor */
struct e1000_rx_desc {
    uint64_t buffer_addr; /* Address of the descriptor's data buffer */
    uint16_t length;     /* Length of data DMAed into data buffer */
    uint16_t csum;       /* Packet checksum */
    uint8_t status;      /* Descriptor status */
    uint8_t errors;      /* Descriptor Errors */
    uint16_t special;
};

struct share_mem
{
    struct e1000_rx_desc rx_rings[RX_DESC_NUM];
    uint32_t rdh;
    uint32_t rdt;
};

extern int g_exit;
extern struct share_mem *g_shm;

#endif
