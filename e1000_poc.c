#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include "e1000_poc.h"

int g_exit;
struct share_mem *g_shm;

extern void *qemu_worker(void *);
extern void *dpdk_worker(void *);

void handle_signal(int sig)
{
    g_exit = 1;
}

int main(int argc, char *argv[])
{
    pthread_t qemu, dpdk;

    int fd = shm_open("e1000_rx_ring", O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        perror("shm_open failed");
        exit(1);
    }
    ftruncate(fd, sizeof(struct share_mem));
    shm_unlink("e1000_rx_ring");

    g_shm = mmap(NULL, sizeof(struct share_mem), PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, 0);

    if (g_shm == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }
    memset(g_shm, 0, sizeof(*g_shm));

    signal(SIGINT, handle_signal);

    if (pthread_create(&qemu, NULL, qemu_worker, NULL) < 0) {
        perror("pthread_create qemu worker");
        exit(1);
    }

    if (pthread_create(&dpdk, NULL, dpdk_worker, NULL) < 0) {
        perror("pthread_create dpdk worker");
        exit(1);
    }

    while (!g_exit)
        sleep(1);

    pthread_join(qemu, NULL);
    pthread_join(dpdk, NULL);

    munmap(g_shm, sizeof(struct share_mem));
    close(fd);

    return 0;
}

