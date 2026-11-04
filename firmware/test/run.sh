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
for t in config layout link hid typing; do /tmp/kms_t_$t; done
