# __GAME__

A Wii homebrew game built on [magnolia](../magnolia).

## Layout

```
__GAME__/
├── Makefile
├── meta.xml          Homebrew Channel entry
├── source/           game code
├── sprites/          PNGs, embedded into the binary by bin2s
├── audio/            raw PCM, embedded into the binary by bin2s
└── ../magnolia/      the engine, checked out beside this directory
```

## Building

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH=$DEVKITPPC/bin:$PATH

make            # build build/__GAME__.dol
make deploy     # stage sdcard/apps/__GAME__/
make dolphin    # push that to the folder Dolphin reads as its SD card
```

`make dolphin` clears the app directory rather than merging, so **saved scores
and settings are deleted on every deploy**. That is right for a dev loop and
wrong to mistake for the game failing to save.

## Onto a real Wii

Two routes, and they do different jobs.

```bash
make card SD=/mnt/e             # install onto an SD card (permanent)
make wii  WIILOAD=tcp:<wii-ip>  # send this build to a running console (temporary)
```

**`make card`** is the one that installs the game. It needs the card's mount
point because a removable drive's letter moves — a card showing as `E:` in
Windows is `/mnt/e` in WSL. Eject it, put it in the console, and the game appears
in the Homebrew Channel under the name in `meta.xml`.

Unlike `make dolphin`, this **merges**: only `boot.dol` and `meta.xml` are
overwritten, so `scores.json` and `settings.json` on the card survive an update.
Wiping is right for a dev loop against an emulator and wrong when it is somebody's
high scores.

**`make wii`** sends the `.dol` over the network and runs it immediately, without
installing anything. It is the fast loop for real hardware — no card, no ejecting,
a couple of seconds. Open the Homebrew Channel, press Home for the netloader
screen, and use the IP address it displays:

```bash
export WIILOAD=tcp:192.168.1.50   # once per shell, then just `make wii`
```

The console has to be sitting on that netloader screen when you send. Nothing is
written to the card, so the game is gone when you quit it.

## Assets

Sprites and audio are linked into the binary, not read from the card: assets on a
card can go missing, go stale, or disagree with the code.

Audio is raw signed 16-bit little-endian PCM:

```bash
ffmpeg -i in.ogg -f s16le -acodec pcm_s16le -ar 48000 -ac 2 audio/track.pcm
```

Budget the memory before committing to a format. Clips are held decoded in RAM:

| Format | Rate | One minute |
|---|---|---|
| 48kHz stereo | ~192 KB/s | ~11.5 MB |
| 24kHz mono | ~48 KB/s | ~2.9 MB |

Against the Wii's 24MB, a long track at 48kHz stereo does not fit. Downsample the
music loop with `audio_play_music_fmt()`; keep short effects at 48kHz stereo.

## Testing without a controller

Reaching gameplay by hand needs button presses into an emulator window. Instead:

```c
#define AUTOSTART_GAMEPLAY      1
#define DEBUG_HEARTBEAT_FRAMES  120
```

Then run the `.dol` and read the log. For `printf` to reach Dolphin's log, its
`Logger.ini` needs `OSREPORT = True` and `WriteToFile = True` -- both default to
False, which makes a working trace look like a dead one.

The engine side of that is `SYS_STDIO_Report(true)`, which `magnolia_init()`
calls for you. It matters because libogc leaves stdout attached to nothing
without it: until magnolia 0.3.0 that call was missing, this page described the
Logger.ini half alone, and every `printf` in every game built on the engine was
discarded. The symptom was a **0-byte `dolphin.log`** however Logger.ini was
set -- which reads as "logging is off" rather than "nothing was ever sent", and
is why it went unnoticed across three games. If you see an empty log now, the
Logger.ini settings really are the thing to check.

Keep `printf` out of per-frame code: it is an EXI write, not free.

**Nothing can press a button for you.** Dolphin's emulated Wiimote reads the
keyboard through DirectInput, which does not observe injected keystrokes, so
neither `SendInput` nor `keybd_event` reaches the game -- and nothing reports an
error, the keys simply do nothing. The mouse is the exception, since `Buttons/A`
defaults to `Click 0`, which means a script can click through a title screen and
then find that no other control responds. That looks exactly like a broken input
mapping. This is the whole reason the autostart define above exists; do not
spend an afternoon rediscovering it.

Dolphin also reuses an already-running instance, so kill it between runs or you
will read the previous run's log and debug a binary that is not running.
