#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
#include <time.h>
#include "e1000_poc.h"

#ifndef WITH_PATCH
#define WITH_PATCH 0
#endif

#define smp_mb() ({ asm volatile("mfence" ::: "memory"); (void)0; })

#define pci_dma_read(d, addr, buf, len) \
do { \
    smp_mb(); \
    memcpy((void*)(buf), (void*)(addr), (len)); \
} while (0)

#define pci_dma_write(d, addr, buf, len) \
do { \
    smp_mb(); \
    memcpy((void*)(addr), (void*)(buf), (len)); \
} while (0)

#define rx_desc_base(s) ((volatile void *)g_shm->rx_rings)

#define mac_reg_RDH (((volatile struct share_mem*)g_shm)->rdh)
#define mac_reg_RDT (((volatile struct share_mem*)g_shm)->rdt)
#define mac_reg_RDLEN (RX_DESC_NUM * sizeof(struct e1000_rx_desc))

static uint8_t s_vlan_status = 0;

static int e1000_has_rxbufs(size_t total_size)
{
    int bufs;
    int rxbuf_size = 1;
    /* Fast-path short packets */
    if (total_size <= rxbuf_size) {
        return mac_reg_RDH != mac_reg_RDT;
    }
    if (mac_reg_RDH < mac_reg_RDT) {
        bufs = mac_reg_RDT - mac_reg_RDH;
    } else if (mac_reg_RDH > mac_reg_RDT) {
        bufs = mac_reg_RDLEN /  sizeof(struct e1000_rx_desc) +
            mac_reg_RDT - mac_reg_RDH;
    } else {
        return 0;
    }
    return total_size <= bufs * rxbuf_size;
}

static ssize_t
e1000_receive_iov(size_t total_size)
{
    struct e1000_rx_desc desc;
    volatile void *base;
    uint32_t rdh_start;
    uint8_t vlan_status = 0;
    uint8_t vlan_special = 0;
    size_t desc_offset = 0;

    rdh_start = mac_reg_RDH;
    if (!e1000_has_rxbufs(total_size)) {
        return -1;
    }
    do {
        base = rx_desc_base(s) + sizeof(desc) * mac_reg_RDH;
        pci_dma_read(d, base, &desc, sizeof(desc));
        desc.special = vlan_special;
        s_vlan_status += 2;
        vlan_status = s_vlan_status;
#if WITH_PATCH
        desc.status &= (~E1000_RXD_STAT_DD);
#else
        desc.status |= (vlan_status | E1000_RXD_STAT_DD);
#endif
        desc_offset++;

        pci_dma_write(d, base, &desc, sizeof(desc));
#if WITH_PATCH
        desc.status |= (vlan_status | E1000_RXD_STAT_DD);
        pci_dma_write(d, base + offsetof(struct e1000_rx_desc, status),
                      &desc.status, sizeof(desc.status));
#endif

        if (++mac_reg_RDH * sizeof(desc) >= mac_reg_RDLEN)
            mac_reg_RDH = 0;
        /* see comment in start_xmit; same here */
        if (mac_reg_RDH == rdh_start ||
            rdh_start >= mac_reg_RDLEN / sizeof(desc)) {
            return -1;
        }
    } while (desc_offset < total_size);

    return total_size;
}

static void maybe_hang(void)
{
    printf("Qemu: Maybe hang RDH=%u RDT=%u\n",
           mac_reg_RDH, mac_reg_RDT);
    sleep(1);
}

void *qemu_worker(void *arg)
{
    uint32_t ok_count = 0;
    uint32_t nobuf_count = 0;
    time_t last = 0;
    time_t now;
    int interval = atoi(getenv("RX_INTERVAL") ? : "0");

    while (!g_exit) {
        if (e1000_receive_iov(1) < 0) {
            nobuf_count++;
            if (!(nobuf_count & 0xfffff)) {
                maybe_hang();
            }
            sched_yield();
            continue;
        }
        if (interval > 0)
            usleep(interval * 1000);
        nobuf_count = 0;
        ok_count++;
        if ((now = time(NULL)) != last) {
            last = now;
            printf("RX COUNT: %u\n", ok_count);
        }
    }

    return NULL;
}

