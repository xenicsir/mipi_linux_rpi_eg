This document present how to build and install the MIPI drivers of Exosens cameras for Raspberry Pi.

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
- Customize /boot/firmware/config.txt :
<pre>
# Uncomment the following line to enable EngineCore camera
#dtoverlay=eg-ec-mipi
# Uncomment the following line for EngineCore 2 MIPI lanes. 1 lane by default.
#dtparam=2lanes
# Uncomment the following line to modify the EngineCore I2C address. 0x16 by default.
#dtparam=i2c-addr=0x16
# Uncomment the following line to enable Dione camera
#dtoverlay=dione-ir
</pre>

- Reboot the RPi

### 2. Building MIPI driver for RPi OS Bullseye

For RPI OS Bullseye, complete Linux kernel must be built from scratch. This is because the last kernel headers for Bullseye (6.1.21) are not available in the packages repository.
The Raspberry Pi linux (branch rpi-6.1.y) can be built :
- on a Raspberry Pi 4 (it takes around 2 hours and a half, it needs 6.5 GB of storage)
- or on a host computer with a cross compiler (much faster)

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

- Customize /boot/config.txt :
<pre>
# Uncomment the following line to enable EngineCore camera
#dtoverlay=eg-ec-mipi
# Uncomment the following line for EngineCore with 2 MIPI lanes. 1 lane by default.
#dtparam=2lanes
# Uncomment the following line to modify the EngineCore I2C address. 0x16 by default.
#dtparam=i2c-addr=0x16
# Uncomment the following line to enable Dione camera
#dtoverlay=dione-ir
</pre>

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
- The following packages have to be installed with apt : 
<pre>
sudo apt install gcc make flex bison libssl-dev
</pre>
- install Linux header if needed : 
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
- Customize /boot/firmware/config.txt :
<pre>
# Uncomment the following line to enable EngineCore camera
#dtoverlay=eg-ec-mipi
# Uncomment the following line for EngineCore 2 MIPI lanes. 1 lane by default.
#dtparam=2lanes
# Uncomment the following line to modify the EngineCore I2C address. 0x16 by default.
#dtparam=i2c-addr=0x16
# Uncomment the following line to enable Dione camera
#dtoverlay=dione-ir
</pre>

- Reboot the RPi

### 4. Building MIPI driver and Linux from scratch for RPI OS with other RPi Linux versions

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

## To grab video on the target

Refer to sources/README.md file.
