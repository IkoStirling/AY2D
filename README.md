# AY2D

**Status:** Phase 0 — docs-only (no code merged yet). See [`design.md`](design.md) for the authoritative architecture.

**Lane:** Present (Presentation) 2D subsystem of AY Engine — `TileFilter::Bilinear` is the default tile sampler; `RenderPassSlot::Forward2DOpaque` is the planned pipeline slot.

**Out of scope:** UI widgets (capability-map §E L85), Jolt/Box2D 2D physics backend (TBD per `AYPhysics/design.md §4.2`), lockstep tile sim (gated on DET-01), any `bgfx::*` in public headers (see `design.md` §11.2).

**Authoritative document:** [`design.md`](design.md) (v0.1, 2026-07-27, including industrial-grade audit patches F-1..F-19).
