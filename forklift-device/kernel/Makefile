obj-m += vl53l1x_driver.o

KDIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	sudo insmod vl53l1x_driver.ko

uninstall:
	sudo rmmod vl53l1x_driver
