# A4F BT — PS4 audio to AirPods through a USB Bluetooth dongle

Goal: on a jailbroken PS4 Pro (GoldHEN, firmware 13.52), send game audio straight to AirPods
through a USB Bluetooth dongle plugged into the console — **no phone, no computer**.

**Status: the PS4 → dongle → AirPods chain works (test tone heard from inside a game).
The game audio still has to be fed into it.**

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
| Game audio in the AirPods | ❌ to do (see below) |

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

---

## What is left to do

**Feed the game audio into the plugin.** The Bluetooth core exposes:

```c
uint32_t a4f_bt_push_pcm(const int16_t *pcm, uint32_t frames);   // interleaved s16 stereo, 48 kHz
void     a4f_bt_set_tone(bool on);                                // false = play the pushed PCM
```

`a4f_bt_push_pcm` can be called from any thread (lock-free buffer, single producer). In the
plugin (`a4f_bt/plugin/source/main.c`) one needs to:

1. get the audio blocks the game sends to `sceAudioOut`;
2. convert them to s16 stereo 48 kHz (the Audio4Free project already has a converter for every
   `sceAudioOut` format: mono/stereo/7.1, s16/float);
3. push them with `a4f_bt_push_pcm()` and call `a4f_bt_set_tone(false)`.

Things observed during testing:
- a game often opens **several** `MAIN` ports (e.g. two 512-frame float stereo ports plus a
  1024-frame s16 port): several ports may need to be mixed; the current buffer has a single input;
- with the GoldHEN Plugins SDK `HOOK32` detours on `sceAudioOutOutput`, calling the original
  (`HOOK_CONTINUE`) no longer blocked: the game called the function ~5 million times per second
  (black screen, then crash CE-34878-0). The detour copies the function prologue without fixing
  RIP-relative instructions (`Detour.c`).

## What is blocking

That part (capturing the game audio inside the PS4 process) has not been written: the assistant
that developed the rest of the project stopped there. Everything else is done and tested; someone
familiar with PS4 game audio can pick it up from `a4f_bt_push_pcm()`.

No-code alternative: a USB Bluetooth audio transmitter that the PS4 sees as a headset
(Creative BT-W3/BT-W4, Avantree DG80), or a transmitter on the PS4 Pro optical output.

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
