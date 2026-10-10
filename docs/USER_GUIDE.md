# PocketSoundMixer user guide

PocketSoundMixer lets you set the volume and sound of each kind of audio on your PC separately. You might put music in one channel, your game in another and voice chat in a third, and give each its own volume and equalizer.

Per-app channels need Windows 11, or Windows 10 build 20348 or newer. On macOS and Linux only the Mic channel works for now.

## First launch

The mixer opens with six channels: **Music**, **Game**, **Film**, **Chat**, **Podcast** and **Mic**. Each channel already has a matching equalizer preset. The channels start empty, so all your apps keep playing exactly as before until you add them to a channel.

The Mic channel holds your default microphone and starts muted, so you don't hear yourself through the speakers. Its meter still moves when you talk, so you can see the mic works.

To get back to these six channels at any time, click **Reset channels** and confirm. Your apps go back to normal, and the Master output, volume and theme stay as they are.

## The buttons under the Master row

| Button | What it does |
|---|---|
| **+ Add channel** | Adds a channel for apps. |
| **+ Add mic channel** | Adds a channel for a microphone (muted at first). |
| **Presets** | Opens the window where you rename or delete your own equalizer presets. |
| Theme list | **Dark**, **Midnight** (a darker, neutral dark theme) or **Light**. The choice is remembered. |
| **Reset channels** | Replaces all channels with the six default ones. |
| **Help** | Opens a short guide on the right side of the window. Click it again or the x to close it. |

## The Master row (top)

| Control | What it does |
|---|---|
| Output list | Where the mixer plays. **System default** follows the default device in Windows, so it changes when you switch devices there. You can also pick specific headphones or speakers. The choice is remembered. |
| Volume | Turns the whole mix up or down. |
| Meter | Shows how loud everything together is. The top bar is the left side and the bottom bar is the right side. |

If the saved device is unplugged, the mixer plays on the system default instead.

## Putting apps in a channel

1. Start playing something in the app, for example a song in Spotify.
2. Click **+ App** on the channel you want, for example Music.
3. Pick the app from the list. The popup stays open, so you can pick more apps for the same channel.

Some things to know about apps in channels:

- One channel can hold up to 8 apps. Their sound is mixed together, then goes through the channel's equalizer and volume.
- An app can be in only one channel. If you pick an app that is already in another channel, it moves to this one. The list shows where each app is.
- If the app isn't in the list because it isn't playing yet, type its program name (for example `Spotify.exe`) and click **Add**.
- If the app is closed, the channel says *waiting for it to start*. It connects by itself when the app starts.
- To take an app out of a channel, click the small **x** next to its name. **Clear all** removes everything from that channel.
- Apps you never put in a channel are not affected. They keep playing normally on your Windows default output.

### Hearing an app only once

The mixer has to capture the app's sound. If the app also kept playing on your speakers, you would hear it twice. So by default (**Hear apps only through the mixer**, in the + App popup) the mixer moves the app's own output to a *spare* device you don't listen to, such as monitor/HDMI audio. The app goes back to normal when you take it out of the channel or close the mixer.

- The spare device is never the one picked in the Master row.
- If your PC has only one output device, there is nowhere to move the app, so you will hear it twice. Connect a second output (a monitor with speakers, or a USB headset), or install the free VB-Cable driver.
- If an app ever stays silent after the mixer crashed, open the + App popup and click **Reset all app outputs**.

## The Mic channel

Microphones have their own channel, so app channels only hold apps. At the top of the Mic channel, pick **Default microphone**, a specific device, or **None**.

The Mic channel starts muted. Its meter still moves, greyed out, when you talk. Unmute it (the **M** button) to hear yourself, for example to check how you sound with an equalizer preset. Use headphones when you do this, or the speakers will feed back into the mic.

Need a second mic? Click **+ Add mic channel**.

## Channel controls

| Control | What it does |
|---|---|
| Name | Click it to rename the channel. |
| **x** (top right) | Removes the channel. |
| Volume fader | 0 % to 100 %. It changes loudness in steps your ear hears as even: 50 % is clearly quieter, and 10 % is barely audible. |
| Meter | Green is normal, yellow is loud, and red means the sound is at the limit, so turn something down. The thin line shows the latest peak for a moment. On a muted channel the meter is greyed out but still moves. |
| Balance | Moves the sound to the left or right. It snaps to the center when you drag close to it. |
| **M** | Mute. |
| **S** | Solo. When any channel is soloed, only soloed channels play. |

## Equalizer

The 10 sliders change different pitches, from deep bass on the left (31 Hz) to the highest treble on the right (16 kHz).

- Up makes that range louder, down makes it quieter, and the middle leaves it unchanged. Hover a slider to see its exact value.
- The small curve above the sliders shows the overall shape.
- To undo all your changes, pick the **Flat** preset.

Some quick recipes:

| Goal | Try |
|---|---|
| More punch in music | Raise 63 and 125 a little. |
| Clearer voices in chat or podcasts | Raise 2k and 4k, and lower 63 and 125. |
| Hear footsteps in games | Raise 2k to 4k, and lower 125 to 250. |
| Less harsh sound | Lower 4k and 8k. |

## Presets

- Pick a preset from the list on each channel. A `*` after the name means you changed the sliders since.
- **Save** stores the current sliders as a preset. Give it a new name, or the name of one of your own presets to update it.
- **Presets** (top bar) opens a window where you can rename or delete your own presets. The 10 built-in presets can't be changed.

## Where settings are saved

The channels, their apps, volumes and the Master output are saved when you close the mixer, and come back on the next launch.

| System | Folder |
|---|---|
| Windows | `%APPDATA%\PocketSoundMixer` |
| macOS | `~/Library/Application Support/PocketSoundMixer` |
| Linux | `~/.config/PocketSoundMixer` |

To start fresh, close the mixer and delete `session.json` from that folder.

## Troubleshooting

| Problem | What to do |
|---|---|
| I hear an app twice | There is no spare output device. See [Hearing an app only once](#hearing-an-app-only-once). |
| An app is silent after the mixer crashed | Open + App and click **Reset all app outputs**. |
| The app isn't in the + App list | Play something in it first, or type its program name. |
| Nothing plays at all | Check the Master output and volume, then check that the channel isn't muted, and that no other channel is soloed. |
| The meter is red | Turn down that channel, or lower the equalizer sliders you raised. |
