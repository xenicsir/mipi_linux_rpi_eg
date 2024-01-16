#!/bin/bash

. ./environment

# Install kernel Image
cp ${LINUX_RPI_SRC}/arch/arm64/boot/Image $LINUX_RPI_INSTALL_KERNEL/
gzip $LINUX_RPI_INSTALL_KERNEL/Image
mv $LINUX_RPI_INSTALL_KERNEL/Image.gz $LINUX_RPI_INSTALL_KERNEL/kernel8.img

# Install kernel modules
ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION make -C $LINUX_RPI_SRC modules_install INSTALL_MOD_PATH=$LINUX_RPI_INSTALL_MODULES

# Install out of tree modules
pushd sources
./build.sh install ${LINUX_RPI_SRC} $LINUX_RPI_INSTALL
popd
