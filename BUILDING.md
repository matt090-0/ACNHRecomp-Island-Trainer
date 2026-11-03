# Building from source

The Windows binary download needs no build tools. These instructions are for source builds of the first public beta, `v1.2.1-beta.1`.

## Runtime

1. Clone the MIT-licensed upstream runtime into a directory named `runtime-source` beside this README:

   ```powershell
   git clone --branch v0.6.4 --depth 1 https://github.com/Nycolazs/ACNHRecomp.git runtime-source
   git -C runtime-source rev-parse HEAD
   ```

   Expected upstream commit: `7a748acbbffb9a2dcc0d40315147d4ad2acdcc2b`.

2. Apply `patches/runtime-v0.6.4.patch` with `git -C runtime-source apply`, using an absolute path to the patch. Copy `trainer/games/acnh/*` into `runtime-source/games/acnh/`.
3. Follow upstream's Windows dependency/build instructions. Use clang-cl, the Windows SDK/MSVC libraries, CMake/Ninja, SDL3, Dear ImGui, Vulkan headers, libvpx, libvorbis and libogg. Configure `NX_GAME=acnh`, `NX_GAME_FROM_FOLDER=ON`, `NX_LAUNCHER=OFF`, `NX_BELLS_TEST_BRIDGE=OFF`, and `CMAKE_BUILD_TYPE=Release`. Supply `NX_IMGUI_DIR`, `NX_VULKAN_INCLUDE`, and the dependency `CMAKE_PREFIX_PATH` for your machine.
4. Build the `acnh` target. Rename the resulting `acnh.exe` to `acnh-trainer.exe` for the public package.

No game files are required to compile this host runtime. Running it requires the user's own matching importer-prepared game folder. Do not alter the upstream CPU/module/runtime ABI headers: the supported imported game library ABI is `7f5dbf0339d8`.

## Portable launcher

From a Developer PowerShell with clang-cl and the Windows SDK/MSVC INCLUDE/LIB environment configured:

```powershell
clang-cl /std:c++20 /EHsc /O2 portable_launcher.cpp '/Fe:Start Island Trainer.exe' /link /SUBSYSTEM:WINDOWS user32.lib ole32.lib shell32.lib uuid.lib
```

It remembers a user-selected game folder, checks the exact game build, invokes the local catalog builder if needed, backs up saves before launch and refuses concurrent ACNH players. `--game-folder PATH --prepare-only` generates/validates setup without launching the game or modifying saves. Hold Shift or use `--choose-game` to select another folder interactively.

## Item catalog builder

Use Python 3.12.9 with the pinned build dependencies in `requirements-build.txt`:

```powershell
python -m pip install -r requirements-build.txt
python -m PyInstaller --onefile --console --noupx --name 'Build Item Catalog' generate_item_catalog.py
```

Place `Build Item Catalog.exe` beside the launcher. It accepts the prepared game folder and output TSV path as positional arguments. It reads the user's own RomFS and generates the private catalog locally. `--language EUen` is the default. English names are used by this beta. Do not publish the generated TSV, provenance JSON, extracted messages or game assets.

## Tests

Native C++20 tests are `test_bells_codec.cpp`, `test_bells_backend.cpp`, `test_item_catalog.cpp` and `test_visitors.cpp`. After copying the trainer modules into the local `runtime-source` tree:

- Compile the codec test by itself.
- Backend: compile its test with `bells_menu.cpp` and `item_catalog.cpp`; link `/HIGHENTROPYVA:NO` on Windows, and include `runtime-source/runtime/include` plus `runtime-source/runtime/src`.
- Visitor: compile its test with `visitor_menu.cpp` with the same include/link settings.
- Catalog: compile its test with `item_catalog.cpp`; give it a directory containing a catalog generated from your own game. It checks all item/variant quantities and all six tool upgrade chains.

Backend and visitor tests take a new test-data directory as their argument and allocate synthetic guest memory. Runtime integration tests must use copied saves and a production build with the test bridge disabled. Never run two players on the same save folder.

## Packaging

Include the two launcher/catalog executables, `acnh-trainer.exe`, SDL3.dll, the compatible Microsoft C++ app-local runtime, README, licenses and checksums. Runtime dependency versions used here: SDL3 3.4.16, Dear ImGui 1.91.9, libvpx 1.16, libvorbis 1.3.7 and libogg 1.3.6. Keep upstream license notices and the bundled Python/Zstandard/PyInstaller notices.

Only publish an explicit allowlist. Exclude imported game libraries, keys, NSP/XCI files, RomFS, saves, catalogs, logs, backups, local folder configuration, build environments and test artifacts. The repository's `.gitignore` is an additional guard, not a substitute for reviewing the release archive.
