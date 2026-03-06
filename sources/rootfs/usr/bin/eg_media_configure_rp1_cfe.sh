#!/bin/bash

media=$(v4l2-ctl --list-devices | egrep "rp1-cfe.*1f00128000" -A 20 | grep -m1 /dev/media)
if [[ x$media != x ]]
then
   echo media = $media
   fmt_line=$(media-ctl -p -d $media | awk '/subtype Sensor/{f=1} f && /\[.*fmt:/{print; exit}')
   echo fmt = $fmt_line

   # Extract only the mbus format code (e.g. Y16_1X16/640x480).
   # media-ctl -p output includes "stream:N " and "colorspace:..." since kernel 6.12.47,
   # but media-ctl -V does not accept those attributes inside the brackets.
   fmt_code=$(echo "$fmt_line" | grep -oP '(?<=fmt:)[^ ]+')
   fmt="[fmt:${fmt_code} field:none]"

   media-ctl -d $media -l '"csi2":4 -> "rp1-cfe-csi2_ch0":0 [1]'
   media-ctl -d $media -V '"csi2":0 '"$fmt"
   media-ctl -d $media -V '"csi2":4 '"$fmt"

   media-ctl -p -d $media

   #media-ctl -d $media --print-dot >config.dot
   #dot -Tpng config.dot -o config.png
fi
