#!/bin/bash

if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # Install on target (not on host)
   if [ ! -f bcm2835-unicam.ko.xz ] # if the module is not already installed in the current directory
   then
      sudo make -C /lib/modules/$(uname -r)/build M=$PWD INSTALL_MOD_DIR=kernel/drivers/media/platform/bcm2835 modules_install
   else
      sudo cp bcm2835-unicam.ko.xz /lib/modules/$(uname -r)/kernel/drivers/media/platform/bcm2835/
   fi
   sudo depmod
fi


