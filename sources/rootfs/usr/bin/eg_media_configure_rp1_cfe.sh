#!/bin/bash

# Configure media pipeline for any EG camera detected on CAM0 or CAM1.
# CSI0 (CAM0) = 1f00110000, CSI1 (CAM1) = 1f00128000.

# Set format on csi2 pads directly via V4L2 subdev ioctl, and print the
# corresponding V4L2 fourcc for the video node.
# Used when the mbus format code is unknown to media-ctl (e.g. Y16_1X16 on v4l-utils 1.22.1).
_set_csi2_fmt_ioctl() {
   local sensor_subdev=$1
   local csi2_subdev=$2
   python3 - "$sensor_subdev" "$csi2_subdev" <<'PYEOF'
import fcntl, struct, sys, os

# VIDIOC_SUBDEV_G_FMT / S_FMT = _IOWR('V', 0x04/0x05, struct v4l2_subdev_format)
# struct v4l2_subdev_format (kernel 6.x): which(u32) + pad(u32) + framefmt(48B) + stream(u32) + reserved[7](u32) = 88B
VIDIOC_SUBDEV_G_FMT = 0xC0585604
VIDIOC_SUBDEV_S_FMT = 0xC0585605
V4L2_SUBDEV_FORMAT_TRY    = 0
V4L2_SUBDEV_FORMAT_ACTIVE = 1
V4L2_FIELD_NONE = 1
SZ = 88

# mbus code → V4L2 fourcc string as accepted by v4l2-ctl (add entries as needed)
MBUS_TO_FOURCC = {
    0x202e: 'Y16 -BE',   # MEDIA_BUS_FMT_Y16_1X16 big-endian
}

def make_buf(pad, code=0, w=0, h=0):
    b = bytearray(SZ)
    struct.pack_into('<II', b, 0, V4L2_SUBDEV_FORMAT_ACTIVE, pad)
    if code:
        struct.pack_into('<IIIII', b, 8, w, h, code, V4L2_FIELD_NONE, 0)
    return b

sensor_dev, csi2_dev = sys.argv[1], sys.argv[2]

buf = make_buf(0)
fd = os.open(sensor_dev, os.O_RDWR)
try:
    fcntl.ioctl(fd, VIDIOC_SUBDEV_G_FMT, buf)
finally:
    os.close(fd)
w, h, code = struct.unpack_from('<III', buf, 8)
print(f'  sensor: code=0x{code:04x} {w}x{h}')

fd = os.open(csi2_dev, os.O_RDWR)
try:
    for pad in (0, 4):
        fcntl.ioctl(fd, VIDIOC_SUBDEV_S_FMT, make_buf(pad, code, w, h))
        rbuf = make_buf(pad)
        fcntl.ioctl(fd, VIDIOC_SUBDEV_G_FMT, rbuf)
        rw, rh, rc = struct.unpack_from('<III', rbuf, 8)
        print(f'  set csi2 pad{pad}: requested=0x{code:04x} {w}x{h} | applied=0x{rc:04x} {rw}x{rh}')
finally:
    os.close(fd)

fourcc = MBUS_TO_FOURCC.get(code, '')
print(f'VIDEONODE_FMT={fourcc},{w},{h}')  # comma-delimited to preserve trailing space in fourcc
PYEOF
}

configure_port() {
   local csi_addr=$1
   local port=$2

   local media
   media=$(v4l2-ctl --list-devices | egrep "rp1-cfe.*${csi_addr}" -A 20 | grep -oP '/dev/media\d+' | head -1)
   [[ -z "$media" ]] && return

   echo "--- CAM${port}: $media ---"

   local fmt_line
   fmt_line=$(media-ctl -p -d "$media" | awk '/subtype Sensor/{f=1} f && /\[.*fmt:/{print; exit}')
   echo "fmt = $fmt_line"

   # Extract only the mbus format code (e.g. Y16_1X16/640x480).
   # media-ctl -p output includes "stream:N " and "colorspace:..." since kernel 6.12.47,
   # but media-ctl -V does not accept those attributes inside the brackets.
   local fmt_code
   fmt_code=$(echo "$fmt_line" | grep -oP '(?<=fmt:)[^ ]+')

   media-ctl -d "$media" -l '"csi2":4 -> "rp1-cfe-csi2_ch0":0 [1]'

   local video_fourcc video_w video_h
   if [[ "$fmt_code" == unknown/* ]]; then
      # media-ctl does not know this mbus format code: set csi2 pads directly via ioctl.
      local sensor_subdev csi2_subdev
      sensor_subdev=$(media-ctl -p -d "$media" | awk '/subtype Sensor/{f=1} f && /device node name/{print $4; f=0; exit}')
      csi2_subdev=$(media-ctl -p -d "$media"   | awk '/entity [0-9]+: csi2 /{f=1} f && /device node name/{print $4; f=0; exit}')
      echo "  format unknown to media-ctl, using ioctl (sensor=$sensor_subdev csi2=$csi2_subdev)"
      local py_out
      py_out=$(_set_csi2_fmt_ioctl "$sensor_subdev" "$csi2_subdev")
      echo "$py_out"
      # Parse VIDEONODE_FMT line: "VIDEONODE_FMT=<fourcc>,<w>,<h>" (comma-delimited)
      IFS=',' read -r video_fourcc video_w video_h < <(echo "$py_out" | awk -F'=' '/^VIDEONODE_FMT=/{print $2}')
   else
      local fmt="[fmt:${fmt_code} field:none]"
      media-ctl -d "$media" -V '"csi2":0 '"$fmt"
      media-ctl -d "$media" -V '"csi2":4 '"$fmt"
   fi

   # Set the video node pixelformat to match the sensor output.
   # Required for media pipeline validation (cfe_video_link_validate_capture).
   if [[ -n "$video_fourcc" ]]; then
      local video_dev
      video_dev=$(media-ctl -p -d "$media" | awk '/^- entity.*rp1-cfe-csi2_ch0/{f=1} f && /device node name/{print $4; f=0; exit}')
      echo "  setting video node $video_dev to $video_fourcc ${video_w}x${video_h}"
      v4l2-ctl -d "$video_dev" --set-fmt-video="width=${video_w},height=${video_h},pixelformat=${video_fourcc}"
   fi

   media-ctl -p -d "$media"
}

configure_port "1f00110000" 0
configure_port "1f00128000" 1
