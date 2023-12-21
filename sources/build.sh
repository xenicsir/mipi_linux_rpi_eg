#!/bin/bash

# Build proprietary modules
for file in *; do
   if [[ -d $file && $(echo $file | cut -c1-7) != "kernel-" ]]
   then
      pushd $file
      if [[ -f build.sh ]]
      then
         echo $1 $file
         ./build.sh $@
      fi
      popd
   fi
done

# Build native modules for current kernel version
if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   KERNEL_VERSION=$(uname -r | rev | cut -d '-' -f '2-' | rev)
else
   KVERSION=$(grep "^VERSION" ${2}/Makefile | awk -F= '{print $2}' | sed 's/ //g')
   KPATCHLEVEL=$(grep "^PATCHLEVEL" ${2}/Makefile | awk -F= '{print $2}' | sed 's/ //g')
   KSUBLEVEL=$(grep "^SUBLEVEL" ${2}/Makefile | awk -F= '{print $2}' | sed 's/ //g')
   KERNEL_VERSION=${KVERSION}.${KPATCHLEVEL}.${KSUBLEVEL}
fi
file=$(echo "kernel-"$KERNEL_VERSION)
if [[ -d $file ]]
then
   pushd $file
   if [[ -f build.sh ]]
   then
      echo $1 $file
      ./build.sh $@
   fi
   popd
fi

# Customize config.txt
if [[ $1 == "install" ]]
then
   if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
   then
      if [ ! $(grep "dtoverlay=dal_mipi" /boot/config.txt) ]
      then
         echo "#dtoverlay=dal_mipi" | sudo tee -a /boot/config.txt
         echo "#dtparam=2lanes # Uncomment it for 2 MIPI lanes. 1 lane by default." | sudo tee -a /boot/config.txt
         echo "#dtparam=i2c-addr=0x16" | sudo tee -a /boot/config.txt
      fi
   fi
fi