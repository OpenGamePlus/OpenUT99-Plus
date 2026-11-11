# Sound and Music

Reader: `tools/usound.py` (registered for `Sound`, `Music`). Class data follows the tagged properties ([properties.md](properties.md)); neither class has properties in retail data.

Verified: all 2,881 `Sound` and 33 `Music` objects in the retail install are consumed byte-exactly, and every format name matches the embedded file's magic bytes.

## Layout

| Field | Type | Note |
|---|---|---|
| format | compact index into names | file type |
| data | lazy byte array ([package.md](package.md#lazy-byte-array)) | a complete file in that format |

## Formats seen

| Class | Format name | Container | Count |
|---|---|---|---|
| `Sound` | `WAV` / `wav` | RIFF WAVE, PCM | 2,164 / 717 |
| `Music` | `it` | Impulse Tracker | 30 |
| `Music` | `s3m` | Scream Tracker 3 | 2 |
| `Music` | `xm` | FastTracker 2 | 1 |

## WAV parameters (all PCM, format tag 1)

| Channels | Rate (Hz) | Bits | Count |
|---|---|---|---|
| 1 | 11025 | 8 | 1,019 |
| 1 | 22050 | 16 | 887 |
| 1 | 22050 | 8 | 563 |
| 1 | 11025 | 16 | 334 |
| 1 | 44100 | 8 / 16 | 35 / 26 |
| 2 | 44100 | 16 | 3 |
| 1 | other (8000–48000) | 8 / 16 | 17 |

## Open

- Loop points (WAV `smpl` chunk) and how the engine uses them: not surveyed.
- Music playback needs a tracker player (IT, S3M, XM): engine decision, not a format question.
