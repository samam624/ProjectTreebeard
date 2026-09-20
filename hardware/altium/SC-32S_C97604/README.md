# Seiko SC-32S C97604 Altium library

Install `SC-32S_C97604.IntLib` in Altium Designer and place
`SC-32S32.768kHz20PPM7pF`. The component already contains:

- the two-pin crystal schematic symbol;
- the Seiko SC-32S recommended 3215 two-pad land pattern;
- an extruded 3D body and courtyard/assembly outlines;
- manufacturer part number `SC-32S32.768kHz20PPM7pF`;
- LCSC part number `C97604`;
- frequency, load-capacitance, tolerance, ESR, package, and temperature metadata.

The matching load capacitors remain the two `10pF C0G 5% 0805` parts from the
controlled Treebeard passive library.

To rebuild the compiled library with Altium installed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\generator\Build-SC32SC97604.ps1
```
