# Adding blocks

Block definitions are JSON, parsed with nlohmann/json at startup. Add a file to
`assets/blocks/`, build, and restart the game. The build copies assets beside the
executable; asset loading is independent of the working directory. Definitions
are immutable while the game runs.

```json
{
  "schema_version": 1,
  "id": "core:sand",
  "name": "Sand",
  "textures": { "all": "core:sand" }
}
```

Register the texture in the `textures` object in `assets/textures.json`:

```json
"core:sand": { "color": [210, 195, 140] }
```

Procedural RGB colors receive deterministic pixel variation. An optional
`edge_color` produces a grassy upper edge on the tile, as used by grass sides.
Alternatively, place an opaque 16x16 PNG under assets and use:

```json
"core:sand": { "file": "textures/sand.png" }
```

Paths are relative to assets. A texture entry uses either `file` or procedural
colors. Transparent, malformed, or incorrectly sized PNGs fail at startup with
the filename. Up to 1024 unique tiles fit the horizontal atlas.

`textures.all` supplies the default face. Override `top`, `bottom`, `left` (-X),
`right` (+X), `back` (-Z), or `front` (+Z). Without `all`, supply all six faces.
Every referenced texture must exist. Block IDs are unique namespaced strings
using lowercase letters, digits, underscores, hyphens, dots, and slashes.
Unknown fields are errors, including properties for unsupported behavior.

Air is required, has no textures, and is excluded from selection. All other
blocks are opaque, collidable full cubes. New blocks appear in the debug picker;
keys 1–9 select the first nine non-air entries. The original runtime IDs remain
Air=0, Grass=1, Dirt=2, Stone=3; additional blocks are sorted by namespaced ID.
Numeric IDs beyond the built-ins can change as content is added. Future world
saves must store an ID-to-name palette rather than assuming those IDs are stable.
Adding a block to terrain generation still requires changing the generator.

CPU checks can be enabled with `-DMINECRAFT_BUILD_TESTS=ON`. Build
`MinecraftBlockTests` and run CTest in the configured build directory.

Loading uses `std::expected` results with filename-qualified error messages.
Callers check success before accessing a result. Unknown names return an empty
`std::optional` from `Find`; `Get` requires a valid runtime ID. Filesystem errors
use error codes, and JSON parsing runs with exceptions disabled.
