> - **PM4/PD4** (server-side navigation/pathing mesh) — declared in scope
  elsewhere (`DESIGN.md`'s Non-goals, 2026-07-31) for a specific pathing
  use case, but genuinely different in kind: never touched by the client
  renderer, so it doesn't belong in a "what does the rendered world look
  like" completeness file the way WMO/ADT do. Track it separately when
  that work starts.

This should be part of the world mesh, as even if it's not rendered directly, for example a debug debug rendering might want to render it, so it should be hidden by default but 100% included

> ADT sound emitter placement	MCNK sub-chunk: MCSE	none	none	n/a	distinct concept from particle/doodad placement — audio only
> ADT chunk-level shadow map	MCNK sub-chunk: MCSH	none	none	n/a	baked shadow data, not geometry
