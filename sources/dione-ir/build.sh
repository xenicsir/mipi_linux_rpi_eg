#!/bin/bash

LIB_FOLDER=kernel/drivers/media/i2c

if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # Do it on target
   VERSION_CODENAME=$(grep VERSION_CODENAME /etc/os-release | awk -F= '{print $2}')
   if [[ $VERSION_CODENAME == "bullseye" ]]
   then
      MODULES_FOLDER=lib/modules/$(uname -r)
      if [[ $1 == "make" ]]
      then
         make -C /${MODULES_FOLDER}/build M=$PWD
         rm -f *.ko.xz
         rm -f lib
      elif [[ $1 == "install" ]]
      then
         if [ ! -d lib ] # if the lib folder exists, do nothing, the module is to be installed from ../lib
         then
            sudo make -C /${MODULES_FOLDER}/build M=$PWD INSTALL_MOD_DIR=${LIB_FOLDER} modules_install
            sudo depmod
         fi
      elif [[ $1 == "clean" ]]
      then
         make -C /${MODULES_FOLDER}/build M=$PWD clean
         rm -rf lib
         rm -f *.ko.xz
      fi
   else
      echo Building is not supported on $VERSION_CODENAME
   fi
else
   # Do it on host
   if [[ $1 == "make" ]]
   then
      make -C $2 M=$PWD
   elif [[ $1 == "install" ]]
   then
      make -C $2 M=$PWD INSTALL_MOD_DIR=${LIB_FOLDER} modules_install INSTALL_MOD_PATH=$PWD modules_install
      if [[ x$3 != x ]]
      then
         make -C $2 M=$PWD INSTALL_MOD_DIR=${LIB_FOLDER} modules_install INSTALL_MOD_PATH=$3 modules_install
      fi
   elif [[ $1 == "clean" ]]
   then
      make -C $2 M=$PWD clean
      rm -rf lib
   fi
fi

