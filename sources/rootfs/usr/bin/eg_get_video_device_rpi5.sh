#!/bin/bash

# This script allows to get the /dev/videoX name associated to an EG camera plugged on the CAM port 1 of the Rpi 5

echo $(media-ctl -p -d $(v4l2-ctl --list-devices | egrep "rp1-cfe.*1f00128000" -A 20 | grep -m1 /dev/media) | grep rp1-cfe-csi2_ch0 -A 5 | grep "device node name" | awk '{print $4}')


