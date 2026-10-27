# Contributing to Aidyn ReChronicled

Thank you for wanting to help. Reports, ideas, art and code are all welcome; this page says how each one fits.

## Reports and ideas

- **A problem** (a crash, a freeze, a wrong picture, anything that looks off): make a problem report and open an issue
  with the [Problem report form](https://github.com/spitzdano/AidynReChronicled/issues/new?template=problem-report.yml),
  as [Found a problem?](README.md#found-a-problem) in the README says. The report's `.zip` (logs, settings and saves,
  nothing from the ROM) is what makes a fix possible.
- **An idea** (a feature, a rule change, a better default): open an issue and say what you'd like and why. Check the
  open issues first: it may be planned already.

## Code: fork, then a pull request

Work in your own fork and send a pull request against `main`; `main` itself is protected. Keep a pull request to one
change, and say in it how you tested it (which version, which place in the game, Linux or Windows).

What you can build from this repository alone:

| Part | Where | Public? |
|---|---|---|
| The port's code (`src/`, `aidyn.us.toml`, `tools/`) | this repository | yes |
| The decompilation the build is made from | `decomp/` ([blackgamma7/Aidyn](https://github.com/blackgamma7/Aidyn)) | yes |
| The renderer and the runtime | `lib/rt64`, `lib/N64ModernRuntime` | yes |
| The redrawn art (portraits, icons, titles, world textures) | `art/` | no: private |
| The restored instrument and effect samples | `sound/` | no: private |
| The HD upscale packs | `hd/` | no: private |

The three private submodules aren't needed to build or run the game: without them it runs with the game's own
pictures and sound (a release's texture and sample packs can be copied into your data folder to see them). Clone with
`git submodule update --init lib/rt64 lib/N64ModernRuntime decomp` and leave the others out.

Building needs **your own ROM** (the North American 1.0 cartridge; Rev 1 doesn't match the decompilation) and the
[n64-recomp-toolkit](https://github.com/spitzdano/n64-recomp-toolkit); the steps are in [Building](README.md#building).
Nothing derived from the ROM is ever committed: `split/`, `RecompiledFuncs/`, `rsp/` and `build/` are regenerated, and
`baserom.z64` is a link to your own dump. Pull requests holding game data can't be taken.

A few habits the code keeps:

- **Patch the game through hooks** (`[[patches.hook]]` in `aidyn.us.toml`, C++ in `src/game/patches.cpp`), not by
  editing the generated code. After changing `aidyn.us.toml`, run `tools/recomp.sh cpu` before building.
- **Comments say why**, in full sentences, above the code they explain; follow the surrounding file's style.
- **Every change to the game's behaviour is listed** in the README (*Changes to the game's behaviour*, *Stubs, ignored
  functions and patches*, *Settings*). A pull request that changes behaviour updates them.
- **Test without touching your own saves**: `tools/dev/pad.sh` runs a test copy of the game you can drive from scripts;
  `AIDYN_MPK` and `AIDYN_SETTINGS_DIR` point it at scratch copies of a save and the settings.

## Art without a fork

Pictures can be replaced without touching the code: the game loads [RT64](https://github.com/rt64/rt64) texture packs
from `texture-packs/` in its data folder (`~/.local/share/AidynReChronicled` on Linux, `%APPDATA%\AidynReChronicled` on
Windows), later packs in `settings.json`'s `texture_packs` winning. Dump the game's textures with `AIDYN_DUMP_TEXTURES=1`,
paint over them, and share the pack (or open an issue with it). Paint over your own dumps: share your painting, not the
game's pictures.

## Licence

The port's code is under the [GPL-3.0](LICENSE); by sending a pull request you agree to your change being under it too.
The game itself, its ROM and its assets belong to their owners and are never part of this repository.
