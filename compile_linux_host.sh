#!/bin/bash

pushd ..
. ./rpi_scripts.lib
popd

# Compile Linux kernel
pushd ${LINUX_RPI_BUILD}
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KERNEL=kernel8 make bcm2711_defconfig
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KERNEL=kernel8 make Image modules dtbs
popd

pushd sources
./build.sh make ${LINUX_RPI_BUILD}
popd
