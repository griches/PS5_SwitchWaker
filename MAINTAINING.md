# Maintaining ProsperoWake

ProsperoWake targets PS5. SwitchWaker and the renderer dependencies are pinned
submodules; changes to their code are stored as patches in this repository.

## Where to make changes

| Area | Files |
| --- | --- |
| Gameplay options and controls | `ps5/src/features/` |
| Hooks into game code | `ps5/src/game_hooks/` |
| Disc regions, controller assets and startup integration | `ps5/src/integration/` |
| Options menu and FPS display | `ps5/src/presentation/` |
| Menu tabs, sections and ordering | `ps5/include/pc/pc_setting_presentation.h` |
| Menu translations | `ps5/src/presentation/pc_menu_translations.inc` |
| Rendering effects and shaders | `ps5/aurora/gfx/` |
| Console platform code | `ps5/src/platform.*`, `ps5/src/startup.c`, `ps5/sdl/` |
| Build configuration | `ps5/cmake/` |
| Build and maintenance scripts | `ps5/tools/` |

Edit these files directly. The PS5 menu and FPS display replace their upstream
counterparts, so upstream UI fixes need to be reviewed for the PS5 versions too.

## Changing upstream code

Edit SwitchWaker in `vendor/switchwaker/`, or a dependency in its `.deps/`
checkout. Export the changes before committing the parent repository:

```sh
python3 -B ps5/tools/manage-patches.py --write --target switchwaker
bash ps5/tools/check-source.sh
```

Choose only the targets you changed. For example, use `--target switchwaker aurora`
when both need exporting.

SwitchWaker and Aurora patches live in `ps5/patches/switchwaker/` and
`ps5/patches/aurora/`. Each directory has:

- `series`: the order in which patches apply.
- `topics.json`: which patch owns each upstream file.
- The exported `.patch` files.

Assign newly patched files in `topics.json` before exporting. Each upstream file
belongs to one patch. SDL, Dawn and Abseil use individual patch files under
`ps5/patches/`.

Do not edit exported patch bodies or generate them with a plain `git diff`.
The exporter includes new files and checks that the complete patch series
reproduces the checkout. Aurora's patches apply after SwitchWaker's Aurora patches.

`check-source.sh` checks source layout, assets, translations, dependency pins and
patch consistency. It does **not** compile the game or test it on a console.
Host tests that compile code have separate `check-*.sh` scripts.

## Dependency versions

Git submodule entries pin exact commits. `.gitmodules` supplies their URLs and
paths. The build initializes missing dependencies and checks existing revisions;
it does not reset populated checkouts to discard local edits.

```sh
python3 -B ps5/tools/setup-submodules.py --check
```

Dependency pins must agree with the versions declared by SwitchWaker and Aurora.
The Payload SDK is the exception: `ps5/tools/setup-sdk.sh` pins and installs it
from the separate `PS5_PayloadSDK` checkout. `PS5_Vulkan` supplies the RADV archive,
link recipe, packaging tool and runtime.

## Updating SwitchWaker or a dependency

1. Commit or back up your work, including unexported edits inside submodules.
2. Use a separate checkout with its own dependency directories. Select the new
   revision and stage its submodule entry in the parent repository; pin checks
   read the staged revision.
3. Check the upstream dependency declarations and update affected pins together.
4. Apply the existing patches to the new revisions and resolve conflicts. Compare
   upstream gameplay and controls with the PS5 implementation before retaining
   duplicate features. Remove fixes that upstream now provides.
5. Export the revised patches, run `check-source.sh`, and review the pin and patch
   changes together.
6. Build and test on PS5 before release. Check both disc regions, save/load,
   controller input, menus and any affected rendering or gameplay features.

Keep upstream files for unsupported platforms unless they interfere with the PS5
build. Excluding them from the target usually costs less maintenance than deleting
them from the dependency.

## USA and PAL support

One executable supports revision-0 `GZLE01` and `GZLP01` discs. It uses PAL game
code and class layouts, selecting resource and save formats at runtime. Region
checks belong where disc data differs; ordinary gameplay does not need separate
USA and PAL implementations.

When changing region support, test title and file selection, PAL languages,
USA English text, controller prompts and saving in the `USA` or `EUR` directory.

## Controller artwork

Xelu artwork, layout rules and resource checksums live in
`ps5/assets/controller-ui/`. Edit the PNGs or `layout.json`, then regenerate:

```sh
python3 -B ps5/tools/generate-controller-ui.py
python3 -B ps5/tests/controller-ui.py
```

Commit both the source changes and generated outputs. The generator uses the
Python standard library; it needs no Pillow installation, mod ZIP or game ISO.
Normal builds package `res/resources-us.bin`, `res/resources-pal.bin` and
`res/menu-prompts.rgba`.
Validation manifests remain in the repository.

The bundles contain Xelu textures, generated zoom arrows and layout instructions, not original game
layouts or artwork. At runtime, `pc_ui_transform.cpp` edits validated layouts
loaded from the player's disc and combines the disc's camera-eye texture with
the R3 prompt. It leaves the original resources unchanged. The separate host
check `bash ps5/tools/check-controller-ui.sh` compiles and tests these transforms
using synthetic data.

## Building and releasing

Build prerequisites and installation instructions are in the [README](README.md).
For normal development:

```sh
bash ps5/tools/build.sh
```

Incremental builds reuse dependencies and generated metadata. For a clean rebuild,
stop any active build and run `bash ps5/tools/build-clean.sh`. It preserves source
checkouts and skips directories containing player data.

For a release, build from committed source and test the resulting title on PS5.
Use `PS5_FULL_PROVENANCE=1 bash ps5/tools/build.sh` to record dependency worktree
details in `BUILD_INPUTS.json`. Check shader caches when changing shaders or the driver.

Each normal build invocation reserves the next number in the build directory's
`param.json`. The menu and packaged title use that number. Cleaning the build
directory removes the local counter, so check for an existing release tag before
publishing; the counter is not a repository-wide release registry.

```sh
bash ps5/tools/package-release.sh
```

This reads the build number from the packaged title and creates
`dist/ProsperoWake-build-N.zip` (for example, `ProsperoWake-build-38.zip`).
Use `Build 38` as the GitHub release title and `build-38` as its tag, targeting the
source commit recorded in `SOURCE.txt`. Packaging checks the executable
against its recorded build metadata and rejects uncommitted port source or player
data in the title folder. It also rejects local home-directory paths in packaged
files, including the executable. Compiler path maps preserve relative source
locations; RADV provenance exports revision/hash fields only. Existing objects
recompile when these flags change. Keep attribution and licences with the package.

The packaging script creates an archive locally; it does not tag, push or upload.
Publish the corresponding source and dependency patches with the release.
GitHub's automatic source archives do not include submodule contents.

## Other tools

- `deploy.sh` uploads the packaged title over FTP using PS5_Vulkan's deployment tool.
- `run.sh` launches it and captures console logs through PS5_Vulkan.
- `check-*.sh` runs host tests for individual features; these are separate from normal builds.
- `ps5/art/make-title-art.sh` regenerates the PS5 icon and backgrounds. This optional
  artwork tool needs Pillow and NumPy; building the game and generating controller
  prompts do not.

Run tools directly in the Linux environment. Set `PS5_VULKAN_DIR`,
`PS5_PAYLOAD_SDK_FORK` and `PS5_BUILD_DIR` when using a different directory layout.
