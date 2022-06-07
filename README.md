POC a issue about Qemu e1000 rx with guest dpdk
===============================================

## Background

We met a issue as same as https://bugs.launchpad.net/qemu/+bug/1853123

After digging about the root cause, I create the project to reproduce
the issue, verify patch effect, and help people easy to understand the
cause of the issue.

## Reproduce Test

```
make
make run
```

- If your glibc <= 2.19, rx running as expected, without error log

- If your glibc >= 2.21, dpdk rx will catch unexpected buffer_addr,
and Qemu e1000 rx maybe hang duo to full rx_ring (RDH == RDT)

I print the log to dentify if the issue has occurred:

```
DPDK: UNEXPECTED ADDR: last x current x
```

and

```
Qemu: Maybe hang RDH=x RDT=x
```

## Memcpy Test

If you use modern glibc with feature `GLIBC_TUNABLES`, you can set
env `GLIBC_TUNABLES` to fallback slow memcpy

```
make
make run SLOW_MEMCPY=1
```

rx runing without issue, like using older version glibc

In my environment, the `avx` implementation of memcpy can cause issue,
but not `ssse3`

## Patch Test

Qemu writing rx desc is not quite correct, we can test set DD status
in a separate operation by `WITH_QEMU_PATCH=1`

```
make WITH_QEMU_PATCH=1
make run
```

this patch let rx running as expected with whatever new or old glibc memcpy

Modify dpdk rx code also can prevent concurrent writing status, the RDH
desc is always hold by hardware, can be treat as not ready, try `WITH_DPDK_PATCH=1`

```
make WITH_DPDK_PATCH=1
make run
```

## Why RDH == RDT ?

`eth_em_recv_pkts` get old value of desc written by `e1000_receive_iov` at previous round,
and consume it again, so RDT inscreased by unexpected.

try details:

```
make RX_DESC_NUM=8
make run RX_INTERVAL=1000 DPDK_VLOG=1

gdb -p `pidof e1000_poc` -x gdb-detail.cmd
```

```
[1654323364.362] DPDK: GET rx_id=1
RX COUNT: 10
[1654323365.363] DPDK: GET rx_id=2
RX COUNT: 11
[1654323366.363] DPDK: GET rx_id=3
[1654323366.363] DPDK: GET rx_id=4
DPDK: UNEXPECTED ADDR: last b current 4
RX COUNT: 12
Qemu: Maybe hang RDH=4 RDT=4
```

The gdb watchpoint information shows that, `rxdp->status = 0` in `eth_em_recv_pkts`
actually written to memory is **1**, may be affected by cache of
`pci_dma_write(d, base, &desc, sizeof(desc))` in `e1000_receive_iov`

```
Thread 3 "e1000_poc" hit Hardware watchpoint 2: *(unsigned char*)$1

Old value = 0 '\000'
New value = 1 '\001'
eth_em_recv_pkts (nb_pkts=4) at dpdk_worker.c:98
98                      nb_rx++;
1: g_shm->rdh = 4
2: g_shm->rdt = 3
rax            0x7f339ce17040      139859652079680
rbx            0x0                 0
rcx            0x0                 0
rdx            0x7f339ce17040      139859652079680
rsi            0x0                 0
rdi            0x7f339c3bb910      139859641219344
rbp            0x7f339c3bbed0      0x7f339c3bbed0
rsp            0x7f339c3bbe70      0x7f339c3bbe70
r8             0x0                 0
r9             0x23                35
r10            0x55b2a5f570c7      94225776865479
r11            0x0                 0
r12            0x7ffcb59310de      140723354800350
r13            0x7ffcb59310df      140723354800351
r14            0x7ffcb59310e0      140723354800352
r15            0x7f339c3bbfc0      139859641221056
rip            0x55b2a5f56aa7      0x55b2a5f56aa7 <eth_em_recv_pkts+407>
eflags         0x246               [ PF ZF IF ]
cs             0x33                51
ss             0x2b                43
ds             0x0                 0
es             0x0                 0
fs             0x0                 0
gs             0x0                 0
   0x55b2a5f56a93 <eth_em_recv_pkts+387>:       adc    $0x2590,%eax
   0x55b2a5f56a98 <eth_em_recv_pkts+392>:       mov    -0x38(%rbp),%rdx
   0x55b2a5f56a9c <eth_em_recv_pkts+396>:       mov    %rax,(%rdx)
   0x55b2a5f56a9f <eth_em_recv_pkts+399>:       mov    -0x38(%rbp),%rax
   0x55b2a5f56aa3 <eth_em_recv_pkts+403>:       movb   $0x0,0xc(%rax)
=> 0x55b2a5f56aa7 <eth_em_recv_pkts+407>:       movzwl -0x44(%rbp),%eax
   0x55b2a5f56aab <eth_em_recv_pkts+411>:       add    $0x1,%eax
   0x55b2a5f56aae <eth_em_recv_pkts+414>:       mov    %ax,-0x44(%rbp)
   0x55b2a5f56ab2 <eth_em_recv_pkts+418>:       movzwl -0x44(%rbp),%eax
   0x55b2a5f56ab6 <eth_em_recv_pkts+422>:       cmp    -0x54(%rbp),%ax
```

Or another situation: missing write status `0` after handle a rx packet

When Qemu using XMM related instructions writing 16 bytes e1000_rx_desc,
concurrent with DPDK using movb writing 1 byte status, the final result of
writing to memory will be one of them, if it made by Qemu which DD flag is on,
DPDK will consume it again, that cause RDH == RDT.

## Tips

- Use gdb hw watchpoint can easier to reproduce the issue

```
gdb -p `pidof e1000_poc` -x gdb.cmd
```

- Use `-ffreestanding` when compiling `qemu_worker.o` to make sure really call
  `memcpy` of glibc

  ```asm
        pci_dma_write(d, base, &desc, sizeof(desc));
    1714:       0f ae f0                mfence
    1717:       48 8d 4d d0             lea    -0x30(%rbp),%rcx
    171b:       48 8b 45 e8             mov    -0x18(%rbp),%rax
    171f:       ba 10 00 00 00          mov    $0x10,%edx
    1724:       48 89 ce                mov    %rcx,%rsi
    1727:       48 89 c7                mov    %rax,%rdi
    172a:       e8 01 fb ff ff          callq  1230 <memcpy@plt>
  ```

  rather than:

  ```asm
        pci_dma_write(d, base, &desc, sizeof(desc));
    16fe:       0f ae f0                mfence
    1701:       48 8b 45 e0             mov    -0x20(%rbp),%rax
    1705:       48 8b 55 e8             mov    -0x18(%rbp),%rdx
    1709:       48 8b 4d d8             mov    -0x28(%rbp),%rcx
    170d:       48 89 01                mov    %rax,(%rcx)
    1710:       48 89 51 08             mov    %rdx,0x8(%rcx)
  ```

## Links

- https://lore.kernel.org/qemu-devel/20200102110504.GG121208@stefanha-x1.localdomain/T/#m25ddd33f3ce777521fb42e42c975eb309e1bf349
- https://github.com/BASM/qemu_dpdk_e1000_test

