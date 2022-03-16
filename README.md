# udev-modem-by-imei
This piece of software is created to be run from ```udev``` daemon scripts and it tries to detect GSM modems, ask the IMEI and then is creating coresponding symlinks in /dev/ by IMEI.

Why? There are two reasons:

#### 1. Beause if you have a SMS system based on multiple GSM modems and these modems are from the same manufacture you will have something like this:

```
# lsusb
Bus 001 Device 004: ID 12d1:1001 Huawei Technologies Co., Ltd. E161/E169/E620/E800 HSDPA Modem
Bus 001 Device 005: ID 12d1:1001 Huawei Technologies Co., Ltd. E161/E169/E620/E800 HSDPA Modem
Bus 001 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub
Bus 002 Device 003: ID 0e0f:0002 VMware, Inc. Virtual USB Hub
Bus 002 Device 002: ID 0e0f:0003 VMware, Inc. Virtual Mouse
Bus 002 Device 001: ID 1d6b:0001 Linux Foundation 1.1 root hub
```

Then the udev rules will try to create only one set of symlinks in ```/dev/serial/by-id/```:

```
# ls -l /dev/serial/by-id/
total 0
lrwxrwxrwx 1 root root 13 Jan 29 14:03 usb-HUAWEI_Technology_HUAWEI_Mobile-if00-port0 -> ../../ttyUSB0
lrwxrwxrwx 1 root root 13 Jan 29 14:03 usb-HUAWEI_Technology_HUAWEI_Mobile-if01-port0 -> ../../ttyUSB1
lrwxrwxrwx 1 root root 13 Jan 29 14:03 usb-HUAWEI_Technology_HUAWEI_Mobile-if02-port0 -> ../../ttyUSB2
```
Even you have 2 modems Linux will create symlinks only for the first modem detected in the system. Unfortunatelly the serial number of the USB device also is not a work around. I couldn't find a solution to create symlinks by SN.

But let's say you are using serial port by path:
```
# ls -l /dev/serial/by-path/
total 0
lrwxrwxrwx 1 root root 13 Jan 29 14:03 pci-0000:02:01.0-usb-0:1:1.0-port0 -> ../../ttyUSB0
lrwxrwxrwx 1 root root 13 Jan 29 14:03 pci-0000:02:01.0-usb-0:1:1.1-port0 -> ../../ttyUSB1
lrwxrwxrwx 1 root root 13 Jan 29 14:03 pci-0000:02:01.0-usb-0:1:1.2-port0 -> ../../ttyUSB2
lrwxrwxrwx 1 root root 13 Jan 29 13:55 pci-0000:02:01.0-usb-0:2:1.0-port0 -> ../../ttyUSB3
lrwxrwxrwx 1 root root 13 Jan 29 13:55 pci-0000:02:01.0-usb-0:2:1.1-port0 -> ../../ttyUSB4
lrwxrwxrwx 1 root root 13 Jan 29 13:55 pci-0000:02:01.0-usb-0:2:1.2-port0 -> ../../ttyUSB5
```

That's good. But you will hit the second issue.

#### 2. You don't know for sure which modem is first after reboot!
Because I have 2 different modems for SMS and one is from one operator and the other one is from another operator, I found out after a reboot that the modem paths where reversed!
So instead havem modem-01 with operator-01 and modem-02 with operator-02, I ended in this situation: modem-01 with operator-02 and modem-02 with operator-01.
In this case my kannel configuration was based on /dev/serial/by-path/ for each modems, but the config can not figured out which modem has which operator! Is just a configuration for operator-01 for fist modem.
This can be an ackward situation because each time you need to check which modem is first and which modem is second. In my case I have only 2 modems, but expand this issue to 10 modems!

## What this software do?
That's I ended to create this piece of software to be run from ```udev``` daemon when it detects any kind of device that is adding a ```tty``` subsystem will access the tty device, sends some ```AT``` commands and it gets the IMEI of the modem from an ```AT``` command.
Then it will create symlinks into ```/dev/serial/by-imei/usb-XXXXXXX-00``` and ```/dev/serial/by-imei/usb-XXXXXXX-02```.

Example:

```
# ls -l /dev/serial/by-imei/
total 0
lrwxrwxrwx 1 root root 12 Jan 29 13:55 usb-354XXXXXXXXXXXX-00 -> /dev/ttyUSB3
lrwxrwxrwx 1 root root 12 Jan 29 13:55 usb-354XXXXXXXXXXXX-02 -> /dev/ttyUSB5
lrwxrwxrwx 1 root root 12 Jan 29 14:03 usb-358YYYYYYYYYYYY-00 -> /dev/ttyUSB0
lrwxrwxrwx 1 root root 12 Jan 29 14:03 usb-358YYYYYYYYYYYY-02 -> /dev/ttyUSB2
```

Well, in this moment I sure that I will not confuse anymore the modems! I will set kannel to use ```/dev/serial/by-imei/usb-354XXXXXXXXXXXX-00``` for operator-01 and ```/dev/serial/by-imei/usb-358YYYYYYYYYYYY-00``` for opreator-02.
When I will add more modems it will be very easy to know which one is the next one and so on.
