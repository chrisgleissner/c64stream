# C64U REST API Reference

This document is a concise version of the official [Ultimate REST API guide](https://1541u-documentation.readthedocs.io/en/latest/api/api_calls.html).

## HTTP Basics

- REST access starts from Ultimate firmware 3.11.
- URLs follow the form `/v1/{route}/{path}:{command}?{arguments}`.

| Verb | Meaning                                                                                           |
| ---- | ------------------------------------------------------------------------------------------------- |
| GET  | Retrieves information without changing state.                                                     |
| PUT  | Sends information or performs an action using the information in the URL or in a referenced file. |
| POST | Performs an action using information attached to the request.                                     |

- Responses normally use `Content-Type: application/json` and always include an `errors` array.
- Firmware 3.12 introduces the optional Network Password. When it is set, include `X-Password: {your-password}` in every request. The firmware returns `403 Forbidden` when the header is missing or incorrect. Supplying the header on an unsecured unit is accepted and ignored.

## Routes

The sections below follow the order and wording of the RST reference.

### About

| Method | Path          | Parameters | Action                                                                             |
| ------ | ------------- | ---------- | ---------------------------------------------------------------------------------- |
| GET    | `/v1/version` | –          | Returns the current version of the REST API.                                       |
| GET    | `/v1/info`    | –          | Returns basic information about the Ultimate device. Available from firmware 3.12. |

Example for `/v1/version`:

```json
{
  "version": "0.1",
  "errors": []
}
```

Example for `/v1/info` (Ultimate 64 device):

```json
{
  "product": "Ultimate 64",
  "firmware_version": "3.12",
  "fpga_version": "11F",
  "core_version": "143",
  "hostname": "Terakura",
  "unique_id": "8D927F",
  "errors": []
}
```

### Runners

| Method | Path                   | Parameters         | Action                                                                                                                       |
| ------ | ---------------------- | ------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| PUT    | `/v1/runners:sidplay`  | `file`, `[songnr]` | Requests playback of a SID file located on the Ultimate. The player loads the optional song lengths file from `SONGLENGTHS`. |
| POST   | `/v1/runners:sidplay`  | `[songnr]`         | Requests playback of an attached SID file. A second attachment may supply song lengths.                                      |
| PUT    | `/v1/runners:modplay`  | `file`             | Requests playback of an Amiga MOD file located on the Ultimate.                                                              |
| POST   | `/v1/runners:modplay`  | –                  | Requests playback of an attached Amiga MOD file.                                                                             |
| PUT    | `/v1/runners:load_prg` | `file`             | Resets the machine and DMA-loads a PRG from the Ultimate file system into memory. The program does not run automatically.    |
| POST   | `/v1/runners:load_prg` | –                  | Resets the machine and DMA-loads the attached PRG into memory. The program does not run automatically.                       |
| PUT    | `/v1/runners:run_prg`  | `file`             | Resets the machine, DMA-loads the specified PRG, and then runs it.                                                           |
| POST   | `/v1/runners:run_prg`  | –                  | Resets the machine, DMA-loads the attached PRG, and then runs it.                                                            |
| PUT    | `/v1/runners:run_crt`  | `file`             | Resets the machine with the specified cartridge active. Configuration remains unchanged.                                     |
| POST   | `/v1/runners:run_crt`  | –                  | Resets the machine with the attached cartridge active. Configuration remains unchanged.                                      |

For `POST /v1/runners:sidplay`, send both the SID and optional song-lengths payload as multipart fields named `file` (two parts). The song-lengths payload is a `.ssl` binary file: two bytes per subsong, stored as packed BCD minutes and seconds. Minutes range 00–99, seconds range 00–59. A maximum of 512 bytes (256 entries) is accepted. Missing or invalid entries fall back to firmware defaults.

### Configuration

| Method | Path                            | Parameters | Action                                                                                            |
| ------ | ------------------------------- | ---------- | ------------------------------------------------------------------------------------------------- |
| GET    | `/v1/configs`                   | –          | Returns every configuration category.                                                             |
| GET    | `/v1/configs/{category}`        | –          | Returns all items in the supplied category. The path depth is 1 and supports wildcards.           |
| GET    | `/v1/configs/{category}/{item}` | –          | Returns the specified configuration items. The path depth is 2 and supports wildcards.            |
| PUT    | `/v1/configs/{category}/{item}` | `value`    | Sets a configuration item to the supplied value. The full path is required and accepts wildcards. |
| POST   | `/v1/configs`                   | –          | Updates configuration items in batch. Submit JSON matching the GET structure.                     |
| PUT    | `/v1/configs:load_from_flash`   | –          | Restores configuration from non-volatile storage.                                                 |
| PUT    | `/v1/configs:save_to_flash`     | –          | Saves the current configuration to non-volatile storage.                                          |
| PUT    | `/v1/configs:reset_to_default`  | –          | Resets the current configuration to factory defaults without altering the flash copy.             |

Example for `/v1/configs`:

```json
{
  "categories": [
    "Audio Mixer",
    "SID Sockets Configuration",
    "UltiSID Configuration",
    "SID Addressing",
    "C64 and Cartridge Settings",
    "U64 Specific Settings",
    "Clock Settings",
    "Network settings",
    "WiFi settings",
    "Modem Settings",
    "LED Strip Settings",
    "Data Streams",
    "Software IEC Settings",
    "User Interface Settings",
    "Tape Settings",
    "Drive A Settings",
    "Drive B Settings"
  ],
  "errors": []
}
```

Example for `/v1/configs/drive%20a%20settings`:

```json
{
  "Drive A Settings": {
    "Drive": "Enabled",
    "Drive Type": "1541",
    "Drive Bus ID": 8,
    "ROM for 1541 mode": "1541.rom",
    "ROM for 1571 mode": "1571.rom",
    "ROM for 1581 mode": "1581.rom",
    "Extra RAM": "Disabled",
    "Disk swap delay": 1,
    "Resets when C64 resets": "Yes",
    "Freezes in menu": "Yes",
    "GCR Save Align Tracks": "Yes",
    "Leave Menu on Mount": "Yes"
  },
  "errors": []
}
```

Example for `/v1/configs/drive%20a*/*bus*`:

```json
{
  "Drive A Settings": {
    "Drive Bus ID": {
      "current": 8,
      "min": 8,
      "max": 11,
      "format": "%d",
      "default": 8
    }
  },
  "errors": []
}
```

### Machine

| Method | Path                      | Parameters            | Action                                                                                                                                                     |
| ------ | ------------------------- | --------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PUT    | `/v1/machine:reset`       | –                     | Sends a reset to the machine. Configuration remains unchanged.                                                                                             |
| PUT    | `/v1/machine:reboot`      | –                     | Restarts the machine and re-initialises the cartridge configuration.                                                                                       |
| PUT    | `/v1/machine:pause`       | –                     | Pulls the DMA line low at a safe moment, stopping the CPU while timers continue.                                                                           |
| PUT    | `/v1/machine:resume`      | –                     | Releases the DMA line so the CPU resumes execution.                                                                                                        |
| PUT    | `/v1/machine:poweroff`    | –                     | Powers off the machine. Responses are not guaranteed.                                                                                                      |
| PUT    | `/v1/machine:menu_button` | –                     | Toggles the Ultimate menu just like the physical button.                                                                                                   |
| PUT    | `/v1/machine:writemem`    | `address`, `data`     | Writes data to C64 memory through DMA using the currently selected memory map. Writing to 6510 I/O registers is not possible. Up to 128 bytes are written. |
| POST   | `/v1/machine:writemem`    | `address`             | Writes attached binary data starting at the supplied address. The data must not wrap beyond `$FFFF`.                                                       |
| GET    | `/v1/machine:readmem`     | `address`, `[length]` | Performs a DMA read and returns binary data. The default length is 256 bytes.                                                                              |
| GET    | `/v1/machine:debugreg`    | –                     | Reads `$D7FF` and returns the value field in hexadecimal. Ultimate 64 only.                                                                                |
| PUT    | `/v1/machine:debugreg`    | `value`               | Writes the hexadecimal value to `$D7FF`, then reads it back. Ultimate 64 only.                                                                             |

Example for `/v1/machine:writemem`:

```text
PUT /v1/machine:writemem?address=D020&data=0504
```

This sequence writes `05` to `$D020` and `04` to `$D021`.

### Floppy Drives

| Method | Path                          | Parameters                  | Action                                                                                                                                            |
| ------ | ----------------------------- | --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| GET    | `/v1/drives`                  | –                           | Returns information about every internal drive, including mounted images and referenced paths.                                                    |
| PUT    | `/v1/drives/{drive}:mount`    | `image`, `[type]`, `[mode]` | Mounts an existing image. Valid `type` values are `d64`, `g64`, `d71`, `g71`, `d81`. Valid `mode` values are `readwrite`, `readonly`, `unlinked`. |
| POST   | `/v1/drives/{drive}:mount`    | `[type]`, `[mode]`          | Mounts an attached image. The optional arguments match the PUT variant.                                                                           |
| PUT    | `/v1/drives/{drive}:reset`    | –                           | Resets the selected drive.                                                                                                                        |
| PUT    | `/v1/drives/{drive}:remove`   | –                           | Removes the mounted image from the drive.                                                                                                         |
| PUT    | `/v1/drives/{drive}:on`       | –                           | Turns on the selected drive and resets it if already on.                                                                                          |
| PUT    | `/v1/drives/{drive}:off`      | –                           | Turns off the selected drive.                                                                                                                     |
| PUT    | `/v1/drives/{drive}:load_rom` | `file`                      | Loads a 16K or 32K ROM from the Ultimate file system into the selected drive. The load is temporary.                                              |
| POST   | `/v1/drives/{drive}:load_rom` | –                           | Loads an attached 16K or 32K ROM into the selected drive. The load is temporary.                                                                  |
| PUT    | `/v1/drives/{drive}:set_mode` | `mode`                      | Changes the drive mode to `1541`, `1571`, or `1581`. The firmware also loads the corresponding ROM.                                               |

Example for `/v1/drives`:

```json
{
  "drives": [
    {
      "a": {
        "enabled": true,
        "bus_id": 8,
        "type": "1581",
        "rom": "1581.rom",
        "image_file": "",
        "image_path": ""
      }
    },
    {
      "b": {
        "enabled": false,
        "bus_id": 9,
        "type": "1541",
        "rom": "1541.rom",
        "image_file": "",
        "image_path": ""
      }
    },
    {
      "softiec": {
        "enabled": false,
        "bus_id": 11,
        "type": "DOS emulation",
        "last_error": "73,U64IEC ULTIMATE DOS V1.1,00,00",
        "partitions": [
          {
            "id": 0,
            "path": "/Temp/"
          }
        ]
      }
    }
  ],
  "errors": []
}
```

### Data Streams (Ultimate 64)

| Method | Path                         | Parameters | Action                                                                                                                                                                                                       |
| ------ | ---------------------------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| PUT    | `/v1/streams/{stream}:start` | `ip`       | Starts the selected stream (`video`, `audio`, or `debug`). Supply an IP address and optional port (defaults: `video` 11000, `audio` 11001, `debug` 11002). Starting the video stream stops the debug stream. |
| PUT    | `/v1/streams/{stream}:stop`  | –          | Stops the selected stream (`video`, `audio`, or `debug`).                                                                                                                                                    |

### File Manipulation

| Method | Path                          | Parameters               | Action                                                                                                                           |
| ------ | ----------------------------- | ------------------------ | -------------------------------------------------------------------------------------------------------------------------------- |
| GET    | `/v1/files/{path}:info`       | –                        | Returns file information such as size and extension. Supports wildcards. Marked unfinished in firmware 3.11 alpha.               |
| PUT    | `/v1/files/{path}:create_d64` | `[tracks]`, `[diskname]` | Creates a D64 image. Tracks default to 35 and may be set to 40. An optional disk name overrides the header title.                |
| PUT    | `/v1/files/{path}:create_d71` | `[diskname]`             | Creates a D71 image with 70 tracks. An optional disk name overrides the header title.                                            |
| PUT    | `/v1/files/{path}:create_d81` | `[diskname]`             | Creates a D81 image with 160 tracks. An optional disk name overrides the header title.                                           |
| PUT    | `/v1/files/{path}:create_dnp` | `tracks`, `[diskname]`   | Creates a DNP image. Tracks are required (maximum 255, 256 sectors per track). An optional disk name overrides the header title. |
