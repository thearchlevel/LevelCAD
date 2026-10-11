# Local official EDA assets

Downloaded from the official KiCad Library Contributors repositories:
- https://gitlab.com/kicad/libraries/kicad-symbols
- https://gitlab.com/kicad/libraries/kicad-footprints
- https://gitlab.com/kicad/libraries/kicad-packages3d
- https://gitlab.com/kicad/libraries/kicad-templates

Original licenses and per-category author credits remain in each repository.
The libraries use CC-BY-SA 4.0 with the exception described in LICENSE.md.

The STEP originals are preserved. mesh-cache contains derived VRML meshes produced
locally using the installed official CLI. The LevelCAD viewer reads mesh geometry,
hierarchical transforms and diffuse materials in physical millimeters.
No generated bitmap texture is substituted for the original model materials.

Mapping: symbol Footprint -> library .pretty/file.kicad_mod -> model reference
-> local STEP -> derived mesh-cache/category/file.wrl.
Symbols without an explicit footprint require a user-selected physical package.
Unsupported named/stacked pads and slotted holes are rejected rather than guessed.
Multi-unit and non-numeric pin symbols remain outside the current importer support.

Runtime lookup: LEVELCAD_LIBRARY_ROOT or libraries/official beside an executable
or an ancestor directory. Keep this folder with the portable distribution.
Conversion is resumable with tools/convert-official-models.ps1.
Symbol-cache consolidates the version-10 per-symbol files for fast category loading.
Regenerate it using tools/build-symbol-cache.ps1 after updating the originals.
