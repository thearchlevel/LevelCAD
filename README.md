<div align="center">

# LevelCAD

### Native Electronic Design Automation

Schematic capture, PCB layout, 3D visualization, and real-time collaboration built with C++20 and Qt 6.

**Created by Wenderson Dias**  
**Published by The Archlevel Foundation**  
**Made in Brazil**

</div>

## About

LevelCAD began as an experiment in understanding how electronic CAD applications represent components and electrical connections. It now combines schematic editing, PCB development, offline libraries, and collaborative workflows in a native desktop application.

The project remains under active development. Designs require engineering review before fabrication.

## Features

| Area | Available functionality |
| --- | --- |
| Schematic capture | Components, properties, connections, net labels, comments, editable background areas, and undo/redo |
| Connectivity | Pin and net identification, connectivity graphs, basic electrical diagnostics, and SPICE netlists |
| PCB layout | Footprints, copper layers, tracks, vias, interactive routing, board appearance, and basic design checks |
| Synchronization | Component and pin mapping, electrical net synchronization, consistency checks, and board sizing |
| 3D visualization | Component models, copper traces, pads, hole visualization, vias, camera presets, and layer inspection |
| Collaboration | Self-hosted server, hosting from the interface, user selection labels, edit logs, host-controlled saving, and moderation |
| Automation | GPT, Groq, and Claude integrations for proposed CAD operations, plus a local plugin manifest system |
| Workspace | Launcher, loading screen, recoverable side panels, language selection, and project object explorer |
| Manufacturing | Gerber, Excellon, BOM, placement files, project metadata, and export validation |

Libraries support offline use. Import compatibility depends on the symbol, footprint, and model formats supported by the current implementation.

## Latest Improvements

- Added assembled, exploded, transparent, and copper inspection views.
- Added component and copper visibility controls.
- Improved 3D depth rendering and fixed overlap artifacts.
- Corrected pad rotation, rounded corners, and via hole visualization.
- Added schematic-to-PCB checks for components, pins, and electrical nets.
- Added basic automatic routing and board sizing.
- Added manufacturing exports with project name, revision, and creator.
- Improved manufacturing validation and error reporting.

All seven Release test suites passed during the latest validation. Manufacturing integration also passed external DRC and Gerber/Excellon checks on the tested fixture.

## Technical Architecture

### Core Data Model

The engine stores geometry using signed 64-bit integer coordinates in nanometers: 1 mm equals 1,000,000 nm. Persistent numeric IDs identify document objects independently of their display labels.

Native JSON files encode coordinates and IDs as decimal strings to preserve integer precision. The schematic format is identified explicitly, and project data can include an embedded PCB and component meshes.

The core data model is separated from Qt presentation code. The desktop layer handles interaction, dialogs, network sessions, serialization adapters, and rendering.

### Electrical and PCB Engines

The connectivity engine derives electrical networks from pins, connections, junction geometry, and labels. Basic diagnostics identify conditions such as missing ground references, floating groups, and incompatible pin relationships.

The PCB model stores footprints, numbered pads, nets, tracks, vias, layers, board dimensions, and project metadata. Synchronization maps schematic components and pins onto physical footprints and checks their correspondence.

Automatic routing uses a limited set of candidate paths with clearance checks. It does not provide a production-grade push-and-shove router, comprehensive high-speed constraints, or guaranteed completion.

### Rendering and Background Work

The 3D viewer projects component meshes and board geometry through a software renderer with per-pixel depth testing. Component meshes retain supported source materials. Transparent and exploded views help inspect copper and layer placement.

Background work uses Qt Concurrent where integrated, including expensive preparation and export tasks. Mesh caching reduces repeated model parsing. Performance still depends on project size, embedded mesh complexity, and full-document collaboration messages.

### AI and Extensions

The automation interface supports user-provided API credentials for GPT, Groq, and Claude integrations. Proposed operations go through the application's command validation and review flow.

The plugin system currently uses versioned JSON manifests defining actions. It does not execute arbitrary scripts or native plugin DLLs. A broader extension API, process isolation, richer events, and compatibility guarantees remain future work.

### Repository Layout

| Directory | Responsibility |
| --- | --- |
| engine/core | Document objects, identities, and core data |
| engine/connectivity | Electrical graph analysis and diagnostics |
| engine/pcb | PCB data, synchronization, routing helpers, and checks |
| application/desktop | Qt UI, rendering, collaboration, automation, and export integration |
| application/server | Standalone collaboration server entry point |
| libraries | Offline symbols, footprints, meshes, and attribution |
| resources | Application resources and translations |
| tests | Automated regression and integration checks |
| packaging | Release deployment and Inno Setup installer |
| docs | Supporting technical documentation |

Some repository folders and supporting documentation describe intended modules. Their existence alone does not mean the proposed functionality is implemented.

## Implementation Status and Roadmap

| System | Already available | Still required |
| --- | --- | --- |
| Schematics | Editing, labels, comments, backgrounds, connectivity, basic diagnostics | Broader hierarchical/multi-unit support and more complete electrical rules |
| PCB | Footprints, routing, vias, layers, synchronization, basic checks | Copper zone filling, robust arbitrary outlines, comprehensive stackups and manufacturing constraints |
| Routing | Manual routing and limited automatic path candidates | Push-and-shove, differential pairs, length matching, and high-speed constraint handling |
| 3D | Imported meshes, depth-tested display, copper and layer inspection | Full substrate hole geometry, richer materials, and more accurate assembly visualization |
| Manufacturing | Gerber/Excellon integration, BOM, placement, metadata, external DRC | Broader geometry coverage, complete bottom-side workflows, and more manufacturing fixtures |
| Collaboration | Shared state, field-level schematic merging, presence, audit, moderation | Fine-grained PCB merging, secure identities, reconnect/replay, and scalable delta transport |
| Simulation | SPICE netlist generation | Integrated simulation execution, waveform analysis, and broader model validation |
| Extensions | Validated JSON actions and AI proposals | Stable public extension API and isolated executable extensions |

These are development priorities rather than release-date commitments.
## Building on Windows

Requirements: Visual Studio C++ tools with C++20 support, CMake, Ninja, and Qt 6 with Widgets, Network, and Concurrent modules.

Use a Visual Studio x64 Native Tools command prompt. Adjust the Qt path to match your installation.

### Debug

~~~bat
cmake --preset desktop-debug -DCMAKE_PREFIX_PATH="C:\Qt\6.12.0\msvc2022_64"
cmake --build --preset desktop-debug --parallel 8
ctest --test-dir build\debug --output-on-failure --timeout 180
~~~

### Release

~~~bat
cmake -S . -B build\release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DLEVELCAD_BUILD_DESKTOP=ON ^
  -DLEVELCAD_BUILD_TESTS=ON ^
  -DCMAKE_PREFIX_PATH="C:\Qt\6.12.0\msvc2022_64"

cmake --build build\release --parallel 8
ctest --test-dir build\release --output-on-failure --timeout 180
~~~

Executables:

~~~text
build/release/application/desktop/LevelCAD.exe
build/release/application/desktop/LevelCADServer.exe
~~~

Make Qt DLLs available through PATH or deploy them with windeployqt before running the application.

## Windows Installer

Install Inno Setup 6 or 7, then run from the repository root:

~~~bat
packaging\build-setup.bat
~~~

The script builds Release, runs tests, deploys Qt dependencies, and packages the application with local libraries, models, resources, and documentation.

Installers are written to <code>dist/setup</code>. Distribute the executable together with every generated <code>.bin</code> file.

Opening <code>packaging/LevelCAD.iss</code> directly uses the prepared <code>dist/LevelCAD</code> distribution when StageDir is not supplied. Use the batch script to prepare an updated release first.

## PCB and Manufacturing Workflow

1. Create or import a schematic and review its connections.
2. Assign compatible physical footprints.
3. Synchronize the PCB and verify pin-to-pad mapping.
4. Arrange components, configure layers, and complete routing.
5. Inspect the board and copper in 3D.
6. Run design checks before exporting manufacturing files.

Manufacturing export uses an installed kicad-cli for Gerber, Excellon, and external DRC processing. Editing and 3D visualization do not require this tool.

See [PCB fabrication](docs/pcb-fabrication.md) for supported geometry and export requirements.

## Collaboration

LevelCAD includes a self-hosted client/server collaboration system. The server maintains the authoritative project state, validates submitted edits, and distributes accepted changes to connected desktop clients.

### Hosting and Joining

- Start a hosted session from the desktop interface, or run the separate collaboration server.
- Choose a TCP port and share the IPv4 address of the network interface reachable by participants.
- Participants enter the host address, port, and a display name.
- The first valid project submitted to an empty session initializes its shared state.
- Later participants receive the server's project instead of replacing it with their local document.
- During collaboration, the host is responsible for saving the project's working file. The server also maintains its own session snapshot and edit log.

The standalone server accepts a port and an optional session directory:

~~~bat
LevelCADServer.exe 45454 "C:\LevelCAD-sessions\MyProject"
~~~

Without a port argument, it prompts for a port; the default is 45454. It lists available IPv4 addresses at startup.

### Transport and Protocol

The implementation uses Qt Network: QTcpServer on the server and QTcpSocket on each client. Messages are compact JSON objects separated by newline characters over a TCP connection.

The current handshake identifies protocol version 1. Message categories include:

| Message | Purpose |
| --- | --- |
| hello / welcome | Negotiate the session and deliver the authoritative document |
| edit | Submit an edited document together with its baseline |
| state | Broadcast the accepted document and associated audit entries |
| conflict | Reject conflicting changes and return the current document |
| presence | Share selected object IDs and the active workspace |
| roster | Update connected participants and host moderation information |
| admin / denied | Process moderation requests or report access rejection |

The receiver applies a 64 MiB buffer limit. This is a basic resource bound, not a complete solution for large embedded model transfers or abusive clients.

Each participant receives an actor identifier and a separate object ID range. The server verifies that newly created schematic objects belong to the submitting actor's range.

### Concurrent Editing and Conflict Handling

Schematic merging covers the symbols, wires, and notes collections. Objects are matched by persistent ID.

For an edited field, the server compares the submitted baseline with its current value. Independent field changes can merge. If another participant has already changed the same field, or an object has been deleted or replaced, the submission is rejected rather than silently overwriting that change.

Changes are first applied to a candidate document and validated. The candidate replaces the shared state only after successful validation.

PCB editing currently has coarser conflict handling: the embedded PCB layout is merged as one block. Two simultaneous PCB modifications can conflict even when they affect different footprints or tracks. Per-object PCB merging remains future work.

When a conflict occurs, the client writes its submitted document to a temporary recovery file named like:

~~~text
LevelCAD-conflict-<actor>.levelsch
~~~

It then receives the authoritative server state. The recovery file allows manual inspection of the rejected proposal; it is not an automatic conflict-resolution interface.

### Presence and Selection

Selection messages carry the workspace name and selected object IDs. Desktop views use this information to display participant labels on selected objects.

Presence is separate from persistent circuit geometry. It shows activity without adding annotations to the saved schematic. Selection visibility does not provide object locking or exclusive ownership.

### Audit Trail and Persistence

The server maintains these files in its session directory:

| File | Contents |
| --- | --- |
| project.levelsch | Current shared project snapshot |
| edits.jsonl | Append-only JSON audit entries |
| blocked.json | Persisted blocked addresses |

Accepted schematic changes record the collection, object ID, field, and previous/new values where applicable. Audit entries also include the display name, actor/session ID, UTC timestamp, and server revision.

PCB changes are logged as a layout update rather than individual track or pad edits. Display names identify session participants but are not verified user identities.

Project snapshots use QSaveFile for atomic file replacement. The audit log is appended separately; snapshot and log writes are not a single transactional database operation.

### Host Controls

The desktop host can inspect participants, disconnect a selected participant, block their address, and remove an address from the block list.

Administrative access requires the configured host token and a loopback connection. Ordinary participants cannot issue accepted host moderation commands.

Blocking is address-based, not account-based. Participants sharing an address can be affected together, and an address change can bypass an existing block. Standalone server startup currently does not expose the desktop host's token-management workflow.

### Current Networking Scope

The current transport is plain TCP. TLS encryption, verified accounts, room passwords, role-based editing permissions, and public relay services are not implemented.

LAN operation requires a reachable host address and a firewall rule allowing the chosen TCP port. Internet operation additionally needs suitable routing or a private network tunnel; the application does not automatically traverse NAT or configure port forwarding.

### Collaboration Work Still Required

- Per-object and per-field PCB merging.
- Delta-based updates instead of transmitting complete document snapshots.
- Automatic reconnection, queued offline edits, and deterministic replay.
- A conflict review interface with selective recovery.
- TLS, authenticated identities, access permissions, and room admission controls.
- Transactional persistence, backup rotation, and explicit disk-write failure recovery.
- Multi-room hosting, relay support, rate limiting, and larger-session stress testing.
- A documented policy for collaborative undo and simultaneous object manipulation.
## Example Design

The separately generated **ATLAS S3** example contains 163 components, embedded models, and a four-layer PCB concept based on the ESP32-S3.

The example still has 58 unrouted connections. Complete routing and review power, antenna clearance, signal integrity, and component requirements before fabrication.

## Current Limitations

- Automatic routing and electrical/design checks remain basic.
- Some imported symbols, footprints, and model formats are unsupported.
- Manufacturing export supports a subset of geometry; custom outlines and complete bottom-side assembly workflows are not yet supported.
- Hole visualization does not simulate full substrate cutouts or solder geometry.
- AI proposals require review and do not establish electrical correctness.
- Project formats and internal APIs may change.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [File format](docs/FILE_FORMAT.md)
- [Offline component libraries](docs/local-component-libraries.md)
- [PCB fabrication](docs/pcb-fabrication.md)
- [Roadmap](docs/ROADMAP.md)

## Contributing

Bug reports, reproducible examples, documentation improvements, and code contributions are welcome. Discuss significant architectural changes before implementation.

Areas for improvement include validation, routing, import compatibility, rendering, collaboration, and manufacturing tests.

## License and Credits

### Project

**Creator and developer: Wenderson Dias.**  
**Publisher: The Archlevel Foundation.**  
**Origin: Brazil.**

LevelCAD is an independent electronic design automation project. Its goal is to develop practical CAD tools while making the architecture and implementation accessible for study and contribution.

### Source License

The repository includes the GNU General Public License, version 3. Read [LICENSE](LICENSE) for the terms distributed with this source tree.

Bundled third-party assets and dependencies retain their own licenses. The application's source license does not replace those terms.

### Component Libraries and Assets

**KiCad Library Contributors** are credited for the symbol, footprint, and component model libraries used by the project. Library license files and attribution notices are retained with the local distribution.

Additional model assets, including bundled SparkFun-derived assets where present, retain their original attribution and license notices. Consult the relevant library directories before redistributing assets separately.

### Technology and Tooling

- **Qt Project and contributors:** desktop UI, networking, and concurrent tasks.
- **CMake and Ninja contributors:** build configuration and execution.
- **Inno Setup contributors:** Windows installer tooling.
- **External fabrication tooling:** manufacturing conversion and DRC integration.

AI services are optional integrations accessed through user-provided credentials. Their providers' service terms apply independently.

### Contributors

Bug reports, reproducible test cases, documentation, and code contributions help improve the project. Specific contributor acknowledgments can be added as contributions are accepted.

Library and technology attribution does not imply affiliation, endorsement, or sponsorship.
