set pagination off
display g_shm->rdh
display g_shm->rdt
display/x s_vlan_status
watch g_shm->rdt
commands
c
end
p &g_shm->rx_rings[4].status
watch *(unsigned char*)$1
commands
info r
x/10i $rip-20
c
end
b maybe_hang
commands
finish
end
c
