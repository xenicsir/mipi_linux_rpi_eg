#!/bin/bash

pushd ..
. ./rpi_scripts.lib
popd

# Get official RPI Linux repo
if [[ ! -d ${LINUX_BUILD} ]]
then
   git clone -b rpi-6.1.y https://github.com/raspberrypi/linux.git ${LINUX_BUILD}
   cd ${LINUX_BUILD}
   #git reset --hard a477a6351575aa173f9f82857f5797e384fbc704  # 6.1.64
   git reset --hard ee8dea337199 # 6.1.21
fi



