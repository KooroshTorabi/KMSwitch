# Firmware

One source tree. For each board variant (`A`, `B`) there is a firmware for MCU-A (the PC 1 side: USB host,
router, microSD, settings) and one for MCU-B (the PC 2 side), plus a small bootloader shared by all.

| File | Flash it onto |
|---|---|
| `kmswitch-A-pc1-x.y.z.uf2` / `kmswitch-B-pc1-x.y.z.uf2` | MCU-A: the RP2040 whose USB goes to PC 1 |
| `kmswitch-A-pc2-x.y.z.uf2` / `kmswitch-B-pc2-x.y.z.uf2` | MCU-B: the RP2040 whose USB goes to PC 2 |
| `kmswitch-A-x.y.z.kmsfw` / `kmswitch-B-x.y.z.kmsfw` | later updates of both MCUs at once (settings page or drive) |

Each `.uf2` holds the bootloader, the firmware in slot 0 and an empty boot state.

First flash: hold the MCU's **BOOT** button, plug in its PC cable (or tap **RESET**), and copy the `.uf2` onto the
`RPI-RP2` drive. See [docs/firmware-update.md](../docs/firmware-update.md) for later updates.

## Build

Needs `cmake` (3.13+), `ninja` or `make`, and the Arm GNU toolchain (`arm-none-eabi-gcc`).

```sh
cd firmware
./get_deps.sh                      # pico-sdk 2.1.1 (TinyUSB 0.18, lwIP) and Pico-PIO-USB into deps/
cmake -S . -B build -G Ninja -DKMS_VERSION=1.0.0
ninja -C build                     # build/kmswitch-{A,B}-{pc1,pc2}-1.0.0.uf2
test/run.sh                        # host unit tests of the portable logic (gcc, sanitizers)
```

### Releases (maintainer)

```sh
python3 tools/kmsfw.py keygen ~/kmswitch-release.key     # once; keep the .key file offline and backed up
cp ~/kmswitch-release.key.pub keys/release.pub            # commit this public key
cmake -S . -B build -G Ninja -DKMS_VERSION=1.0.0 -DKMS_SIGN_KEY=$HOME/kmswitch-release.key
ninja -C build                     # also build/kmswitch-{A,B}-1.0.0.kmsfw (signed bundles)
```

Then copy the `.uf2` and `.kmsfw` files to `firmware/1.0.0/` and fill in `latest.json` (sha256 and size of each
bundle). Without `keys/release.pub` the build is a development build: it runs normally but refuses every update
bundle (flash new development builds through BOOTSEL).

## Layout

| Path | What |
|---|---|
| `src/core/` | portable logic with host tests: settings file (`kms_config`), pointer engine and edge switching (`kms_layout`), hotkeys, link framing, HID report descriptor parser, typing commands for each keyboard layout |
| `src/app_a.c` | MCU-A: PIO-USB host on core 1; router, hotkeys, pointer, settings on core 0 |
| `src/app_b.c` | MCU-B: USB device for PC 2, fed over the isolated UART; the drive is proxied block by block to MCU-A |
| `src/pc_port.c`, `src/usb_desc.c` | USB device towards the own PC: boot keyboard, mouse (relative and absolute report), mass storage |
| `src/storage.c`, `src/sd_spi.c`, `src/fatfs/` | microSD over SPI, FatFs (exFAT enabled), `KMSWITCH.TXT`, safe writes while a PC has the drive |
| `src/led.c` | WS2812 on the RP2040-Zero (A) or the red / green / blue LEDs (B), and the optional RGB header |
| `src/link_io.c` | UART at 2 Mbaud, COBS frames with CRC-16 |
| `src/net/`, `src/web_api.c` | USB network (RNDIS / CDC-ECM, lwIP, DHCP), HTTP server, the settings page API and the screen measurement |
| `src/update.c`, `src/bootctl.c`, `boot/` | signed updates, A/B slots, the bootloader; `tools/kmsfw.py` makes keys, bundles and recovery files |
| `src/crypto/` | Monocypher (Ed25519 signature check) |

## Status

| Part | State |
|---|---|
| Keyboard and mouse routing, hotkeys, Win/Alt mask key, edge switching with monitors and arrangement, speed and acceleration, relative mode, BIOS (boot protocol) | done |
| `KMSWITCH.TXT` read at power-up and after the PC edits it; speed hotkeys written back | done |
| Shared drive for the active PC (directly on PC 1, over the link on PC 2) | done |
| `Ctrl+Alt+Shift+F8` held 3 s: BOOTSEL of the active PC's MCU | done |
| USB network, settings page served by the box, web API, automatic measurement | done |
| Bootloader, signed `.kmsfw` updates (page and drive), confirmation with Win+F8, roll-back, MCU-B re-sync | done |

Everything above builds and the portable parts pass their unit tests; the firmware has not run on a real board
yet. Expect a first round of fixes when the boards arrive.

`start_on = last` remembers the PC across resets; after a power cut the box starts on PC 1 (the flash is not
written on every switch, so a switch never stalls the USB host).

## Release files

- `latest.json`: describes the newest release; the settings page reads it to offer updates. Format:

```json
{
  "version": "1.2.0",
  "date": "2027-01-15",
  "notes": "Short text shown on the settings page",
  "files": {
    "A": { "url": "https://raw.githubusercontent.com/KooroshTorabi/KMSwitch/main/firmware/1.2.0/kmswitch-A-1.2.0.kmsfw", "sha256": "…", "size": 412345 },
    "B": { "url": "https://raw.githubusercontent.com/KooroshTorabi/KMSwitch/main/firmware/1.2.0/kmswitch-B-1.2.0.kmsfw", "sha256": "…", "size": 412345 }
  }
}
```

- `x.y.z/`: the signed update bundles (`.kmsfw`) and the BOOTSEL recovery images (`.uf2`) of each release.
