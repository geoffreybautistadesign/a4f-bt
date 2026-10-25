# A4F BT — PS4 audio to AirPods through a USB Bluetooth dongle

Goal: on a jailbroken PS4 Pro (GoldHEN, firmware 13.52), send game audio straight to AirPods
through a USB Bluetooth dongle plugged into the console — **no phone, no computer**.

**Status: game audio plays in the AirPods in real time (Rayman, inFamous Second Son, Ratchet &
Clank, Shadow of the Colossus: ~48 A2DP packets/s, ~40 ms buffer), with an adjustable volume and
the TV muted while the AirPods play. All MAIN audio ports are mixed (v8). Open issue: the sound
in the AirPods sometimes stops while the plugin keeps sending (see below).**

---

## What works

| Step | Result |
|---|---|
| TnB dongle = CSR8510 A10 (`0a12:0001`, BT 4.0) driven by BTstack on macOS (libusb) | ✅ |
| AirPods pairing + A2DP/SBC test tone on macOS | ✅ |
| Our own s16 stereo 48 kHz PCM stream (UDP, A4F format) to the AirPods on macOS | ✅ 0 loss, buffer steady at 37 ms |
| Dongle access from inside a PS4 game through `libSceUsbd`, **without jailbreaking the process** | ✅ (**right** USB port only) |
| HCI commands, events and Bluetooth inquiry from the game (AirPods found) | ✅ |
| BTstack ported to the PS4: GoldHEN plugin that pairs, stores the link key, reconnects on its own and plays a tone | ✅ heard in the AirPods |
| Game audio captured from `sceAudioOut` (import-table redirection, all formats converted to s16 stereo) | ✅ |
| Game audio in the AirPods | ✅ Rayman (see the compatibility table) |

### Key test results (PS4 Pro, 13.52)

- **Left USB port: the dongle does not show up at all** (neither in `libSceUsbd` nor in
  `/dev/usb`). **Right** port: it shows up (`/dev/ugen0.4`) and everything works.
- `libSceUsbd` loads inside a game (without jailbreak for some games, after
  `sys_sdk_jailbreak()` for others) and inside a homebrew app. It only lists the dongle.
- Sony error code for a timeout: `0x80240007` (equivalent of `LIBUSB_ERROR_TIMEOUT`).
- AirPods **refuse to change sample rate** once connected (AVDTP reconfigure → `0x1f`): 48 kHz
  must be preferred at connection time. When the AirPods initiate the connection they sometimes
  pick 44.1 kHz; the buffer then resamples.
- AirPods Bluetooth latency: about 150–200 ms, plus ~40 ms of buffering.
- **`CLOCK_MONOTONIC` is wrong on the PS4**: the toolchain headers come from musl, where it is 1;
  for the PS4 kernel (FreeBSD) clock 1 is `CLOCK_VIRTUAL`, the process CPU time. BTstack then ran
  at the game's CPU pace (~1/3 of real time in Rayman): a third of the A2DP throughput, choppy
  audio. The run loop now uses FreeBSD's monotonic clock (id 4).
- Raising the plugin thread priority (`scePthreadSetprio`) coincided with RE4 Remake freezing at
  launch; the plugin keeps the default priority.
- The GoldHEN FTP server decrypts SELF/PRX files on download: a downloaded `.prx` is not a usable
  backup.
- Some games' sandbox hides `libSceUsbd` (`sceKernelLoadStartModule` → `0x80020002`, ENOENT:
  RE4 Remake, inFamous Second Son). The plugin then leaves the sandbox (`sys_sdk_jailbreak`) just
  long enough to load the module and goes back (`sys_sdk_unjailbreak`): staying outside changes the
  process root, the game no longer finds `/app0` and stays on a black screen.
- `fscanf` did not read the settings files (musl headers + PS4 libc); they are parsed by hand.
- **Importing `sceKernelGetProcessTime` (or `scePthreadSelf`) crashes the game when the plugin is
  loaded**, before `plugin_load` runs (CE-34878-0) — a diagnostic build and an empty control plugin
  did. Before adding a libkernel import, compare `nm -u` with a known-good build.
- When a game is closed, the plugin dies without hanging up and the AirPods call the dongle back by
  themselves. Their connection request reached the next game's BTstack during initialisation, stayed
  pending after the dongle reset (Accept Connection → `0x02`) and blocked our own connection forever.
  The plugin now rejects incoming connections (it always calls the headset) and restarts the
  Bluetooth stack if a connection is not established within 20 s.

---

## Game compatibility

| Game | Audio capture | `libSceUsbd` | AirPods | Notes |
|---|---|---|---|---|
| Rayman | ✅ 5 ports (2 MAIN float stereo, 1 MAIN s16, pad speaker) | ✅ | ✅ | tested with v7 (first MAIN port only); to retest with the v8 mix |
| inFamous Second Son | ✅ 1 MAIN 7.1 float port, `sceAudioOutOutputs` | ✅ after leaving the sandbox | ✅ complete audio, TV muted | |
| Ratchet & Clank | ✅ 1 MAIN 7.1 float port + BGM port | ✅ | ✅ complete audio (menu and game) | the BGM port (system background music) is not captured |
| Shadow of the Colossus | ✅ 2 MAIN 7.1 float ports + BGM + pad speaker | ✅ after leaving the sandbox | ✅ with v8 | the game switches ports at a cutscene: v7 lost the sound there |
| Resident Evil 4 (remake) | ✅ 1 MAIN 7.1 float port, `sceAudioOutOutputs` | ❌ before v6 (`0x80020002`) | ❌ | froze at launch twice with the plugin: keep the plugin out of this game |

`debug.log` (`/data/GoldHEN/a4f_bt/`) has one line per second: audio ports opened, captured blocks,
captured ports with their peak level of the last second (`ports [7M:12 6:0]`: port `…07` is the
master and peaks at 12 %), `usbd tries/step/error`, A2DP packets sent, dropped frames. Healthy
numbers: `paquets` ~47/s, `jetees` not increasing.

`debug.log` (`/data/GoldHEN/a4f_bt/`) has one line per second: audio ports opened, captured blocks,
`usbd tries/step/error`, A2DP packets sent, dropped frames. Healthy numbers: `paquets` ~47/s,
`jetees` not increasing.

## Settings

Plain text files in `/data/GoldHEN/a4f_bt/` (created on first launch, editable over FTP, re-read
every second while the game runs):

| File | Default | Meaning |
|---|---|---|
| `volume` | `30` | AirPods volume, 0–100 (quadratic curve: 50 ≈ −12 dB, 30 ≈ −21 dB) |
| `tv` | `0` | `0` = TV muted while the AirPods play (it comes back if Bluetooth drops), `1` = sound on both |
| `tone` | absent | if present: test tone instead of the game audio |

## What is left to do

1. **Sound stopping in the AirPods while the plugin keeps streaming** (Shadow of the Colossus,
   once after taking one AirPod out, once without touching anything): the logs show 48 packets/s
   going out and no pause request from the AirPods; the link was later lost (disconnect reason
   `0x08`, connection timeout). Not understood yet.
2. Test more games and fill in the table above.
3. Volume control from the controller or from the AirPods (AVRCP absolute volume), instead of the
   `volume` file.

How the capture works: `sceAudioOutOpen`, `sceAudioOutClose`, `sceAudioOutOutput` and
`sceAudioOutOutputs` are redirected in the import tables (GOT) of the game modules, so
`libSceAudioOut` itself is untouched and the TV keeps playing. GoldHEN `HOOK32` detours were
dropped: they copy the prologue without fixing RIP-relative instructions, and calling the original
returned immediately (~5 million calls per second, black screen, crash CE-34878-0).

---

## Repository layout

| Folder | Purpose |
|---|---|
| `a4f_bt/core/` | Core shared by macOS and PS4: A2DP application (`a4f_bt.c`), synchronous USB HCI transport (`hci_transport_a4f_usb.c`), pthread BTstack run loop (`btstack_run_loop_a4f.c`), lock-free 48 kHz buffer (`a4f_ring.c`), BTstack config |
| `a4f_bt/plugin/` | GoldHEN plugin `a4f_bt.prx`: USB through `libSceUsbd`, notifications, keys in `/data/GoldHEN/a4f_bt/`, HCI log `hci.pklg` and `debug.log` |
| `a4f_bt/mac/` | Same core on macOS with libusb, for debugging (`./a4f_bt_mac` tone, `./a4f_bt_mac pcm` audio received over UDP) |
| `probe/` | GoldHEN probe that validated USB access on 13.52 (libSceUsbd, direct `ugen` access as a fallback, HCI commands, inquiry) |
| `test_app/` | Script that packages the probe as a homebrew `.pkg` app (TITLE_ID `AFBT00001`) |
| `mac_prototype/` | First macOS prototype (derived from `a2dp_source_demo`) + test sender `pcm_send.py` |
| `tools/` | `build_plugin_env.sh` (PS4 toolchain on macOS without Homebrew), `pklg.py` (HCI log reader) |

Source comments are in French.

### Plugin behaviour

1. 5 s after the game starts: load `libSceUsbd`, open the `0a12:0001` dongle.
2. Reconnect to the last known headset; otherwise look for a headset in pairing mode (audio
   class), SSP pairing accepted automatically (no screen).
3. On failure or disconnection: retry every ~4 s (reconnect and inquiry alternate).
4. PS4 notification on every state change ("Dongle Bluetooth pret", "Connecte a …").

## Building

Nothing to install system-wide: LLVM comes from the `ziglang` Python package, the OpenOrbis
toolchain and the GoldHEN SDK are downloaded into `/tmp/a4f_ps4_build`. Paths passed to `make`
must not contain spaces.

```bash
sh tools/build_plugin_env.sh             # sets up /tmp/a4f_ps4_build (and builds the probe)
git clone --depth 1 https://github.com/bluekitchen/btstack /tmp/btstack
make -f a4f_bt/plugin/Makefile BTSTACK=/tmp/btstack A4F_BT=$PWD/a4f_bt   # -> a4f_bt.prx
```

macOS build (libusb built statically, see `mac_prototype/`):

```bash
make -f a4f_bt/mac/Makefile BTSTACK=/tmp/btstack A4F_BT=$PWD/a4f_bt LIBUSB=<libusb folder>
```

## Installing on the PS4

1. Copy `a4f_bt.prx` to `/data/GoldHEN/plugins/` (GoldHEN FTP, port 2121).
2. Add to `/data/GoldHEN/plugins.ini`, `[default]` section:
   `/data/GoldHEN/plugins/a4f_bt.prx=true`
3. Enable "Plugin Loader" in the GoldHEN settings.
4. Dongle on the **right** USB port, AirPods in pairing mode the first time, launch a game.

## Licenses

- [BTstack](https://github.com/bluekitchen/btstack): free for personal, non-commercial use only.
  This repository does not include its sources; some files are derived from it
  (`a2dp_source_demo.c`, `btstack_run_loop_freertos.c`).
- GoldHEN Plugins SDK, OpenOrbis PS4 Toolchain: see their repositories.
