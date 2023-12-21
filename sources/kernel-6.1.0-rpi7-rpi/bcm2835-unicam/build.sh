#!/bin/bash

if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # Do it on target
   if [[ $1 == "make" ]]
   then
      patch -p1 < y16.patch
      make -C /lib/modules/$(uname -r)/build M=$PWD
      rm -f *.ko.xz
   elif [[ $1 == "install" ]]
   then
      if [ ! -f bcm2835-unicam.ko.xz ] # if the module is not already installed in the current directory
      then
         sudo make -C /lib/modules/$(uname -r)/build M=$PWD INSTALL_MOD_DIR=kernel/drivers/media/platform/bcm2835 modules_install
      else
         sudo cp bcm2835-unicam.ko.xz /lib/modules/$(uname -r)/kernel/drivers/media/platform/bcm2835/
      fi
      sudo depmod
   elif [[ $1 == "clean" ]]
   then
      make -C /lib/modules/$(uname -r)/build M=$PWD clean
   fi
else
   # Do it on host
   if [[ $1 == "make" ]]
   then
      patch -p1 < y16.patch
      ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -C $2 M=$PWD
   elif [[ $1 == "install" ]]
   then
      ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -C $2 M=$PWD INSTALL_MOD_DIR=. modules_install
   elif [[ $1 == "clean" ]]
   then
      ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -C $2 M=$PWD clean
   fi
fi


