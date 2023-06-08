#!/bin/bash

export CC=/usr/bin/x86_64-linux-gnu-gcc-12

make
sudo rmmod hid_magicmouse
sudo rmmod hid_magicmouse2
sudo insmod ./hid-magicmouse2.ko