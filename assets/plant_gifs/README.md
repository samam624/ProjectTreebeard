# Plant GIF Source Art

Put source animated plant GIFs in this folder.

Use simple lowercase filenames because the filename becomes the JSON art key:

```text
fern.gif        -> "art": "fern"
monstera.gif    -> "art": "monstera"
snake_plant.gif -> "art": "snake_plant"
```

Then regenerate firmware/web assets from the repo root:

```powershell
python scripts\generate_plant_sprites.py
```

Generated outputs are written to:

- `arduino/MSV2_ESP32_Display_Runtime/plant_sprites.h`
- `arduino/MSV2_ESP32_Display_Runtime/PlantSpriteCatalog.h`
- `arduino/MSV2_ESP32_Display_Runtime/MobileAppPage.h` art picker options and previews
- `generated/plant_sprites/previews/`
- `generated/plant_sprites/mobile_art_catalog_snippet.js`
