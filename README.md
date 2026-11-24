# ACZUltrawide

Ultrawide support for the Steam version of **ACE COMBAT ZERO: THE BELKAN WAR**.

The game only offers 16:9 and 4:3. This mod adds your ultrawide resolution to the launcher, widens the 3D view and
keeps the HUD in its correct proportions. It is loaded as a `version.dll` proxy and patches the game in memory, so
`acz.exe` is never modified.

Unofficial fan project, not affiliated with or endorsed by Bandai Namco. No game files are included; you need your own
copy of the game.

## Features

- Native ultrawide rendering and swap chain, no black bars in missions
- Hor+ field of view with matching culling, in cockpit, HUD and chase view and in real-time cutscenes
- HUD, radar, pause menu, subtitles, movies and info cards in a centered 16:9 area
- Target markers line up with the world and also appear in the areas outside 16:9
- Menus, briefing and hangar keep their original layout, centered and undistorted

## Installation

1. Copy `version.dll` and `ACZUltrawide.ini` from the [`release`](release) folder (or the zip) into the folder that
   contains `acz.exe` (`...\ACE COMBAT ZERO THE BELKAN WAR (Steam Version)\ACZero`).
2. Start the game. In the launcher choose the resolution marked **(Ultrawide)**, which replaces 2048 x 1152, and set the
   display mode to **Borderless**.

The resolution of your primary monitor is used unless you set `Width` and `Height` in the ini.

**Uninstall:** delete `version.dll`, `ACZUltrawide.ini` and `ACZUltrawide.log`, then pick another resolution.

## Configuration

`ACZUltrawide.ini`:

| Setting | Default | Meaning |
|---|---|---|
| `Enabled` | 1 | 0 turns the mod off |
| `Width`, `Height` | 0 | Resolution to add; 0 = desktop resolution of the primary monitor |
| `FixFOV` | 1 | Hor+ field of view; 0 leaves the picture stretched |
| `FixHUD` | 1 | Centered 16:9 HUD and movies; 0 = stretched to the full width |
| `Debug` | 0 | 1 = F10 dumps draw calls to a csv file, F9 reloads the ini |

## Compatibility

Supported game version: Steam build **25908842** only. Any other `acz.exe` is detected, noted in `ACZUltrawide.log`, and
left alone, so after a game update the mod has to be updated too.

Tested: 3440 x 1440 borderless on Windows 10, menus and the first mission.

Not tested: other resolutions, windowed mode, VS split screen, replays, missions with a visible sun (the lens flare may
be misplaced), `FixFOV=0` and `FixHUD=0`, Linux/Proton.

Known limits:

- Maximum width is 4095 pixels. For wider monitors set for example `Width=3840` and `Height=1080`.
- The in-game 4:3 aspect option is ignored while an ultrawide resolution is active.
- Windows has to be installed in `C:\Windows` (the proxy forwards to `C:\Windows\System32\version.dll`).
- Cannot be combined with another mod that also uses `version.dll`.

If something does not work, please open an issue with your resolution and the contents of `ACZUltrawide.log`.

## Building

Requires Visual Studio 2022 (or the Build Tools) with the C++ workload. Run `build.cmd`; the result is
`build\version.dll`.

## How it works

The port translates the original PS2 graphics commands to Direct3D 12.

- **Resolution:** the launcher table, the settings parser and the internal render target, normally fixed to
  `height * 16 / 9`, are patched.
- **Field of view:** the mission camera uses a 640 x 360 film that is stretched over the screen. The film is widened before
  the projection and culling matrices are built.
- **Menus:** these use a virtual canvas 682 units wide, which is enlarged so that its 512 unit center keeps its proportions.
- **HUD:** flat 2D primitives are recognized by their emulated GS state and drawn into a centered 16:9 area. The HUD code
  receives a copy of the camera that maps to that area while keeping the ultrawide clip test.
- **Radar:** its scissor table assumes 16:9 and is rescaled.

The executable is unpacked by its Steam wrapper at startup, so the patches are applied from functions the game calls
right afterwards.

## License

[MIT](LICENSE)
