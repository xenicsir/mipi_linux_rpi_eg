ilumos-objs := ilumos_kmod.o

obj-m := ilumos.o

BUILD ?= normal
EXTRA_FLAGS :=
ifeq ($(BUILD),debug)
    EXTRA_FLAGS := -DDEBUG -g -O0
endif

KVERSION = $(shell uname -r)

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build \
	    M=$(PWD) EXTRA_CFLAGS="$(EXTRA_FLAGS)" modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
