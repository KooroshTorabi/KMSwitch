#!/bin/sh
# Host unit tests for the portable firmware logic (settings file, pointer engine, hotkeys, link, HID parser, typing).
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
FLAGS="-Wall -Wextra -O1 -fsanitize=address,undefined -fno-omit-frame-pointer"
C=../src/core
$CC $FLAGS -o /tmp/kms_t_config t_config.c $C/kms_config.c -lm
$CC $FLAGS -o /tmp/kms_t_layout t_layout.c $C/kms_layout.c $C/kms_config.c $C/kms_hotkey.c -lm
$CC $FLAGS -o /tmp/kms_t_link t_link.c $C/kms_link.c
$CC $FLAGS -o /tmp/kms_t_hid t_hid.c $C/kms_hidparse.c
$CC $FLAGS -o /tmp/kms_t_typing t_typing.c $C/kms_typing.c
$CC $FLAGS -o /tmp/kms_t_fw t_fw.c $C/kms_fw.c ../src/crypto/monocypher.c ../src/crypto/monocypher-ed25519.c
for t in config layout link hid typing; do /tmp/kms_t_$t; done
# a signed bundle from the release tool, checked by the firmware code
python3 ../tools/kmsfw.py keygen /tmp/kms_test_key > /dev/null
head -c 3000 /dev/urandom > /tmp/kms_b0.bin; head -c 5000 /dev/urandom > /tmp/kms_b1.bin
python3 ../tools/kmsfw.py pack --board B --version 1.2.0 --key /tmp/kms_test_key -o /tmp/kms_test.kmsfw \
    pc2:0=/tmp/kms_b0.bin pc2:1=/tmp/kms_b1.bin > /dev/null
/tmp/kms_t_fw /tmp/kms_test.kmsfw /tmp/kms_test_key.pub
