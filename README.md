Model           : Raspberry Pi 4 Model B


### Building MIPI driver for RPI OS BookWorm
From this OS version, the Linux kernel is not built from the Raspberry Pi Linux, but is a Debian package.
<pre>
cat /etc/os-release
PRETTY_NAME="Debian GNU/Linux 12 (bookworm)"

uname -a
Linux pi <b>6.1.0-rpi7-rpi-v8</b> #1 SMP PREEMPT Debian 1:<b>6.1.63-1+rpt1</b> (2023-11-24) aarch64 GNU/Linux
</pre>

The Linux version is 6.1.0-rpi7-rpi-v8, which is based on kernel 6.1.63, but is not available for download and cross compilation on a host. So the MIPI driver hast to be built out of tree on the Raspberry Pi.
The corresponding kernel headers are installed by default with BookWorm.

- Copy the ***sources*** folder to the RPi and build/install it :
<pre>
./build.sh make
./build.sh install
</pre>
- Customize /boot/config.txt :
<pre>
dtoverlay=dal_mipi
#dtparam=2lanes # Uncomment it for 2 MIPI lanes. 1 lane by default.
dtparam=i2c-addr=0x16
</pre>

### Building MIPI driver for RPI OS BullEye

./install_env.sh         # install the environment
./compile_kernel.sh      # compile de Linux kernel
./copy_kernel.sh         # copy the needed files from the kernel to rootfs_target
./compile_pi-gen.sh      # generate the whole Raspberry Pi image

The Linux images are in pi-gen/work/Raspi-dal/export-image/
Use Raspberry Pi Imager to flash an image on an SD card


### To grab video

Install gstreamer if needed:
<pre>
apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
</pre>
- YCbCr
<pre>
#v4l2-ctl -d /dev/video0 --stream-mmap --set-fmt-video=width=640,height=480,pixelformat="UYVY"
gst-launch-1.0 -v v4l2src device=/dev/video0 ! "video/x-raw, format=(string)UYVY, width=640, height=480" ! videoconvert ! autovideosink
gst-launch-1.0 -v v4l2src device=/dev/video0 ! "video/x-raw, format=(string)UYVY, width=1280, height=1024" ! videoconvert ! autovideosink
</pre>

- RGB
<pre>
#v4l2-ctl -d /dev/video0 --stream-mmap --set-fmt-video=width=640,height=480,pixelformat="RGB3"
gst-launch-1.0 -v v4l2src device="/dev/video0" ! "video/x-raw, format=(string)RGB, width=640, height=480" ! videoconvert ! autovideosink
gst-launch-1.0 -v v4l2src device="/dev/video0" ! "video/x-raw, format=(string)RGB, width=1280, height=1024" ! videoconvert ! autovideosink
</pre>

- Y16
<pre>
#v4l2-ctl -d /dev/video0 --stream-mmap --set-fmt-video=width=640,height=480,pixelformat="Y16 "
gst-launch-1.0 -v v4l2src device="/dev/video0" ! "video/x-raw, format=(string)GRAY16_BE, width=640, height=480" ! videoconvert ! autovideosink
gst-launch-1.0 -v v4l2src device="/dev/video0" ! "video/x-raw, format=(string)GRAY16_BE, width=1280, height=1024" ! videoconvert ! autovideosink
</pre>


### To use de camera :
libecctrl

ECSwCtrl

ECSwUpgrade

