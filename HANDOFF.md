# Handoff — R36H Performance Optimization (`perf/gl-vao-uniforms`)

**Date:** 2026-11-24  
**Branch:** `perf/gl-vao-uniforms` (clean performance-only branch off `main`, suitable for upstream PR; Thai/subtitle/GPS binary patches are applied separately downstream)  
**Target Hardware:** R36H (`HL-R36H-V20`, RK3326 Quad Cortex-A35 @ 1.3 GHz, Mali-G31 MP2 ES 3.2, 1 GB RAM, 640×480, playOS Debian 13 arm64)  
**Target Binary:** `nfsu2.aarch64.v10` (for US v1.0 `SPEED2.EXE`, `4,788,224` bytes, SHA-1 `f4bb0d9942815bb6e054e773994710c612acbc34`)

---

## 1. Completed Work

### Phase 1 — GL Driver Optimization (Backlog #11)
Implemented and committed in `f23c3cd`:
- **`ports/r36s/d3d9_gles/d3d9_gles_device.h` & `ports/r36s/d3d9_gles/d3d9_gles.cpp`**:
  - Added a **4-way set-associative VAO cache** (`128 sets × 4 ways = 512 slots`, zero heap allocation) for OpenGL ES 3.0+ contexts (Mali-G31 reports ES 3.2).
  - Caches vertex attribute descriptors, VBOs, and `GL_ELEMENT_ARRAY_BUFFER` (IBO) per VBO-backed non-instanced draw (`!upData && !instanceLayout && elemCount <= 8`).
  - Added A/B toggle **`NFSU2_VAO=0`** to disable VAO caching for direct benchmark comparison, plus shutdown hit/miss/eviction statistics logging.
- **`ports/r36s/d3d9_gles/gl_state_cache.h`**:
  - Added `vaoDeleteBufferHook()` in `glsc::DeleteBuffers` and `vaoEnsureDefaultHook()` in `glsc::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ...)`.
- **`ports/r36s/d3d9_gles/d3d9_gles_shaders.h`**:
  - Added `std::memcmp` dirty check in `copyRegs()` and zero-copy / contiguous block upload optimizations in `uploadPacked()`.

### Phase 2 — Game-Thread Scenery Object Setup & Direct Frustum Culling (Backlog #16 & #3)
- **Disassembly Finding (`sub_005B9940` / Backlog #3):**
  - `sub_005B9940` sets up views and tail-jumps (`jmp 0x5e97a0`) to `sub_005E97A0` → `sub_005E9730` → `sub_005D6700` (scenery tree walk) → `sub_005D62C0` (scenery object setup into `[view+0x4C]`). Thus Backlog #3 and Backlog #16 share the same hot call chain (`sub_005D6700` → `sub_005D62C0`), and `sub_005D6700` is the sole caller of `sub_005D62C0` in the entire binary.
- **`src/runtime/native_scene.c` (`sub_005D62C0` & `sub_005D6220` Native C Port):**
  - Ported `sub_005D62C0` (`sceneObject`) and inlined `sub_005D6220` (`pixelSize`), `sub_005B95C0`, `sub_005BDEA0`, `sub_00401EA0`, `sub_005D5F90`, `sub_005D5F60`, and the `sub_0048B1B0`/`sub_0048D830` checks directly in C:
    1. **Early Exit 1:** Precull visibility bitmap test (`[view+0x84]` against `[self+0x30]`).
    2. **Early Exit 2:** View flag filter (`M8(view+0x44) & (M16(obj+0x1A) ^ 0x60)`).
    3. **Early Exit 3:** Object AABB frustum test when parent node is partially visible (`vis == 1`).
    4. **Early Exit 4:** Apparent pixel-size projection (`pixelSize` / `sub_005D6220`) and LOD threshold selection (`info + 0x30/0x34/0x38`).
    5. **Static & Placed Instance Insertion:** Directly appends static objects (`M8(obj+0x1B) & 1`) and unpacks 64-byte `int16` rotation/translation matrices for non-flare placed instances into `[view+0x4C]` without guest stack calls. Rare animated-rotation or light-flare objects fall back to `sub_005D62C0_orig`.
  - Called `sceneObject()` directly from `sceneWalk()` without pushing/popping on `g_esp`, and added `obj %u/%u` write-log verification under `NFSU2_NATIVE_SCENE=2`.
- **`src/runtime/native_math.c` (`nfs_frustum_direct`):**
  - Added `nfs_frustum_direct()` so `frustum()` in `native_scene.c` calls `frustumTest()` directly in C when `NFSU2_NATIVE_MATH=1`, eliminating 4-word guest stack push/pop overhead per node, object, and solid frustum test.

### Phase 3 (Group 1) — Native Material/Item Setup, Hot D3DX Setters & `CommitChanges` (Backlog #2 & #4)
- **`src/runtime/fx_cache.c`:**
  - Added `nfs_fx_setup_mode()` (`NFSU2_NATIVE_SETUP=0/1/2`, default `1`) and native ports of D3DX dirty-flag propagation (`canMarkDirty` / `markDirty`, matching `sub_00660280` & `sub_0065A835`) and hot effect setters: `nfs_fx_set_matrix` (`sub_0065CC5D`), `nfs_fx_set_vector`, `nfs_fx_set_vector_array`, `nfs_fx_set_float` (`sub_0066F215` with ordered-equal no-op), `nfs_fx_set_int_array` (`sub_0066E5BD` with 16-byte stride), and `nfs_fx_set_texture_noop`.
  - Ported `ID3DXEffect::CommitChanges` (`sub_0066A93B` + `sub_00669F83` + `sub_006620FE` `gatherFloatConst` + bit-exact `_controlfp([fx+0x114], 0x30000)` `restoreControlFpPC` on `g_fpu_cw`) into `nfs_fx_commit_changes()`.
- **`src/runtime/overrides.c` & `src/runtime/hud_half.c`:**
  - Routed hot `ID3DXEffect::Set*` methods (`28/32/36/38/42/52`) in `ov_fx_method()` and lazy `CommitChanges` in `hud_half.c` (`nfs_fx_lazy_begin_pass` / `nfs_fx_lazy_commit`) through the native C fast paths when `nfs_fx_setup_mode() == 1`.
- **`src/runtime/world_cull.c`:**
  - Ported full material setup (`sub_005BC940` -> `fullMaterialSetup`, including UV scroll matrix at `[8]/[9]` and texture/shader state setup) and all 4 per-item parameter setters (`sub_005BDAF0`, `sub_005BD7B0`, `sub_005BCE50`, `sub_005BD1B0`) with inline SSE-matched `matMulSSE()` (guarded on `M32(0x803E74) == NFS_VA(0x00689A17)`).
  - Wired `commitChangesCall()` to `nfs_fx_commit_changes()`, with full bit-exact verification harness (`NFSU2_NATIVE_SETUP=2`) and micro-benchmark timer (`NFSU2_SETUP_TIMING=1`).

### Phase 4 (Group 2) — Configurable Trade-off Toggles (Backlog #13, #15, #14)
- **`src/runtime/hud_half.c`, `android/app/src/main/cpp/d3d9_render_thread.inc`, `android/app/src/main/cpp/d3d9_bridge.cpp`:**
  - **Backlog #13 (`NFSU2_HALF_REFLECT`):** Added `nfs_reflect_on_present()` invoked once per `Present` on the game thread. Signature-locates the cubemap schedule table (`0x7FEE90`/`0x7FEEA0`, v10/v12 compatible), zeroes active schedule rows on skipped driving frames (disabling cubemap views `0x10..0x15` in both `sub_005B9940` and `sub_005CA7A0` while keeping `g_CarEnvironmentMapEnable` enabled so cars sample the previous cubemap with zero flicker), and rotates every face evenly on update frames. Default `0` (off); `1`/`2` = half rate, `N` = `1/N` rate; menus/garage remain full rate.
  - **Backlog #15 (`NFSU2_CROWD`, `NFSU2_PARTICLES`):** Exposed clean independent `0|1` overrides for `g_CrowdEnable` (`0x870CD0`) and `g_ParticleSystemEnable` (`0x870CEC`) both via `NFSU2.sh` registry injection and runtime enforcement in `nfs_reflect_on_present()`.
- **`src/runtime/hud_half.c` & `src/runtime/overrides.c`:**
  - **Backlog #14 (`NFSU2_DISTANT_AI_RATE`, `NFSU2_DISTANT_AI_DIST`):** Added vtable-level `overrides.c` hooks (`0` guest `.o` rebuild, zero overhead when `=0` default) for `PhysicsMover::Update` (`sub_005B6FF0`) and AI controllers (`DragController` `sub_0042DF50`, `RaceCarController` `sub_0042C100`, `TrafficController` `sub_00429DD0`), strictly skipping human player `DriverClass == 1` (`RealController`). Beyond `NFSU2_DISTANT_AI_DIST` metres (default `45m`) from the player car:
    1. Scales the internal 227 Hz physics substep `[0x7FC070]` (`0.0044s`) by `rate` (capped at `0.015s` >= 66 Hz), cutting distant AI physics substeps (e.g. `8 -> 4` at 30 FPS) while still integrating the full `dt` every frame (zero slowdown/stutter).
    2. Staggers and skips distant AI controller ticks at `1/rate` (`(frame + (ctrl >> 4)) % rate != 0`), holding last frame's steering/throttle/brake.
- **`ports/r36s/package/NFSU2.sh` & `ports/r36s/package/nfsu2.cfg`:**
  - Documented and wired `NFSU2_HALF_REFLECT`, `NFSU2_CROWD`, `NFSU2_PARTICLES`, `NFSU2_DISTANT_AI_RATE`, and `NFSU2_DISTANT_AI_DIST` (all opt-in / off by default).

### Full Docker Build & Fast Relink (Completed on Linux Mint 22.3)
- **Cached Docker state:** Image `nfsu2-r36s-build:latest` and volume `nfsu2-r36s-work` (containing `pcrecomp`, lifted C, and `113` guest `.o` files) are preserved on the Linux host.
- **Generated artifacts in `build/r36s/` (Phases 1–4):**
  - `nfsu2-v10.aarch64` (`87,398,760` bytes, SHA-1 `23a3111491b48c9b9b70e54da55eadd0225a134a`, `glibc compat OK: max imported version GLIBC_2.18`, `NFSU2_EXE_BUILT_FOR=v10:4788224`)
  - `guest-objs-v10.tar.gz` (`34,228,930` bytes, key `9d5fc65cf7d2cb0c5481a2e2cb0820c622570837`)
  - `nfsu2-v10.aarch64.debug`, `build-v10.txt`, `lift_stats-v10.json`, `build.log`

---

## 2. Physical R36H Benchmark Results

### Phase 1 A/B Benchmark (VAO Cache & Zero-Copy Uniforms)
Deployed `nfsu2-v10.aarch64` to `/roms/ports/nfsu2/nfsu2.aarch64.v10` and `/roms/ports/nfsu2/nfsu2.aarch64` (stock binary backed up as `/roms/ports/nfsu2/nfsu2.aarch64.v10.bak_stock`).

Measured on physical R36H with `nfsu2.cfg` set to `NFSU2_GRAPHICS=performance`, `NFSU2_FPS=30`, `NFSU2_LANG=Thai`, `NFSU2_DRAW_DIST=200` (city driving at matched `~610–618` draws/frame):

| Metric | Session B (`NFSU2_VAO=0`, `611–618 draws`) | Session A1 (`NFSU2_VAO=1`, `614 draws`) | Session A2 (`NFSU2_VAO=1`, `610 draws`) |
| :--- | :--- | :--- | :--- |
| **FPS / Wall Time** | `28.0–28.8 fps` (`34.73–35.78 ms`) | **`29.9–30.0 fps` (`33.35–33.48 ms`)** | `28.3–29.5 fps` (`34.0–35.3 ms` @ `511–610` draws) |
| **GL state calls (requested)** | `21,707–22,604 / frame` | **`17,146–17,150 / frame`** | **`15,471 / frame`** (`-31.6%`) |
| **GL state calls (reached driver)** | `3,235–3,345 / frame` | **`2,284–2,286 / frame`** | **`2,281 / frame`** (`-31.8%`) |
| **CPU inside Mali GL driver** | `16.37–16.50 ms/frame` | **`15.22–15.48 ms/frame`** | `16.82 ms/frame` |

### Phase 2 Verification & Benchmark (`sub_005D62C0` Native Port + Direct Frustum Culling)
1. **Bit-Exact Verification (`NFSU2_NATIVE_SCENE=2`, Circuit Quick Race on R36H):**
   ```text
   [NFSU2 SCENE] verify: pool 2324573/0 bad, item 1750177/0, model 703343/0, walk 188407/0, obj 2457212/0
   ```
   - Verified **`2,457,212` calls to `sub_005D62C0` (`obj`) with `0 bad` (zero memory or pointer mismatches)** across `188,407` scenery tree walks (`~1,020` object setup calls/frame).
2. **Scene Subtree Timing (`NFSU2_SCENE_TIMING=1`, measured over `3,720` frames on R36H):**
   - `sub_005C5600` (`model`): **`869,862` calls @ `4.337 µs/call`**
   - `sub_005D6700` + `sub_005D62C0` (`walk`): **`247,679` calls @ `12.133 µs/call`** (`~0.83 ms/frame` total for the entire scenery tree walk and object setup across all active views)
3. **City Driving Frame Performance (`488–594` draws/frame, `performance`, `NFSU2_DRAW_DIST=200`):**
   - At `488–550 draws/frame`: **`28.8–29.9 fps`** (`33.47–34.68 ms` wall, render thread `20.54–23.90 ms/frame`, Mali GL driver `12.84–15.02 ms/frame`, GL state calls reached driver `1,870–2,064/frame`, game thread `26.6–30.0 ms/frame`).
   - At `571–627 draws/frame`: **`28.1–29.2 fps`** (`34.29–35.65 ms` wall, render thread `24.19–26.81 ms/frame`, Mali GL driver `15.23–16.92 ms/frame`, GL state calls reached driver `2,165–2,228/frame`, game thread `29.7–32.3 ms/frame`).
- Raw logs saved at `/home/svvv/.gemini/antigravity/brain/ce23717f-cc37-4526-8a3b-7f65a7ecd8b0/scratch/log_phase2_native1.txt` and `log_phase2_verify2.txt`.

### Phase 3 (Group 1) Verification & A/B Benchmark (`NFSU2_NATIVE_SETUP`)
1. **Bit-Exact Verification (`NFSU2_NATIVE_SETUP=2`, city driving on R36H):**
   ```text
   [NFSU2 SETUP] verify: mat 121313/0 bad, wv 484300/0, v9 28896/0, light 407436/0, ovr 463669/0, commit 393962/0
   ```
   - Verified **`1,899,576` total calls across all 6 components (`mat`, `wv`, `v9`, `light`, `ovr`, `commit`, including `g_fpu_cw`) with `0 bad` (zero mismatches)**.
2. **A/B Micro-Benchmark & Game-Thread CPU (`NFSU2_SETUP_TIMING=1`, 12 steady-state 300-frame windows each on R36H, `performance`, `NFSU2_DRAW_DIST=200`):**

| Component | Lifted (`NFSU2_NATIVE_SETUP=0`) | Native (`NFSU2_NATIVE_SETUP=1`) | Improvement |
| :--- | :--- | :--- | :--- |
| **`mat` (`sub_005BC940`)** | `36.16 µs/call` (`1.19 ms/f`) | **`28.99 µs/call` (`0.98 ms/f`)** | **`1.25×` faster (`-19.8%`)** |
| **`params` (`5BDAF0`/`5BD7B0`/`5BCE50`/`5BD1B0`)** | `13.70 µs/call` (`1.66 ms/f`) | **`5.99 µs/call` (`0.76 ms/f`)** | **`2.29×` faster (`-56.3%`)** |
| **`commit` (`ID3DXEffect::CommitChanges`)** | `13.57 µs/call` (`1.64 ms/f`) | **`10.72 µs/call` (`1.36 ms/f`)** | **`1.27×` faster (`-21.0%`)** |
| **Combined `mat + params + commit`** | `4.50 ms/f` (`4.67 ms/f` @ 126.5 items/f) | **`3.10 ms/f`** (@ 126.5 items/f) | **`-1.57 ms/frame` (`-33.6%`)** |
| **Game-thread CPU (normalized @ 600 draws)** | `31.68 ms/frame` (`596 draws`, `28.04 FPS`) | **`29.87 ms/frame`** (`637 draws`, `28.00 FPS`) | **`-1.81 ms/frame` (`-5.7%`)** |

- Raw logs saved at `/home/svvv/.gemini/antigravity/brain/e5a1aded-5ecf-426d-a670-34d5ffae205e/scratch/log_phase3_verify3.txt`, `log_phase3_bench_native.txt`, and `log_phase3_bench_lifted.txt`.

### Phase 4 (Group 2) Physical R36H Verification & Benchmarks
1. **Backlog #13 & #15 (`NFSU2_HALF_REFLECT=2`, `g_CarEnvironmentMapEnable=1;g_CarEnvironmentMapUpdateData=1`, `NFSU2_CROWD=0`, `NFSU2_PARTICLES=0`):**
   - Verified `2,250 updated / 2,250 skipped` cubemap frames with zero flicker or visual artifacts.
   - **A/B Comparison (`draws >= 450` steady driving windows):**
     - Full-rate baseline (`=0`, 23 windows, `608 draws`, `1225 MHz`): `gameCPU = 34.68 ms/f` (`32.78 ms/f` @ 1296 MHz), `renderCPU = 28.46 ms/f` (`26.90 ms/f` @ 1296 MHz), `26.48 FPS`.
     - Half-rate (`=2`, 38 windows, `636 draws`, `1205 MHz`): **`gameCPU = 32.23 ms/f`** (**`29.97 ms/f`** @ 1296 MHz, **`-2.81 ms/f` / `-8.6%`**), **`renderCPU = 27.01 ms/f`** (**`25.11 ms/f`** @ 1296 MHz, **`-1.79 ms/f` / `-6.7%`**), **`27.55 FPS`** (`+1.07 FPS` despite +28 more draws/frame).
2. **Backlog #14 (`NFSU2_DISTANT_AI_RATE=2`, `NFSU2_DISTANT_AI_DIST=45`, `performance` preset):**
   - Verified live on R36H (`2,250/4,500` distant traffic/AI controller ticks skipped per 300 frames; `154/154` distant AI racer `PhysicsMover` substep updates scaled `8 -> 4` when >45m away; human player unaffected; zero crashes or glitches).
   - Steady driving (23 windows, `587 draws`, `1279 MHz`): **`gameCPU = 29.05 ms/f`**, **`renderCPU = 24.08 ms/f`**, **`28.76 FPS`** (vs Phase 3 baseline `31.69 ms/f` / `28.00 FPS` @ `637 draws`).
- Raw logs & Cortex-A35 1 kHz hardware profile (`prof_samples.txt`, 2,705 game-thread samples: 11.3% Physics, 6.3% AI/Traffic) saved under `/home/svvv/.gemini/antigravity/brain/e5a1aded-5ecf-426d-a670-34d5ffae205e/scratch/`.
- `/roms/ports/nfsu2/nfsu2.cfg` on R36H has been restored to clean default configuration.

---

## 3. Environment & Operational Notes

- **Linux Build Host (Linux Mint 22.3 / GLIBC 2.39):**
  - Do **not** use `--native` on this host (`glibc 2.39` triggers `imports too-new symbol versions`). Always use Docker image `nfsu2-r36s-build` via `sg docker -c '...'`.
  - **Fast runtime-only relink (~20 seconds):**
    ```bash
    sg docker -c 'docker run --rm -v nfsu2-r36s-work:/work -v /mnt/d/r36h/nfsu2/source/nfsu2-recompiled-r36s:/src nfsu2-r36s-build bash -c "rm -rf /work/host-v10 && GUEST_DIR=/work/guest-v10 NFS_EXE_VARIANT_H=/work/nfsu2-v10/src/recomp/gen/nfs_exe_variant.h OUT=/work/host-v10 bash /src/ports/r36s/build_cross.sh && cp /work/host-v10/package/nfsu2.aarch64 /src/build/r36s/nfsu2-v10.aarch64 && cp /work/host-v10/package/nfsu2.aarch64.debug /src/build/r36s/nfsu2-v10.aarch64.debug && sha1sum /src/build/r36s/nfsu2-v10.aarch64 | cut -d\" \" -f1 > /src/build/r36s/nfsu2-v10.aarch64.sha1"'
    ```
- **R36H Device Access:**
  - Credentials: `ark` / `ark` via SSH/SCP (use Python `pty` helper).
  - Observed DHCP IPs: `192.168.1.47` / `192.168.1.38` / `192.168.1.37` / `192.168.1.230`. Wi-Fi disconnects while the game is running and reconnects after exiting.

---

## 4. Roadmap Status & Optional Follow-Ups

**All user-approved optimization phases (Phases 1–4: Group 1 & Group 2) are now complete and verified on physical R36H:**
- **Phase 1 (Backlog #11):** VAO Cache & Zero-Copy Uniforms — Complete (`-31.8%` GL state calls to driver).
- **Phase 2 (Backlog #16 & #3):** Native `sub_005D62C0` & Direct Frustum Culling — Complete (`2,457,212/0 bad`).
- **Phase 3 / Group 1 (Backlog #2 & #4):** Native Material/Item Setup, Hot D3DX Setters & `CommitChanges` — Complete (`1,899,576/0 bad`, `-1.57 ms/f` setup+commit, `-1.81 ms/f` normalized game CPU).
- **Phase 4 / Group 2 (Backlog #13, #15, #14):** `NFSU2_HALF_REFLECT`, `NFSU2_CROWD`, `NFSU2_PARTICLES`, `NFSU2_DISTANT_AI_RATE`/`NFSU2_DISTANT_AI_DIST` — Complete and verified on R36H (`-8.6%` game CPU / `-6.7%` render CPU with `NFSU2_HALF_REFLECT=2`; clean distant AI/physics throttling with `NFSU2_DISTANT_AI_RATE=2`).

Optional next steps (outside core roadmap):
1. **Downstream R36H Playable Pack:** Copy `build/r36s/nfsu2-v10.aarch64` to `ports/nfsu2/nfsu2.aarch64.v10`, apply the 4 downstream patchers (`patch_subtitles.py`, `patch_video.py`, `patch_gps.py`, `patch_attract.py`), and rebuild `dist/` packages.
2. **Upstream PR:** Push branch `perf/gl-vao-uniforms` and open PR (optionally relinking `v12` as well).
3. *(Skipped per scope)* Group 3 low-ROI / full-guest-rebuild items (Backlog #17 ThinLTO, #9 compiler flags, #6/#7 minor icall/x87, #12 NEON sound mixer).
