POC test for Qemu e1000 rx with guest dpdk
==========================================

#### normal test

```
make
make run
```

- if your glibc <= 2.19, rx running as expected, without log output

- if your glibc >= 2.21, dpdk rx will catch unexpected buffer_addr,
and Qemu e1000 rx maybe hang duo to full rx_ring (RDH==RDT)

I print the log to verify the issue:

```
DPDK: UNEXPECTED ADDR: last x current x
```

and later

```
Qemu: Maybe hang RDH=x RDT=x
```

#### memcpy test

if you use modern glibc with feature `GLIBC_TUNABLES`, you can set
env `GLIBC_TUNABLES` to fallback slow memcpy

```
make
make run SLOW_MEMCPY=1
```

rx runing without issue, like using older version glibc

#### patch test

Qemu writing rx desc is not quite correct, we can test set DD status
in a separate operation by `WITH_PATCH=1`

```
make WITH_PATCH=1
make run
```

this patch let rx running as expected with whatever new or old glibc memcpy

#### tips

- use gdb hardware breakpoint can easier to reproduce the issue

```
gdb -p `pidof e1000_poc` -x gdb.cmd
```

- use `-ffreestanding` when compiling `qemu_worker.o` to make sure call `memcpy` from glibc

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

#### reference

- https://lore.kernel.org/qemu-devel/20200102110504.GG121208@stefanha-x1.localdomain/T/#m25ddd33f3ce777521fb42e42c975eb309e1bf349
- https://github.com/BASM/qemu_dpdk_e1000_test

