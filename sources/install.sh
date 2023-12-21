#!/bin/bash

# Install proprietary modules and dtb
for file in *; do
   if [[ -d $file && $(echo $file | cut -c1-7) != "kernel-" ]]
   then
      pushd $file
      if [[ -f install.sh ]]
      then
         echo Installing $file
         ./install.sh
      fi
      popd
   fi
done

# Install native modules
file=$(echo "kernel-"$(uname -r))
if [[ -d $file ]]
then
   pushd $file
   if [[ -f install.sh ]]
   then
      echo Installing $file
      ./install.sh
   fi
   popd
fi

if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # Customize config.txt
   if [ ! $(grep "dtoverlay=dal_mipi" /boot/config.txt) ]
   then
      echo "dtoverlay=dal_mipi" | sudo tee -a /boot/config.txt
      echo "#dtparam=2lanes # Uncomment it for 2 MIPI lanes. 1 lane by default." | sudo tee -a /boot/config.txt
      echo "dtparam=i2c-addr=0x16" | sudo tee -a /boot/config.txt
   fi
fi
