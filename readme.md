Light Host
---

A lightweight VST3/AU plugin host that lives in the system tray (Windows) or menu bar (macOS). Chain a few plugins between your input and output device and forget about it.

Based on [Light Host](https://github.com/opencma/LightHost) by Rolando Islas.

### Screenshots

![Light Host 2.0](https://i.ibb.co/sJqQ84XK/2026-09-21-020640.png)
![Light Host 2.0](https://i.ibb.co/5gDvNd8T/2026-09-21-020651.png)

### Features

- Runs from the tray/menu bar, no main window needed
- Serial plugin chain: add, reorder, bypass and remove plugins
- Control Panel window for managing the chain and saved configurations
- Save, load, import and export plugin chain configurations
- Plugin state is remembered between sessions
- Audio device selection, including ASIO on Windows
- Optional start with Windows
- Denormal protection around the whole chain to avoid CPU spikes from plugin tails

### Download

Prebuilt Windows and macOS builds are attached to each [release](../../releases). The macOS build is unsigned, so Gatekeeper will ask for confirmation on first launch.

### Building

Requires CMake 3.22+ and a C++17 compiler. JUCE 9.0.2 is fetched automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Options:

- `-DLIGHTHOST_ENABLE_ASIO=ON` enables ASIO support (Windows). The Steinberg ASIO SDK is fetched automatically unless it is present in `ThirdParty/ASIOSDK`.

On Windows with Ninja, run the build from a Visual Studio developer prompt. Release builds use LTO, and AVX2 on x86.

### License

GNU General Public License, version 2 or later. See [license](license) and [gpl.txt](gpl.txt). Third-party components are listed in [third_party](third_party).
