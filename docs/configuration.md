# Configuration

kmswitch works without any configuration. Everything below is optional: the defaults are used for anything
that is missing, misspelled or out of range.

There are two ways to change the settings, and both end up in the same place:

1. **The settings page served by the box itself** (recommended). Press **Win+F10**, or open
   `http://192.168.7.1` in any browser. Changes are saved on the box automatically; there is no file to pick,
   copy or overwrite. The page also shows which PCs are connected and measures their screens by itself.
2. **The settings file** `KMSWITCH.TXT` in the root of the microSD card (the shared drive). Edit it with any text
   editor, or with the offline copy of the settings page, `KMSWITCH.HTM`, on the same drive. This is the fallback
   for PCs where the box's USB network is blocked or switched off.

The firmware reads `KMSWITCH.TXT` at power-up and whenever it changes, and the settings page writes to the same
file through the box. Problems (unknown key, bad value) are written to `KMSWITCH.LOG` on the same drive; the
line in question falls back to its default.

Files for the card: [`config/KMSWITCH.TXT`](../config/KMSWITCH.TXT) (all defaults, with comments) and
[`config/KMSWITCH.HTM`](../config/KMSWITCH.HTM) (the settings page). The firmware puts both on a new card
by itself; copy them only if you prepare a card by hand. The protocol between the page and the box is in
[web-api.md](web-api.md).

## Why the mouse needs settings, but no screen resolution

The box reports the pointer to each PC as an **absolute** position (0 to 32767 on each axis), not as a
movement. The PC stretches that range over its whole desktop, so the pointer always lands where the box thinks
it is, on any resolution and with any number of monitors.

What does differ between PCs, and what the settings are for:

| Problem | Handled by |
|---|---|
| Crossing a screen takes the same hand movement on every PC, so a big or high-resolution screen feels fast and coarse | measured screen size, plus `speed_x` / `speed_y` per PC |
| Screens of different shape (16:9, 16:10, 21:9, portrait) give different horizontal and vertical speeds | measured monitors (or `aspect` if not measured) |
| Two monitors of different size on one PC leave "holes" in the desktop that the pointer cannot enter | measured monitors: the box keeps its pointer inside the real screens |
| Where PC 2's screens are on the desk, and where the pointer may cross | `arrangement` (drag PC 2 on the settings page) |
| Windows and macOS do not apply pointer acceleration to absolute devices | `accel` per PC (done in the firmware) |
| Games, Blender and remote-desktop tools that capture the mouse need relative movement | relative mode hotkey |

In the BIOS / boot menu none of this applies: the box falls back to a plain boot-protocol mouse and you
switch with the hotkey.

## The settings page

The box adds a small USB network adapter to each PC (CDC-NCM, with RNDIS for Windows 10). It is a private
link between that PC and the box only: no internet, no gateway, no change to the PC's normal network.
The box answers at **`http://192.168.7.1`** on both PCs and serves the settings page from its own flash.

What the page does that a file cannot:

- **Saves by itself.** Every change goes to the box about a second after you make it ("Saved" appears in the
  header). The box writes `KMSWITCH.TXT`, so the file and the page never disagree.
- **Shows what is connected, live.** A PC whose cable is not plugged in (or that is switched off) is drawn grey
  and marked *not connected*; its settings stay visible but locked. The moment it is plugged in, it turns active,
  without reloading the page.
- **Shows the real screens.** Each PC is drawn with its actual monitors, resolutions and shapes, as measured
  (see below). A PC that is connected but not measured yet is marked *not measured*.

The same page works on either PC: the box forwards PC 2's requests to MCU-A over the isolated UART.

Opening it with **Win+F10** (`open_config`): the box types the address into the active PC: `Win+R`,
`http://192.168.7.1`, Enter on Windows; `Cmd+Space`, `Terminal`, `open http://192.168.7.1; exit` on macOS;
`Ctrl+Alt+T`, `xdg-open http://192.168.7.1; exit` on Linux. Only plain characters are typed, so the
keyboard layout matters much less than with file paths, but set `kbd_layout` for non-US layouts anyway.

Set `usb_network = off` to remove the network adapter (for example on PCs where USB network adapters are not
allowed). The box then behaves as before: keyboard, mouse and drive only, configured through the file.

## Automatic screen measurement

The box measures the screens of both PCs by itself. While it does, both status LEDs (and the external RGB LED,
if fitted) blink, and keyboard and mouse input is held back for those few seconds.

**When:** the first time a PC is connected to a port that has no measurement yet, when you press **Win+F9**
(`measure`), or when you click **Measure screens** on the settings page. `auto_measure = off` limits it to the
hotkey and the button.

**What happens, for each connected PC in turn (about 3 to 5 seconds each):**

1. The box switches to that PC and opens `http://192.168.7.1/measure` the same way as Win+F10.
2. The measurement page covers its window and asks the box to place the pointer at a series of absolute
   positions. It first sweeps a coarse grid until the pointer shows up inside the page, then places it at
   points a known distance apart. From where the pointer actually appears, the page learns how the PC maps the
   box's 0-32767 range onto its desktop: the size of the whole desktop in pixels, and so its exact shape
   (2560 x 1600 is 16:10, 3440 x 1440 is 21:9).
3. The page reads the monitor it is on (`window.screen`: resolution, position, scaling). To reach the other
   monitors of the same PC it asks the box to press the system shortcut that moves a window to the next monitor
   (Windows: `Win+Shift+Right`; GNOME and KDE: `Super+Shift+Right`) and reads `window.screen` again, until it
   is back on the first monitor.
4. The page sends the result to the box, which stores it in `KMSWITCH.TXT` (`monitors = ...`, see below).
   If the page still has the keyboard focus, the box closes its tab with `Ctrl+W` (`Cmd+W` on a Mac).
5. The box switches back to the PC you were on and the LEDs stop blinking.

**Limits:** the PC must be unlocked (not at the login screen) and the launch must not be blocked by policy.
If no answer comes within 8 seconds, the box gives up, blinks the LEDs red twice, switches back and marks that
PC *not measured*; everything keeps working with the `aspect` setting instead. macOS has no shortcut that moves
a window to another display, so on a Mac with several displays the page asks you to drag its window to the
other display once; the whole-desktop size is still measured automatically.

Re-measure (Win+F9) after you add, remove or rearrange monitors on a PC.

## Arranging the two PCs

The settings page arranges the two PCs the way the OS display settings arrange monitors: all monitors of both PCs
are drawn in one picture, in pixels, and you **drag PC 2** to where its screens stand next to PC 1's. While you
drag, PC 2 snaps against PC 1's monitor edges (and to top, bottom or centre alignment when it is close), and it can
never overlap PC 1. The quick buttons *left / right / above / below* place PC 2 against PC 1's outer edge as a
starting point.

**The pointer crosses only where a monitor of PC 1 and a monitor of PC 2 touch** (drawn blue). Everywhere else the
edge holds the pointer, exactly like the free edges between two monitors of different size on one PC. So with a
tall PC 1 screen next to a short PC 2 screen, the part of PC 1's edge that has no screen beside it does not switch,
and you can place PC 2 lower or higher to choose which part does. Several touching places are possible, for example
when PC 2's two monitors sit against two different monitors of PC 1, or PC 2 is placed in a corner and touches
both a side and the bottom. The pointer keeps its pixel position along the shared edge when it crosses.

The position is stored as `arrangement = X,Y` in `[general]`: the top-left corner of PC 2's primary monitor in
PC 1's desktop pixels. Without it, PC 2 sits to the right of PC 1, tops aligned. If PC 2's or PC 1's monitors
change after a new measurement so that the stored position overlaps or no longer touches, the box (and the page)
moves PC 2 to the nearest valid position.

## Several monitors on one PC

The measurement records every monitor of a PC: its resolution, its position on the desktop and its scaling.
Monitors of different shape and size are fine, for example a 16:10 laptop panel next to a 21:9 screen, or a
landscape screen next to a portrait one. The firmware uses this to:

- keep its pointer inside the real monitors (the empty corners of the combined desktop cannot be entered, just as
  with a normal mouse),
- keep the speed the same in pixels on every monitor, also when they use different scaling,
- move between the PC's own monitors only where the OS lets the pointer cross (where they touch), and switch to
  the other PC only where a monitor touches one of the other PC's monitors (see above).

The settings page draws each PC's monitors in their real arrangement next to the other PC's.

## Optional companion app (planned)

A small portable program (one file per OS, nothing to install) is planned for after the firmware. It would talk
to the box over a vendor HID interface, which needs no driver on any OS, so it works without the USB network
adapter and without a browser. It reads every monitor directly from the OS, also on macOS, re-measures by itself
when a monitor is plugged in or rearranged, and could later share the clipboard between the PCs.
It stays optional: the box, the settings page and the file keep working without it, which matters on PCs where
running unsigned programs is not allowed.

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
| `arrangement` | `X,Y` (pixels) or empty | empty | Position of PC 2's desktop in PC 1's pixels, set by dragging on the settings page; empty means right of PC 1, tops aligned |
| `edge_switch` | on / off | `on` | Switch by pushing the pointer past the edge |
| `edge_delay_ms` | 0 - 1000 | `0` | Pointer must stay at the edge this long before switching (stops accidental switches) |
| `edge_corner_block` | on / off | `on` | No switch while the pointer is in a screen corner (avoids clashes with hot corners / close buttons) |
| `start_on` | `pc1`, `pc2`, `last` | `pc1` | Active PC after power-up. `last`: the PC that was active before a reset or firmware update (after a power cut: PC 1) |
| `usb_network` | on / off | `on` | Offer the USB network adapter and the settings page at `http://192.168.7.1` |
| `auto_measure` | on / off | `on` | Measure a PC's screens automatically the first time it is connected |
| `config_ui` | on / off | `on` | Allow the `open_config` and `measure` hotkeys to type into the PC |

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
| `open_config` | `WIN+F10` | Open the settings page on the active PC |
| `measure` | `WIN+F9` | Measure the screens of both PCs now |

**Reserved:** `WIN+F8` confirms a firmware update and `CTRL+ALT+SHIFT+F8` (held 3 s) restarts into BOOTSEL; see
[firmware-update.md](firmware-update.md). Do not assign them to anything else.

Changes made with `speed_up` / `speed_down` / `speed_reset` are written back to `KMSWITCH.TXT` two seconds
after the last key press, so they survive a power cycle.

**Modifiers:** `CTRL`, `SHIFT`, `ALT`, `WIN` (the Windows key; Command on a Mac keyboard), plus
`RCTRL`, `RSHIFT`, `RALT`, `RWIN` for the right-hand keys only. They are written in the order
`CTRL+ALT+WIN+SHIFT+key`.
**Keys:** `A`-`Z`, `0`-`9`, `F1`-`F24`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `HOME`, `END`, `PGUP`, `PGDN`,
`INS`, `DEL`, `ESC`, `TAB`, `SPACE`, `ENTER`, `BACKSPACE`, `PAUSE`, `SCROLLLOCK`, `NUMLOCK`, `CAPSLOCK`,
`PRINTSCREEN`, `KP0`-`KP9`.
A hotkey with no modifier (for example `SCROLLLOCK`) is allowed; pick a key you do not otherwise use.

### `[pc1]` and `[pc2]`

| Key | Values | Default | Meaning |
|---|---|---|---|
| `name` | text | `PC 1` / `PC 2` | Shown on the settings page and in the log |
| `speed_x` | 0.25 - 4.0 | `1.0` | Horizontal speed |
| `speed_y` | 0.25 - 4.0 | `1.0` | Vertical speed (normally equal to `speed_x`) |
| `accel` | `off`, `low`, `medium`, `high` | `medium` | Pointer acceleration curve |
| `os` | `windows`, `mac`, `linux` | `windows` | Used by the `open_config` and `measure` hotkeys |
| `kbd_layout` | `us`, `de`, `at`, `ch`, `fr`, `uk`, `it`, `es` | `us` | The keyboard layout set in that PC's OS, so typed commands come out right |
| `monitors` | written by the box | empty | Measured monitors, see below. Delete the line to forget a measurement |
| `aspect` | `16:9`, `16:10`, `21:9`, `32:9`, `4:3`, `5:4`, `9:16`, or `W:H` | `16:9` | Shape of the whole desktop, used only while `monitors` is empty |

`monitors` lists every monitor as `WIDTHxHEIGHT+X+Y@SCALE`, separated by commas, in desktop pixels with the
PC's own origin (the primary monitor's top-left corner is `+0+0`; positions can be negative). `SCALE` is the
OS scaling (1.25 = 125 %). Example, a 16:10 laptop with a 21:9 screen to its right, 300 px higher:

```
monitors = 2560x1600+0+0@1.5, 3440x1440+2560-300@1
```

**How the speed is applied.** For a mouse movement of `dx`, `dy` counts, the firmware adds
`dx * base * speed_x * accel(v)` and `dy * base * speed_y * accel(v)` desktop pixels to its pointer, keeps it
inside the measured monitors, and converts it to the 0 - 32767 range of that PC's whole desktop. `base` is chosen
so that with speed 1.0 an 800 DPI mouse moves about as fast as on a 1080p screen without acceleration,
on every monitor. Without a measurement it uses `aspect` and assumes one 1920-pixel-wide screen.
When the pointer crosses into the other PC, it keeps its position along the shared edge in the common pixel space
of `arrangement`.

## Example

```
[general]
arrangement = 2560,-120
edge_delay_ms = 150

[hotkeys]
switch = SCROLLLOCK
speed_up = CTRL+ALT+PGUP
speed_down = CTRL+ALT+PGDN

[pc1]
name = Office laptop
speed_x = 0.9
speed_y = 0.9
monitors = 2560x1600+0+0@1.5

[pc2]
name = Desktop
speed_x = 1.3
speed_y = 1.3
kbd_layout = de
monitors = 3440x1440+0+0@1, 1440x2560+3440-560@1
```
