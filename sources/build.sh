#!/bin/bash

echo ARCH $ARCH 
echo CROSS_COMPILE $CROSS_COMPILE
echo KERNEL $KERNEL
echo LOCALVERSION $LOCALVERSION

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

# Build native patched modules for current kernel version on target only
if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   KERNEL_VERSION=$(uname -r | rev | cut -d '-' -f '2-' | rev)
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
fi

# Install Kernel image and modules that were built on host
if [[ -d linux_install ]]
then
   if [[ $1 == "install" ]]
   then
      if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
      then
         codename=$(lsb_release -a 2>/dev/null |grep Codename| awk '{print $2}')
         if [[ x$codename == xbullseye ]]
         then
            KVERSION=bullseye
         elif [[ x$codename == xmantic ]]
         then
            KVERSION=mantic
         else
            echo "Error, $codename is not supported"
            exit
         fi
      
         KNAME=$(basename $(ls linux_install/kernel_bullseye/kernel*.img))
         echo KNAME $KNAME
      
         KERNEL_PATH=/boot/
         if [[ -f /boot/$KNAME ]] # Before Bookworm
         then
            KERNEL_PATH=/boot
         fi
         if [[ -f /boot/firmware/$KNAME ]] # Bookworm or Ubuntu
         then
            KERNEL_PATH=/boot/firmware
         fi
         echo KERNEL_PATH $KERNEL_PATH
         
         sudo cp $KERNEL_PATH/$KNAME $KERNEL_PATH/$KNAME.bak
         sudo rsync -iahHAXxvz --progress --chown=root:root linux_install/kernel_$KVERSION/$KNAME $KERNEL_PATH/

         sudo rsync -iahHAXxvz --progress --chown=root:root linux_install/modules/lib/modules/* /lib/modules/
         sudo depmod
      fi
   fi
fi

# Customize config.txt on target
if [[ $1 == "install" ]]
then
   if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
   then
      if [ ! $(grep "dtoverlay=eg-ec-mipi" /boot/config.txt) ]
      then
         echo "# Uncomment the following line to enable EngineCore camera" | sudo tee -a /boot/config.txt
         echo "#dtoverlay=eg-ec-mipi" | sudo tee -a /boot/config.txt
         echo "# Uncomment the following line for EngineCore with 2 MIPI lanes. 1 lane by default." | sudo tee -a /boot/config.txt
         echo "#dtparam=2lanes" | sudo tee -a /boot/config.txt
         echo "# Uncomment the following line to modify the EngineCore I2C address. 0x16 by default." | sudo tee -a /boot/config.txt
         echo "#dtparam=i2c-addr=0x16" | sudo tee -a /boot/config.txt
      fi
      if [ ! $(grep "dtoverlay=dione-ir" /boot/config.txt) ]
      then
         echo "# Uncomment the following line to enable Dione camera" | sudo tee -a /boot/config.txt
         echo "#dtoverlay=dione-ir" | sudo tee -a /boot/config.txt
      fi
   fi
fi

if [[ $1 == "clean" ]]
then
   rm -rf linux_install
fi
