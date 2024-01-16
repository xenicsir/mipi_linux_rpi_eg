#!/bin/bash

. ./environment

KVERSION=$(grep "^VERSION" ${LINUX_RPI_SRC}/Makefile | awk -F= '{print $2}' | sed 's/ //g')
KPATCHLEVEL=$(grep "^PATCHLEVEL" ${LINUX_RPI_SRC}/Makefile | awk -F= '{print $2}' | sed 's/ //g')
KSUBLEVEL=$(grep "^SUBLEVEL" ${LINUX_RPI_SRC}/Makefile | awk -F= '{print $2}' | sed 's/ //g')
KERNEL_VERSION=${KVERSION}.${KPATCHLEVEL}.${KSUBLEVEL}
patches_folder=$(echo $(pwd)/"kernel_patches/"$KERNEL_VERSION)
echo Apply kernel patches $patches_folder
pushd $LINUX_RPI_SRC
for file in $patches_folder/*; do
   patch -N -p1 < $file
done
popd

# Compile Linux kernel
ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION make -C $LINUX_RPI_SRC bcm2711_defconfig
ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION make -C $LINUX_RPI_SRC Image modules dtbs

pushd sources
ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION ./build.sh make ${LINUX_RPI_SRC}
popd
