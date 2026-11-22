# Handoff — R36H Performance Optimization (`perf/gl-vao-uniforms`)

**Date:** 2026-11-22  
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

### Full Docker Build & Fast Relink (Completed on Linux Mint 22.3)
- **Cached Docker state:** Image `nfsu2-r36s-build:latest` and volume `nfsu2-r36s-work` (containing `pcrecomp`, lifted C, and `113` guest `.o` files) are preserved on the Linux host.
- **Generated artifacts in `build/r36s/` (Phase 1 + Phase 2):**
  - `nfsu2-v10.aarch64` (`87,379,792` bytes, SHA-1 `3d210eef9db61a4649742aef0a1498248ed2c207`, `glibc compat OK: max imported version GLIBC_2.18`, `NFSU2_EXE_BUILT_FOR=v10:4788224`)
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

## 4. Next Steps — Phase 3: Backlog #2 (`sub_005BC940` & `sub_005BDAF0` Native Port in `world_cull.c`)

Target the remaining non-replayed group & item setup bottleneck on the Game Thread (**Backlog #2**: `sub_005BC940` `3.3%` + `sub_005BDAF0` and related helpers `2.6%`, `~6%` combined of game-thread busy time, `34–48` full groups and `~72–104` commit-run items per frame):

1. **Architecture Advantage (Zero Guest-Object Rebuild Needed):**
   - `nativeFlush()` in `src/runtime/world_cull.c` already invokes these lifted functions directly via `ccall()`:
     - Line 1100 (full/non-replayed group material setup): `ccall(sub_005BC940, 1, a)`
     - Lines 355–366 (`paramCalls(uint32_t it)` for full & commit-run items):
       - `ccall(sub_005BDAF0, 2, a)` — world/view/projection matrices & effect parameters from `(M32(it + 0x24), view)`
       - `ccall(sub_005BD7B0, 2, a)` — shadow/projected view 9 matrix parameters (when `M32(M32(M32(it + 0x18) + 0x1c) + 0x59c) != 0`)
       - `ccall(sub_005BCE50, 2, a)` — per-item light context setup (`M32(it + 0x1c)`)
       - `ccall(sub_005BD1B0, 2, a)` — per-item texture override setup (`M32(it + 0x20)`)
   - Therefore, replacing these `ccall(...)` sites in `src/runtime/world_cull.c` with native C implementations requires **only the 20-second fast Docker relink (`ports/r36s/build_cross.sh`)** against the cached `/work/guest-v10` objects.

2. **Implementation Plan for Next Session:**
   - **Step 1 — Extract Lifted C from `nfsu2-r36s-work`:**
     - Extract `sub_005BC940`, `sub_005BDAF0`, `sub_005BD7B0`, `sub_005BCE50`, and `sub_005BD1B0` from `/work/guest-v10/recomp_*.c` inside `nfsu2-r36s-work` (and inspect their disassembly in `SPEED2.EXE`).
   - **Step 2 — Port to Native C in `src/runtime/world_cull.c`:**
     - Extend `replaySetup(uint32_t mat)` (lines 881–916 of `world_cull.c`, which already implements the render-state and blend/alpha globals portion of `sub_005BC940`) into a full native `fullMaterialSetup(uint32_t mat)` that also sets the effect texture/parameter handles and updates `M32(0x870940) = mat`.
     - Port `sub_005BDAF0` (and `sub_005BCE50` / `sub_005BD1B0` / `sub_005BD7B0`) in `paramCalls(uint32_t it)` to compute the W/WV/WVP matrix products in native C (calling `nfs_fx_param_set` / effect parameter write helpers directly).
   - **Step 3 — Add Verify Mode (`NFSU2_NATIVE_SETUP=0/1/2`):**
     - `0` = lifted `ccall(...)`
     - `1` = native C (default)
     - `2` = verify: compare memory state, effect parameter buffers, and captured D3D calls between native C and lifted `ccall(...)` (`[NFSU2 SETUP] verify`).
   - **Step 4 — Fast Relink & R36H Benchmark:**
     - Relink `nfsu2-v10.aarch64` in ~20s via Docker (`nfsu2-runtime-relink-verify` skill), verify `0 bad` with `NFSU2_NATIVE_SETUP=2` on R36H, then measure game-thread CPU ms/frame reduction.
