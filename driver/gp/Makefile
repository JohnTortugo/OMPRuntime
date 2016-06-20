
ARCH=arm
COMPILER=arm-linux-gnueabihf-
obj-m := nexusIO.o
# KDIR  := /home/dallou/linux-xlnx/
KDIR  := /home/dallou/linux-digilent
PWD := $(shell pwd)
default:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(COMPILER) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) clean