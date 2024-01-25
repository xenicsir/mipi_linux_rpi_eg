if [ $(grep -c Raspberry /proc/cpuinfo) -eq 1 ]
then
   # On target
   if [[ $1 == "install" ]]
   then
      if [[ -d "/boot/firmware/overlays" ]]
      then
         OVERLAY_FOLDER="/boot/firmware/overlays"  # For Ubuntu Rpi
      else
         OVERLAY_FOLDER="/boot/overlays"
      fi
      sudo dtc -I dts -O dtb -o $OVERLAY_FOLDER/eg-ec-mipi.dtbo eg-ec-mipi.dts
      sudo dtc -I dts -O dtb -o $OVERLAY_FOLDER/dione-ir.dtbo dione-ir.dts
   fi
else
   # On host
   if [[ $1 == "install" ]]
   then
      if [[ x$3 != x ]]
      then
         mkdir -p $3/boot/overlays
         dtc -I dts -O dtb -o $3/boot/overlays/eg-ec-mipi.dtbo eg-ec-mipi.dts
         dtc -I dts -O dtb -o $3/boot/overlays/dione-ir.dtbo dione-ir.dts
      fi
   fi
fi
