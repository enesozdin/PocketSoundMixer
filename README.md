# PocketSoundMixer

A lightweight desktop sound mixer for Windows, macOS and Linux. You can add or remove as many channels as you like. Each channel has its own 10-band equalizer and presets you can save and delete.

## Features

- **Dynamic channels**: add or remove up to 64. The first launch starts with Music, Game, Film, Chat and Podcast.
- **10-band graphic EQ per channel** at 31 Hz to 16 kHz, ±12 dB. Double-click a slider to reset it.
- **Presets**: 10 built-in ones (Flat, Bass Boost, Treble Boost, Vocal, Loudness, Music, Game, Film, Chat, Podcast) plus as many of your own as you want. Use **Save** on a channel to create one and the **Presets** window to rename or delete.
- **Sources per channel**: an audio file (WAV, MP3 or FLAC, with play, pause, loop and seek) or a live input (mic, line-in or audio interface). Drag a file onto a channel to load it. Dropping a file anywhere else creates a new channel.
- Volume, balance, mute, solo and peak meters on each channel, plus a master fader.
- The channel layout is saved on exit and restored on the next launch.

Settings live in `%APPDATA%\PocketSoundMixer` on Windows, `~/Library/Application Support/PocketSoundMixer` on macOS, and `~/.config/PocketSoundMixer` on Linux.

## Build

You need CMake 3.20+ and a C++20 compiler (MSVC 2022, Clang or GCC 11+). Dependencies are downloaded on the first configure, except miniaudio, which is vendored.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

On Linux, install the X11 and OpenGL headers first:

```sh
sudo apt-get install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
```

To build only the engine and tests (no GUI), add `-DPSM_BUILD_APP=OFF`.

## Architecture

```
UI thread (Dear ImGui)                      Audio thread (miniaudio callback)
  channel strips, presets, session            for each channel slot:
  writes atomics ─────────────────────────►     source -> 10 biquads -> gain/balance -> mix bus
  add/remove via atomic slot publish           master gain, clamp, peak meters
  frees retired objects after epoch ◄──────    epoch++ at the end of each callback
```

- `src/core`: the engine, with no UI dependency.
  - `GraphicEq`: RBJ peaking biquads in Direct Form II Transposed. Bands at 0 dB are skipped, and coefficients are rebuilt only when a slider moves.
  - `Channel`: lock-free parameters as atomics, with gain ramps per block so fader moves don't click.
  - `Mixer`: a fixed array of 64 atomic slots. Removed channels and sources go to a graveyard and are freed only once the audio thread has moved past them, so the audio thread never locks, allocates or frees.
  - `AudioEngine`: the output device, file streaming (decoded on a job thread by the miniaudio resource manager), and live capture through a lock-free ring buffer.
  - `Presets`: built-in presets plus user presets stored as JSON.
- `src/app`: the GLFW + OpenGL3 + Dear ImGui front end. The app sleeps in `glfwWaitEventsTimeout` and only redraws at about 30 fps while meters are moving.

## Roadmap

1. **Per-app capture on Windows** (top priority): route apps such as Spotify, games and Discord into the Music, Game and Chat channels, using WASAPI process loopback (Windows 10 2004+).
2. Per-app capture on macOS (Core Audio process taps, 14.2+) and Linux (PipeWire virtual sinks).
3. A native file picker and channel reordering.
