#!/bin/bash

LIB_FOLDER=kernel/drivers/media/i2c

if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # Do it on target
   MODULES_FOLDER=lib/modules/$(uname -r)
   if [[ $1 == "make" ]]
   then
      make -C /${MODULES_FOLDER}/build M=$PWD
      rm -f *.ko.xz
      rm -f lib
   elif [[ $1 == "install" ]]
   then
      MODNAME=${MODULES_FOLDER}/${LIB_FOLDER}/dal_mipi.ko.xz
      if [ ! -f $MODNAME ] # if the module is not already installed in the current directory
      then
         sudo make -C /${MODULES_FOLDER}/build M=$PWD INSTALL_MOD_DIR=${LIB_FOLDER} modules_install
      else
         sudo rsync -iahHAXxvz --progress $MODNAME /${MODULES_FOLDER}/${LIB_FOLDER}/
      fi
      sudo depmod
   elif [[ $1 == "clean" ]]
   then
      make -C /${MODULES_FOLDER}/build M=$PWD clean
      rm -rf lib
      rm -f *.ko.xz
   fi
else
   # Do it on host
   if [[ $1 == "make" ]]
   then
      ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -C $2 M=$PWD
   elif [[ $1 == "install" ]]
   then
      ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -C $2 M=$PWD INSTALL_MOD_DIR=${LIB_FOLDER} modules_install INSTALL_MOD_PATH=$PWD modules_install
      if [[ x$3 != x ]]
      then
         ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -C $2 M=$PWD INSTALL_MOD_DIR=${LIB_FOLDER} modules_install INSTALL_MOD_PATH=$3 modules_install
      fi
   elif [[ $1 == "clean" ]]
   then
      ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -C $2 M=$PWD clean
      rm -rf lib
   fi
fi

