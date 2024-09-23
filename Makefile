# obj-m := tc358748.o

tc358748-objs := tc358748_main.o tc358748_i2c.o
obj-m += tc358748.o

KDIR = ~/linux-imx-v5.15.71_2.2.2-phy/
PWD = $(shell pwd)

all:
	make -C $(KDIR) SUBDIRS=$(PWD) M=$(PWD) modules

clean:
	make -C $(KDIR) SUBDIRS=$(PWD) M=$(PWD) clean
