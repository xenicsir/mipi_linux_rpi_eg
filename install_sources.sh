#!/bin/bash

. ./environment $1 $2

# Install kernel Image
mkdir -p $LINUX_RPI_INSTALL_KERNEL
cp ${LINUX_RPI_SRC}/arch/arm64/boot/Image $LINUX_RPI_INSTALL_KERNEL/
gzip $LINUX_RPI_INSTALL_KERNEL/Image
mv $LINUX_RPI_INSTALL_KERNEL/Image.gz $LINUX_RPI_INSTALL_KERNEL/$KERN_KERNEL.img

# Install kernel modules
mkdir -p $LINUX_RPI_INSTALL_MODULES
if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # On the target
   ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION make -C $LINUX_RPI_SRC modules_install INSTALL_MOD_PATH=$LINUX_RPI_INSTALL_MODULES
else
   # On a host computer
   ARCH=$KERN_ARCH KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION make -C $LINUX_RPI_SRC modules_install INSTALL_MOD_PATH=$LINUX_RPI_INSTALL_MODULES
fi

# Install out of tree modules
pushd sources
./build.sh install ${LINUX_RPI_SRC} $LINUX_RPI_INSTALL_MODULES
popd
