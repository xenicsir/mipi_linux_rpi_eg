#!/bin/bash

# Return the /dev/videoX node for an EG camera on the specified CAM port.
# Usage: eg_get_video_device_rpi5.sh [0|1]
# Default port: 1

PORT=${1:-1}

case "$PORT" in
   0) CSI_ADDR="1f00110000" ;;
   1) CSI_ADDR="1f00128000" ;;
   *) echo "Usage: $0 [0|1]" >&2; exit 1 ;;
esac

media=$(v4l2-ctl --list-devices | egrep "rp1-cfe.*${CSI_ADDR}" -A 20 | grep -oP '/dev/media\d+' | head -1)
if [[ -z "$media" ]]; then
   echo "No camera found on CAM${PORT}" >&2
   exit 1
fi

media-ctl -p -d "$media" | grep rp1-cfe-csi2_ch0 -A 5 | grep "device node name" | awk '{print $4}'
