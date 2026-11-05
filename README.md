# Last Stand

A pixel-art zombie survivors game in C++20, compiled to WebAssembly so it runs in a browser with no install or account. All art, music and sound are generated in code, so the project has no image or audio assets.

**Play in your browser: [shanpla.github.io/Last-Stand](https://shanpla.github.io/Last-Stand/)**

![A soldier fights zombies in a ruined city, with explosions and gems flying](docs/screenshots/gameplay.gif)

| | |
|---|---|
| ![The title screen with its live background, map and difficulty selectors](docs/screenshots/title.png) | ![Three level-up cards showing what each choice changes in numbers](docs/screenshots/level-up.png) |
| ![The ruined city at dusk](docs/screenshots/city.png) | ![The desert highway wasteland](docs/screenshots/desert.png) |

<img src="docs/screenshots/phone.png" alt="The game on a phone-sized screen, with a dash button in the corner" width="260">

## The game

Survive 10 minutes against a growing horde, then fight the final boss. You only move: weapons fire automatically.

- **10 weapons,** each with 5 levels: Rifle, Shotgun, SMG, Sniper, Flamethrower, Grenade Launcher, Homing Rockets, Landmines, Tesla Coil and Combat Blades. Hold up to 6.
- **6 passives** and **5 evolutions.** A maxed weapon plus the right passive evolves into a stronger form.
- **Limited ammo.** Guns use magazines and reloads, and crates keep you supplied. An empty Rifle still fires weak free shots, so you can always fight back.
- **8 zombie types and elites,** plus a 3-phase boss with telegraphed attacks. Its health scales with your build.
- **Two endless maps,** a ruined city and a desert highway wasteland, generated from the run's seed, with a day cycle that moves from dusk into night.
- **Solid wrecked cars.** You and the zombies slide around them, Chargers crash into them, and the boss flattens every one it steps on.
- **A dash with a cooldown,** and **Easy, Normal, Hard and Hell** on the title screen. Play stays one press away.
- **A boss with a move set per level.** Harder levels add new attacks, give shorter warnings, and make it look more horrid.

### Controls

| Device | Move | Dash | Menus |
|---|---|---|---|
| Keyboard | WASD or arrow keys | Space or Shift | Arrow keys, Enter, Esc |
| Touch | The stick in the lower left corner | The DASH button in the lower right corner | Tap |
| Gamepad | Left stick | Right shoulder button | D-pad or stick, A, B, Start |
| Mouse | Not used for moving | Click the dash icon under the weapons | Click |

## How it works

| Topic | Approach |
|---|---|
| Language and libraries | C++20, [raylib](https://www.raylib.com/), CMake, [Emscripten](https://emscripten.org/) targeting WebGL2 |
| Simulation | Fixed 60 Hz step, separate from rendering, with interpolation. Plain C++ with no graphics dependency |
| Repeatable runs | Seeded random numbers, so the same seed and inputs give the same run. Checked by a test |
| Large crowds | Object pools and a spatial hash, with no heap allocation per frame in gameplay |
| Art | Sprites and map chunks painted in code at runtime, using a palette-indexed texture atlas |
| Audio | Sound effects and layered music synthesized in code at startup |
| Solid cars | Car positions come from the seed in code with no graphics, so the simulation and the painted map always agree. No pathfinding: zombies steer to corners |
| Saves | Local storage only, versioned and safe if data is missing or corrupt. No accounts, backend or analytics |
| Offline | An installable web app that keeps working with no connection. Each cached file is checked against the build's own hash |

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) explains the whole system in plain language, including what was chosen, what was rejected and why.

## Measured

Measured on a laptop with an Intel UHD integrated GPU, in headless Chrome. Real phones have not been measured yet.

| Measure | Result |
|---|---|
| Stress test: 2,000 zombies and 3,000 shots at 1920x1080 | Locked at the 144 Hz display, with a 99th percentile frame gap of 7.3 ms |
| 2,000 zombies at phone size, with the processor slowed 6 times as a stand-in for a weak phone | Average above 30 fps, though single frames drop below it. This does not slow the graphics chip |
| Compressed download | About 391 KB on a first visit |
| Tests | 265 unit test cases and 278 browser tests, all passing |

Balance is checked by bots that play 100 games per setting on seeds not used for tuning:

| Difficulty | Novice bot | Average bot | Skilled bot |
|---|---|---|---|
| Easy | Reaches the boss in 55 to 60% of games | Wins 98 to 100% | Wins 99 to 100% |
| Normal | Survives a median of about 3:56 | Wins 26% | Wins 61 to 70% |
| Hard | Survives a median of about 3:40 | Wins 2 to 3% | Wins 26 to 30% |
| Hell | Survives a median of about 2:20 | Never wins | Wins 7 to 13% |

Bots can measure balance, but not whether the game is fun. That needs people playing it.

## Status

The whole game is playable from title to victory or death. A PC download is on the [Releases](https://github.com/ShanPla/Last-Stand/releases) page.

[docs/prototypes](docs/prototypes) shows how the look developed.

## Build it

You need Visual Studio 2022 with C++ tools, CMake 3.24 or newer, Ninja, Node 22 and, for the browser build, [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) 6.0.13 with `EMSDK` set. raylib is downloaded by CMake at a pinned version.

```sh
# Browser build, served at http://127.0.0.1:8080/
cmake --preset web
cmake --build --preset web
npm ci
npm run serve

# Native Windows build, for debugging
cmake --preset native
cmake --build --preset native
```

Add `?debug` to the page URL to show the performance overlay. On the PC build, developer builds show it, and a Release build shows it when started with `--debug`.

## Tests

```sh
# Unit tests and full bot runs, no window needed
./build/native/RelWithDebInfo/ls_tests.exe

# Browser tests, using your installed Chrome
npm run test:e2e

# Plays many bot games and prints balance tables
./build/native/RelWithDebInfo/ls_balance.exe --seeds 100
```

## License

[MIT](LICENSE), copyright Shan Platon.

Third-party parts keep their own licenses: [raylib](https://github.com/raysan5/raylib) (zlib), [Pixelify Sans](src/components/Ui/Font/OFL.txt) (SIL Open Font License) and [doctest](third_party/doctest/LICENSE.txt) (MIT, tests only).
