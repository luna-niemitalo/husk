# vkd3d-proton environment variables (pulled from upstream README, 2026-08-20)

Reference for setting up the next targeted shader-capture session
(`SHADER_SCAN_FINDINGS.md`'s "Next step"). Source: `README.md`,
[HansKristian-Work/vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton)
(fetched via WebFetch, not vetted against the exact version installed
locally — check `VKD3D_DEBUG=warn` output or the actual Proton build's
bundled README if a flag doesn't behave as described here).

## Confirmed: no readable-disassembly-on-capture flag exists

`VKD3D_SHADER_DUMP_PATH` only dumps raw bytecode (`$hash.{spv,dxbc,dxil}`).
There is no built-in vkd3d-proton flag that also disassembles to human-
readable text — that's an unavoidable separate step, same as this session
did with `vkd3d-compiler` (DXBC → `d3d-asm`) and what upstream itself
recommends `spirv-cross` for on the SPIR-V side.

## Already confirmed in use (from `shortcuts.vdf`, the real prior capture)

```
VKD3D_SHADER_DUMP_PATH=/media/luna/work/cache/wow_shader_dump
VKD3D_SHADER_CACHE_PATH=/media/luna/work/cache/wow_shader_cache
VKD3D_CONFIG=no_upload_hvv,force_static_cbv
VKD3D_DEBUG=warn
```

`no_upload_hvv`/`force_static_cbv` are unrelated perf/compat tweaks (NVIDIA
speed hack, blocks host-visible-VRAM upload heap use) — nothing to do with
shader dumping specifically. These were already correct and complete for
what the corpus hunt needed.

## `VKD3D_CONFIG` flags (comma/semicolon-separated)

- `vk_debug` — enables Vulkan debug extensions + validation layer
- `skip_application_workarounds` — debugging aid, skips all app-specific workarounds
- `nodxr` / `dxr` / `dxr12` — DXR (ray tracing) enable/disable/experimental-1.2
- `force_static_cbv` — NVIDIA speed hack, unsafe, already in use
- `single_queue` — disables async compute/transfer queues
- `no_upload_hvv` — blocks host-visible-VRAM UPLOAD heap use, already in use
- `force_host_cached` — forces host-visible allocations CACHED, "greatly accelerates captures" — **worth adding for a RenderDoc-style capture session**
- `no_invariant_position` — workaround toggle
- `breadcrumbs` — instruments command lists for GPU-hang debugging
- `pipeline_library_app_cache` — alternative shader-cache disable mechanism
- `descriptor_qa_checks` — GPU-assisted descriptor validation (build-dependent)

## Other relevant env vars

- `VKD3D_SHADER_DEBUG=<none|err|info|fixme|warn|trace>` — log level for the
  shader compilers specifically (separate from `VKD3D_DEBUG`, which is
  vkd3d-proton's own log level)
- `VKD3D_LOG_FILE=<path>` — redirect `VKD3D_DEBUG` output to a file
- `VKD3D_SHADER_OVERRIDE=<path>` — if set and
  `$VKD3D_SHADER_OVERRIDE/$hash.spv` exists, that SPIR-V is used instead of
  the app-provided one — a live shader-replacement mechanism, not directly
  useful for capture but notable for later verification (could substitute a
  hand-written test shader to confirm a formula match empirically)
- `VKD3D_AUTO_CAPTURE_SHADER` / `VKD3D_AUTO_CAPTURE_COUNTS` — RenderDoc
  auto-capture triggers, tied to a specific shader hash or submission index —
  could help pin down exactly which draw call uses a target combiner once its
  hash is known from a first dump pass
- `VKD3D_VULKAN_DEVICE=<index>` / `VKD3D_FILTER_DEVICE_NAME=<substr>` — GPU
  selection, irrelevant to shader dumping
- `VKD3D_FRAME_RATE` / `VKD3D_SWAPCHAIN_PRESENT_MODE` — perf/latency tuning,
  irrelevant here

## For the next targeted capture

Keep `VKD3D_SHADER_DUMP_PATH`/`VKD3D_SHADER_CACHE_PATH` as-is. Consider
adding `force_host_cached` to `VKD3D_CONFIG` if capture felt slow last time.
No flag changes needed to get readable output faster — the
`vkd3d-compiler`/`spirv-dis` disassembly pass afterward is unavoidable
either way, and this session's tooling for that is already in
`nix/flake.nix`'s dev shell (`pkgs.vkd3d`).
