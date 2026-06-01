This document present how to build and install the MIPI drivers of Exosens cameras for Raspberry Pi.

The MIPI_deployment.xlsx sheet presents an overview of the supported cameras/boards/OS versions.

## Building environment

### 0. Environment used with this building environment

- Rpi model : Raspberry Pi 4 Model B Rev 1.5
- Host computer Ubuntu 20.04.1 LTS
- Cross compiler gcc-9-aarch64-linux-gnu

Note about vanilla Rpi OS cross compiler versions :
- Bookworm 12 was compiled with gcc-12 (12.2.0-14)
- Bullseye 11 was compiled with gcc-8 (8.4.0)

### 1. Building MIPI driver for RPi OS Bookworm
For this RPi OS version, the Linux kernel is not built from the Raspberry Pi Linux repository, but is a Debian package.
<pre>
cat /etc/os-release
PRETTY_NAME="Debian GNU/Linux 12 (bookworm)"

uname -a
Linux pi <b>6.6.20+rpt-rpi-v8</b> #1 SMP PREEMPT Debian 1:<b>6.6.20-1+rpt1</b> (2024-03-07) aarch64 GNU/Linux
</pre>

The 6.6.20+rpt-rpi-v8 Linux version is not available for download and cross compilation on a host. So the MIPI driver hast to be built out of tree on the Raspberry Pi.
The corresponding kernel headers are installed by default with Bookworm.

- Be carefull that the **sources** folder is clean of object files (*.o, *.ko) and **linux_install** folder
- Copy the **sources** folder to the Raspberry Pi
- Log to the Raspberry Pi, go to the **sources** folder and build/install the drivers :
<pre>
./build.sh make
./build.sh install
</pre>
- Configure the cameras — see [Camera configuration](#camera-configuration) section below
- Reboot the RPi

### 2. Building MIPI driver for RPi OS Bullseye

For RPI OS Bullseye, complete Linux kernel must be built from scratch. This is because the last kernel headers for Bullseye (6.1.21) are not available in the packages repository.
The Raspberry Pi linux (branch rpi-6.1.y) can be built :
- on a Raspberry Pi 4 (it takes around 2 hours and a half, it needs 6.5 GB of storage)
- or on a host computer with a cross compiler (much faster)

Note : the original kernel Image and modules version 6.1.21-v8+ are replaced by a 6.1.21-v8-eg version.
<pre>
$ uname -r
6.1.21-v8-eg
</pre>

#### Building on a Raspberry Pi 4

- The following packages have to be installed with apt : git gcc make flex bison libssl-dev
- Copy the the current folder to the Raspberry Pi
- Log to the Raspberry Pi and go to the current folder

- Install the RPi Linux environment :
<pre>
./install_env.sh bullseye rpi4
</pre>

- Compile Linux and the MIPI drivers :
<pre>
./compile_linux.sh bullseye rpi4
</pre>

- Install it :
<pre>
./install_sources.sh bullseye rpi4
</pre>

#### Building on a host computer

- Install the RPi Linux environment :
<pre>
./install_env.sh bullseye rpi4
</pre>

- Compile Linux and the MIPI drivers :
<pre>
./compile_linux.sh bullseye rpi4
</pre>

- Install the Linux build in the **sources** folder :
<pre>
./install_sources.sh bullseye rpi4
</pre>

- Copy the **sources** folder to the Raspberry Pi
- Log to the Raspberry Pi, go to the **sources** folder and install it :
<pre>
./build.sh install
</pre>

#### Raspberry configuration

- Configure the cameras — see [Camera configuration](#camera-configuration) section below
- Reboot the RPi

Note : it is possible to clean the **sources** folder with this command
<pre>
./clean_sources.sh bullseye rpi4
</pre>

### 3. Building MIPI driver for RPi OS Ubuntu

**Note :**
- Ubuntu 22.04.4 LTS \
Linux git repo : https://git.launchpad.net/~ubuntu-kernel/ubuntu/+source/linux-raspi/+git/jammy \
git tag : Ubuntu-raspi-5.15.0-1046.49
- Ubuntu 23.10 \
Linux git repo : https://git.launchpad.net/~ubuntu-kernel/ubuntu/+source/linux-raspi/+git/mantic \
git tag : Ubuntu-raspi-6.5.0-1005.7
- Ubuntu 24.04 LTS \
Linux git repo : https://git.launchpad.net/~ubuntu-kernel/ubuntu/+source/linux-raspi/+git/noble \
git tag : Ubuntu-raspi-6.8.0-1001.1

**Steps :**
- Install packages for build : 
<pre>
sudo apt install gcc make flex bison libssl-dev
</pre>
- Install Linux headers if needed : 
<pre>
sudo apt install linux-headers-$(uname -r)
</pre>
- Be carefull that the **sources** folder is clean of object files (*.o, *.ko) and **linux_install** folder
- Copy the **sources** folder to the Raspberry Pi
- Log to the Raspberry Pi, go to the **sources** folder and build/install the drivers :
<pre>
./build.sh make
./build.sh install
</pre>
- Configure the cameras — see [Camera configuration](#camera-configuration) section below
- Reboot the RPi

### 4. Building MIPI driver and Linux from scratch for other RPI OS versions

- Find the right branch and commit at https://github.com/raspberrypi/linux.git
- Modify **install_env.sh** :
<pre>
git clone -b <b>$your_branch</b> https://github.com/raspberrypi/linux.git ${LINUX_RPI_SRC}
pushd ${LINUX_RPI_SRC}
git reset --hard <b>$your_commit</b>
popd
</pre>
- Go to chapter 2.

**Note : code in "sources" folder may not compile because of incompatible Linux version**

## Camera configuration

Edit the config file before rebooting:
- **Bookworm / Ubuntu:** `/boot/firmware/config.txt`
- **Bullseye:** `/boot/config.txt`

### RPi4 — single camera port

The RPi4 has one CSI camera port. Connect the camera and enable the corresponding overlay.

<pre>
# EngineCore (eg-ec-mipi)
dtoverlay=eg-ec-mipi
# Optional: 2 MIPI lanes (1 lane by default)
#dtparam=2lanes
# Optional: I2C address (0x16 by default)
#dtparam=i2c-addr=0x16

# Dione IR
#dtoverlay=dione-ir
</pre>

Only one `dtoverlay` should be active at a time on RPi4.

### RPi5 — two camera ports (CAM0 and CAM1)

The RPi5 has two independent CSI camera ports. Each port is selected with the `cam0` or `cam1` dtparam.
The default (no `cam0`/`cam1` param) is CAM1, which preserves RPi4 compatibility.

**Single camera on CAM1 (default):**
<pre>
dtoverlay=eg-ec-mipi
</pre>

**Single camera on CAM0:**
<pre>
dtoverlay=eg-ec-mipi,cam0
</pre>

**Two cameras of different types:**
<pre>
dtoverlay=eg-ec-mipi,cam0
dtoverlay=dione-ir,cam1
</pre>

**Two cameras of the same type, one with 1 MIPI lane (MicroCube) on cam0, one with 2 MIPI lanes (Crius1280) on cam1:**
<pre>
dtoverlay=eg-ec-mipi,cam0
dtoverlay=eg-ec-mipi,cam1,2lanes
</pre>

**Two Dione cameras:**
<pre>
dtoverlay=dione-ir,cam0
dtoverlay=dione-ir,cam1
</pre>

**Available dtparam per overlay:**

| Overlay | `cam0` | `cam1` | `2lanes` |
|---------|--------|--------|----------|
| eg-ec-mipi | ✓ | ✓ | ✓ |
| dione-ir | ✓ | ✓ | |

## To grab video on the target

**RPi4:** the video device is always `/dev/video0`.

**RPi5:** the video device index depends on which ports are active and the driver probe order.
Use the provided script to retrieve the video device and the associated I2C character device for each port:
<pre>
# CAM1 (default)
$ eg_get_video_device_rpi5.sh
/dev/video8
/dev/eg-ec-i2c-11-0016

# CAM0
$ eg_get_video_device_rpi5.sh 0
/dev/video16
/dev/eg-ec-i2c-10-0016
</pre>

The script outputs two lines: the V4L2 capture device and the I2C character device exposed by the camera driver (used for direct register access).

Refer to `sources/streaming_examples.txt` for capture and streaming commands.
