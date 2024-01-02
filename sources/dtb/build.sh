if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # On target
   if [[ $1 == "install" ]]
   then
      sudo dtc -I dts -O dtb -o /boot/overlays/dal_mipi.dtbo dal_mipi.dts
   fi
else
   # On host
   if [[ $1 == "install" ]]
   then
      if [[ x$3 != x ]]
      then
         sudo dtc -I dts -O dtb -o $3/boot/overlays/dal_mipi.dtbo dal_mipi.dts
      fi
   fi
fi
