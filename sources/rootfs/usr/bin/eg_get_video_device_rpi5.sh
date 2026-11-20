#!/bin/bash

# Report EG camera devices, sensor formats and streaming commands on Raspberry Pi 5.
# Sensor pad formats are authoritative; capture nodes may retain a default format.

usage() {
   printf 'Usage: %s [0|1|all] [--devices-only]\n' "${0##*/}"
   printf 'Default: report every connected EG camera in CAM0/CAM1 order.\n'
   printf 'Read sensor pad 0 for resolution and format; do not configure or stream.\n'
   printf 'Print V4L2 capture and GStreamer display commands (run them separately).\n'
   printf 'With --devices-only: retain the original device-only output.\n'
}

selection=all
selected=0
devices_only=0
for arg in "$@"; do
   case "$arg" in
      0|1|all)
         if (( selected )); then usage >&2; exit 1; fi
         selection=$arg
         selected=1 ;;
      --devices-only) devices_only=1 ;;
      -h|--help) usage; exit 0 ;;
      *) usage >&2; exit 1 ;;
   esac
done
case "$selection" in
   0|1) ports=("$selection") ;;
   all) ports=(0 1) ;;
esac

for tool in v4l2-ctl media-ctl awk readlink find; do
   if ! command -v "$tool" >/dev/null 2>&1; then
      printf 'Required command not found: %s\n' "$tool" >&2
      exit 1
   fi
done

if ! media_list=$(LC_ALL=C v4l2-ctl --list-devices); then
   printf 'Cannot enumerate V4L2 devices.\n' >&2
   exit 1
fi

# Query the active sensor pad through the subdevice found in the media graph.
# Numeric codes also work when media-ctl's name table reports "unknown".
report_format() {
   local video=$1 subdev=$2 sensor_format width height code fourcc gst_format
   local current current_size current_pixel formats
   local -a fields
   if ! sensor_format=$(LC_ALL=C v4l2-ctl -d "$subdev" --get-subdev-fmt pad=0); then
      printf 'Cannot read the active sensor format from %s; streaming commands unavailable.\n' "$subdev" >&2
      return 1
   fi
   mapfile -t fields < <(awk '
      /Width\/Height[[:space:]]*:/ { split($NF, size, "/"); w=size[1]; h=size[2] }
      /Mediabus Code[[:space:]]*:/ { sub(/^.*:[[:space:]]*/, ""); split($0, f, /[[:space:]]+/); code=f[1] }
      END { printf "%s\n%s\n%s\n", w,h,tolower(code) }
   ' <<< "$sensor_format")
   width=${fields[0]}; height=${fields[1]}; code=${fields[2]}
   if [[ ! $width =~ ^[1-9][0-9]*$ || ! $height =~ ^[1-9][0-9]*$ || ! $code =~ ^0x[0-9a-f]+$ ]]; then
      printf 'Invalid sensor format from %s; streaming commands unavailable.\n' "$subdev" >&2
      return 1
   fi
   # Mapping for direct rp1-cfe capture with the EG driver patches.
   # Y16_1X16 describes the bus, not memory endianness: EG raw16 uses Y16_BE.
   case "$code" in
      0x200f) fourcc=UYVY; gst_format=UYVY ;;
      0x2011) fourcc=YUYV; gst_format=YUY2 ;;
      0x100a) fourcc=RGB3; gst_format=RGB ;;
      0x1013) fourcc=BGR3; gst_format=BGR ;;
      0x2001) fourcc=GREY; gst_format=GRAY8 ;;
      0x202e) fourcc='Y16 -BE'; gst_format=GRAY16_BE ;;
      *)
         printf 'Sensor: %sx%s, media-bus code %s (%s, pad 0)\n' "$width" "$height" "$code" "$subdev"
         printf 'Unsupported media-bus code; no streaming commands generated.\n' >&2
         return 1 ;;
   esac
   printf 'Sensor: %sx%s, media-bus code %s (%s, pad 0)\n' "$width" "$height" "$code" "$subdev"
   printf 'Capture format: %s; GStreamer: %s\n' "$fourcc" "$gst_format"
   if ! formats=$(LC_ALL=C v4l2-ctl -d "$video" --list-formats); then
      printf 'Cannot enumerate capture formats for %s.\n' "$video" >&2
      return 1
   fi
   if ! awk -F "'" -v fmt="$fourcc" '$2 == fmt { found=1 } END { exit !found }' <<< "$formats"; then
      printf 'Required format %s is not advertised by %s; check the installed EG kernel patches.\n' "$fourcc" "$video" >&2
      return 1
   fi
   if current=$(LC_ALL=C v4l2-ctl -d "$video" --get-fmt-video); then
      current_size=$(awk '/Width\/Height[[:space:]]*:/ { print $NF; exit }' <<< "$current")
      current_pixel=$(awk -F "'" '/Pixel Format[[:space:]]*:/ { print $2; exit }' <<< "$current")
      printf 'Current video node: %s, %s\n' "$current_size" "$current_pixel"
      if [[ $current_size != "$width/$height" || $current_pixel != "$fourcc" ]]; then
         printf 'Note: the capture node differs from the sensor; the commands below select the sensor format.\n'
      fi
   else
      printf 'Note: the current capture-node format could not be read.\n' >&2
   fi
   printf '# Capture (Ctrl+C to stop):\n'
   printf 'v4l2-ctl -d %s --set-fmt-video=width=%s,height=%s,pixelformat="%s" --stream-mmap\n' "$video" "$width" "$height" "$fourcc"
   printf '# Display (requires a graphical session; Ctrl+C to stop):\n'
   printf 'gst-launch-1.0 -e v4l2src device=%s ! "video/x-raw,format=%s,width=%s,height=%s" ! videoconvert ! autovideosink sync=false\n' "$video" "$gst_format" "$width" "$height"
}

# Return 2 for a port without an EG sensor, 1 for an incomplete/broken mapping.
find_camera() {
   local port=$1 address media topology sensor_name sensor_subdev client_path i2c_client
   local -a nodes controls
   case "$port" in
      0) address=1f00110000 ;;
      1) address=1f00128000 ;;
   esac

   # Reset at every device header: never consume a following controller's nodes.
   media=$(awk -v address="$address" '
      /^[^[:space:]]/ {
         selected = ($0 ~ "^rp1-cfe[[:space:]]*\\(platform:" address "\\.csi\\):[[:space:]]*$")
         next
      }
      selected && $1 ~ /^\/dev\/media[0-9]+$/ { print $1; exit }
   ' <<< "$media_list")
   [[ -n "$media" ]] || return 2

   if ! topology=$(LC_ALL=C media-ctl -p -d "$media"); then
      printf 'CAM%s: cannot read topology of %s.\n' "$port" "$media" >&2
      return 1
   fi

   # Limit each lookup to its entity; names may include an I2C identifier.
   mapfile -t nodes < <(awk '
      /^- entity [0-9]+:/ {
         entity = $0
         sub(/^- entity [0-9]+: /, "", entity)
         sub(/ \([^)]*\)$/, "", entity)
         sensor = 0
      }
      /subtype Sensor([[:space:]]|$)/ {
         sensor = 1
         count++
         split(entity, fields, /[[:space:]]+/)
         sensor_name = fields[1]
      }
      /device node name/ {
         if (entity == "rp1-cfe-csi2_ch0") video = $4
         if (sensor) subdev = $4
      }
      END { printf "%s\n%s\n%s\n%d\n", video, sensor_name, subdev, count }
   ' <<< "$topology")

   if [[ ${nodes[3]} == 0 ]]; then
      return 2
   elif [[ ${nodes[3]} != 1 ]]; then
      printf 'CAM%s: multiple sensors in %s; cannot select one safely.\n' "$port" "$media" >&2
      return 1
   fi
   sensor_name=${nodes[1]}
   sensor_subdev=${nodes[2]}
   case "$sensor_name" in
      eg-ec-*|dioneir*|ilumos*|microlynx*) ;;
      *) return 2 ;;
   esac
   if [[ ! ${nodes[0]} =~ ^/dev/video[0-9]+$ || ! $sensor_subdev =~ ^/dev/v4l-subdev[0-9]+$ ]]; then
      printf 'CAM%s: missing capture node or sensor subdevice in %s.\n' "$port" "$media" >&2
      return 1
   fi

   if ! client_path=$(readlink -f "/sys/class/video4linux/${sensor_subdev##*/}/device"); then
      printf 'CAM%s: cannot resolve the I2C client of %s.\n' "$port" "$sensor_subdev" >&2
      return 1
   fi
   i2c_client=${client_path##*/}
   if [[ ! $i2c_client =~ ^[0-9]+-[[:xdigit:]]{4}$ ]]; then
      printf 'CAM%s: invalid I2C client for %s: %s\n' "$port" "$sensor_subdev" "$i2c_client" >&2
      return 1
   fi

   # Exact client boundary; Dione can append its FPGA address after the bridge ID.
   local matches
   if ! matches=$(find /dev -maxdepth 1 -type c \
      \( -name "${sensor_name}*-${i2c_client}" -o -name "${sensor_name}*-${i2c_client}-*" \) -print); then
      printf 'CAM%s: cannot enumerate I2C control devices.\n' "$port" >&2
      return 1
   fi
   controls=()
   if [[ -n "$matches" ]]; then
      mapfile -t controls <<< "$matches"
   fi
   if (( ${#controls[@]} != 1 )); then
      printf 'CAM%s: expected one I2C control device for %s (%s), found %s.\n' \
         "$port" "$sensor_name" "$i2c_client" "${#controls[@]}" >&2
      return 1
   fi
   printf '%s\n%s\n' "${nodes[0]}" "${controls[0]}"
   if (( ! devices_only )); then
      report_format "${nodes[0]}" "$sensor_subdev" || return 1
   fi
}

found=0
failed=0
for port in "${ports[@]}"; do
   if devices=$(find_camera "$port"); then
      if [[ "$selection" == all ]] || (( ! devices_only )); then
         (( found > 0 )) && printf '\n'
         printf 'CAM%s:\n' "$port"
      fi
      printf '%s\n' "$devices"
      (( found += 1 ))
   else
      status=$?
      if [[ -n "$devices" ]]; then
         printf 'CAM%s:\n%s\n' "$port" "$devices"
         (( found += 1 ))
      fi
      if (( status != 2 )); then
         failed=1
      elif [[ "$selection" != all ]]; then
         printf 'No EG camera found on CAM%s.\n' "$port" >&2
      fi
   fi
done

if (( found == 0 )); then
   [[ "$selection" == all ]] && printf 'No EG camera found on CAM0 or CAM1.\n' >&2
   exit 1
fi
exit "$failed"
