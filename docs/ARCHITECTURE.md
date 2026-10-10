# LevelCAD Architecture

## Principles

1. **Engine independence**: no Qt, GUI, or OpenGL dependencies inside the core data model.
2. **Exact stored coordinates**: signed 64-bit nanometers; 1 mm = 1,000,000 nm. Floating point is for viewport display only.
3. **Stable identities**: document objects have persistent numeric IDs.
4. **Versioned files**: `.levelsch` contains an explicit schema name/version. Coordinates and IDs use decimal strings to preserve full integer precision in JSON.
5. **Separation of concerns**: core owns data; Qt application owns presentation, input, and current serialization adapters.

## Current implementation

`engine/core` -> `application/desktop` (depends on Qt Widgets)

`tests` -> `engine/core` (no Qt required)

## Intended modules (not yet implemented)

- `engine/geometry`: robust transforms, intersection, spatial indexes.
- `engine/connectivity`: netlist graph, ERC, pin electrical rules.
- `engine/pcb`: board stackups, footprints, vias, copper shapes.
- `engine/drc`: manufacturing constraints.
- `engine/router`: shove/walk-around manual and assisted routing.
- `graphics`: separate accelerated viewport implementation.
- `exporters`: Gerber X2, Excellon, BOM; rigorous golden-file tests.
- `simulation`: optional ngspice adapter.
- `plugins`: versioned extension API and isolation strategy.

## Shortcomings to resolve before production use

- Currently no undo/redo, selection model, edit-in-place, or connectivity semantics.
- Scene rendering is rebuilt after insertions; project tree refreshes at save/load only.
- Need clean/dirty tracking, confirmation before discarding edits, schema validation/migration, and atomic document editing.
- Wire tool supports endpoint clicks but does not magnetically snap to component pins.
- No electrical correctness or manufacturing guarantee.
