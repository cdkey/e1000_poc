#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>
#include <sched.h>
#include "e1000_poc.h"

/**
 * Compiler barrier.
 *
 * Guarantees that operation reordering does not occur at compile time
 * for operations directly before and after the barrier.
 */
#define rte_compiler_barrier() do {             \
        asm volatile ("" : : : "memory");       \
} while(0)
#define E1000_PCI_REG_WRITE(reg, value) \
do { \
    rte_compiler_barrier(); \
    *(volatile uint32_t*)(reg) = (value); \
} while (0)

static inline void rte_prefetch0(const volatile void *p)
{
        asm volatile ("prefetcht0 %[p]" : : [p] "m" (*(const volatile char *)p));
}
#define rte_em_prefetch(p)      rte_prefetch0(p)

uint64_t s_dma_addr = 0;
uint64_t s_last_recv_addr = (uint64_t)-1;
uint16_t s_rx_tail = 0;
uint16_t s_nb_rx_hold = 0;
uint32_t g_nb_rx_desc = RX_DESC_NUM;

uint16_t
eth_em_recv_pkts(uint16_t nb_pkts)
{
        volatile struct e1000_rx_desc *rx_ring;
        volatile struct e1000_rx_desc *rxdp;
        struct e1000_rx_desc rxd;
        uint16_t rx_id;
        uint16_t nb_rx;
        uint16_t nb_hold;
        uint8_t status;

        nb_rx = 0;
        nb_hold = 0;
        rx_id = s_rx_tail;
        rx_ring = g_shm->rx_rings;
        while (nb_rx < nb_pkts) {
                /*
                 * The order of operations here is important as the DD status
                 * bit must not be read after any other descriptor fields.
                 * rx_ring and rxdp are pointing to volatile data so the order
                 * of accesses cannot be reordered by the compiler. If they were
                 * not volatile, they could be reordered which could lead to
                 * using invalid descriptor fields when read from rxd.
                 */
                rxdp = &rx_ring[rx_id];
                status = rxdp->status;
                if (! (status & E1000_RXD_STAT_DD))
                        break;
                rxd = *rxdp;

                nb_hold++;
                rx_id++;
                if (rx_id == g_nb_rx_desc)
                        rx_id = 0;

                /*
                 * When next RX descriptor is on a cache-line boundary,
                 * prefetch the next 4 RX descriptors and the next 8 pointers
                 * to mbufs.
                 */
                if ((rx_id & 0x3) == 0) {
                        rte_em_prefetch(&rx_ring[rx_id]);
                }

                if (rxd.buffer_addr - s_last_recv_addr != 1) {
                        printf("DPDK: UNEXPECTED ADDR: last %"PRIx64" current %"PRIx64"\n",
                               s_last_recv_addr, rxd.buffer_addr);
                }
                s_last_recv_addr = rxd.buffer_addr;

                rxdp->buffer_addr = s_dma_addr++;
                rxdp->status = 0;

                nb_rx++;
        }
        s_rx_tail = rx_id;

        /*
         * If the number of free RX descriptors is greater than the RX free
         * threshold of the queue, advance the Receive Descriptor Tail (RDT)
         * register.
         * Update the RDT with the value of the last processed RX descriptor
         * minus 1, to guarantee that the RDT register is never equal to the
         * RDH register, which creates a "full" ring situtation from the
         * hardware point of view...
         */
        nb_hold = (uint16_t) (nb_hold + s_nb_rx_hold);
        if (nb_hold > 0) {
                rx_id = (uint16_t) ((rx_id == 0) ?
                        (g_nb_rx_desc - 1) : (rx_id - 1));
                E1000_PCI_REG_WRITE(&g_shm->rdt, rx_id);
                nb_hold = 0;
        }
        s_nb_rx_hold = nb_hold;
        return nb_rx;
}

void dpdk_init(void)
{
    volatile struct e1000_rx_desc *rx_ring = g_shm->rx_rings;
    uint32_t rx_id;

    for (rx_id = 0; rx_id < g_nb_rx_desc; rx_id++) {
        rx_ring[rx_id].buffer_addr = s_dma_addr++;
        rx_ring[rx_id].status = 0;
    }
    rx_id = g_nb_rx_desc - 1;
    E1000_PCI_REG_WRITE(&g_shm->rdt, rx_id);
}

void *dpdk_worker(void *arg)
{
    dpdk_init();
    while (!g_exit) {
        if (eth_em_recv_pkts(4) == 0) {
            sched_yield();
        }
    }
    return NULL;
}

