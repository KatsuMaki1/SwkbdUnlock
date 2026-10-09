# Swkbd Unlock

A Wii U Aroma plugin by **Katsu** that unlocks software keyboard symbols and preserves `@` in Mii Maker names.

## Installation

Copy `SwkbdUnlock.wps` to `sd:/wiiu/environments/aroma/plugins/` and restart Aroma. Remove older versions first so only one copy is loaded. Open the Aroma plugin configuration menu and select **Swkbd Unlock**.

## Settings

- **Unlock symbols**: enables the full keyboard and clears its prohibited character list. Default: on.
- **Keep @ in Mii names (PAL v50)**: bypasses the Mii Maker routine that replaces names containing `@` or `＠` with `???`. Default: on. Requires Unlock symbols on and Splatoon only off.
- **Unlimited keyboard length (experimental)**: removes the input length requested by the application. Default: off. It does not enlarge the application's saved fields or buffers. Mii names still have a fixed capacity of ten UTF-16 units.
- **Splatoon only**: restricts changes to the three regional Splatoon title IDs. Default: off.

Close and reopen the keyboard after changing a setting. A name already overwritten with `???` must be entered again.

## Compatibility

The internal keyboard patch requires the instruction signature from the tested custom `swkbd.rpl`. It is skipped if the signature differs. The Mii name patch is limited to **Mii Maker PAL, title 000500101004A200, version 50** and verifies its instruction signature before installation. Hooks are removed when the application exits. Other applications may enforce their own character validation, storage limits or font restrictions.

The previous version's symbol and Mii name patches were reported working on hardware. Version 1.0.0 removes diagnostic counters/menu rows and credits Katsu; it compiles successfully but has not received a separate hardware test yet.

## Build with Docker

From the project directory in PowerShell:

```powershell
docker build -t swkbd-unlock-builder .
docker run --rm --mount "type=bind,source=$PWD,target=/project" swkbd-unlock-builder make -j2
```

The output is `SwkbdUnlock.wps`. The Dockerfile supplies devkitPPC, WUT, WiiUPluginSystem and libfunctionpatcher; Nintendo binaries are not needed for compilation or included in this source package.

With the corresponding native devkitPro libraries installed, `make -j2` also builds the plugin.

## Source and license

`src/main.cpp` contains the configuration and keyboard hooks. `src/mii_name.cpp` contains the Mii Maker name hook. This plugin's source is MIT licensed; see LICENSE.

The Makefile is adapted from the official WiiUPluginSystem C++ example. Third-party dependencies retain their own licenses. The included `LICENSE-WiiUPluginSystem.txt` reproduces its LGPL license; see the upstream projects for their complete licensing information.

- [WiiUPluginSystem](https://github.com/wiiu-env/WiiUPluginSystem)
- [libfunctionpatcher](https://github.com/wiiu-env/libfunctionpatcher)
- [WUT](https://github.com/devkitPro/wut)

## Version 1.0.0

- Unlocks the software keyboard's prohibited characters.
- Preserves @ in the supported Mii Maker names.
- Provides optional experimental keyboard length override.
- Keeps signature checks and reversible hooks.
- Removes debug counters and diagnostic menu rows.
