set pagination off
display g_shm->rdh
display g_shm->rdt
watch g_shm->rdt
commands
c
end
watch g_shm->rx_rings[15].status
commands
info r
x/10i $rip-20
c
end
c
