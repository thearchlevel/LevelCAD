# Local component libraries

The full official KiCad repositories are stored under libraries/official:
22,907 symbol files, 15,468 footprint files, 7,256 original STEP models, and project templates.
The original repositories occupy about 3.8 GB before Git history and derived caches.
Source commit IDs and original-file counts are recorded in inventory.json.

All 7,256 STEP files have a corresponding converted mesh in mesh-cache.
library-status.json records completion. STEP originals remain unchanged.

LevelCAD imports 18,981 currently supported symbols. Multi-unit symbols, non-numeric
pin numbers and stacked pads still require importer work; their originals are preserved.
A symbol's Footprint property selects the exact .kicad_mod pad geometry.
The footprint's model reference selects its local mesh, including offsets, scale,
rotation and diffuse materials. Generic symbols without a unique package require
the user to choose a footprint; matching pad counts alone is not a package specification.

On the PCB, select a component and choose Encapsulamento to select a local footprint.
The pin numbers must match. Component properties also list the complete local 3D cache.
Sync migrates supported older native footprints to exact official definitions.
Existing custom model files are preserved. Imported boards load available official models.

The viewer reads converted indexed-face geometry and transforms in millimeters.
Model materials are stored per face and preserved in native project files.
STEP rendering uses the prebuilt mesh cache and needs no network or converter at runtime.
Converting updated STEP files requires the official CLI installed locally; the included
PowerShell converter is resumable and can split categories across multiple workers.

Run tools/download-official-libraries.ps1 to reproduce the downloads.
Run tools/build-symbol-cache.ps1 after updating symbol sources.
Run tools/convert-official-models.ps1 to regenerate model caches.
LEVELCAD_LIBRARY_ROOT can point to a relocated official-library directory.

Library contributors retain their copyrights. The original LICENSE.md and per-category
CREDITS.md files are preserved. Derived models retain the library license.
Official downloads: https://www.kicad.org/libraries/download/