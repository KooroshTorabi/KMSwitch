# Firmware

Not written yet. This folder will hold the firmware sources and the released update files.

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

How updates work: [docs/firmware-update.md](../docs/firmware-update.md).
