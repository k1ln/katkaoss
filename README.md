# KatKaoss

A build pipeline for developing custom effect units for the [Korg Nu:Tekt NTS-3 kaoss pad kit](https://www.korg.com/products/dj/nts_3) using Korg's [logue SDK](https://github.com/korginc/logue-sdk), and getting them onto the hardware.

## Layout

```
KatKaoss/
├── logue-sdk/            # git submodule: korginc/logue-sdk (toolchain, headers, docker build env)
├── shared/
│   └── dsp.h               # canonical shared DSP library (mirrored into each unit)
├── tools/
│   └── probe/              # host-side measurement rig (no device needed)
├── units/                 # your custom unit projects live here (kept out of the submodule)
│   └── gritcrush/          # example genericfx unit, scaffolded from the SDK template
└── scripts/
    ├── new_unit.sh         # scaffold a new unit project from the SDK template
    ├── build_unit.sh       # build a unit into a .nts3unit file via the SDK's Docker image
    ├── sync_dsp.sh         # copy shared/dsp.h into every unit that uses it
    ├── sync_params.py      # regenerate header.c parameter tables from effect.h
    ├── probe_units.sh      # measure units on the host (see "Measuring units")
    ├── calibrate_levels.py # level-match every mode to the dry signal
    └── deploy.sh           # collect all .nts3unit files into deploy/
```

The NTS-3 only supports one kind of user unit: **genericfx** (a generic audio effect / sound
generator loaded into one of its 4 effect runtime slots). There's no oscillator/modfx/delfx/revfx
split like on prologue/minilogue xd/NTS-1.

Every unit exposes the same 4 controls: **X** and **Y** on the pad, a bipolar **DEPTH** (dry/wet),
and a 4-way **MODE** switch. Shared DSP building blocks (reverb, grain engine, pitch shifter,
filters, delays) live in the header-only [shared/dsp.h](shared/dsp.h). The SDK builds each unit
directory in isolation, so every unit carries a mirrored copy — **edit `shared/dsp.h`, never a
unit's `dsp.h`**, then run `scripts/sync_dsp.sh` to fan it out (it overwrites the unit copies).

## Effect catalog

46 units, all building to `.nts3unit`. Every unit exposes **X**, **Y**, a bipolar
**DEPTH** (dry/wet), a 4-way **MODE**, and **DRIVE** (saturation, 0 = clean,
~250 = warm, 1023 = fuzz). Granular units add three more knobs — **SHAPE**
(grain envelope, percussive ↔ gated), **SCATTER** (per-grain pitch/size/timing
randomness) and **REVERSE** (share of backwards grains) — reachable from the
NTS-3 edit menu and assignable to X/Y.

Every mode is level-matched to within ±0.6 dB of the dry signal at full wet
(`scripts/calibrate_levels.py`), so switching units or modes changes the sound,
not the volume.

### Delays (tempo-synced unless noted)
| Unit | Description | X / Y | Modes |
|------|-------------|-------|-------|
| tapedelay | Performance delay; Y past ~90% runs away into self-oscillation | TIME / FEEDBACK | TAPE/DUB/DIGI/SPACE |
| revdelay | Reverse delay — every repeat plays backwards | TIME / FEEDBACK | REV/SWELL/OCT/SMEAR |
| riser | Pitch-shifting feedback delay: each repeat climbs or falls | TIME / FEEDBACK | FIFTH/OCT+/DOWN/SHIMMER |
| nebula | Ambient delay into a reverb wash (free time) | TIME / FEEDBACK | SOFT/GLASS/DARK/INF |
| pingcloud | Ping-pong delay whose echoes get granulated (free time) | TIME / FEEDBACK | WIDE/DUB/GRAIN/INFIN |
| stutter | Beat repeat: X = repeats per catch, touch to punch in | REPEATS / GATE | QTR/8TH/16TH/ROLL |
| glitch | Beat repeat → bitcrush → feedback delay | RATE / CRUSH | STUT/REPEAT/TAPE/MANGLE |

### Core / misc
| Unit | Description | X / Y | Modes |
|------|-------------|-------|-------|
| gritcrush | Bitcrusher + sample-rate reducer (the SDK example unit) | CRUSH / RATE | CLEAN/GRIT/CRUSH/NUKE |
| drift | Stereo flanger / phaser / chorus / jet | RATE / FEEDBACK | FLANGER/PHASER/CHORUS/JET |
| ripple | Stereo chorus / ensemble | RATE / MOD | 1V/2V/3V/WIDE |
| warp | Pitch shifter with feedback (harmonizer / detune) | PITCH / FEEDBACK | OCT-/5TH/OCT+/12TH |
| voxwah | Formant (vowel) filter → grains: the input talks | VOWEL / GRAIN | AEIOU/TALK/CRY/ROBOT |

### Reverbs
| Unit | Description | X / Y | Modes |
|------|-------------|-------|-------|
| space | The all-rounder: four genuinely different spaces | SIZE / TONE | ROOM/HALL/PLATE/VAST |
| roomverb | Small real rooms, low diffusion so early reflections show | SIZE / TONE | TIGHT/WOOD/TILE/BOOTH |
| hallverb | Concert halls, pre-delay scaling with size (0.9 s → 12 s) | SIZE / TONE | SMALL/MED/LARGE/EPIC |
| plateverb | Studio plate: no pre-delay, maximum density | SIZE / TONE | STD/BRITE/DARK/WIDE |
| springverb | Spring tank — dispersion inside the loop, not a room | TENSION / TONE | 1SPR/2SPR/3SPR/DRIP |
| gateverb | Gated / reverse-ramp / ducking / slammed | SIZE / GATE | GATE/REV/DUCK/SLAM |
| modverb | Modulated reverb: the tail itself moves | SIZE / MODRATE | SOFT/LUSH/SEASICK/WOW |
| shimmer | Long cathedral with a pitch-shifted recirculating tail | SIZE / SHIMMER | OCT+/5TH/OCT-/DUAL |
| ice | Short, glassy, bright — sparkle on top, no feedback | TUNE / GLISTEN | OCT+/2OCT/5TH/DETUNE |
| ringverb | Ring modulator into a space | FREQ / SIZE | BELL/METAL/ALIEN/SUB |
| phaseverb | Stereo phaser into a space | RATE / FEEDBACK | WARM/JET/DEEP/WASH |
| meltdown | Wavefolder into a drifting space | FOLD / SIZE | WARM/HARSH/LIQUID/OOZE |
| wahdelverb | Auto-wah → synced delay → reverb | WAH / DELAY | SLOW/FUNK/DUB/SPACE |
| cosmic | Pitch cascade → delay → huge reverb (up to a 30 s void) | PITCH / TIME | RISE/FALL/WARP/BLKHOLE |

### Granular
| Unit | Description | X / Y | Modes |
|------|-------------|-------|-------|
| grain | Dry granular delay — the grains themselves, no space | SCATTER / SIZE | FWD/REV/PITCH/WILD |
| grainpitch | Grain harmonizer: smooth, dense, pitched | PITCH / SIZE | UNISON/OCT+/5TH/OCT- |
| grainrev | A cloud of backwards grains | SCATTER / SIZE | SLOW/MED/FAST/CHAOS |
| scatter | Grains sprayed across the stereo field | SPRAY / SIZE | NEAR/WIDE/PING/RAIN |
| swarm | Dense detuned swarm | SPREAD / SIZE | BEES/DRONE/STORM/CHOIR |
| texture | Sustained granular textures | POSITION / SIZE | SPARSE/SOFT/DENSE/HAZE |
| freeze | Capture and hold forever — touch the pad to catch | POSITION / SIZE | LIVE/FREEZE/SMEAR/GLIDE |

### Granular + reverb
| Unit | Description | X / Y | Modes |
|------|-------------|-------|-------|
| clouds | Granular texture feeding a reverb wash | TEXTURE / SIZE | GRAIN/CLOUD/DENSE/FREEZE |
| nimbus | Always-long soft grains blooming into a wide space | POSITION / DENSITY | SOFT/DENSE/FREEZE/BLOOM |
| mist | Sparse droplets dissolving into a haze (reverb-forward) | DENSITY / SIZE | FOG/HAZE/DAMP/DEW |
| cloudhall | Grains poured into big halls, grains still audible on top | TEXTURE / SIZE | HALL/CHURCH/CAVE/VOID |
| grancath | Cathedral fed by choir-like octave/fifth grains | GRAIN / SIZE | NAVE/APSE/CRYPT/HEAVEN |
| shimgrain | Sustained pitched grain cloud into a shimmer loop | GRAIN / SHIMMER | OCT/5TH/2OCT/DUST |
| stardust | Tiny percussive pitched-up sparkles in a bright plate | SPARKLE / SIZE | TWINKLE/COMET/NOVA/DRIFT |
| frostbite | Icy octave-up reverse grains in a thin bright space | FREEZE / SIZE | FROST/CRACK/BLIZZARD/THAW |
| glacier | Slow pitched-down grains sinking into an immense space | PITCH / SIZE | CALM/FLOW/CALVE/DEEP |
| aurora | Grains drifting through the buffer, heavy tail modulation | DRIFT / SIZE | DAWN/NIGHT/SOLAR/POLAR |
| vapor | Vaporwave slow-down with tape wow | SLOW / SIZE | MALL/DREAM/SLOW/PLUSH |
| crushcloud | Bitcrush → grains → small space | CRUSH / GRAIN | CLEAN/GRIT/CRUSH/NUKE |
| flangrain | Stereo flanger → grains | RATE / GRAIN | SOFT/JET/METAL/CHAOS |

## Prerequisites

- [Docker](https://docs.docker.com/get-docker/) (Docker Desktop or Engine), running.
- `rsync` (preinstalled on macOS).
- A KAOSS NTS-3 unit connected via USB, and [KORG KONTROL Editor](https://www.korg.com/products/dj/nts_3/editor.php) installed (used to install units on the device — see [Uploading to the device](#uploading-to-the-device)).

## One-time setup

```sh
git submodule update --init logue-sdk/platform/ext/CMSIS
cd logue-sdk/docker
./build_image.sh          # builds the local Docker image, OR:
docker pull xiashj/logue-sdk   # use the prebuilt image from Docker Hub instead
```

## Creating a new unit

```sh
scripts/new_unit.sh <unit-name> "<Display Name>" <dev_id_hex> [unit_id_hex]
```

- `unit-name`: project slug, e.g. `my-crusher` (used for the `units/` directory and internal project name).
- `Display Name`: up to 19 characters, this is what's shown on the NTS-3's display. Allowed characters: `` `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_`` and space.
- `dev_id_hex`: your own 32-bit developer ID (e.g. `0x00000001`). Pick something not already listed in [logue-sdk/developer_ids.md](logue-sdk/developer_ids.md), and not `0x00000000` / `KORG` / `korg` (reserved). This only needs to be unique enough to avoid clashing with units you load from other developers.
- `unit_id_hex` (optional, default `0x0`): distinguishes multiple units you release under the same `dev_id`.

Example:

```sh
scripts/new_unit.sh gritcrush "GritCrush" 0x7574756e 0x1
```

This copies `logue-sdk/platform/nts-3_kaoss/dummy-genericfx/` into `units/gritcrush/` and patches
`config.mk` / `header.c` with your project name, dev/unit ID and display name.

Then implement your DSP:

- `units/<name>/effect.h` — your effect class (parameter handling + audio processing).
- `units/<name>/unit.cc` — glue code implementing the logue SDK unit callbacks (usually only `effect.h` needs edits for simple effects).
- `units/<name>/header.c` — unit metadata and the 8 parameter descriptors/mappings exposed to the device's X/Y pad and depth control.

## Building a unit

```sh
scripts/build_unit.sh <unit-name>
```

This stages a copy of `units/<unit-name>` inside the `logue-sdk` submodule's `platform/nts-3_kaoss/`
tree (required by the SDK's build tooling), runs the Dockerized `arm-none-eabi-gcc` toolchain via
`logue-sdk/docker/run_cmd.sh`, and copies the resulting `.nts3unit` file back to
`units/<unit-name>/`. The staged copy is removed afterwards, so the submodule checkout stays clean.

To clean build artifacts instead of building:

```sh
scripts/build_unit.sh <unit-name> --clean
```

## Uploading to the device

`.nts3unit` files are loaded onto the NTS-3 kaoss pad kit using **KORG KONTROL Editor** (Korg's
official desktop librarian/editor app for the NTS-3):

1. Connect the NTS-3 to your computer via USB and power it on.
2. Open KORG KONTROL Editor and select the NTS-3 kaoss pad kit.
3. Go to the unit/librarian install screen and choose "Install Unit" (or equivalent), then select
   `units/<unit-name>/<unit-name>.nts3unit`.
4. Follow the on-screen steps to transfer it to one of the unit slots on the device.

Loaded units appear at the end of the effects selection list, in slot order. The NTS-3 remembers
units by their dev/unit ID and name, so they can be freely reassigned to different slots later.

## Testing in the browser (WASM simulator)

You can run any unit in a browser-based device simulator before flashing hardware. It compiles the
exact same DSP code (`unit.cc` + `effect.h` + `dsp.h`) to WebAssembly and loads it into the SDK's
`xypad.html` shell, which emulates the NTS-3: an XY pad, a Depth slider, the built-in
oscillator/QWERTY-keyboard and sample players as sources, time/frequency scopes, and an
auto-generated slider for **every** parameter (including MODE and DRIVE, with MODE showing its
string values).

```sh
scripts/sim_unit.sh <unit-name>          # build + serve, then open the printed URL in Chrome
SIM_NO_SERVE=1 scripts/sim_unit.sh <name>   # build only (output in units/<name>/sim/)
SIM_PORT=8080 scripts/sim_unit.sh <name>    # serve on a custom port
```

- Compilation uses the official `emscripten/emsdk` Docker image, so no local emscripten install is
  needed (the first run pulls the image).
- Open the printed `http://localhost:<port>/<name>.html` in Chrome, click **Toggle playback**, pick
  a sample or play the keyboard, then drag the XY pad and tweak the parameter sliders.
- The server ([scripts/serve_sim.py](scripts/serve_sim.py)) sets the COOP/COEP headers that the
  audio-worklet backend requires — a plain static server will not work.

## Going deeper: editing effects and adding parameters

Each effect is a self-contained C++ class in `units/<name>/effect.h`, and that
file is the source of truth for the unit's controls:

- `process()` — the audio loop, usually a per-mode table plus a short inner loop.
- `setParameter()` / `getParameterStrValue()` — parameter index → value, MODE labels.
- The `enum { X, Y, DEPTH, MODE, NUM_PARAMS }` **names are what the NTS-3
  displays**, so renaming a parameter there renames it on the device.

`header.c` (the descriptors the device reads) is **generated** from `effect.h` by
`scripts/sync_params.py` — don't hand-edit it. Comment directives control the
parts that aren't in the enum:

```c
// @map x 0 1023 460      pad X range and default (default: 0 1023 256)
// @map y 0 1023 400      pad Y range and default (default: 0 1023 512)
// @param 5 TONE 0 1023 400   an extra knob in slot 5..7 (edit menu, assignable to X/Y)
// @drive 250             DRIVE default
// @manual-header         leave this unit's header.c alone (gritcrush)
```

The genericfx format allows **8** parameters. Five are always used (X, Y, DEPTH,
MODE, DRIVE); granular units spend the other three on SHAPE / SCATTER / REVERSE,
and any other unit has them free.

After changing a unit's sound, re-run the level calibration so it still matches
the others, then rebuild:

```sh
scripts/calibrate_levels.py <unit>     # measures and rewrites its kLevel[] line
scripts/build_unit.sh <unit>
```

## Measuring units (no hardware needed)

`tools/probe` compiles a unit's real `effect.h` for the host and plays test
signals through it, so a change can be checked in seconds without Docker or the
device:

```sh
scripts/probe_units.sh                 # every unit: level, tail, brightness, width, CPU
scripts/probe_units.sh clouds freeze   # just these
scripts/probe_units.sh --similar       # + which modes sound most alike, across units
```

Columns: `level` (full-wet vs dry on pink noise, DRIVE 0 — should be ~0 dB),
`tail` (seconds to -60 dB after the input stops; 20 = holds forever), `bright`
(spectral centroid), `L/R` (1 = mono, 0 = wide, <0 = phasey), `ns/smp` (host CPU,
useful for comparing units, not as a device figure). Modes that are silent or
produce NaN are flagged — that check is how five permanently-silent units and one
NaN unit were found.

`--similar` ranks every mode against every other unit's modes on a spectral +
envelope + stereo fingerprint. Use it after voicing a unit to see whether it
actually occupies its own space.

## Reference

- [logue-sdk/platform/nts-3_kaoss/README.md](logue-sdk/platform/nts-3_kaoss/README.md) — full API reference (parameter types, curves, runtime hooks, touch events, etc).
- [logue-sdk/developer_ids.md](logue-sdk/developer_ids.md) — registry of known developer IDs.
- [logue-sdk/docker/README.md](logue-sdk/docker/README.md) — Docker build environment details.
