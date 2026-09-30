# Toolchain Decision Document

> Team **Pathfinder** · Project **InNavi**
> Owned by: **Tech Lead.** This records the team's stack and platform decisions -
> framework, language, libraries/plugins, AI tools, file/data formats, and target
> devices/platforms - each with a short reason. This is the baseline every other
> technical obligation is measured against; any later change to it requires
> instructor approval.

## Engine / Framework

| Choice | Reason |
|---|---|
| **Unreal Engine 5.8** | A standalone runtime application (not just an editor tool) was required from the start - UE ships both an editor and a real packaged executable from the same project. Its built-in `ProceduralMeshComponent` and UMG cover the two hardest parts of this product (runtime-generated interior geometry, and a fully custom desktop UI) without needing a separate rendering or windowing stack. |

## Programming Language

| Choice | Reason |
|---|---|
| **C++** | Every gameplay/UI system (building generation, interior editor, navigation, UI) is written in native C++, not Blueprint. |
| **Pure-code UMG (`UUserWidget::RebuildWidget()`), not Blueprint Widget Designer** | The team started in the Blueprint Widget Designer, but hit real friction building a moderately complex feature (a building-switcher dropdown) that way. Switching to building every screen as a plain C++ `UUserWidget` subclass means the whole UI lives in text files git can diff and merge normally, instead of opaque `.uasset` Blueprint graphs that conflict badly between team members working in parallel. |

## Libraries / Plugins

| Choice | Reason |
|---|---|
| **`SQLiteCore`** (Epic's own bundled engine plugin - `FSQLiteDatabase`/`FSQLitePreparedStatement`) | The exterior building/road/POI data pipeline needs to read a real SQLite database (the course's own OSM-derived campus data format). Epic's own plugin does this without adding a third-party dependency or marketplace asset - it ships with the engine install already. |
| **`ProceduralMeshComponent`** (built-in) | Every generated shape in this project (building shells, interior rooms/walls, road ribbons, connector markers) is built from runtime data, not authored as static meshes - this is the standard UE mechanism for that. |
| **Own `WavFileLoader.h`** (in-house, no third-party plugin) | Playing back arbitrary audio files at runtime (button clicks, UI feedback, background audio) needs decoding outside the normal asset-import pipeline. A third-party plugin (`RuntimeAudioImporter`) was vendored early on for mp3 support, then removed once the professor confirmed wav-only playback is sufficient - a small in-house wav reader covers that with no external dependency. |

## AI Tools

| Choice | Reason |
|---|---|
| **Claude Code** (Anthropic) | Assisted with implementing and debugging features, and with keeping project documentation in sync with the codebase, throughout development. |

## File / Data Formats

| Choice | Reason |
|---|---|
| **JSON** (`buildings.json` and friends) | The primary save/load format for hand-authored building + interior data (rooms, walls, doors, connectors). Human-readable, diffable in git, and UE's own `FJsonObject`/`FJsonSerializer` read/write it with no extra dependency. |
| **SQLite (`.db`)** | A second, additional loading path (not a replacement for JSON) for real-world campus data - roads, POIs, and building footprints sourced from OSM-derived data in the same schema the course's own reference material uses. Chosen because that source data already ships in this format, and `SQLiteCore` (above) reads it natively. |
| **WAV** (`InNavi/Data/Audio/*.wav`) | Confirmed sufficient by the professor for this project's UI/feedback sound needs (button clicks, selection sound, notification sound, plus arbitrary background audio) - avoids needing the mp3-decoding third-party plugin mentioned above. |

## Target Devices / Platforms

| Choice | Reason |
|---|---|
| **Desktop, Windows-first** | The team originally proposed splitting a desktop editor and a mobile navigation app; **confirmed with the professor** to instead combine both into one application with an Edit/Navigation mode toggle, and to target desktop first for Milestone 1, extending to mobile in M2/M3. Windows is the platform actually packaged/tested so far (Win64 Development build); the dev environment itself is also expected to work on Ubuntu and macOS per the team's own README, though the packaged M1 deliverable is Windows. |
