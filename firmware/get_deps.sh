#!/bin/sh
# Fetch the firmware dependencies into firmware/deps (pinned versions).
set -e
cd "$(dirname "$0")"
mkdir -p deps
cd deps
if [ ! -d pico-sdk ]; then
    git clone --depth 1 -b 2.1.1 https://github.com/raspberrypi/pico-sdk.git
    (cd pico-sdk && git submodule update --init --depth 1 lib/tinyusb lib/lwip)
fi
if [ ! -d Pico-PIO-USB ]; then
    git clone https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
    (cd Pico-PIO-USB && git checkout -q fe9133fc513b82cc3dc62c67cb51f2339cf29ef7)   # tested with TinyUSB 0.18
fi
echo "dependencies ready in $(pwd)"
