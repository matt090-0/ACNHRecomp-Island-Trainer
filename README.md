# ACNHRecomp Island Trainer

An unofficial **Windows x64** trainer built into ACNHRecomp's in-game settings. Press **F2** for currency, items, tool upgrades and visitor scheduling.

**Compatibility:** Windows 10/11 x64, an **AVX2-capable CPU** and graphics hardware/drivers compatible with ACNHRecomp **0.6.4**. Animal Crossing: New Horizons **3.0.2**, build `FCD2BB238ABE99E925B4E452DF1F41F1`. This first public release is a beta. It has been tested on one copied island and with native backend tests; compatibility with other game/runtime versions is not established.

## Download and start

1. Download the **Windows x64 ZIP** from this repository's Releases page and extract the entire ZIP into a writable folder.
2. Prepare your own game folder for Windows using [ACNHRecomp-Importer](https://github.com/Nycolazs/ACNHRecomp-Importer-Releases/releases/latest). You need your own legally obtained game files and console keys. This project does not provide them.
3. Save and close any running copy of Animal Crossing. Open **Start Island Trainer.exe**.
4. Choose the prepared **game folder**, which contains `game.ini`, `romfs`, and `game/windows-x64/acnh_game.dll`.
5. The launcher builds a private item catalog from your own RomFS, backs up your saves, and starts the modded player. Load your island, press **F2**, then use **Find / refresh player** if needed.

Hold **Shift** while starting the launcher to choose another game folder. Python is not required for the Windows download. Keep all files from the ZIP together. The trainer uses your existing game folder and saves; it does not need another import.

## Features

- **Wallet:** add 10,000 Bells or fill to 99,999.
- **ABD bank:** add 100 / 1,000 / 100,000 / 100,000,000 Bells.
- **Nook Miles:** add 100 / 1,000 / 100,000 Miles; lifetime earned Miles is updated too.
- **Items:** searchable local catalog, category filters, quantities, customization variants and DIY/cooking recipes. The tested 3.0.2 files produced **10,604 item IDs**, **27,000 variants**, including **922 recipes**. Known internal, dummy, world-only, story-only and unnamed records are excluded.
- **Tools:** repair carried tools, upgrade one tier, or upgrade to golden. Supports the axe, fishing rod, net, shovel, watering can and slingshot families.
- **Visitors:** schedule Redd, Celeste, C.J., Flick, Leif, Saharah, Kicks, Gulliver, Gullivarrr, Label, Mabel and Wisp by weekday.

Close pockets before granting items or upgrading tools. Put away your held tool. Reopen pockets afterwards. Item grants use empty unlocked slots and never deliberately replace occupied slots. Turnip quantity units each represent ten turnips. Save normally to keep changes.

**Visitors are scheduled, not spawned immediately.** Save, return to the title screen and reload your island after scheduling. Normal story unlocks, time, weather and holiday conditions still apply. Celeste visits at night, 7 pm–4 am. An early island must unlock the appropriate visitor first. Celeste and Wisp have separate weekly schedules alongside daytime visitors. Only the current week's weekday schedule is edited.

F1 opens normal settings. F11 / Alt+Enter toggles fullscreen. Existing game controls are unchanged.

## Saves, logs and uninstalling

The launcher copies `user/device_save` and `config.ini` into a timestamped **save-backups** folder beside the launcher before each game launch. Backups are local and are never uploaded. Keep a separate manual backup of saves you care about.

The chosen folder is stored in `trainer-game-folder.txt` beside the launcher. Logs are `trainer-run.log` and `catalog-build.log` in that folder. Local undo records are `bells-menu-undo.log` and `visitors-menu-undo.log` inside your prepared game folder. These files may contain private local information: review them before sharing.

To uninstall, close the game and remove the trainer folder, keeping any backups you want. Start your original ACNHRecomp player. Changes you saved in-game remain in the save. To revert those changes, close all players, manually preserve your current save, then restore a chosen backup into the same prepared game's `user/device_save` folder.

## Troubleshooting and limits

- If another player is running, save and close it first. The launcher refuses concurrent ACNH sessions.
- A version mismatch is rejected. Android, macOS and Linux builds are not included.
- If setup fails, check `catalog-build.log`. Use writable folders and extract the whole ZIP.
- If the catalog needs rebuilding, close the game, rename its `mod-menu-items.tsv`, then start the launcher again.
- Refresh the loaded player after changing islands or residents. No shared empty slot means the grant is refused.
- Visitor state and item persistence were checked after a normal save/reload. Actual NPC appearance and every possible item interaction have not been individually verified.
- This is an unsigned community build. No online-play compatibility is claimed. It has no telemetry or uploader; the underlying runtime retains its upstream network features and settings.

## Source and verification

The repository includes independently written trainer modules, portable launcher, catalog generator, native tests and a patch for the MIT-licensed upstream runtime. See [BUILDING.md](BUILDING.md). SHA-256 checksums accompany release downloads.

Verification covers currency codecs and caps; queued multi-copy writes; empty/locked/occupied slot protection; stack and variant encoding; all six tool chains; visitor schedule field preservation and stale identity rejection. Representative items, stacks, furniture, recipes and golden tools were visible in the original game's pockets UI on a copied save. A fresh production process reloaded the saved items and Redd/Celeste schedules.

## Credits and license

- [ACNHRecomp 0.6.4](https://github.com/Nycolazs/ACNHRecomp/tree/v0.6.4) by Nycolazs and contributors; based on SuperMarioWonderRecomp by BelmanteGu. Upstream MIT notices are retained.
- Format references: [NHSE](https://github.com/kwsch/NHSE) and [ACNHMobileSpawner](https://github.com/berichan/ACNHMobileSpawner). Trainer implementation is independent; no decompiled game code is included.
- SDL3, Dear ImGui, libvpx, libvorbis, libogg, Microsoft C++ runtime, Python, python-zstandard/Zstandard and PyInstaller: see `THIRD-PARTY.txt` and `licenses` in the download.
- Trainer code and release preparation were developed with AI assistance from **OpenAI Codex (GPT-6)** and tested against an isolated local game copy.

Trainer code is MIT-licensed; bundled components retain their respective licenses. This project is not affiliated with Nintendo or the ACNHRecomp maintainers. No game files, keys, generated game library, extracted item names/catalog, or player saves are distributed.
