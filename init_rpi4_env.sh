#!/bin/bash

sh -ex ./submod.sh

mkdir -p ./sdcard/

sed -i "s/Loading.../$(date +%Y%m%d)-$(git rev-parse --short HEAD)/g" src/userinterface.cpp

set -ex
wget -q https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf.tar.xz
tar xf arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf.tar.xz

set -ex
export PATH=$(readlink -f ./arm-*/bin/):$PATH
RPI=4 bash -ex build.sh
cp ./src/kernel*.img ./sdcard/

cd ./circle-stdlib/libs/circle/boot
make
make armstub64
cd -
cp -r ./circle-stdlib/libs/circle/boot/* sdcard
rm -rf sdcard/config*.txt sdcard/README sdcard/Makefile sdcard/armstub sdcard/COPYING.linux
cp ./src/config.txt ./src/minidexed.ini ./src/performance.ini sdcard/
cp ./getsysex.sh sdcard/
echo "usbspeed=full" > sdcard/cmdline.txt
# Performances
git clone https://github.com/Banana71/Soundplantage --depth 1
cp -r ./Soundplantage/performance ./Soundplantage/*.pdf ./sdcard/
# Hardware configuration
cd hwconfig
sh -ex ./customize.sh
cd -
mkdir -p ./sdcard/hardware/
cp -r ./hwconfig/minidexed_* ./sdcard/minidexed.ini ./sdcard/hardware/
# WLAN firmware
mkdir -p sdcard/firmware
cp circle-stdlib/libs/circle/addon/wlan/sample/hello_wlan/wpa_supplicant.conf sdcard/
cd sdcard/firmware
make -f ../../circle-stdlib/libs/circle/addon/wlan/firmware/Makefile
cd -
