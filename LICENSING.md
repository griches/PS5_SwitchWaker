# Licences

## Project origin

ProsperoWake is an independently maintained fork of
[PS5_SwitchWaker](https://github.com/griches/PS5_SwitchWaker) by griches and its
contributors. This project retains its platform integration, build tools and
console support.

Inherited code retains its original licences and copyright notices.

## Source licences

The repository as a whole, and any title built from it, is offered under the GNU
General Public License, version 3 ([`LICENSE`](LICENSE)). Its files come from
projects under several licences. Individual file headers and the notices below
identify their terms.

| Path | Licence | Text |
| --- | --- | --- |
| `ps5/tools/png-to-title-dds.py` (encoder from PS5 RetroArch, by Mihawk) | GPL-3.0-or-later | [`licenses/GPL-3.0.txt`](licenses/GPL-3.0.txt) |
| `ps5/src/platform.c`, `ps5/src/platform.h` and the tools taken from the PS5 Vulkan Template (by Mihawk) | MIT | [`licenses/MIT.txt`](licenses/MIT.txt) |
| `ps5/src/emutls.c` (LLVM compiler-rt, adapted in flashman4's PR #5) | Apache-2.0 WITH LLVM-exception | [`licenses/LLVM-compiler-rt.txt`](licenses/LLVM-compiler-rt.txt) |
| The rest of `ps5/src/`, `ps5/tools/`, `ps5/cmake/`, `ps5/art/*.sh` and `ps5/nod/` (including work inherited from PS5_SwitchWaker and PS5_Dusklight) | MIT | [`licenses/MIT.txt`](licenses/MIT.txt) |
| `ps5/sdl/` (SDL3 drivers for the console) and `ps5/patches/sdl.patch` | Zlib, as SDL | [`licenses/Zlib.txt`](licenses/Zlib.txt) |
| `ps5/patches/dawn.patch` | BSD-3-Clause, as Dawn | in Dawn's source |
| `ps5/patches/dawn-abseil.patch` | Apache-2.0, as Abseil | in Abseil's source |
| `ps5/patches/aurora/` | MIT, as Aurora | in Aurora's source |
| `ps5/patches/switchwaker/` | MIT, as SwitchWaker | in SwitchWaker's source |
| The icon and the background | made for this port | |
| `ps5/shader-cache/` | compiled output of this build's shader generator and driver, recorded on a console; no game data | |

## What is built from elsewhere

The dependencies below come from their own projects, under their respective
licences. Git submodules pin the source checkouts; this repository carries the
PS5 patches. The SDK and Vulkan stack are prepared separately.

| Part | Project | Licence |
| --- | --- | --- |
| The game's code | [SwitchWaker](https://github.com/centollOS/SwitchWaker) and the [Wind Waker decompilation fork](https://github.com/snrubrm/tww) | MIT; CC0-1.0 |
| Sound | Dolphin's DSP HLE, as SwitchWaker uses it | GPL-2.0-or-later |
| The GameCube layer | [Aurora](https://github.com/encounter/aurora) | MIT |
| WebGPU | Dawn, at the revision Aurora pins | BSD-3-Clause |
| Windowing, input and sound output | [SDL3](https://github.com/libsdl-org/SDL) | Zlib |
| The Vulkan driver | RADV from [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa), linked by [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) | MIT (Mesa); GPL-3.0 (PS5_Vulkan's tools) |
| The platform layer and SDK | [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) | GPL-3.0 |

## A built title

A built `eboot.bin` links the GPL-3.0 platform layer and Dolphin's
GPL-2.0-or-later DSP HLE with everything above, so a title as distributed is
under **GPL-3.0**. The player supplies their own disc image. Original game layouts
and camera artwork are read from that disc at runtime.

## Controller UI

Controller button artwork is by **Xelu**, from [Open Game Art Prompts](https://thoseawesomeguys.com/prompts/),
released under [CC0](https://creativecommons.org/publicdomain/zero/1.0/).
The original PS5 PNGs are bundled in `ps5/assets/controller-ui/xelu/`.
ProsperoWake generates its button textures and ImGui atlas from those files,
with its own adjustments to stock game layouts. No third-party mod assets are used.
The controller bundles contain generated Xelu textures, ProsperoWake zoom arrows
and layout instructions. The generated arrow artwork is MIT-licensed. Original
game layouts and camera-eye artwork are loaded from the player's disc and
modified in memory. No stock layouts or extracted game artwork
are included in these bundles or their source directory.
See [asset credits](licenses/Controller-prompts.txt).
