# SPDX-License-Identifier: GPL-2.0
# Standalone build glue for the multi-channel SLCAN driver.

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

obj-m := slcanx.o
slcanx-y := slcan-core.o slcan-ethtool.o

.PHONY: all clean install modules modules_install

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install: modules_install

modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
