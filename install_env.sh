#!/bin/bash

. ./environment $1 $2

# Get official RPI Linux repo
if [[ ! -d ${LINUX_RPI_SRC} ]]
then
   if [[ $1 == bullseye ]]
   then
      git clone -b rpi-6.1.y https://github.com/raspberrypi/linux.git ${LINUX_RPI_SRC}
      pushd ${LINUX_RPI_SRC}
      git reset --hard ee8dea337199 # 6.1.21, latest version for BullsEye
      popd
   elif [[ $1 == jammy ]]
   then
      # Ubuntu 22.04.4
      git clone -b master-next https://git.launchpad.net/~ubuntu-kernel/ubuntu/+source/linux-raspi/+git/jammy ${LINUX_RPI_SRC}
      pushd ${LINUX_RPI_SRC}
      git checkout Ubuntu-raspi-5.15.0-1046.49
   elif [[ $1 == mantic ]]
   then
      # Ubuntu 23.10
      git clone -b master-next https://git.launchpad.net/~ubuntu-kernel/ubuntu/+source/linux-raspi/+git/mantic ${LINUX_RPI_SRC}
      pushd ${LINUX_RPI_SRC}
      git checkout Ubuntu-raspi-6.5.0-1005.7
   elif [[ $1 == mantic ]]
   then
      # Ubuntu 23.10
      git clone -b master-next https://git.launchpad.net/~ubuntu-kernel/ubuntu/+source/linux-raspi/+git/noble ${LINUX_RPI_SRC}
      pushd ${LINUX_RPI_SRC}
      git checkout Ubuntu-raspi-6.5.0-1005.7
   else
      echo "Error, specify a a supported Linux name : bullseye, jammy, or mantic"
      exit
   fi
fi


