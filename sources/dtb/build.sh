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
      for dts in *.dts; do
         dtbo="${dts%.dts}.dtbo"
         sudo dtc -I dts -O dtb -o "$OVERLAY_FOLDER/$dtbo" "$dts"
      done
   fi
else
   # On host
   if [[ $1 == "install" ]]
   then
      if [[ x$3 != x ]]
      then
         mkdir -p $3/boot/overlays
         for dts in *.dts; do
            dtbo="${dts%.dts}.dtbo"
            dtc -I dts -O dtb -o "$3/boot/overlays/$dtbo" "$dts"
         done
      fi
   fi
fi
