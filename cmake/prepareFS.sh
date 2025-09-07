#!/bin/bash

BASE_DIRECTORY=$1
IMAGE_NAME=$2
RPI=$3
TARGET=$4
QEMU_SUPPORT=$5
FULL_DIRECTORY_PATH=${BASE_DIRECTORY}/${IMAGE_NAME}
FS_IMAGE_PATH=${FULL_DIRECTORY_PATH}.img
QEMU_SCRIPT=${BASE_DIRECTORY}/simulateQEMU.sh
DEBUG_SCRIPT=${BASE_DIRECTORY}/debugQEMU.sh

if [[ "${QEMU_SUPPORT}" == "ON" ]]; then

echo "Preparing file system image for QEMU..."

# Remove any previously created sdcard.img file
if [[ -e ${FS_IMAGE_PATH} ]]; then
    rm -f ${FS_IMAGE_PATH}
fi

# Create new file system image of 128 MB
dd if=/dev/zero of=${FS_IMAGE_PATH} bs=1M count=128

# Format the new file system using FAT32
mkfs.vfat ${FS_IMAGE_PATH}

# Creating an .mtoolsrc file
echo "drive c: file=\"${FS_IMAGE_PATH}\"" > ~/.mtoolsrc

# Copy all files from the original raw directory
echo "Copying files into the ${FS_IMAGE_PATH} image:"
for file in ${FULL_DIRECTORY_PATH}/*; do
    echo "  + ${file#${FULL_DIRECTORY_PATH}/}"
    mcopy "${file}" c:
done

echo "Configuring QEMU simulation script ($(basename ${QEMU_SCRIPT}))..."

# Define the supported QEMU machines and CPUs based on RPI version
if [[ ${RPI} == 1 ]]; then
    QEMU_MACHINE="raspi0"
    QEMU_CPU="arm1176"  # BCM2835 uses ARM1176JZF-S
    QEMU_MEMORY="1G"
    DTD=bcm2710-rpi-zero-2-w.dtb
elif [[ ${RPI} == 2 ]]; then
    QEMU_MACHINE="raspi2b"
    QEMU_CPU="cortex-a7"  # BCM2836 uses Cortex-A7
    QEMU_MEMORY="1G"
    DTD=bcm2710-rpi-zero-2-w.dtb
elif [[ ${RPI} == 3 ]]; then
    QEMU_MACHINE="raspi3b"
    QEMU_CPU="cortex-a53"  # BCM2837 uses Cortex-A53
    QEMU_MEMORY="1G"
    DTD=bcm2710-rpi-zero-2-w.dtb
else
    echo "QEMU only supports Raspberry Pi versions 1, 2, and 3"
    exit 1
fi
(
    echo "#!/bin/bash"
    echo ""
    echo "# QEMU simulation script for MiniDexed"
    echo "# Usage: ./simulateQemu.sh [--debug]"
    echo ""
    echo "trap 'echo \"Stopping QEMU...\"; exit 0' INT"
    echo ""
    echo "DEBUG_MODE=0"
    echo "if [ \"\$1\" = \"--debug\" ]; then"
    echo "    DEBUG_MODE=1"
    echo "    echo \"Starting QEMU in debug mode...\""
    echo "fi"
    echo ""
    echo "QEMU_CMD=\"qemu-system-aarch64 \\"
    echo "    -M ${QEMU_MACHINE} \\"
    echo "    -cpu ${QEMU_CPU} \\"
    echo "    -m ${QEMU_MEMORY} \\"
    echo "    -kernel ${FULL_DIRECTORY_PATH}/${TARGET}.img \\"
    # echo "    -dtb ${FULL_DIRECTORY_PATH}/${DTD} \\"
    echo "    -nographic \\"
    echo "    -d unimp,guest_errors,in_asm \\"
    echo "    -D qemu.log \\"
    echo "    -append \\\"console=ttyAMA0,115200 8250.nr_uarts=1 earlycon=pl011,0x3f201000\\\" \\"
    echo "    -drive file=${FS_IMAGE_PATH},if=sd,format=raw \\"
    echo "    -serial mon:stdio \\"
    echo "    \""
    echo ""
    echo "if [[ \"\$DEBUG_MODE\" == \"1\" ]]; then"
    echo "    QEMU_CMD=\"\$QEMU_CMD -s -S\""
    echo "    echo \"Connect GDB to localhost:1234\""
    echo "fi"
    echo ""
    echo "echo \"Executing QEMU (Ctrl+a + x to quit): \$QEMU_CMD\""
    echo "eval \"\$QEMU_CMD\""
    echo ""
) > ${QEMU_SCRIPT}
chmod +x ${QEMU_SCRIPT}

(
    echo "#!/bin/bash"
    echo ""
    echo "# Debug of QEMU simulation script for MiniDexed"
    echo "# Usage: ./debugQemu.sh"
    echo ""
    echo "echo \"Starting GDB...\""
    echo "gdb-multiarch -ex \"target remote localhost:1234\" -ex \"b *0x80000\" ${FULL_DIRECTORY_PATH}/${TARGET}.elf"
    echo ""
) > ${DEBUG_SCRIPT}
chmod +x ${DEBUG_SCRIPT}

fi
