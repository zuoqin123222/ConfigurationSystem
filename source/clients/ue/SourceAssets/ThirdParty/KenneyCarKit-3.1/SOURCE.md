# Kenney Car Kit 3.1 intake

## Origin and license

- Creator: Kenney
- Asset: Car Kit 3.1
- Source page: <https://kenney.nl/assets/car-kit>
- Download archive: `kenney_car-kit.zip`
- License: Creative Commons Zero 1.0
- License text: `LICENSE.txt`
- Imported candidate: `sedan-sports`

The official source page identifies the pack as CC0. The included license permits
personal, educational, and commercial use. Attribution is not required, but this
record preserves provenance.

## Preserved source

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `sedan-sports.fbx` | 120400 | `2dbcb4e218ee32d1bb4884d0e96bd29a0f3891d54e31e480c957752567ffea7c` |
| `sedan-sports.glb` | 177676 | `2889c428ca1dd9c975c3ff760eb8757883490410555ab60b818f712f583826d6` |
| `sedan-sports.obj` | 122016 | `818ebc108112506d2ab6d23f959eb1de35cefbe123688c8c223ad82a64b31e40` |
| `sedan-sports.mtl` | 98 | `45a6736aa344a0c7e286b9c43c6a48f25b6a2f78d8c5949e430c3b4a93b4e060` |
| `preview.png` | 2520 | `e90757944428f2ae7d7f6a76201602176529b00d81ce8835771c328f9e35fbe4` |
| `LICENSE.txt` | 696 | `c33b7f6453d134deae7b1b8493717d9ccfa754c25ab97f6de89b88f8fda19b00` |

## Intake status

The unmodified source is intentionally not represented by a
`vehicle-model.sidecar.json`: it does not satisfy the project’s model contract.
It is imported only to:

```text
/Game/Configurator/_ImportStaging/KenneyCarKit31
```

Do not move these assets into a production namespace until the following work is
complete:

- Rotate/re-export to `+X` forward, `+Y` right, `+Z` up, centimeters.
- Build the required `Vehicle.Root` and `Vehicle.Body` hierarchy.
- Separate body paint, glass, lights, interior, frame, tire, and wheel-rim slots.
- Add door, hood, trunk, steering, and wheel-spin pivots.
- Regenerate smoothing groups and non-degenerate tangents.
- Add contract-compliant LODs and animation clips.
- Generate sidecars from measured output and pass the repository validator.

See `intake-audit.json` for the measured UE import result.
