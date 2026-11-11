# Configuration (.ini, .int) and package lookup

Readers: `tools/uini.py` (reference), `engine/config.*`, `engine/vfs.*`.

Verified: all 18 retail `.ini` and 38 `.int` files are single-byte text without BOM. `uini.py --deps <UT99_DIR>` and `ut_dump --deps <UT99_DIR>` both resolve every top-level package imported by every package (395 packages, 0 missing).

## Syntax

- `[Section]` lines open a section; `Key=Value` lines follow; `;` starts a comment line.
- Keys repeat (`Paths=` several times); values are kept in file order.
- `.int` (localisation) files use the same syntax.

## Package lookup

From `[Core.System]` of `System/UnrealTournament.ini` (defaults in `Default.ini`):

| Key | Meaning |
|---|---|
| `Paths` | `../Folder/*.ext`, relative to `System/`; a package `Name` is the file `Name.ext` in that folder |
| `CachePath`, `CacheExt` | download cache folder (`../Cache`) and extension (`.uxx`) |
| `LangPaths` | localisation file patterns (`<lang>` placeholder); not used yet |

Order: `Paths` entries in file order, then the cache; the first match wins. Names compare case-insensitively.

Cache: `Cache/cache.ini` section `[Cache]` maps `GUID=OriginalFileName.ext`; the file is `Cache/GUID.uxx`, the package name is the original file name without extension. Entries whose file is missing are ignored.

A package's dependencies are its imports of class `Package` with outer 0.
