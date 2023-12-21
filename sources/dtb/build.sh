if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   if [[ $1 == "install" ]]
   then
      sudo dtc -I dts -O dtb -o /boot/overlays/dal_mipi.dtbo dal_mipi.dts
   fi
else
   if [[ $1 == "install" ]]
   then
      sudo dtc -I dts -O dtb -o $3/boot/overlays/dal_mipi.dtbo dal_mipi.dts
   fi
fi
