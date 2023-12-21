#!/bin/bash

if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # Install on target (not on host)
   if [ ! -f dal_mipi_src.ko.xz ] # if the module is not already installed in the current directory
   then
      sudo make -C /lib/modules/$(uname -r)/build M=$PWD INSTALL_MOD_DIR=kernel/drivers/media/i2c modules_install
   else
      sudo cp dal_mipi_src.ko.xz /lib/modules/$(uname -r)/kernel/drivers/media/i2c/
   fi
   sudo depmod
fi
