## Build

Commands to build, install or clean the kernel modules on host (locally) :
<pre>
. ../environment;ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION ./build.sh make $(pwd)/../kernel
. ../environment;ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION ./build.sh install $(pwd)/../kernel
. ../environment;ARCH=$KERN_ARCH CROSS_COMPILE=$KERN_CROSS_COMPILE KERNEL=$KERN_KERNEL LOCALVERSION=$KERN_LOCALVERSION ./build.sh clean $(pwd)/../kernel
</pre>

Commands to build, install or clean the kernel modules on target :
<pre>
./build.sh make
./build.sh install
./build.sh clean
</pre>


## To grab video on target

Install gstreamer if needed:
<pre>
sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
</pre>

### EngineCore cameras
- YCbCr 4:2:2
<pre>
v4l2-ctl -d /dev/video0 --stream-mmap --set-fmt-video=width=640,height=480,pixelformat="UYVY"
gst-launch-1.0 -v v4l2src device=/dev/video0 ! "video/x-raw, format=(string)UYVY, width=640, height=480" ! videoconvert ! autovideosink sync=false

v4l2-ctl -d /dev/video0 --stream-mmap --set-fmt-video=width=1280,height=1024,pixelformat="UYVY"
gst-launch-1.0 -v v4l2src device=/dev/video0 ! "video/x-raw, format=(string)UYVY, width=1280, height=1024" ! videoconvert ! autovideosink sync=false
</pre>

- RGB888
<pre>
v4l2-ctl -d /dev/video0 --stream-mmap --set-fmt-video=width=640,height=480,pixelformat="RGB3"
gst-launch-1.0 -v v4l2src device="/dev/video0" ! "video/x-raw, format=(string)RGB, width=640, height=480" ! videoconvert ! autovideosink sync=false

v4l2-ctl -d /dev/video0 --stream-mmap --set-fmt-video=width=1280,height=1024,pixelformat="RGB3"
gst-launch-1.0 -v v4l2src device="/dev/video0" ! "video/x-raw, format=(string)RGB, width=1280, height=1024" ! videoconvert ! autovideosink sync=false
</pre>

- Y16 : **not supported on Bullseye and Ubuntu 22.04.4**
<pre>
gst-launch-1.0 -v v4l2src device="/dev/video0" ! "video/x-raw, format=(string)GRAY16_BE, width=640, height=480" ! videoconvert ! autovideosink sync=false
gst-launch-1.0 -v v4l2src device="/dev/video0" ! "video/x-raw, format=(string)GRAY16_BE, width=1280, height=1024" ! videoconvert ! autovideosink sync=false
</pre>

### Dione cameras

- RGB888
<pre>
Bullseye
v4l2-ctl -d /dev/video0 --stream-mmap --set-fmt-video=width=640,height=480,pixelformat="BGR3"
gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=BGR,width=640,height=480 ! videoconvert ! ximagesink sync=false

v4l2-ctl -d /dev/video0 --stream-mmap --set-fmt-video=width=1280,height=1024,pixelformat="BGR3"
gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=BGR,width=1280,height=1024 ! videoconvert ! ximagesink sync=false

Bookworm, Ubuntu
gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=BGR,width=640,height=480 ! videoconvert ! autovideosink sync=false
gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=BGR,width=1280,height=1024 ! videoconvert ! autovideosink sync=false
</pre>

