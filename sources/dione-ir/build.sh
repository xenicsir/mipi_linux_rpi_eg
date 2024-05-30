#!/bin/bash

LIB_FOLDER=kernel/drivers/media/i2c

if [[ x$2 == x ]]
then
   # Do it on target
   MODULES_FOLDER=lib/modules/$(uname -r)
   if [[ x$1 == "xmake" ]]
   then
      make -C /${MODULES_FOLDER}/build M=$PWD
      rm -f *.ko.xz
      rm -f lib
   elif [[ x$1 == "xinstall" ]]
   then
      if [ ! -d lib ] # if the lib folder exists, do nothing, the module is to be installed from the root build script
      then
         sudo make -C /${MODULES_FOLDER}/build M=$PWD INSTALL_MOD_DIR=${LIB_FOLDER} modules_install
         sudo depmod
      fi
   elif [[ x$1 == "xclean" ]]
   then
      make -C /${MODULES_FOLDER}/build M=$PWD clean
      rm -rf lib
      rm -f *.ko.xz
   fi
else
   # Do it on host
   if [[ x$1 == "xmake" ]]
   then
      make -C $2 M=$PWD
   elif [[ x$1 == "xinstall" ]]
   then
      make -C $2 M=$PWD INSTALL_MOD_DIR=${LIB_FOLDER} INSTALL_MOD_PATH=$PWD modules_install
      if [[ x$3 != x ]]
      then
         make -C $2 M=$PWD INSTALL_MOD_DIR=${LIB_FOLDER} INSTALL_MOD_PATH=$3 modules_install
      fi
   elif [[ x$1 == "xclean" ]]
   then
      make -C $2 M=$PWD clean
      rm -rf lib
   fi
fi

