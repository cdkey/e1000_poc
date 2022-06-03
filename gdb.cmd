set pagination off
display g_shm->rdh
display g_shm->rdt
watch *(int*)(g_shm->rx_rings+4) if 1 == 0
c
