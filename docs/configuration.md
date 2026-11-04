# Configuration

kmswitch works without any configuration. Everything below is optional: the defaults are used for anything
that is missing, misspelled or out of range.

The settings live in one plain-text file, **`KMSWITCH.TXT`**, in the root of the microSD card (the shared
drive that appears on the active PC). Edit it with any text editor (Notepad, TextEdit, gedit), or with the
visual editor **`KMSWITCH.HTM`** next to it (see [Visual editor](#visual-editor)).
The firmware reads the file at power-up and again whenever it changes. Problems (unknown key, bad value) are
written to **`KMSWITCH.LOG`** on the same drive; the line in question falls back to its default.

Files for the card: [`config/KMSWITCH.TXT`](../config/KMSWITCH.TXT) (all defaults, with comments) and
[`config/KMSWITCH.HTM`](../config/KMSWITCH.HTM) (visual editor). Copy both to the root of the card.

## Why the mouse needs settings, but not the screen resolution

The box reports the pointer to each PC as an **absolute** position (0 to 32767 on each axis), not as a
movement. The PC stretches that range over its whole desktop, so the box never needs to know how many pixels a
screen has, and edge switching works on any resolution and with several monitors per PC (the switch happens
at the outer edge of the whole desktop).

What does differ between PCs, and what the settings are for:

| Problem | Setting |
|---|---|
| Crossing a screen takes the same hand movement on every PC, so a big or high-resolution screen feels fast and coarse | `speed_x`, `speed_y` per PC |
| A 16:9 and a 21:9 (or portrait) screen give different horizontal and vertical speeds | `aspect` per PC |
| Which side PC 2 is on | `layout` |
| Screens of different height or mounted at different heights | `y_offset` per PC |
| Windows and macOS do not apply pointer acceleration to absolute devices | `accel` per PC (done in the firmware) |
| Games, Blender and remote-desktop tools that capture the mouse need relative movement | relative mode hotkey |

In the BIOS / boot menu none of this applies: the box falls back to a plain boot-protocol mouse and you
switch with the hotkey.

## File format

```
# comment
[section]
key = value      # comment after a value
```

- Section and key names are case-insensitive. Spaces around `=` are ignored.
- Unknown keys are ignored (and logged). A missing key uses its default.
- Numbers use a dot as decimal separator (`1.25`).
- Booleans: `on` / `off` (also `yes` / `no`, `true` / `false`, `1` / `0`).

### `[general]`

| Key | Values | Default | Meaning |
|---|---|---|---|
| `layout` | `right`, `left`, `above`, `below` | `right` | Where PC 2's screen is, seen from PC 1 |
| `edge_switch` | on / off | `on` | Switch by pushing the pointer past the edge |
| `edge_delay_ms` | 0 - 1000 | `0` | Pointer must stay at the edge this long before switching (stops accidental switches) |
| `edge_corner_block` | on / off | `on` | No switch while the pointer is in a screen corner (avoids clashes with hot corners / close buttons) |
| `start_on` | `pc1`, `pc2`, `last` | `pc1` | Active PC after power-up |
| `config_ui` | on / off | `on` | Allow the "open visual editor" hotkey |

### `[hotkeys]`

Every hotkey is a list of modifiers plus one key, joined with `+`. The box swallows a hotkey; the PC never sees it.
Set a hotkey to `none` to disable it.

| Key | Default | Action |
|---|---|---|
| `switch` | `WIN+F12` | Switch to the other PC |
| `relative_mode` | `WIN+F11` | Toggle relative mouse mode on the active PC (for games); edge switching pauses while it is on |
| `speed_up` | `WIN+SHIFT+UP` | Active PC: speed x 1.1 |
| `speed_down` | `WIN+SHIFT+DOWN` | Active PC: speed / 1.1 |
| `speed_reset` | `WIN+SHIFT+HOME` | Active PC: speed back to 1.0 |
| `open_config` | `WIN+F10` | Open the visual editor on the active PC (see below) |

Changes made with `speed_up` / `speed_down` / `speed_reset` are written back to `KMSWITCH.TXT` two seconds
after the last key press, so they survive a power cycle.

**Modifiers:** `CTRL`, `SHIFT`, `ALT`, `WIN` (the Windows key; Command on a Mac keyboard), plus
`RCTRL`, `RSHIFT`, `RALT`, `RWIN` for the right-hand keys only.
**Keys:** `A`-`Z`, `0`-`9`, `F1`-`F24`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `HOME`, `END`, `PGUP`, `PGDN`,
`INS`, `DEL`, `ESC`, `TAB`, `SPACE`, `ENTER`, `BACKSPACE`, `PAUSE`, `SCROLLLOCK`, `NUMLOCK`, `CAPSLOCK`,
`PRINTSCREEN`, `KP0`-`KP9`.
A hotkey with no modifier (for example `SCROLLLOCK`) is allowed; pick a key you do not otherwise use.

### `[pc1]` and `[pc2]`

| Key | Values | Default | Meaning |
|---|---|---|---|
| `name` | text | `PC 1` / `PC 2` | Shown in the visual editor and the log |
| `aspect` | `16:9`, `16:10`, `21:9`, `32:9`, `4:3`, `5:4`, `9:16`, or `W:H` | `16:9` | Shape of the PC's whole desktop; evens out horizontal and vertical speed |
| `speed_x` | 0.25 - 4.0 | `1.0` | Horizontal speed |
| `speed_y` | 0.25 - 4.0 | `1.0` | Vertical speed (already corrected for `aspect`; leave equal to `speed_x` normally) |
| `accel` | `off`, `low`, `medium`, `high` | `medium` | Pointer acceleration curve |
| `y_offset` | -50 - 50 (% of screen height) | `0` | Shifts where the pointer appears after crossing the edge (for `layout = left` / `right`); `x_offset` does the same for `above` / `below` |
| `x_offset` | -50 - 50 | `0` | See `y_offset` |
| `os` | `windows`, `mac`, `linux` | `windows` | Only used by the `open_config` hotkey |
| `kbd_layout` | `us`, `de`, `at`, `ch`, `fr`, `uk`, `it`, `es` | `us` | The keyboard layout set in that PC's OS; only used by `open_config` to type the launch command correctly |

**How the speed is applied.** For a mouse movement of `dx`, `dy` counts the firmware adds
`dx * base * speed_x * accel(v)` to the horizontal position and
`dy * base * speed_y * (W / H) * accel(v)` to the vertical one (with `aspect = W:H`), then clamps to 0 - 32767.
`base` is chosen so that with `speed = 1.0` an 800 DPI mouse crosses the screen in about 12 cm.
When the pointer crosses into the other PC, its position along the shared edge is carried over as a fraction
of the screen and then shifted by the target PC's `y_offset` (or `x_offset`).

## Visual editor

`KMSWITCH.HTM` is a single web page with no installation and no internet access. It runs in any current browser
on Windows, macOS, Linux and ChromeOS. It shows the two screens side by side as you set them up, with sliders for
speed and offset, drop-downs for the hotkeys, and a live preview of the resulting `KMSWITCH.TXT`.

- **Open:** double-click `KMSWITCH.HTM` on the kmswitch drive, then **Open KMSWITCH.TXT** and pick the file on the
  same drive.
- **Save:** in Chrome, Edge, Opera and Brave the page writes straight back to the file. In Firefox and Safari it
  downloads a new `KMSWITCH.TXT`; copy it onto the kmswitch drive, replacing the old one.

### Opening it with a hotkey (`open_config`, default `WIN+F10`)

The box is a keyboard, so on this hotkey it types a short launch command on the active PC. That is why it needs
to know the PC's `os` and `kbd_layout`; a wrong layout types wrong characters. The SD card's volume label must be
`KMSWITCH` (the firmware formats new cards that way).

| `os` | What the box types |
|---|---|
| `windows` | `Win+R`, then `powershell -w h -c "ii ((Get-Volume -FileSystemLabel KMSWITCH).DriveLetter+':\KMSWITCH.HTM')"` and Enter |
| `mac` | `Cmd+Space`, `Terminal`, Enter, then `open /Volumes/KMSWITCH/KMSWITCH.HTM; exit` and Enter |
| `linux` | `Ctrl+Alt+T`, then `xdg-open "$(findmnt -nro TARGET -S LABEL=KMSWITCH)/KMSWITCH.HTM"; exit` and Enter |

This is best effort: a busy PC, an unusual desktop or a locked-down Run dialog can stop it. Opening
`KMSWITCH.HTM` from the drive by hand always works. Set `config_ui = off` if you do not want the box to type
commands at all.

## Example

```
[general]
layout = right
edge_delay_ms = 150

[hotkeys]
switch = SCROLLLOCK
speed_up = CTRL+ALT+PGUP
speed_down = CTRL+ALT+PGDN

[pc1]
name = Office laptop
aspect = 16:10
speed_x = 0.9
speed_y = 0.9

[pc2]
name = Desktop (4K)
aspect = 21:9
speed_x = 1.3
speed_y = 1.3
y_offset = -10
os = windows
kbd_layout = de
```
