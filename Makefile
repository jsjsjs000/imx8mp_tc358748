# obj-m := tc358748.o

# tc358748-objs := tc358748_main.o tc358748_i2c.o
tc358748-objs := tc358748-358743-.o tc358748_i2c.o
obj-m += tc358748.o

KDIR = ~/linux-imx-v5.15.71_2.2.2-phy/
PWD = $(shell pwd)
BUILD = $(shell pwd)/build/

all:
	make -j $(nproc) -C $(KDIR) SUBDIRS=$(PWD) src=$(PWD) M=$(BUILD) modules

clean:
	make -C $(KDIR) SUBDIRS=$(PWD) src=$(PWD) M=$(BUILD) clean
