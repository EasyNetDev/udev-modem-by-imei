PWD	:= $(shell pwd)

all:
	gcc -Wall -g -o udev-modem-imei-detection udev-modem-imei-detection.c

clean:
	rm $(PWD)/udev-modem-imei-detection

help:
	$(MAKE) -C $(KDIR) M=$(PWD) help

install:
	install $(PWD)/85-usb_modem-by-imei.rules /etc/udev/rules.d/85-usb_modem-by-imei.rules
	install $(PWD)/udev-modem-imei-detection  /usr/local/bin/udev-modem-imei-detection

uninstall:
	rm /etc/udev/rules.d/85-usb_modem-by-imei.rules
	rm /usr/local/bin/udev-modem-imei-detection

.PHONY : all clean install uninstall
