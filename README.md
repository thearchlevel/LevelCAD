<div align="center">

# LevelCAD

### Open Source Electronic Design Automation

**A native C++ application for electronic schematic design and real-time collaboration.**

![C++](https://img.shields.io/badge/C++-20-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?style=flat-square&logo=qt&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-Build_System-064F8C?style=flat-square&logo=cmake&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-Windows_x64-333333?style=flat-square)
![Status](https://img.shields.io/badge/Status-Under_Development-orange?style=flat-square)

**Developed by Wenderson Dias**
</div>

---

## About LevelCAD

LevelCAD started because I wanted to understand what happens behind the interface of an electronic CAD program.

I was using KiCad to work on circuits and kept wondering how its editor handled components, wires, connections, footprints, and all the other things we usually take for granted. Eventually, reading about it wasn't enough. I decided to try building an editor myself.

That's how LevelCAD began.

It's a native EDA application written in **C++20** with **Qt 6**. What started as an experiment with schematic editing has grown into a project that also explores PCB design, circuit checking, SPICE exports, 3D visualization, and collaborative editing.

I'm developing it as a long-term, open-source project. I would like it to become a genuinely useful tool, but I'm not pretending it's ready to compete with mature EDA software. There's still plenty to build, test, and get right.

## Where the project stands

> [!WARNING]
> **LevelCAD is still under active development.** You can try the current development builds, but expect bugs, incomplete workflows, and changes to project files or internal APIs. Don't rely on it for critical production work yet.

The schematic editor is functional, and several of the harder systems now have working initial implementations. Some features are much further along than others.

| Area | Current state |
| --- | --- |
| Native Windows desktop application | Working |
| Schematic editing and component placement | Working |
| Wire drawing, selection, rotation, undo/redo | Working |
| Offline component libraries | Integrated; import compatibility varies |
| Electrical connectivity checks | Basic implementation |
| SPICE netlist export | Implemented |
| Real-time collaboration | Experimental |
| Self-hosted collaboration server | Experimental |
| PCB layout | Working foundation; advanced workflows missing |
| PCB routing and rule checking | Basic implementation |
| 3D PCB viewer | Implemented |
| Gerber and drill output | Available via external tooling, with limitations |
| Linux support | Planned |

This table describes development progress, not production readiness.

## What you can do with it

### Draw schematics

The schematic editor is where LevelCAD began, and it's still a major focus. You can place components, move or rotate them, duplicate objects, draw orthogonal wires, edit component properties, and navigate the canvas with a grid, zoom, and pan controls.

There's also a project explorer for browsing objects in a document.

Under the hood, LevelCAD identifies connections between pins, junctions, nets, and labels. It can run some basic checks and generate SPICE netlists for external tools. More complete electrical rule checking, hierarchical designs, and complex multi-unit components still need work.

One limitation worth mentioning: moving a component may require you to reconnect its wires manually. Automatic wire reconnection isn't finished yet.

### Use offline component libraries

LevelCAD includes locally available KiCad symbol, footprint, and 3D model libraries. You can search the library, place supported symbols on a schematic, and associate them with footprints and models.

The goal is to make the editor usable without having to download components individually. The full library collection is available locally, but that doesn't mean every file is parsed correctly yet. More complex symbols, stacked pins, and some footprints and models still need better support.

The original credits and licenses are kept with the bundled libraries. See [Offline component libraries](docs/local-component-libraries.md) for details.

### Work on a PCB

The PCB side is no longer just a mock-up. LevelCAD has an editor for footprints, pads, tracks, vias, layers, and electrical nets, with basic schematic-to-PCB synchronization.

The 3D viewer can show board geometry, copper, and component models from different angles. There are also basic routing and design checks.

There's still a lot missing before I'd trust it for a serious manufacturing workflow: proper copper pours, push-and-shove routing, differential pairs, complex board outlines, stackups, and more complete DRC are among the bigger tasks.

Manufacturing output currently covers Gerber files, Excellon drill files, BOM, and placement data. Some conversions and checks depend on an installed `kicad-cli`, and only a subset of PCB geometry is supported. See [PCB fabrication](docs/pcb-fabrication.md).

### Collaborate on a schematic

Collaboration is one of the features I've been most interested in building.

Instead of requiring a hosted cloud service, LevelCAD includes a separate server that users can run themselves. Multiple clients can connect to the same session and edit a shared schematic.

The two executables are:

- `LevelCAD.exe` — the desktop editor
- `LevelCADServer.exe` — the collaboration server

The server coordinates shared project state and distributes edits between connected clients.

This is also one of the most challenging parts of the project. Synchronizing changes is one thing; handling simultaneous edits, conflicts, reconnects, and failures reliably is another. Those areas are still being developed and tested.

> [!IMPORTANT]
> The collaboration server is experimental. Review authentication, transport security, and network access before exposing it to an untrusted network.

## Technology

I wanted LevelCAD to remain a native desktop application, with direct control over its editor and underlying data structures. It doesn't use Electron or an embedded browser for the main interface.

| Technology | Used for |
| --- | --- |
| C++20 | Core application and editor logic |
| Qt 6 / Qt Widgets | Desktop interface |
| CMake | Configuring the build |
| Ninja | Build execution |
| MSVC | Windows compilation |
| Git | Version control |

The code is being organized so the interface, schematic data, graphics, project handling, and networking don't become one inseparable block.

This matters for future work such as headless validation, import/export tools, scripting or extension APIs, and testing the CAD core independently of the interface.

## Building LevelCAD

Right now, the tested development setup is **Windows 11 x64**, with **Qt 6.12.0**, **MSVC**, **CMake**, and **Ninja**. The project has been built with the Visual Studio 2026 Insiders toolchain and the Qt MSVC 2022 x64 kit.

You'll need a working C++20 toolchain and the matching Qt kit. Don't mix MinGW Qt libraries with an MSVC build.

### Clone the repository

```bash
git clone https://github.com/wendersondiazx/LevelCAD
cd LevelCAD
```

### Configure

Open an x64 Native Tools Command Prompt, then run:

```cmd
cmake --preset desktop-debug -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64"
```

Adjust the Qt path if you installed it elsewhere.

### Build

```cmd
cmake --build --preset desktop-debug --parallel 8
```

The current Debug configuration produces the application and server under:

```text
build/debug/application/desktop/
├── LevelCAD.exe
└── LevelCADServer.exe
```

### Run

During development, Qt's runtime DLLs and plugins need to be available. One option is to add the Qt binaries to the terminal's `PATH`:

```cmd
set "PATH=C:\Qt\6.12.0\msvc2022_64\bin;%PATH%"
build\debug\application\desktop\LevelCAD.exe
```

You can also use `windeployqt` to collect dependencies next to the executable:

```cmd
"C:\Qt\6.12.0\msvc2022_64\bin\windeployqt.exe" --debug "build\debug\application\desktop\LevelCAD.exe"
```

Release packages should be built and tested separately; Debug DLLs are not intended for public distribution.

### Windows installer

The repository includes `packaging/build-setup.bat` and `packaging/LevelCAD.iss` for preparing a Release package and generating an installer with Inno Setup.

The installer can include the application, required Qt files, shortcuts, and the collaboration server. Test the resulting package on a PC without the development environment before publishing it.

## Using the editor

A basic schematic workflow looks like this: open or create a document, search for a component, place it on the canvas, arrange it, connect the pins, check the resulting connections, and save the project.

These are the shortcuts currently documented for development builds:

| Shortcut | Action |
| --- | --- |
| `S` | Select |
| `P` | Pan |
| `W` | Draw orthogonal wires |
| `R` | Rotate selection |
| `F` | Fit drawing |
| `F8` | Check connections |
| `Esc` | Cancel active tool |
| `Ctrl + D` | Duplicate |
| `Delete` | Remove selection |
| Double-click | Open object properties |

You can zoom using the mouse wheel. Controls may change as the editor evolves.

## Running a collaboration session

You can start `LevelCADServer.exe` separately or from the desktop interface. The standalone server accepts a TCP port and an optional session directory; the default port is **45454**.

Other participants connect using the host's reachable IPv4 address, port, and a display name.

For sessions across different networks, you may need to configure firewall rules, routing, or a VPN. Because the protocol is still changing, I don't recommend putting a development server directly on the public internet.

## What's next

I don't want the roadmap to become a checklist of features that only look finished on paper. My main priority is making the existing systems dependable before piling on more.

Some of the work ahead:

- **Schematic editor:** better wire reconnection, hierarchical designs, complex symbols, and a more complete ERC system.
- **PCB design:** copper zones, improved manual routing, push-and-shove, arbitrary board outlines, and stronger DRC.
- **Collaboration:** consistent synchronization, conflict resolution, permissions, reconnection, and recovery.
- **Simulation:** making the SPICE workflow more complete, from model mapping to running and inspecting results.
- **Project stability:** testing, format compatibility, documentation, and eventually broader platform support.

Real-time collaboration is a particular focus. It's easy to demonstrate two cursors editing a drawing; it's much harder to guarantee that both clients end up with the same valid schematic after concurrent edits and connection failures.

## Why I'm building it

The biggest reason I keep working on LevelCAD is what I've learned from actually implementing the things I used to see only as features in other programs.

A wire isn't just a line on screen. A component isn't just a symbol. The editor has to understand how the objects relate, what counts as an electrical connection, how changes affect the document, and how to preserve those relationships when the user edits something.

Add networking and concurrent editing to that, and the challenge gets even more interesting.

LevelCAD is a way for me to study those problems through real code rather than only theory. If the project becomes useful to other people along the way, even better.

## Contributing

LevelCAD is an open-source project, and contributions, bug reports, and technical discussions are welcome.

If you want to help, take a look at the areas still under development. C++, Qt, geometry, schematic connectivity, PCB routing, testing, and networking are all relevant.

For larger changes, please open an issue or discussion first so we can agree on an approach before anyone spends time implementing it. More detailed contribution guidelines will be added as the repository matures.

## License and credits

LevelCAD is distributed under the **GNU General Public License v3**. See [LICENSE](LICENSE) for the full terms.

Bundled third-party libraries, symbols, footprints, and models keep their respective licenses and attribution notices. In particular, the local electronic component libraries are credited to **KiCad Library Contributors**.

The application uses Qt, CMake, Ninja, and Inno Setup in its development and packaging workflows. Optional GPT, Groq, and Claude integrations are available; the local plugin system currently supports validated JSON actions rather than arbitrary scripts or native plugin DLLs.

LevelCAD is an independent project and isn't affiliated with KiCad, Altium, or other commercial EDA vendors.

**Created by Wenderson Dias**  
Published under **The Archlevel Foundation**  
Made in Brazil.

---

<div align="center">

### LevelCAD

**From Silicon to Software.**

*Built with C++. Driven by curiosity.*

</div>
