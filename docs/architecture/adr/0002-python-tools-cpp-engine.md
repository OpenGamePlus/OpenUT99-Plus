# 0002 — Python stdlib for tools, C++ for the engine

- Status: Accepted
- Date: 2026-11-10

## Context

Format research needs fast iteration over hundreds of retail files; the runtime needs performance. Same split as the sibling project OpenBF1942-Plus.

## Decision

- Research tools and reference readers: Python 3, standard library only, in `tools/`, each with a unit test on synthetic data.
- Engine: C++, porting a format from its Python reader once the spec is verified.

## Consequences

- No pip dependencies; tools run anywhere Python 3 runs.
- A format exists twice (Python reference, C++ port); the Python reader and its spec are the reference when they disagree.
