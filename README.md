This document present how to build and install the MIPI drivers of Exosens cameras for Raspberry Pi.

## Building framework

### 0. Environment used with this building framework

- Rpi model : Raspberry Pi 4 Model B
- Host computer OS Ubuntu 20.04.1 LTS
- Cross compiler gcc-9-aarch64-linux-gnu

Note about cross compiler versions :
- BookWorm 12 was compiled with gcc-12 (12.2.0-14)
- BullsEye 11 was compiled with gcc-8 (8.4.0)

### 1. Building MIPI driver for RPi OS BookWorm
For this RPi OS version, the Linux kernel is not built from the Raspberry Pi Linux, but is a Debian package.
<pre>
cat /etc/os-release
PRETTY_NAME="Debian GNU/Linux 12 (bookworm)"

uname -a
Linux pi <b>6.1.0-rpi7-rpi-v8</b> #1 SMP PREEMPT Debian 1:<b>6.1.63-1+rpt1</b> (2023-11-24) aarch64 GNU/Linux
</pre>

The 6.1.0-rpi7-rpi-v8 Linux version, which is based on kernel 6.1.63, is not available for download and cross compilation on a host. So the MIPI driver hast to be built out of tree on the Raspberry Pi.
The corresponding kernel headers are installed by default with BookWorm.

- Copy the **sources** folder to the RPi and build/install it :
<pre>
./build.sh make
./build.sh install
</pre>
- Customize /boot/config.txt :
<pre>
dtoverlay=eg-ec-mipi
#dtparam=2lanes # Uncomment it for 2 MIPI lanes. 1 lane by default.
dtparam=i2c-addr=0x16
</pre>

- Reboot the RPi

### 2. Building MIPI driver for RPi OS BullsEye

For RPI OS BullsEye, the driver must be built on a host computer, as the last kernel headers for BullsEye (6.1.21) are not available in the packages repository.
So the Raspberry Pi linux (branch rpi-6.1.y) has to be built on a host computer with a cross compiler.

- Install the RPi Linux environment on the host :
<pre>
./install_env_host.sh
</pre>

- Cross compile Linux and the MIPI drivers :
<pre>
./compile_linux_host.sh
</pre>

- Install the Linux build in the **sources** folder :
<pre>
./install_sources_host.sh
</pre>

- Copy the **sources** folder to the Raspberry Pi
- Log to the Raspberry Pi, go to the **sources** folder and install the drivers :
<pre>
./build.sh install
</pre>
- Customize /boot/config.txt :
<pre>
dtoverlay=eg-ec-mipi
#dtparam=2lanes # Uncomment it for 2 MIPI lanes. 1 lane by default.
dtparam=i2c-addr=0x16
</pre>

- Reboot the RPi

### 3. Building MIPI driver on host for RPI OS with other RPi Linux versions

- Find the right branch and commit at https://github.com/raspberrypi/linux.git
- Modify **install_env_host.sh** :
<pre>
git clone -b <b>$your_branch</b> https://github.com/raspberrypi/linux.git ${LINUX_RPI_SRC}
pushd ${LINUX_RPI_SRC}
git reset --hard <b>$your_commit</b>
popd
</pre>
- Go to chapter 2.

**Note : code in "sources" folder may not compile because of incompatible Linux version**

## To grab video

Install gstreamer if needed:
<pre>
sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
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


## To use the camera :
libecctrl

ECSwCtrl

ECSwUpgrade

