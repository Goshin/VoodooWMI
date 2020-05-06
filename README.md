# TongfangKeyboardUtility

A set of the kernel extension, DSDT patch, daemon program for keyboard Fn keys functionality on the Tongfang 8/9th gen laptop ODM models with Darwin-based OS.

## Fn Key Features

| Fn + Fx  | Function                             | Note                   |
| -------- | ------------------------------------ | ---------------------- |
| F1       | Sleep                                | -                      |
| F2       | Lock LGUI Key                        | -                      |
| F3       | Switch Screen Mirroring/Extended     | Open display settings. |
| F4       | Toggle WiFi                          | -                      |
| F5       | Toggle Touchpad                      | VoodooI2C required     |
| F6, F7   | Increase/Decrease Keyboard Backlight | -                      |
| F8 - F10 | Adjust Volume                        | -                      |
| F11, F12 | Adjust Screen Backlight              | VoodooPS2 required     |

## Usage

### Build Dependencies

- `cpplint`: static code checker;
- `iasl`: ACPI Source Language compiler/decompiler.

### Installation

1. Install the kernel extension `TongfangKeyboardUtility.kext`;
2. Apply the DSDT patch `SSDT-FN.aml` and rename the methods detailed in `patch-config.txt`;
3. Install the daemon with the `install_daemon.sh`.

## Credits

- hieplpvip for [AsusSMC](https://github.com/hieplpvip/AsusSMC).

## License

GPL v2
