# Handoff — R36H Performance Optimization (`perf/gl-vao-uniforms`)

**Date:** 2026-11-20  
**Branch:** `perf/gl-vao-uniforms` (clean performance-only branch off `main`, suitable for upstream PR; Thai/subtitle/GPS binary patches are applied separately downstream)  
**Latest Commit:** `f23c3cd` — `perf(d3d9_gles): add ES3 512-slot VAO cache and zero-copy uniform uploads`  
**Target Hardware:** R36H (`HL-R36H-V20`, RK3326 Quad Cortex-A35 @ 1.3 GHz, Mali-G31 MP2 ES 3.2, 1 GB RAM, 640×480, playOS Debian 13 arm64)  
**Target Binary:** `nfsu2.aarch64.v10` (for US v1.0 `SPEED2.EXE`, `4,788,224` bytes, SHA-1 `f4bb0d9942815bb6e054e773994710c612acbc34`)

---

## 1. Completed Work

### Phase 1 — GL Driver Optimization (Backlog #11)
Implemented and committed in `f23c3cd`:
- **`ports/r36s/d3d9_gles/d3d9_gles_device.h` & `ports/r36s/d3d9_gles/d3d9_gles.cpp`**:
  - Added a **4-way set-associative VAO cache** (`128 sets × 4 ways = 512 slots`, zero heap allocation) for OpenGL ES 3.0+ contexts (Mali-G31 reports ES 3.2).
  - Caches vertex attribute descriptors, VBOs, and `GL_ELEMENT_ARRAY_BUFFER` (IBO) per VBO-backed non-instanced draw (`!upData && !instanceLayout && elemCount <= 8`).
  - Cache hit collapses `glBindBuffer(ARRAY)` + `glEnableVertexAttribArray` + `glVertexAttribPointer` + `glDisableVertexAttribArray` + `glBindBuffer(ELEMENT_ARRAY)` into a single `glBindVertexArray(vao)` call.
  - Added A/B toggle **`NFSU2_VAO=0`** to disable VAO caching for direct benchmark comparison, plus shutdown hit/miss/eviction statistics logging.
  - Enforced `bindDefaultVao()` before Virtual Keyboard overlay, FPS overlay, `presentBackBuffer()`, user-pointer draws (`DPUP`/`DIPUP`), and instanced draws.
- **`ports/r36s/d3d9_gles/gl_state_cache.h`**:
  - Added `vaoDeleteBufferHook()` in `glsc::DeleteBuffers` to invalidate any cached VAO referencing a deleted VBO/IBO (prevents GL buffer ID reuse bugs).
  - Added `vaoEnsureDefaultHook()` in `glsc::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ...)` to automatically switch back to VAO `0` before `IndexBuffer::Unlock()` or non-VAO draws touch the element array binding.
  - Added `onVaoSwitchedToDefault()` to reset default-VAO state cache tracking cleanly.
- **`ports/r36s/d3d9_gles/d3d9_gles_shaders.h`**:
  - `copyRegs()` now checks `std::memcmp` before writing and returns `bool`; redundant constant writes no longer increment `m_vsGen` / `m_psGen` and skip `uploadPacked()` entirely.
  - `uploadPacked()` avoids per-draw `.assign(size, 0)` zeroing, copies contiguous `registerCount × 16` byte blocks per uniform instead of looping per register, uses `std::vector::swap` (`fs.swap(m_scratchF)`) instead of deep-copying, and skips int/bool work when unused.

### Compilation Verification (Runtime Objects)
- Verified on physical R36H (`192.168.1.19`) with `g++ 14.2.0` (`-mcpu=cortex-a35`):
  ```bash
  CROSS_COMPILE="" CC=gcc CXX=g++ STRIP=strip OBJDUMP=objdump COMPILE_ONLY=1 NFS_EXE_VARIANT=1 bash ports/r36s/build_ci.sh
  ```
- Result: **All 15 runtime objects compiled cleanly (`EXIT: 0`)**.

---

## 2. Baseline Performance Measurements (Pre-Phase 1 Binary)

Measured on physical R36H (`192.168.1.19`) running `ultra_performance` (`NFSU2_DRAW_DIST=120`, `NFSU2_RENDER_SCALE=0.75`, `NFSU2_GPU_PROBE=0`, `NFSU2_PRESENT_SLOTS=3`, `smbd`/`nmbd`/`filebrowser` paused, `swappiness=10`):

| Metric (In-City Driving) | Baseline (`v1.1.0` binary + `ultra_performance`) | Target After Phase 1 + Phase 2 |
| :--- | :--- | :--- |
| Frame Wall Time / FPS | `33.36–33.79 ms` (`29.6–30.0 fps`), dips to `36.04 ms` (`27.7 fps`) | Locked `33.3 ms` (`30.0 fps`) with headroom |
| `game` thread CPU | `24.3–25.8 ms/frame` | `~20 ms/frame` (Phase 2: Native Scene) |
| `render` thread CPU | `15.7–17.5 ms/frame` | `~10–12 ms/frame` (Phase 1: VAO + Uniforms) |
| CPU inside Mali GL driver | `10.16–11.15 ms/frame` | `< 8 ms/frame` |
| GL state calls (driver) | `1,930–2,150 / frame` (`15,054–17,205` requested) | `< 1,000 / frame` |
| Draws / frame | `422–482 draws/frame` | — |
| CPU Temp / Clock | `71.7–73.8 °C` @ `1296 MHz` | Lower thermal headroom |

---

## 3. Current Lifting & Build State

1. **Lifting (`SPEED2.EXE` v1.0 → C) is COMPLETE**:
   - Lifted natively on Windows PC (`python build.py ... --lift-only`, no Docker needed).
   - Output is cached locally at `src/recomp/gen/` (gitignored): **27,758 functions, 70 C files, 0 lift failures, full x87-window annotations**.
   - Compressed archive also exists on Windows host at `C:\Users\Parkpoom_p\AppData\Local\nfsu2-r36s\gen_v10.tar.gz` (`85.3 MB`), and `pcrecomp` patched checkout at `C:\Users\Parkpoom_p\AppData\Local\nfsu2-r36s\pcrecomp`.
2. **R36H Device State (`192.168.1.19` / `playos.local`)**:
   - **Cleaned up completely**: `/roms/.build_swap` (2 GB), `/roms/build/` (734 MB), `g++` packages, and `apt` cache/lists have been removed.
   - Free space restored: `/` = `1.1 GB` free (`89%`), `/roms` = `3.3 GB` free (`84%`).
   - Do **not** compile guest objects on R36H (895 MB RAM causes heavy SD-card swap thrashing); build on a Linux host instead.

---

## 4. Recommended Next Steps (Linux Host Build)

### Step 1: Build `nfsu2.aarch64.v10` on Linux
- **Option A (Recommended — Native Linux via `build.py --native`):**
  1. Checkout branch `perf/gl-vao-uniforms` on the Linux build machine.
  2. Run `python3 build.py /path/to/SPEED2.EXE --native --export-guest` (builds `guest-objs-v10.tar.gz` + `nfsu2.aarch64` in minutes using all Linux CPU cores; subsequent runtime-only edits link in seconds via `ports/r36s/build_ci.sh`).
- **Option B (Linux with Docker):**
  - Run `python3 build.py /path/to/SPEED2.EXE --export-guest` (uses the official Ubuntu 22.04 ARM64 cross-compile container).

### Step 2: Link & A/B Benchmark Phase 1 on R36H
1. Link `nfsu2.aarch64.v10` using `ports/r36s/build_ci.sh` (`NFS_EXE_VARIANT=1 OUT_NAME=nfsu2.aarch64.v10`).
2. Copy binary to `/roms/ports/nfsu2/nfsu2.aarch64.v10` (keep backup of stock binary).
3. Run A/B test in city driving:
   - Session A: default (`NFSU2_VAO=1`)
   - Session B: `NFSU2_VAO=0`
4. Compare `GL state calls` reached driver, `CPU inside the GL driver`, `render` thread ms/frame, and `[D3D9 GLES] VAO cache stats` in `/roms/ports/nfsu2/log.txt`.

### Step 3: Phase 2 — Game-Thread CPU Optimization (Backlog #3 & #16)
Once Phase 1 is verified and the fast 20-second link loop is live:
- Target the remaining primary bottleneck (`game` thread `24.8 ms/frame`):
  1. **Backlog #16:** Native C early-exit / full port of `sub_005D62C0` (scenery object setup, already in `WRAP_V10`!) in `src/runtime/native_scene.c` with `NFSU2_NATIVE_SCENE=2` verify mode.
  2. **Backlog #3:** Replay / commit runs for the second render path (`sub_005B9940`, ~7% of game-thread busy time).
  3. **Optional Cleanup:** Remove `/roms/.build_swap` and `/roms/build/` on R36H once final binaries are archived.
