#!/bin/bash

. ./rpi_scripts.lib

echo LINUX_RPI_BUILD $LINUX_RPI_BUILD

# Get official RPI Linux repo
if [[ ! -d ${LINUX_RPI_BUILD} ]]
then
   git clone -b rpi-6.1.y https://github.com/raspberrypi/linux.git ${LINUX_RPI_BUILD}
   pushd ${LINUX_RPI_BUILD}
   git reset --hard ee8dea337199 # 6.1.21, latest version for BullEye
   popd
fi



