#!/bin/bash

. ./environment

# Get official RPI Linux repo
if [[ ! -d ${LINUX_RPI_SRC} ]]
then
   git clone -b rpi-6.1.y https://github.com/raspberrypi/linux.git ${LINUX_RPI_SRC}
   pushd ${LINUX_RPI_SRC}
   git reset --hard ee8dea337199 # 6.1.21, latest version for BullEye
   popd
fi


