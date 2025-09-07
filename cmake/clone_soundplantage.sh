#!/bin/bash
set -e
TARGET_DIR="$1"

if [ ! -d "$TARGET_DIR/Soundplantage" ]; then
    git clone https://github.com/Banana71/Soundplantage --depth 1 "$TARGET_DIR/Soundplantage"
else
    echo "Soundplantage already exists"
fi
