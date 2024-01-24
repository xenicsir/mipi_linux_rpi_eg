#!/bin/bash

LIB_FOLDER=kernel/drivers/media/platform/bcm2835/

if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # Do it on target
   MODULES_FOLDER=lib/modules/$(uname -r)
   if [[ $1 == "make" ]]
   then
      patch -N -p1 < y16.patch
      make -C /${MODULES_FOLDER}/build M=$PWD
      rm -f *.ko.xz
      rm -f lib
   elif [[ $1 == "install" ]]
   then
      MODNAME=${MODULES_FOLDER}/${LIB_FOLDER}/bcm2835-unicam.ko.xz
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
fi

