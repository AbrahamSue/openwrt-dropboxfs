ifeq ($(target), mips)
ext:=.$(target)
BASE_DIR=/opt/acp/dev/openwrt
CROSS_COMPILE:=$(BASE_DIR)/OpenWrt-Toolchain-ar71xx-for-mips_r2-gcc-4.6-linaro_uClibc-0.9.33.2/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/bin/mips-openwrt-linux-
STAGING_DIR:=$(BASE_DIR)/OpenWrt-SDK-ar71xx-for-linux-i486-gcc-4.6-linaro_uClibc-0.9.33.2/staging_dir/target-mips_r2_uClibc-0.9.33.2
endif

CC=${CROSS_COMPILE}gcc
STRIP=${CROSS_COMPILE}strip

EXEC:=dropbox-share-get$(ext)
SRCS:=dropbox-share-get.c

OBJS:=$(patsubst %.c,%.o, $(SRCS))

CFLAGS=-g -O2 -Wl,--no-as-needed -D_FILE_OFFSET_BITS=64 -I $(STAGING_DIR)/usr/include/ -L $(STAGING_DIR)/usr/lib/ -pthread -lfuse -lrt -ldl
.PHONY: all clean

all: $(EXEC) hellofs$(ext)
ifneq ($(CROSS_COMPILE),)
	tar -czf- $^ dropbox-share-get.c | ssh txafrodo.lsi.com "ssh 10.0.0.1 \"ssh 192.168.0.1 'tar -C /tmp/ -xzvf-'\"" 
endif

clean: 
	rm -f $(OBJS) $(EXEC) hellofs$(ext)

hellofs$(ext): hellofs.c
	$(CC) -o $@ $^ $(CFLAGS)
	$(STRIP) $@

$(EXEC): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)
	$(STRIP) $@

%.o: %.c
	$(CC) -c -o $@ $^ $(CFLAGS)

