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

LevelCAD is an experimental Electronic Design Automation (EDA) application developed primarily in C++20 using Qt 6.

The project started from a simple curiosity: understanding how electronic CAD software works internally.

While working with electronic circuits in KiCad, I became interested in how EDA applications represent electronic components, manage electrical connections, manipulate schematic objects and process circuit designs.

Instead of only studying these concepts, I decided to implement my own CAD application.

LevelCAD is the result of that experiment.

The project now includes schematic capture, electrical connectivity analysis, SPICE netlists, real-time collaboration, PCB layout, 3D visualization, and manufacturing export. Advanced engineering workflows remain under development.

LevelCAD is primarily a learning and research project. Its long-term goal is to evolve into a reliable, extensible and community-driven EDA application.

---

## Project Status

> [!WARNING]
> LevelCAD is currently under active development.
>
> The application is not yet intended to replace established professional EDA software such as KiCad or Altium Designer.
>
> Features, internal APIs, project formats and collaboration mechanisms may change as development progresses.

The current development builds provide a functional graphical environment for creating and editing electronic schematics.

Several advanced EDA capabilities are still experimental or planned.

### Feature Overview

| Feature | Status |
|---------|--------|
| Native desktop application | Implemented |
| Schematic editor | Implemented |
| Interactive 2D workspace | Implemented |
| Component library | Offline libraries with supported symbol, footprint, and model import |
| Component placement | Implemented |
| Component selection | Implemented |
| Component movement | Implemented |
| Component rotation | Implemented |
| Component duplication | Implemented |
| Component properties | Initial implementation |
| Wire drawing | Implemented |
| Orthogonal wire routing | Implemented |
| Undo / Redo | Implemented |
| Project object explorer | Implemented |
| Grid and zoom | Implemented |
| Connection verification | Basic connectivity analysis and diagnostics |
| Real-time collaboration | Initial implementation |
| Self-hosted collaboration server | Initial implementation |
| Advanced electrical rule checking | Basic checks implemented; comprehensive rules pending |
| SPICE netlist generation | Implemented |
| PCB layout editor | Implemented; advanced workflows pending |
| PCB design rule checking | Basic checks and external export DRC |
| Interactive PCB routing | Implemented; push-and-shove pending |
| Gerber export | Implemented through external fabrication tooling |
| 3D PCB visualization | Implemented |
| Linux support | Planned |

Implementation status refers to the current experimental development stage and does not imply production readiness.

---

## Features

### 1. Schematic Editor

LevelCAD provides an interactive graphical environment for creating electronic circuit diagrams.

The editor allows users to place electronic components, manipulate schematic objects and create electrical connections.

Current editing capabilities include:

- Component placement and positioning.
- Object selection and movement.
- Component rotation.
- Component duplication.
- Object deletion.
- Orthogonal wire drawing.
- Grid-based navigation.
- Interactive zoom and panning.
- Undo and redo operations.
- Editing component properties.

The schematic editor is designed to evolve toward a more complete electrical representation system.

Connectivity graphs, net identification, basic electrical validation, and SPICE netlist generation are available. Hierarchical designs, broader multi-unit support, and comprehensive electrical rules remain future work.

### 2. Electronic Component Library

LevelCAD integrates the KiCad component libraries directly into the project. The full downloaded symbol, footprint, and 3D model collections are included locally, allowing offline access without downloading individual components during use.

The integration connects schematic symbols to compatible PCB footprints and associates those footprints with their referenced 3D models. Local caches support library searches and model loading, while original library files and license notices are retained in the project.

Bundling the complete collections does not mean every library entry is supported by the current importer. Multi-unit symbols, stacked pins, and some complex footprint or model formats still require broader compatibility support.

The libraries are credited to **KiCad Library Contributors** and distributed under their respective licenses. See [Offline component libraries](docs/local-component-libraries.md) for library organization and setup details.

Components can be selected from the library panel and placed directly into the schematic workspace.

The component library includes a search interface to simplify component selection.

Supported library symbols carry pin definitions and can be associated with physical footprints and component models. Unsupported multi-unit, stacked-pin, and complex footprint cases still require broader import support.

### 3. Electrical Connections

Electrical connections are represented graphically through schematic wires.

The current editor supports orthogonal wire placement, allowing users to construct circuits using horizontal and vertical wire segments.

The connectivity engine identifies pin connections, junctions, electrical networks, and net labels. Basic diagnostics include missing ground references, floating groups, and pin compatibility checks. SPICE netlists can be generated for external simulators.

Comprehensive ERC coverage and integrated simulation execution remain under development. Geometry that visually touches still needs an electrically valid pin/net mapping.
### 4. Real-Time Collaboration

One of the main experimental features of LevelCAD is its real-time collaboration system.

The application is designed to allow multiple participants to work on the same schematic while connected to a shared collaboration session.

Instead of depending exclusively on a centralized online service, LevelCAD uses a self-hosted server model.

A participant can host the collaboration server and allow other users to connect to the shared project.

The collaboration system currently supports:

- Shared schematic editing.
- Synchronization of project changes.
- Multiple connected participants.
- Collaborative component placement.
- Collaborative circuit editing.
- Self-hosted development sessions.

The project includes a separate server executable named:

```text
LevelCADServer.exe
```

The main application is distributed as:

```text
LevelCAD.exe
```


#### Collaboration Architecture

Conceptually, the collaboration system follows a client-server model:

```text
                 LevelCAD Collaboration
                          |
                          |
                  LevelCADServer
                          |
                 Shared Project State
                          |
             +------------+------------+
             |                         |
             |                         |
        LevelCAD A                 LevelCAD B
          Windows                    Windows
             |                         |
             |                         |
        Developer A               Developer B
             |                         |
             +------ Synchronization --+
```

The server coordinates the collaboration session while the desktop clients provide the graphical editing environment.

The exact synchronization protocol, conflict-resolution rules and network configuration are still subject to development and documentation.

> [!IMPORTANT]
> The collaboration system is experimental.
>
> Do not expose a development server directly to the public internet without reviewing its authentication, access control and transport security.

### 5. Project Object Explorer

The application includes an object explorer for inspecting elements associated with the current schematic.

The interface organizes schematic objects into categories such as components and wires.

This provides an overview of the elements contained in the project.

Component entries can display information such as:

- Component reference.
- Component value.
- Component type.

As the project evolves, the explorer may be extended to support hierarchical schematics, electrical networks and PCB-related objects.

### 6. Native Graphical Interface

LevelCAD uses Qt 6 to provide a native desktop interface.

The application includes:

- A central schematic workspace.
- A component library panel.
- A project object explorer.
- Editing toolbars.
- Application menus.
- Dockable interface panels.
- A dark visual theme.

The interface is designed around the workflow of electronic schematic development.

The application does not rely on Electron or an embedded browser for its main user interface.

---

## Technology Stack

LevelCAD is developed using modern C++ and native desktop technologies.

| Technology | Purpose |
|------------|---------|
| C++20 | Primary programming language |
| Qt 6 | Desktop graphical interface |
| Qt Widgets | Native graphical components |
| CMake | Project configuration and build system |
| Ninja | Build execution |
| MSVC | Windows C++ compiler |
| Visual Studio Code | Development environment |
| Git | Source control |

### Development Environment

The project has been successfully compiled in the following environment:

| Component | Version |
|-----------|---------|
| Operating System | Windows 11 x64 |
| Qt | 6.12.0 |
| Compiler | MSVC 19.51 |
| Visual Studio | 2026 Insiders |
| CMake | 4.3.1 |
| Ninja | 1.13.2 |
| Language Standard | C++20 |

The Windows development environment currently uses Qt libraries distributed for MSVC 2022 x64.

Although the project has compiled successfully with the newer MSVC 19.51 toolchain, this compiler and Qt combination should be validated during future testing.

---

## Software Architecture

LevelCAD is being developed with a modular architecture.

The main objective is to separate the application interface from the core mechanisms responsible for representing and processing electronic designs.

The architecture is intended to support future development without requiring the graphical interface and the underlying CAD engine to remain tightly coupled.

### Architectural Overview

```text
LevelCAD
|
+-- Desktop Application
|   |
|   +-- Main Window
|   +-- Schematic Workspace
|   +-- Component Library
|   +-- Project Explorer
|   +-- Editing Tools
|   +-- User Interaction
|
+-- CAD Core
|   |
|   +-- Document Management
|   +-- Object Representation
|   +-- Geometry
|   +-- Schematic Data
|   +-- Editing Operations
|   +-- Connectivity
|
+-- Graphics
|   |
|   +-- 2D Rendering
|   +-- Grid
|   +-- Viewport Navigation
|   +-- Object Visualization
|
+-- Collaboration
|   |
|   +-- Client Communication
|   +-- Session Management
|   +-- State Synchronization
|   +-- Collaboration Server
|
+-- Future Modules
    |
    +-- PCB Editor
    +-- Electrical Rule Checking
    +-- Design Rule Checking
    +-- SPICE Integration
    +-- Manufacturing Export
    +-- 3D Visualization
```

This diagram describes the intended logical architecture rather than an exact listing of every source directory.

### CAD Core

The CAD core is responsible for representing design objects and managing the data associated with schematic documents.

The architecture is intended to remain as independent as possible from the graphical interface.

This separation is important for implementing features such as:

- Automated design validation.
- Circuit connectivity analysis.
- File conversion.
- Headless processing.
- Command-line utilities.
- External simulation integration.

### Geometry and Coordinates

Electronic CAD applications require accurate geometric representations.

LevelCAD's architecture considers integer-based coordinate systems for internal geometry, reducing dependence on floating-point arithmetic for persistent positioning.

The long-term geometry system is intended to support high-precision operations required by PCB design.

This includes component placement, track geometry, pad positioning, vias and manufacturing clearances.

### Schematic Data Model

A schematic document contains objects representing electronic components and electrical connections.

A simplified representation can be described as:

```text
Schematic Document
|
+-- Components
|   |
|   +-- Identifier
|   +-- Reference
|   +-- Value
|   +-- Position
|   +-- Rotation
|   +-- Pins
|
+-- Wires
|   |
|   +-- Identifier
|   +-- Geometry
|   +-- Connection Points
|
+-- Electrical Networks
    |
    +-- Network Identifier
    +-- Connected Terminals
```

This representation illustrates the data-model concepts rather than a guaranteed serialization format.

---

## Building from Source

### Requirements

Before building LevelCAD, install:

1. A C++20-compatible compiler.
2. CMake.
3. Ninja.
4. Qt 6 with the appropriate compiler kit.
5. Visual Studio Code or another compatible development environment.

For the currently tested Windows configuration, use:

```text
C++ Compiler : MSVC
Qt Version   : 6.12.0
Qt Kit       : MSVC 2022 64-bit
Build System : CMake
Generator    : Ninja
```

The Qt compiler kit must be compatible with the compiler used to build the application.

Do not mix Qt libraries compiled for MinGW with an MSVC build.

### Clone the Repository

Clone the project's repository and enter its root directory:

```bash
git clone <LEVELCAD_REPOSITORY_URL>
cd LevelCAD
```

Replace `<LEVELCAD_REPOSITORY_URL>` with the actual repository address.

### Configure the Build

Open an x64 Native Tools Command Prompt with the MSVC environment initialized.

From the project root, run:

```cmd
cmake --preset desktop-debug -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64"
```

The command configures the desktop development build.

If configuration succeeds, CMake generates the required build files.

### Compile

Run:

```cmd
cmake --build --preset desktop-debug
```

For parallel compilation:

```cmd
cmake --build --preset desktop-debug --parallel 8
```

The number of parallel build jobs can be adjusted according to the available CPU and memory resources.

### Output Files

In the tested Debug configuration, the executables are generated under:

```text
build/
└── debug/
    └── application/
        └── desktop/
            ├── LevelCAD.exe
            └── LevelCADServer.exe
```

Intermediate build files and debugging symbols may also be present.

### Running LevelCAD

During development, the Qt runtime libraries must be available to the application.

One option is to add the Qt binary directory to the current terminal's PATH:

```cmd
set "PATH=C:\Qt\6.12.0\msvc2022_64\bin;%PATH%"
```

Then run:

```cmd
build\debug\application\desktop\LevelCAD.exe
```

Alternatively, the required Qt runtime dependencies can be deployed beside the executable.

---

## Deploying Qt Dependencies

Qt applications typically depend on shared libraries and platform plugins.

On Windows, the official `windeployqt` utility can identify and copy the necessary Qt dependencies.

For a Debug build:

```cmd
"C:\Qt\6.12.0\msvc2022_64\bin\windeployqt.exe" --debug "build\debug\application\desktop\LevelCAD.exe"
```

The deployed application directory may include:

```text
LevelCAD.exe
LevelCADServer.exe

Qt6Cored.dll
Qt6Guid.dll
Qt6Widgetsd.dll

platforms/
    qwindowsd.dll
```

Additional libraries and plugin directories depend on the modules used by the application.

The Debug runtime libraries use the `d` suffix.

For public distribution, a Release build should be created and deployed using the corresponding Release libraries.

---

## Windows Installer

LevelCAD includes packaging/build-setup.bat and packaging/LevelCAD.iss for Release deployment and Windows installer generation. The batch script prepares dependencies, local libraries, models, and resources. Direct Inno Setup compilation uses dist/LevelCAD when StageDir is not supplied. Distribute all generated installer BIN files with the EXE.

The Windows packaging workflow uses Inno Setup to create a conventional installation package.

The packaging scripts provide:

- Application installation.
- Start Menu shortcuts.
- Optional desktop shortcut.
- Qt runtime dependencies.
- Uninstallation support.

The package should contain the main LevelCAD application and the collaboration server when the server is included in the selected release.

Public releases should be built in Release mode and tested on systems without a development environment installed.

---

## Using the Schematic Editor

### Basic Workflow

A typical schematic editing workflow consists of:

1. Open LevelCAD.
2. Create or open a schematic document.
3. Select an electronic component from the library.
4. Place the component in the workspace.
5. Adjust its position and orientation.
6. Connect terminals using the wire tool.
7. Edit component values and properties.
8. Check the connections.
9. Save the project.

The available commands and their behavior may change as the editor evolves.

### Keyboard Shortcuts

The current graphical interface provides the following shortcuts:

| Shortcut | Action |
|----------|--------|
| `S` | Selection tool |
| `P` | Pan / move view |
| `W` | Orthogonal wire tool |
| `R` | Rotate selection |
| `F` | Fit drawing |
| `F8` | Verify connections |
| `Esc` | Cancel active tool |
| `Ctrl + D` | Duplicate selection |
| `Delete` | Delete selected object |
| Double-click | Open object properties |

Mouse-wheel interaction is used for viewport zoom.

These shortcuts correspond to the current experimental editor and may be refined in future versions.

---

## Collaboration Workflow

LevelCAD includes a separate application for hosting collaborative sessions.

The collaboration system is designed around a self-hosted model.

### General Workflow

```text
Host
 |
 +-- Start LevelCADServer
 |
 +-- Open LevelCAD
 |
 +-- Create or join the hosted session
 |
 +-- Share connection information
 |
 +-- Collaborators connect
 |
 +-- Shared schematic editing
```

The host can start the server from the desktop interface or run LevelCADServer.exe with a TCP port and optional session directory. Participants connect using the host IPv4 address, port, and display name. The default standalone port is 45454.

### Networking Considerations

For collaboration between computers on different networks, the host must provide a reachable network endpoint.

Depending on the network environment, this may require VPN connectivity, firewall configuration or other networking arrangements.

Because the collaboration subsystem is still under development, security and access-control mechanisms should be reviewed before exposing it to untrusted networks.

---

## External Circuit Simulation

One of the long-term objectives of LevelCAD is interoperability with external electronic circuit simulators.

Instead of implementing a complete analog simulation engine from scratch, the project intends to support the generation of circuit representations compatible with established simulation tools.

### SPICE Integration

The intended workflow is:

```text
Electronic Schematic
        |
        v
Electrical Connectivity
        |
        v
Circuit Netlist
        |
        v
SPICE-Compatible Output
        |
        v
External Circuit Simulator
        |
        v
Simulation Results
```

SPICE-based tools, such as ngspice, are potential integration targets.

Future development may support:

- Extended component-to-model validation.
- Component-to-model mapping.
- Voltage and current analysis.
- Transient simulation.
- DC operating-point analysis.
- AC frequency analysis.

SPICE netlist generation is implemented. Integrated simulator execution, waveform inspection, and the analysis workflows listed above still require implementation.

---

## PCB Development

PCB editing now supports footprints, numbered pads, electrical nets, copper layers, tracks, vias, and schematic synchronization.

The 3D viewer shows component models, copper traces, pads, hole representations, and vias. Assembled, exploded, transparent, and copper inspection modes include camera presets and visibility controls. Per-pixel depth testing improves overlap rendering.

### Implemented Features

- Footprint placement and physical pin mapping.
- Through-hole and surface-mount pad geometry.
- Multiple copper layers, tracks, and vias.
- Schematic-to-PCB consistency checks.
- Basic automatic routing and rectangular board sizing.
- Basic design checks and 3D copper inspection.
- Project name, revision, and creator metadata.

### Planned Features

- Push-and-shove routing, differential pairs, and length matching.
- Copper zone filling and validated ground planes.
- Robust arbitrary board outlines and comprehensive stackups.
- Complete bottom-side assembly workflows.
- Broader manufacturing constraints and import compatibility.

### Manufacturing Outputs

Manufacturing export provides Gerber, Excellon drill files, BOM, component placement, native project snapshots, and metadata. An installed kicad-cli performs fabrication conversion and external DRC; schematic editing and 3D viewing do not require it.

Export supports a subset of PCB geometry. See [PCB fabrication](docs/pcb-fabrication.md) for prerequisites and limitations.

---
## Current Limitations

LevelCAD is still an experimental EDA application.

The following limitations should be considered:

**Electrical connectivity**

The current connection verification functionality should not be treated as a complete professional ERC implementation.

**Component movement**

In the demonstrated development build, moving a component does not automatically move or reconnect its attached wires.

Connections may need to be adjusted manually after changing component positions.

**Simulation**

Automatic generation of simulation-ready SPICE netlists is still planned.

**PCB development**

The PCB editor and manufacturing export subsystem are not yet part of the functional feature set.

**Collaboration**

The real-time collaboration system is experimental and may require further work on synchronization, reconnection, access control and concurrent editing.

**Project compatibility**

Project and document formats may change before the first stable release.

---

## Development Roadmap

### Current Implementation Update

The foundation, schematic editing, and collaboration phases below are already represented in the application. Connectivity analysis, SPICE output, PCB editing, and 3D viewing have also been implemented at a basic level.

Remaining priorities include comprehensive ERC/DRC, advanced routing, copper zones, robust outlines, integrated simulation, fine-grained collaborative PCB merging, secure collaboration transport, and a stable extension API.

The existing phase list describes development areas, not a claim that every capability in a phase is complete.


### Phase 1 — Foundation

Establish the initial native CAD application.

- [x] C++20 project foundation
- [x] CMake build environment
- [x] Qt 6 desktop application
- [x] 2D schematic workspace
- [x] Component placement
- [x] Basic electronic symbols
- [x] Interactive navigation

### Phase 2 — Schematic Editing

Improve schematic editing and object manipulation.

- [x] Component library interface
- [x] Component search
- [x] Orthogonal wires
- [x] Object selection
- [x] Component movement
- [x] Component rotation
- [x] Undo and redo
- [x] Object explorer
- [ ] Automatic wire reconnection
- [ ] Advanced symbol editor
- [ ] Hierarchical schematics

### Phase 3 — Collaboration

Expand the experimental collaboration subsystem.

- [x] Initial collaboration server
- [x] Initial real-time collaboration
- [ ] Advanced concurrent editing
- [ ] Conflict-resolution improvements
- [ ] Session permissions
- [ ] Reconnection and recovery
- [ ] Collaboration testing
- [ ] Security hardening

### Phase 4 — Electrical Connectivity

Develop the electrical interpretation engine.

- [ ] Complete connectivity graph
- [ ] Pin-based network identification
- [ ] Electrical junction validation
- [ ] Advanced ERC
- [ ] Netlist generation
- [ ] SPICE interoperability

### Phase 5 — PCB Development

Implement the PCB design environment.

- [ ] PCB editor
- [ ] Footprint library
- [ ] Copper layers
- [ ] Manual track routing
- [ ] Via placement
- [ ] Copper zones
- [ ] Design Rule Checking
- [ ] Manufacturing exports

### Phase 6 — Advanced EDA

Explore more advanced design capabilities.

- [ ] 3D PCB visualization
- [ ] External circuit simulation workflow
- [ ] Advanced project organization
- [ ] Custom component libraries
- [ ] Extended platform support
- [ ] Improved automation capabilities

The roadmap is experimental and may change according to research priorities and project development.

---

## Development Philosophy

LevelCAD is primarily a project about learning through implementation.

The objective is not simply to reproduce the interface of existing CAD applications.

The interesting part is understanding the mechanisms underneath:

- How electronic components are represented.
- How schematic objects are transformed.
- How electrical connections are identified.
- How circuit data is stored.
- How multiple users can edit a shared document.
- How CAD software maintains geometric precision.
- How a schematic becomes a physical PCB.
- How electrical models can be exported for simulation.

Implementing these systems provides an opportunity to study C++, software architecture, computational geometry, graphical interfaces, networking and electronic engineering.

The project is intended to remain useful as a learning resource even while its professional CAD capabilities are still incomplete.

---

## Contributing

Contributions, bug reports and technical discussions are welcome as the source code becomes publicly available.

Potential contribution areas include:

- C++ software architecture.
- Qt desktop development.
- Computational geometry.
- Electronic schematic modeling.
- Electrical connectivity.
- PCB design algorithms.
- Real-time communication.
- Automated testing.
- Documentation.

Before making significant architectural changes, contributors should discuss the intended implementation and its impact on the existing project.

As the public repository matures, contribution guidelines and development standards will be documented in `CONTRIBUTING.md`.

---

## Open Source and Licensing

LevelCAD is intended to be an open-source project.

Source distribution terms are documented in the repository LICENSE file.

The repository now includes the GNU General Public License, version 3; see [LICENSE](LICENSE).

Third-party assets retain their own licenses and attribution notices.

Third-party dependencies, including Qt, remain subject to their respective licenses.

---

## Authors and Credits

**LevelCAD**

Developed by **Wenderson Dias**.

Published under **The Archlevel Foundation**.

The project is an independent initiative focused on exploring electronic design automation through practical software development.


### Library and Technology Credits

Symbol, footprint, and component model libraries: **KiCad Library Contributors**, under their respective library licenses. Additional bundled assets retain original attribution, including SparkFun-derived models where present.

Qt Project and contributors provide the desktop and network framework. CMake and Ninja provide build tooling, and Inno Setup provides Windows installer tooling.

**Creator: Wenderson Dias.**  
**Publisher: The Archlevel Foundation.**  
**Made in Brazil.**

AI integrations with GPT, Groq, and Claude are optional. The local plugin system supports validated JSON actions; arbitrary scripts and native plugin DLLs are not executed. Broader extension APIs remain future work.
LevelCAD is not affiliated with KiCad, Altium or other commercial EDA software vendors.

---

<div align="center">

### LevelCAD

**From Silicon to Software.**

*Built with C++. Driven by curiosity.*

</div>

