# Settings page protocol (box <-> browser)

This is the contract between the firmware and the settings page `config/KMSWITCH.HTM`. The firmware serves the
page and these endpoints at `http://192.168.7.1` on the USB network adapter of **each** PC (MCU-A for PC 1;
MCU-B for PC 2, which forwards every request over the isolated UART to MCU-A, the owner of the settings and the
microSD card). All bodies are UTF-8; JSON unless stated otherwise. A future companion app can use the same
messages over a vendor HID interface instead of HTTP.

The page detects where it runs: loaded over `http(s):` it uses this API; loaded from a file (`file:`), it works
offline with Open / Save as before.

## USB network

| | |
|---|---|
| Classes | CDC-NCM (Windows 11, macOS, Linux, inbox drivers); RNDIS as a second configuration for Windows 10 |
| Addresses | box `192.168.7.1/24`; the box's DHCP server gives the PC `192.168.7.2`, **no gateway, no DNS server** |
| Effect on the PC | a link-local network to the box only; normal networking and routes are untouched |
| Off switch | `usb_network = off` in `[general]` |

## Endpoints

### `GET /` and `GET /measure`
The settings page (the same file as `config/KMSWITCH.HTM`). `/measure` opens it in measurement mode.

### `GET /api/status`
Polled by the page about once a second.

```json
{
  "fw": "1.0.0",
  "fw_b": "1.0.0",
  "board": "A",
  "you": "pc1",
  "active": "pc1",
  "pcs": {
    "pc1": { "connected": true,  "measured": "2026-11-04T10:12:00", "monitors": "2560x1600+0+0@1.5" },
    "pc2": { "connected": false, "measured": null, "monitors": "" }
  },
  "measuring": null,
  "update": null
}
```

- `you`: which PC this request came from (the port the request arrived on).
- `connected`: the PC has enumerated the box's USB device on that port (MCU-B reports its state to MCU-A over the UART).
- `fw` / `fw_b`: firmware version of MCU-A / MCU-B (`dev` for an unsigned development build); `board`: `A` or `B`.
- `update`: `null`, or `{ "version": "1.2.0", "state": "verifying" | "confirm" | "installing" | "rebooting" |
  "done" | "failed", "progress": 0-100, "message": "..." }` while a firmware update runs.
- `measuring`: `null`, or `{ "pc": "pc2", "state": "opening" | "probing" | "done" | "failed" }` while an
  automatic measurement runs.

### `GET /api/config`
`text/plain`: the current `KMSWITCH.TXT`.

### `PUT /api/config`
`text/plain` body: the whole new `KMSWITCH.TXT`. The box parses it with the same rules as at power-up, writes the
file to the microSD card (temporary file + rename, so a power cut never leaves half a file) and applies it.
Answer: `200 {"ok": true, "problems": ["pc2.speed_x"]}`, where `problems` lists keys that fell back to their
default. The page sends the whole file about one second after the last change.

The box keeps the `monitors` lines it measured even if a page without them sends a config (a page that was opened
before a measurement finished must not erase it).

### `POST /api/measure`
Body `{"pcs": ["pc1", "pc2"]}` (or one of them). Starts the automatic measurement (see
[configuration.md](configuration.md#automatic-screen-measurement)). Answer `202`; progress shows in
`/api/status`.

### `POST /api/firmware`
`application/octet-stream` body: a `.kmsfw` update bundle (at most 2 MB). The box checks magic, board, hashes and
the Ed25519 signature before anything is written ([firmware-update.md](firmware-update.md)).
Answer `202 {"version": "1.2.0"}` and `update.state` becomes `confirm`: the box waits up to 60 s for **Win+F8** on
its own keyboard, then installs. Rejections answer `400 {"error": "signature" | "board" | "hash" | "format" |
"downgrade" | "busy"}`.

### `POST /api/firmware/cancel`
Cancels an update that is still waiting for confirmation. Answer `200 {"ok": true}`.

## Measurement mode (`/measure`)

The page drives the measurement; the box only moves the pointer when asked and stores the result.
Absolute coordinates are the box's 0 - 32767 range on each axis.

| Request | Body | The box... | Answer |
|---|---|---|---|
| `POST /api/m/hello` | `{"screen": {...}, "dpr": 1.5, "win": {"x": 0, "y": 0, "w": 1280, "h": 900}}` | starts a session for this PC, holds back user input, keeps the LEDs blinking | `{"session": "s1", "timeout_ms": 8000}` |
| `POST /api/m/move` | `{"x": 16384, "y": 16384}` | sends one absolute pointer report to this PC | `{"ok": true}` after the report went out |
| `POST /api/m/key` | `{"keys": "WIN+SHIFT+RIGHT"}` | presses and releases that combination on this PC (only shortcuts from a fixed list: move-window-to-next-monitor) | `{"ok": true}` |
| `POST /api/m/result` | see below | stores `monitors` for this PC in `KMSWITCH.TXT` | `{"ok": true}` |
| `POST /api/m/close` | `{"focused": true}` | if `focused`, presses `Ctrl+W` (`Cmd+W` on a Mac) to close the tab; ends the session; switches back | `{"ok": true}` |

`screen` is what the browser reports for the monitor the window is on: `{"w", "h", "availLeft", "availTop"}` in
CSS pixels. The box refuses `move` and `key` outside a session, and a session ends by itself after `timeout_ms`.

### How the page measures

1. **Find the window.** Ask for moves on a coarse grid (8 x 8 points over the 0 - 32767 range) until a
   `pointermove` arrives inside the page; the page covers its whole window with a sensor layer.
2. **Calibrate.** From the point found, ask for two more moves `+D` away on each axis (`D` = 1024, or `-D` if that
   leaves the window) and record `screenX` / `screenY` of each. Per axis:
   `scale = (s2 - s1) / D` (CSS px per unit) and `origin = s1 - a1 * scale`. The whole desktop the PC maps the
   absolute range onto is then `[origin, origin + 32768 * scale]` on each axis, in CSS pixels; times `dpr` gives
   device pixels.
3. **Monitors.** Record `window.screen` (size, `availLeft` / `availTop`) for the current monitor. Ask the box for
   the OS's move-to-next-monitor shortcut, wait for the `resize` / `screen` change, record again, until a monitor
   repeats or after 6 monitors. Where the shortcut does nothing (macOS), the page asks the user to drag the window
   to each other display, or skips the step if there is one display.
4. **Result.** Combine 2 and 3 into one list in device pixels with the primary monitor at `+0+0`:

```json
{ "monitors": [ {"w": 2560, "h": 1600, "x": 0, "y": 0, "scale": 1.5},
                {"w": 3440, "h": 1440, "x": 2560, "y": -300, "scale": 1} ],
  "desktop": {"x": 0, "y": -300, "w": 6000, "h": 1900},
  "abs_check": {"scale_x": 0.1831, "scale_y": 0.0580} }
```

The firmware converts each monitor rectangle into absolute units with `desktop` and keeps its pointer inside the
union of the rectangles.
