# VoodooWMI

A WMI (Windows Management Instrumentation) platform driver for macOS.
This project also includes a generic Fn hotkey driver based on WMI protocol.


## Usage

### Build Dependencies

- `cpplint`: a static code checker.

### Installation

1. Install the kernel extension `VoodooWMI.kext` and `VoodooWMIHotkey.kext`.
2. Install the OSD daemon with `install_daemon.sh`.

### Add a hotkey scheme

The hotkey implementation is platform-specific. `VoodooWMIHotkey.kext` has a default hotkey scheme for Tongfang ODM model that might not work for you.
You can easily add a hotkey scheme for your laptop model in `VoodooWMIHotkey.kext/Contents/info.plist`, check out the tutorial in wiki pages.

## Credits

- @hieplpvip for the fancy daemon in [AsusSMC](https://github.com/hieplpvip/AsusSMC).

## License

GPL v2
