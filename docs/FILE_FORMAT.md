# `.levelsch` format (experimental, schema v1)

JSON object with `format = "levelcad-schematic"`, `version = 1`, `symbols` and `wires` arrays.

IDs and coordinates are decimal **strings** because JSON numbers cannot reliably represent all 64-bit integer values. Coordinates are **nanometers**. `kind` enumerates: 0 resistor, 1 capacitor, 2 ground. This format is not stable yet.

Sample:

```json
{"format":"levelcad-schematic","version":1,"symbols":[{"id":"1","kind":0,"x":"0","y":"0","rotation":0,"reference":"R1","value":"10k"}],"wires":[]}
```
