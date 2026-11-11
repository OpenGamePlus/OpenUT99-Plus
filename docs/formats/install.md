# Retail install layout

Verified on the Steam release with the OldUnreal patch 469e (`System/Manifest.ini`: `Version=469e`).

| Folder | Content | Files |
|---|---|---|
| `Maps/` | levels, `.unr` packages | 83 |
| `System/` | code packages `.u` (24), native `.dll` (33), `.exe` (9), `.ini` (17), localisation `.int` (38), plus logs, images and misc | 137 |
| `Textures/` | texture packages `.utx` | 110 |
| `Sounds/` | sound packages `.uax` | 29 |
| `Music/` | music packages `.umx` (tracker modules: IT, S3M, XM; see [sound.md](sound.md)) | 30 |
| `Cache/` | downloaded packages `.uxx` + `cache.ini` | 121 |
| `SystemLocalized/` | localisation for other languages | 10 |
| `Help/`, `Manual/`, `Web/`, `Logs/` | docs, web admin templates, logs | — |

All of `.unr .u .utx .uax .umx .uxx` share one container: [package.md](package.md).

## Code packages (`System/*.u`)

`Core`, `Engine` (base classes, native-backed), `UnrealShare`, `UnrealI` (Unreal 1 content), `BotPack` (UT gameplay), `UWindow`, `UMenu`, `UTMenu`, `UBrowser`, `UTBrowser`, `UWeb`, `UTServerAdmin`, `IpDrv`, `IpServer`, `Fire`, `Editor`, `udemo`, `multimesh`, `relics`, `relicsbindings`, `epiccustommodels`, `SkeletalChars`, `de`, `OldUnreal469c` (469 patch).

## Level families (`Maps/`)

| Prefix | Game type | Maps |
|---|---|---|
| `DM-` | Deathmatch | 34 |
| `CTF-` | Capture the Flag | 20 |
| `DOM-` | Domination | 11 |
| `AS-` | Assault | 8 |
| other | menus, intro, credits, ladder (`Entry`, `CityIntro`, `UTCredits`, `UT-Logo-Map`, `EOL_*`) | 1 + 1 + 1 + 1 + 6 |
