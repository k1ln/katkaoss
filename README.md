# KatKaoss

A build pipeline for developing custom effect units for the [Korg Nu:Tekt NTS-3 kaoss pad kit](https://www.korg.com/products/dj/nts_3) using Korg's [logue SDK](https://github.com/korginc/logue-sdk), and getting them onto the hardware.

## Layout

```
KatKaoss/
├── logue-sdk/            # git submodule: korginc/logue-sdk (toolchain, headers, docker build env)
├── units/                 # your custom unit projects live here (kept out of the submodule)
│   └── gritcrush/          # example genericfx unit, scaffolded from the SDK template
└── scripts/
    ├── new_unit.sh         # scaffold a new unit project from the SDK template
    └── build_unit.sh       # build a unit into a .nts3unit file via the SDK's Docker image
```

The NTS-3 only supports one kind of user unit: **genericfx** (a generic audio effect / sound
generator loaded into one of its 4 effect runtime slots). There's no oscillator/modfx/delfx/revfx
split like on prologue/minilogue xd/NTS-1.

Every unit exposes the same 4 controls: **X** and **Y** on the pad, a bipolar **DEPTH** (dry/wet),
and a 4-way **MODE** switch. Shared DSP building blocks (reverb, grain engine, pitch shifter,
filters, delays) live in a header-only `dsp.h` copied into each unit directory.

## Effect catalog

All effects below are implemented and build to `.nts3unit`. Each row lists its `MODE` options.

### Core / misc
| Unit | Description | Modes |
|------|-------------|-------|
| gritcrush | Bitcrusher + sample-rate reducer | CLEAN/GRIT/CRUSH/NUKE |
| ripple | Chorus / ensemble | 1V/2V/3V/WIDE |
| drift | Flanger / phaser / jet | FLANGER/PHASER/CHORUS/JET |
| warp | Pitch shifter / harmonizer | OCT-/5TH/OCT+/12TH |

### Reverbs
| Unit | Description | Modes |
|------|-------------|-------|
| space | General reverb | ROOM/HALL/PLATE/VAST |
| roomverb | Tight rooms | TIGHT/WOOD/TILE/BOOTH |
| hallverb | Concert halls | SMALL/MED/LARGE/EPIC |
| plateverb | Bright plate | STD/BRITE/DARK/WIDE |
| springverb | Dispersive spring tank | 1SPR/2SPR/3SPR/DRIP |
| gateverb | Gated reverb | GATE/REV/DUCK/SLAM |
| modverb | Modulated reverb | SOFT/LUSH/SEASICK/WOW |
| shimmer | Octave-up shimmer reverb | OCT+/5TH/OCT-/DUAL |
| nebula | Ambient delay + reverb wash | SOFT/GLASS/DARK/INF |

### Granular synthesis
| Unit | Description | Modes |
|------|-------------|-------|
| clouds | Granular cloud + reverb wash | GRAIN/CLOUD/DENSE/FREEZE |
| grain | Granular delay / scatter | FWD/REV/PITCH/WILD |
| freeze | Infinite grain freeze | LIVE/FREEZE/SMEAR/GLIDE |
| ice | Crystalline pitched grains | OCT+/2OCT/5TH/DETUNE |
| texture | Sustained texture generator | SPARSE/SOFT/DENSE/HAZE |
| stutter | Beat-repeat / glitch | QTR/8TH/16TH/ROLL |
| grainpitch | Pitched grain harmonizer | UNISON/OCT+/5TH/OCT- |
| grainrev | Reverse grain cloud | SLOW/MED/FAST/CHAOS |
| scatter | Wide stereo grain spray | NEAR/WIDE/PING/RAIN |
| swarm | Dense detuned swarm | BEES/DRONE/STORM/CHOIR |

### Reverb + granular combinations
| Unit | Description | Modes |
|------|-------------|-------|
| cloudhall | Grains into a huge hall | HALL/CHURCH/CAVE/VOID |
| shimgrain | Pitched grains + shimmer | OCT/5TH/2OCT/DUST |
| frostbite | Icy reverse grains + bright verb | FROST/CRACK/BLIZZARD/THAW |
| nimbus | Soft grain bloom + reverb | SOFT/DENSE/FREEZE/BLOOM |
| aurora | Evolving grains + mod reverb | DAWN/NIGHT/SOLAR/POLAR |
| grancath | Cathedral reverb + grains | NAVE/APSE/CRYPT/HEAVEN |
| mist | Sparse grains + soft haze | FOG/HAZE/DAMP/DEW |
| glacier | Slow pitched-down grains + verb | CALM/FLOW/CALVE/DEEP |
| stardust | Sparkling pitched-up grains + verb | TWINKLE/COMET/NOVA/DRIFT |
| vapor | Vaporwave slow-down + wow verb | MALL/DREAM/SLOW/PLUSH |

### Wild combinations
| Unit | Description | Modes |
|------|-------------|-------|
| wahdelverb | Auto-wah → delay → reverb | SLOW/FUNK/DUB/SPACE |
| crushcloud | Bitcrush → grains → reverb | CLEAN/GRIT/CRUSH/NUKE |
| phaseverb | Phaser → reverb | WARM/JET/DEEP/WASH |
| flangrain | Flanger → grains | SOFT/JET/METAL/CHAOS |
| ringverb | Ring mod → reverb | BELL/METAL/ALIEN/SUB |
| glitch | Stutter + bitcrush + delay | STUT/REPEAT/TAPE/MANGLE |
| voxwah | Formant/vowel filter → grains | AEIOU/TALK/CRY/ROBOT |
| cosmic | Pitch → delay → reverb wash | RISE/FALL/WARP/BLKHOLE |
| pingcloud | Ping-pong delay + grains | WIDE/DUB/GRAIN/INFIN |
| meltdown | Wavefolder → reverb → drift | WARM/HARSH/LIQUID/OOZE |

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

## Reference

- [logue-sdk/platform/nts-3_kaoss/README.md](logue-sdk/platform/nts-3_kaoss/README.md) — full API reference (parameter types, curves, runtime hooks, touch events, etc).
- [logue-sdk/developer_ids.md](logue-sdk/developer_ids.md) — registry of known developer IDs.
- [logue-sdk/docker/README.md](logue-sdk/docker/README.md) — Docker build environment details.
