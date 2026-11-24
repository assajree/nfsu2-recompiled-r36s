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

### Full Docker Build & Fast Relink (Completed on Linux Mint 22.3)
- **Cached Docker state:** Image `nfsu2-r36s-build:latest` and volume `nfsu2-r36s-work` (containing `pcrecomp`, lifted C, and `113` guest `.o` files) are preserved on the Linux host.
- **Generated artifacts in `build/r36s/` (Phase 1 + Phase 2 + Phase 3):**
  - `nfsu2-v10.aarch64` (`87,427,048` bytes, SHA-1 `3253114e1f2ef9e7f7ed359d98b0fdd96ef590a0`, `glibc compat OK: max imported version GLIBC_2.18`, `NFSU2_EXE_BUILT_FOR=v10:4788224`)
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

## 4. Remaining Optimization Roadmap (Target Scope: Group 2)

Group 1 (Phase 3 — Backlog #2 & #4) is **complete and verified** on R36H (`1,899,576` calls / `0 bad`). User-approved remaining optimization scope now moves to **Group 2** (high-gain configurable trade-off toggles). Low-ROI / full-guest-recompile items (Group 3: Backlog #17 ThinLTO, #9 compiler flags, #6/#7 minor icall/x87 tweaks, #12 sound mixer) are skipped.

---

### Group 2 (Phase 4) — High-Gain Configurable Trade-off Toggles (Backlog #13, #15, #14)

Implement as configurable environment variables / `nfsu2.cfg` options so users can enable them for locked 30 FPS in heavy city/race scenes:

1. **Backlog #13 — Half-Rate Car Reflection Map Update (`5–10%` Game + Render Thread Saving, Complexity `3/10`):**
   - **Mechanism:** Update the car environment reflection render target (`view` / reflection pass) every other frame (`30 Hz → 15 Hz`, controlled via `NFSU2_HALF_REFLECT=1`, `0` off) while reusing the previous frame's reflection texture on skipped frames.
   - **Why Highest Priority in Group 2:** Cuts both the game-thread scene walk/submission and the render-thread draw calls for the reflection pass in half, with negligible visual difference on the 3.5" 640×480 screen.
2. **Backlog #15 — Fewer Particles / Crowd Toggle (`2–5%` Game + Render Thread Saving, Complexity `2/10`):**
   - **Mechanism:** Expose clean runtime/config toggles for crowd disable (`g_CrowdEnable=0`) and particle density throttling (`NFSU2_PARTICLE_SKIP` or cap).
3. **Backlog #14 — Lower Physics / AI Update Rate for Distant Cars (`5–10%` Game Thread Saving, Complexity `5/10`):**
   - **Mechanism:** Identify distant AI/traffic car simulation steps beyond a distance threshold and throttle non-visible/distant update frequency (`NFSU2_DISTANT_AI_RATE`), preserving full physics rate for the player and nearby opponents.
