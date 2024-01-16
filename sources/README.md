To build and install the kernel modules on host :
. ../environment;ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION ./build.sh make $(pwd)/../kernel
. ../environment;ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION ./build.sh install $(pwd)/../kernel

To build and install the kernel modules on target :
./build.sh make
./build.sh install


