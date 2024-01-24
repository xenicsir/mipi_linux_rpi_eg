#!/bin/bash

. ./environment

# Get official RPI Linux repo
if [[ ! -d ${LINUX_RPI_SRC} ]]
then
   if [[ $1 == bullseye ]]
   then
      git clone -b rpi-6.1.y https://github.com/raspberrypi/linux.git ${LINUX_RPI_SRC}
      pushd ${LINUX_RPI_SRC}
      git reset --hard ee8dea337199 # 6.1.21, latest version for BullEye
      popd
   elif [[ $1 == mantic ]]
   then
      git clone -b master-next https://git.launchpad.net/~ubuntu-kernel/ubuntu/+source/linux-raspi/+git/mantic ${LINUX_RPI_SRC}
      pushd ${LINUX_RPI_SRC}
      git checkout Ubuntu-raspi-6.5.0-1005.7
   else
      echo "Error, specify a Linux name : bullseye or mantic"
      exit
   fi
fi


