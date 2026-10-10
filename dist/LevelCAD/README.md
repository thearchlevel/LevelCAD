# LevelCAD

**LevelCAD** is an early-stage, community-oriented, native electronic design automation (EDA) project written in **C++20**.

> **Status: 0.1.0 foundation / experimental. Not ready for production electronic design or fabrication.**

## What works today

- Qt 6 Widgets desktop application with dockable panels and a dark canvas.
- Grid, pan (Select tool), cursor-centered wheel zoom.
- Place resistors, capacitors, ground symbols, and basic point-to-point wires.
- Save/load schematic files (`.levelsch`, versioned JSON; 64-bit coordinates stored as decimal strings).
- Qt-independent C++20 document model and basic automated tests.

**Not implemented:** electrical net connectivity/ERC, PCB editor, footprint assignment, 3D viewer, manufacturing exports, simulation, undo/redo, component editing, plugin system. Modules in the long-term architecture are planned, not shipping.

## Building (Windows, Linux)

Requirements: CMake >= 3.21, C++20 compiler, Qt >= 6.4 (Widgets), Ninja recommended.

```sh
cmake -S . -B build -DLEVELCAD_BUILD_DESKTOP=ON -DLEVELCAD_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

For **VS Code**: install CMake Tools and C/C++ extensions, configure the Qt SDK in `CMAKE_PREFIX_PATH` if Qt is not detected. Example (edit to your Qt install):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build
```

Qt MSVC builds require a matching MSVC toolchain; MinGW Qt needs a compatible MinGW compiler. On Linux, install Qt 6 development packages and a C++20 compiler.

Build/test only the engine without Qt:

```sh
cmake -S . -B build-core -DLEVELCAD_BUILD_DESKTOP=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## Repository layout

- `engine/core/` — Qt-free geometry units and editable document model.
- `application/desktop/` — Qt 6 desktop, rendering and versioned file I/O.
- `tests/` — independent engine tests.
- `docs/` — architecture, roadmap and formats.
- `cmake/`, `graphics/`, `simulation/`, `exporters/`, `plugins/`, `libraries/`, `tools/` — reserved for future modules; see their README files.

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE). Contributions are subject to the same license unless otherwise noted.

## Contributing

Read [`CONTRIBUTING.md`](CONTRIBUTING.md) and open an issue before proposing substantial architectural changes.
