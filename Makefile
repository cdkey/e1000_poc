ifeq ($(SLOW_MEMCPY),1)
	MEMCPY_HOOK:=GLIBC_TUNABLES=glibc.cpu.hwcaps=-AVX_Usable,-AVX2_Usable,-AVX_Fast_Unaligned_Load,-ERMS_Usable,-Fast_Unaligned_Copy
else
	MEMCPY_HOOK:=
endif

ifeq ($(WITH_PATCH),)
	WITH_PATCH:=0
endif

CFLAGS=-g -DWITH_PATCH=$(WITH_PATCH)
LDFLAGS=-lrt -lpthread

ifneq ($(RX_DESC_NUM),)
CFLAGS+=-DRX_DESC_NUM=$(RX_DESC_NUM)
endif

all: clean e1000_poc

run: e1000_poc
	$(MEMCPY_HOOK) ./e1000_poc

e1000_poc: e1000_poc.o qemu_worker.o dpdk_worker.o
	gcc -o $@ $^ $(LDFLAGS)

e1000_poc.o: e1000_poc.c e1000_poc.h
	gcc -o $@ $(CFLAGS) -c $<

qemu_worker.o: qemu_worker.c e1000_poc.h
	gcc -o $@ $(CFLAGS) -ffreestanding -c $<

dpdk_worker.o: dpdk_worker.c e1000_poc.h
	gcc -o $@ $(CFLAGS) -c $<

.PHONY: clean
clean:
	rm -f *.o e1000_poc
