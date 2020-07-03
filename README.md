# TongfangKeyboardUtility

A WMI platform driver for Fn hotkeys functionality on Tongfang 8/9th gen laptop ODM models with Darwin-based OS.

## Fn Key Features

| Fn + Fx  | Function                             | Note                   |
| -------- | ------------------------------------ | ---------------------- |
| F1       | Sleep                                | -                      |
| F2       | Lock LGUI Key                        | -                      |
| F3       | Switch Screen Mirroring/Extended     | Open display settings |
| F4       | Toggle WiFi                          | -                      |
| F5       | Toggle Touchpad                      | VoodooPS2 required for remapping dead key |
| F6, F7   | Increase/Decrease Keyboard Backlight | -                      |
| F8 - F10 | Adjust Volume                        | -                      |
| F11, F12 | Adjust Screen Backlight              | Remap to F14, F15 |

## Usage

### Build Dependencies

- `cpplint`: a static code checker.

### Installation

1. Install the kernel extension `TongfangKeyboardUtility.kext`.
3. Install the OSD daemon with `install_daemon.sh`.

## Credits

- hieplpvip for [AsusSMC](https://github.com/hieplpvip/AsusSMC).

## License

GPL v2
