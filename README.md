# PocketSoundMixer

A lightweight desktop sound mixer for Windows, macOS and Linux. You can add or remove as many channels as you like. Each channel has its own 10-band equalizer and presets you can save and delete.

## Features

- **Master on top**: pick the speakers or headphones the mixer plays on ("System default" follows Windows), with a master volume and meter.
- **Dynamic channels**: add or remove up to 64. The first launch starts with Music, Game, Film, Chat, Podcast and a muted Mic channel; **Reset channels** brings these back.
- **10-band graphic EQ per channel** at 31 Hz to 16 kHz, ±12 dB.
- **Presets**: 10 built-in ones (Flat, Bass Boost, Treble Boost, Vocal, Loudness, Music, Game, Film, Chat, Podcast) plus as many of your own as you want. Use **Save** on a channel to create one and the **Presets** window to rename or delete.
- **Several apps per channel** on Windows (for example Spotify and YouTube in Chrome both in Music), up to 8. Microphones have their own Mic channel.
- **Per-channel output**: each channel plays on the Master output ("Automatic") or on a device you pick, e.g. chat on a headset and music on the speakers. Up to 3 extra devices.
- **Themes**: Dark, Midnight and Light.
- Volume in percent (0-100 %), balance, mute, solo and peak meters on each channel. Meters use the broadcast (IEC 60268-18) scale with a peak-hold line, and keep moving on a muted channel.
- A **Help** window in the app, and a [user guide](docs/USER_GUIDE.md).
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
  writes atomics ─────────────────────────►     sources -> sum -> 10 biquads -> gain/balance -> mix bus
  add/remove via atomic slot publish           master gain, clamp, peak meters
  frees retired objects after epoch ◄──────    epoch++ at the end of each callback
```

- `src/core`: the engine, with no UI dependency.
  - `GraphicEq`: RBJ peaking biquads in Direct Form II Transposed. Bands at 0 dB are skipped, and coefficients are rebuilt only when a slider moves.
  - `Mixer`: a fixed array of 64 atomic slots. Removed channels and sources go to a graveyard and are freed only once the audio thread has moved past them, so the audio thread never locks, allocates or frees.
  - `Channel`: lock-free parameters as atomics, with gain ramps per block so fader moves don't click. Up to 8 sources sit in atomic slots and are summed before the EQ, so one channel can hold several apps.
  - `AudioEngine`: the output device (switchable at runtime) and live capture through a lock-free ring buffer. miniaudio is built without its decoders and resource manager.
  - Extra outputs: the audio thread mixes each channel into the bus of its output; extra devices drain their bus from a wait-free ring (20 ms target latency, clamped against clock drift), so every source is still read by one thread.
  - `Presets`: built-in presets plus user presets stored as JSON.
- `src/app`: the GLFW + OpenGL3 + Dear ImGui front end. The app sleeps in `glfwWaitEventsTimeout` and only redraws at about 30 fps while meters are moving.

## Per-app channels (Windows)

Click **+ App** on a channel and pick one or more apps that are playing sound, or type its exe name (for example `Spotify.exe`). The channel captures that app and its child processes through WASAPI process loopback. This needs Windows 11 or Windows 10 build 20348+, and no driver. If the app isn't running yet, the channel waits and connects when it starts. Picking an app that is already in another channel moves it. The choice is saved with the session.

The mixer also moves the app's own output to a spare device you don't listen to, such as monitor/HDMI audio or the speakers while you use a headset. That way you hear the app only once, through its channel. It does this through the same Windows setting as "App volume and device preferences", and puts the app back when you remove it from the channel or close the mixer. You pick that device with **Spare output** in the top row (Automatic by default, or Off). It is never a device you listen on: not the Master output and not a channel's output. If there is no spare device, the app plays directly as well. **Reset all app outputs** in the same list puts every app back on the normal output.

## Roadmap

1. Our own virtual audio driver, so every channel shows up as a Windows output device (like Sonar or Wave Link) and no spare device is needed.
2. Per-app capture on macOS (Core Audio process taps, 14.2+) and Linux (PipeWire virtual sinks).
3. Channel reordering.
