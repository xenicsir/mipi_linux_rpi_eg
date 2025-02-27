#!/bin/bash

media=$(v4l2-ctl --list-devices | grep rp1-cfe -A 20 | grep -m1 /dev/media)
if [[ x$media != x ]]
then
   echo media = $media
   fmt=$(media-ctl -p -d $media | grep -E "entity.*dioneir|entity.*eg-ec-i2c" -A 10 | grep -m1 fmt)
   echo fmt = $fmt

   media-ctl -d $media -l ''\''csi2'\'':4 -> '\''rp1-cfe-csi2_ch0'\'':0 [1]'
   media-ctl -d $media -V ""\""csi2"\"":0 $fmt"
   media-ctl -d $media -V ""\""csi2"\"":4 $fmt"

   media-ctl -p -d $media

   #media-ctl -d $media --print-dot >config.dot
   #dot -Tpng config.dot -o config.png
fi

