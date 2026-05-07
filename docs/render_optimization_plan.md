# Render Optimization Plan

## Current Direction

Use measurement-first optimization in `RelWithDebInfo`. Debug builds are too slow to judge renderer changes.

The failed CPU occlusion and GPU Hi-Z culling routes should stay removed. They reduced command counts in some captures, but added CPU or GPU cost and caused visible instability.

## Phase 1: Instrumentation

- GPU timer queries for opaque, cutout, and transparent world passes.
- MDI work counters for commands, submitted vertices, and estimated vertex bytes.
- `BlockVertex` layout audit shown in the dashboard. The current format is 52 bytes per vertex.
- Cutout distance controls in the dashboard for A/B testing.

## Phase 2: Low-Risk Cutout Limit

Cutout rendering can be capped by horizontal chunk distance. Opaque and transparent passes remain unchanged.

Recommended defaults:

- Enabled in profiling builds.
- 4 chunks for aggressive testing.
- 6 to 8 chunks if visual loss is too obvious.

This is intentionally a visual-quality/performance knob rather than a correctness-sensitive culling system.

## Phase 3: Vertex Format Audit

Before changing meshing or LOD, verify whether the renderer is bandwidth bound.

Candidates after measurement:

- Pack normal and light values into integers.
- Pack AO and animation flags.
- Keep position as local chunk coordinates plus per-draw or per-batch offset if the shader pipeline supports it.
- Split rarely used animation attributes from most opaque vertices.

Any format change should be benchmarked with GPU timer data because smaller vertices can trade bandwidth for shader decode cost.

## Phase 4: In-Render-Distance LOD

LOD only starts when render distance is greater than 16 chunks. At 16 chunks or below, render full meshes only.

Recommended distance bands:

- LOD0: 0 to 16 chunks, full chunk mesh.
- LOD1: 16 to 24 chunks, 2x horizontal simplification where safe.
- LOD2: 24+ chunks, 4x horizontal simplification where safe.

Implementation outline:

- Generate LOD meshes asynchronously alongside normal sub-chunk meshes.
- Use opaque terrain only for the first version.
- Keep cutout and transparent out of LOD meshes initially.
- Build LOD in vertical sections or chunk columns, not single huge world meshes, so updates stay localized.
- Add skirts or overlap margins on LOD tile boundaries to hide cracks.
- Cross-fade or dither between LOD levels for one chunk band to reduce popping.
- Prefer full-resolution meshes near edited blocks until the matching LOD tile is regenerated.

LOD should reduce vertex count inside high render distances, not extend view distance like Distant Horizons.

## Validation

- Compare `RelWithDebInfo` with GPU timer query on and off.
- Capture opaque/cutout/transparent GPU time separately.
- Track MDI command counts and vertex MiB at 16, 24, and 32 chunk render distances.
- Check fast movement, block editing, water/glass views, and looking across LOD boundaries.
