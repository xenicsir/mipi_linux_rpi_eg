#!/bin/bash

. ./environment $1 $2

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
if [[ $2 == rpi4 ]]
then
   DEFCONFIG=bcm2711_defconfig
elif [[ $2 == rpi5 ]]
then
   DEFCONFIG=bcm2712_defconfig
else
   echo "Error, specify a target board : rpi4 or rpi5"
   exit
fi

if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # Compile the kernel on the target
   ARCH=$KERN_ARCH KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION make -C $LINUX_RPI_SRC $DEFCONFIG
   ARCH=$KERN_ARCH KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION make -C $LINUX_RPI_SRC Image modules dtbs

   pushd sources
   ARCH=$KERN_ARCH KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION ./build.sh make ${LINUX_RPI_SRC}
   popd
else
   # Compile on a host computer
   ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION make -C $LINUX_RPI_SRC $DEFCONFIG
   ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION make -C $LINUX_RPI_SRC Image modules dtbs

   pushd sources
   ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION ./build.sh make ${LINUX_RPI_SRC}
   popd
fi