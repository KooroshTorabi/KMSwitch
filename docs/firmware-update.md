# Firmware updates

The box can be updated in three ways. The settings are kept in every case, because they live on the microSD
card, not in the firmware.

| Way | When | What the user does |
|---|---|---|
| [From the settings page](#1-from-the-settings-page) | normal case | click **Update**, press **Win+F8** on the box's keyboard |
| [From the drive](#2-from-the-drive) | no USB network | copy the update file to the kmswitch drive, press **Win+F8** |
| [BOOTSEL recovery](#3-bootsel-recovery) | the firmware does not start | hold BOOT with a pin, drag a file onto `RPI-RP2` |

The first two update **both** microcontrollers from one PC: MCU-A passes MCU-B's part over the isolated UART.
Both are protected the same way: the box checks a digital signature, asks for a key press on its own keyboard,
and keeps the previous version until the new one has started correctly.

## 1. From the settings page

Open the settings page (`Win+F10`, or `http://192.168.7.1`). The **Firmware** card shows the installed version
and the board variant.

1. If the PC has internet access, the page asks the project's GitHub repository whether there is a newer
   version ([`firmware/latest.json`](../firmware/latest.json), read directly by the browser) and offers
   **Update to x.y.z**. Without internet, download the update file (`kmswitch-A-x.y.z.kmsfw` or
   `kmswitch-B-x.y.z.kmsfw`, matching your board) on any computer and use **Install from file**.
2. The page sends the file to the box. The box checks that it is a kmswitch update, for this board variant,
   with a valid signature. A file that fails any check is rejected and nothing changes.
3. **Confirm on the box's keyboard.** The LEDs blink orange and the page shows *Press Win+F8 on the keyboard
   connected to kmswitch*. Press it within 60 seconds (`Esc` cancels). The box only accepts this key press from
   the keyboard plugged into its own USB-A port, so a program on the PC cannot confirm an update by itself.
4. The box installs MCU-B's part first, then MCU-A's (LEDs show the progress, about 10 to 20 seconds), restarts,
   and the page reconnects and shows the new version. Keyboard and mouse are unavailable during this time.

## 2. From the drive

Copy the update file (`*.kmsfw`) to the root of the kmswitch drive (the microSD card). Within a few seconds the
box notices it, runs the same checks and blinks orange; confirm with **Win+F8** as above. After a successful
update the box deletes the file; a rejected file is renamed to `*.rejected` and the reason is written to
`KMSWITCH.LOG`.

## 3. BOOTSEL recovery

The RP2040's USB bootloader is in its mask ROM and cannot be erased, so the box can always be recovered.
Every release also contains plain `.uf2` files for this case:
`kmswitch-<A|B>-pc1-x.y.z.uf2` (MCU-A) and `kmswitch-<A|B>-pc2-x.y.z.uf2` (MCU-B).

1. Unplug the PC's cable from the port of the MCU you want to flash (PC 1 port = MCU-A, PC 2 port = MCU-B).
2. Hold that MCU's **BOOT** button and plug the cable back in (or, with the cable in, hold BOOT and tap RESET).
   - Variant B: pinholes in the lid, labelled on the board as BOOT / RST next to each MCU.
   - Variant A: two pinholes next to each RP2040-Zero's USB-C, over the module's BOOT and RESET buttons
     (check the module's silkscreen for which is which).
3. A drive called `RPI-RP2` appears. Copy the matching `.uf2` onto it; the MCU restarts with the new firmware.
4. Repeat for the other MCU from the other PC.

If the firmware still runs, **Ctrl+Alt+Shift+F8 held for 3 seconds** on the box's keyboard restarts the active
PC's MCU straight into BOOTSEL, without the pin.

## How it is built (for firmware developers)

### Flash layout (2 MB, both variants)

| Offset | Size | Content |
|---|---|---|
| `0x000000` | 256 B | RP2040 boot2 (flash setup, required by the ROM) |
| `0x000100` | 32 KB - 256 B | kmswitch bootloader |
| `0x008000` | 4 KB | boot state, copy 1 (with a CRC) |
| `0x009000` | 4 KB | boot state, copy 2 (the newer valid copy wins; a write always replaces the older one) |
| `0x00A000` | 1000 KB | slot 0 (application) |
| `0x104000` | 1000 KB | slot 1 (application) |
| `0x1FE000` | 8 KB | reserved |

Every application is built twice, linked for slot 0 and for slot 1, and the bundle carries both builds; an update
writes the build for whichever slot is not running. The vector table sits 256 bytes into the slot.

### Boot and roll-back

The boot state holds the active slot, a pending slot and a try counter.

1. An update writes the inactive slot, checks its SHA-256 against the bundle, and marks it *pending* with
   one try.
2. The bootloader starts a pending slot once and decrements the counter before jumping.
3. The new firmware marks itself *good* only when it is healthy: USB enumerated or the UART link to the
   other MCU up, within 10 seconds.
4. If it crashes, hangs (watchdog) or never confirms, the next boot finds the counter at zero and goes back to the
   previous slot. A power cut at any point leaves either the old or the new firmware bootable.

The bootloader also checks the image's SHA-256 (stored in the boot state at install time) before it jumps, and
arms the watchdog (8 s) for the trial start; the new firmware turns it off when it confirms.

MCU-A and MCU-B check each other's firmware version over the UART at start-up. If they differ (for example the
power failed between the two parts of an update), MCU-A re-sends MCU-B's part from the copy of the last installed
bundle it keeps on the microSD card (`/.kmswitch/fw/current.kmsfw`), so the pair always ends up matching.
An update bundle is received onto the card first (`/.kmswitch/fw/incoming.kmsfw`), so the drive is not shown to
the PCs while an update runs.

### Update bundle (`.kmsfw`)

A 4096-byte header (little-endian), then the images, each padded to 4 KB:

```
0     magic      "KMSFW1\0\0"                                     8 bytes
8     board      "A" or "B", padded to 4                          4 bytes
12    version    major, minor, patch, 0                           4 x uint16
20    flags      bit 0: allow downgrade                           uint32
24    count      number of images (4)                             uint32
28    images     4 x { role u8 (0 = MCU-A, 1 = MCU-B), slot u8, 0 u16, offset u32, length u32, SHA-256 }
...   zero padding
4032  signature  Ed25519 over bytes 0-4031 (they hold every image's SHA-256, so the signature covers the images)
```

`firmware/tools/kmsfw.py` makes the key pair (`keygen`), the bundle (`pack`) and the recovery `.uf2` files (`uf2`).
The release signing key is kept offline by the maintainer; the firmware contains only the public key
(`firmware/keys/release.pub`, compiled in). A build without that file is a development build (`dev` on the
settings page) and accepts no update bundles; flash it through BOOTSEL.

A bundle for the wrong board, with a bad hash or a bad signature is rejected before anything is written. An
older version is refused unless its `allow downgrade` flag is set (used to publish a deliberate roll-back).

### Release files

| File | Used by |
|---|---|
| `firmware/latest.json` | settings page update check (`version`, `date`, `notes`, per board: `url`, `sha256`, `size`) |
| `kmswitch-A-x.y.z.kmsfw`, `kmswitch-B-x.y.z.kmsfw` | ways 1 and 2 |
| `kmswitch-A-pc1-x.y.z.uf2`, `kmswitch-A-pc2-x.y.z.uf2`, and the same for B | way 3 (full image: boot2, bootloader, application in slot 0) |

The `.kmsfw` files are committed under `firmware/x.y.z/` so the settings page can download them from
`raw.githubusercontent.com`, which allows cross-origin reads.
