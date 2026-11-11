# Roadmap

Order is binding; each phase lists its exit check. Tentative beyond phase A.

| Phase | Goal | Exit check |
|---|---|---|
| A | Package and object loading: tagged properties, classes, textures, sounds, music | every object of every retail package deserialises with no bytes left over |
| B | Level viewer: BSP, lightmaps, static actors, meshes, sky | fly through every retail map in the C++ engine |
| C | Single-player gameplay: actors, physics, weapons, bots, game types | play a DM match against bots on a retail map |
| D | Fidelity to the original | side-by-side comparison with the original on chosen maps |
| E | Network compatibility ([ADR 0005](../architecture/adr/0005-network-compatibility.md)) | an original client joins our server, and our client joins an original server |
