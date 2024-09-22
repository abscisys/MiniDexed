#!/bin/bash

KERNEL_PATH=./sdcard
FIRMWARE_PATH=./sdcard

# Enable QEMU debug output
export QEMU_LOG="guest_errors,unimp"
export QEMU_LOG_FILENAME="qemu_log.txt"

qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a72 \
  -nographic \
  -smp 4 \
  -m 2G \
  -kernel ${KERNEL_PATH}/kernel8-rpi4.img \
  -dtb ${FIRMWARE_PATH}/bcm2711-rpi-4-b.dtb \
  -append "console=ttyAMA0 root=/dev/mmcblk0p2 rw rootwait earlycon=pl011,0x3f201000 debug loglevel=7 initcall_debug ignore_loglevel" \
  -device virtio-blk-device,drive=hd \
  -drive file=${KERNEL_PATH}/sdcard.img,if=none,format=raw,id=hd \
  -serial mon:stdio \
  -d cpu_reset,guest_errors,unimp \
  -D debug/qemu_log.txt \
  -trace events=debug/trace-events \
  -monitor telnet:127.0.0.1:55555,server,nowait \
  -gdb tcp::1234 \
  -S

# Note: Create a file named 'trace-events' in the same directory as this script
# with the events you want to trace, one per line. For example:
# aio_*
# virtio_*