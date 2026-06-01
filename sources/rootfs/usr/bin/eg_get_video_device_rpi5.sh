#!/bin/bash

# Return the /dev/videoX node and associated I2C device for an EG camera.
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

topology=$(media-ctl -p -d "$media")

# Video capture device
echo "$topology" | awk '/^- entity.*rp1-cfe-csi2_ch0/{f=1} f && /device node name/{print $4; f=0; exit}'

# I2C character device: resolve via the sensor subdev sysfs path
sensor_name=$(echo "$topology" | awk '/^- entity/ { name=$4 } /subtype Sensor/ { print name; exit }')
sensor_subdev=$(echo "$topology" | awk '/subtype Sensor/{f=1} f && /device node name/{print $4; f=0; exit}')

if [[ -n "$sensor_subdev" && -n "$sensor_name" ]]; then
   i2c_client=$(basename "$(readlink -f /sys/class/video4linux/$(basename "$sensor_subdev")/device 2>/dev/null)")
   ls /dev/${sensor_name}*${i2c_client}* 2>/dev/null | head -1
fi
