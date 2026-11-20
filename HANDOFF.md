# Handoff — R36H Performance Optimization (`perf/gl-vao-uniforms`)

**Date:** 2026-11-21  
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
  - Added A/B toggle **`NFSU2_VAO=0`** to disable VAO caching for direct benchmark comparison, plus shutdown hit/miss/eviction statistics logging.
- **`ports/r36s/d3d9_gles/gl_state_cache.h`**:
  - Added `vaoDeleteBufferHook()` in `glsc::DeleteBuffers` and `vaoEnsureDefaultHook()` in `glsc::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ...)`.
- **`ports/r36s/d3d9_gles/d3d9_gles_shaders.h`**:
  - Added `std::memcmp` dirty check in `copyRegs()` and zero-copy / contiguous block upload optimizations in `uploadPacked()`.

### Full Docker Build & Guest Object Export (Completed on Linux Mint 22.3)
- Built from stock US v1.0 `SPEED2.EXE` (`/media/svvv/7D81CFD971486FB1/Game/Need for Speed Underground 2/SPEED2.EXE`, SHA-1 `f4bb0d9942815bb6e054e773994710c612acbc34`) using Docker (`nfsu2-r36s-build`, Ubuntu 22.04, `4 parallel compiles`, total time `5,316 s` / `88 min`):
  ```bash
  sg docker -c 'python3 build.py "/media/svvv/7D81CFD971486FB1/Game/Need for Speed Underground 2/SPEED2.EXE" --export-guest'
  ```
- **Cached Docker state:** Image `nfsu2-r36s-build:latest` and volume `nfsu2-r36s-work` (containing `pcrecomp`, lifted C, and `113` guest `.o` files) are preserved on the Linux host.
- **Generated artifacts in `build/r36s/`:**
  - `nfsu2-v10.aarch64` (`87,371,264` bytes, SHA-1 `b938ea219ce853a77b2917de00e99e41b8fc6053`, `glibc compat OK: max imported version GLIBC_2.18`, `NFSU2_EXE_BUILT_FOR=v10:4788224`)
  - `guest-objs-v10.tar.gz` (`34,228,930` bytes, key `9d5fc65cf7d2cb0c5481a2e2cb0820c622570837`)
  - `nfsu2-v10.aarch64.debug`, `build-v10.txt`, `lift_stats-v10.json`, `build.log`

---

## 2. Phase 1 A/B Benchmark Results (Physical R36H)

Deployed `nfsu2-v10.aarch64` to `/roms/ports/nfsu2/nfsu2.aarch64.v10` and `/roms/ports/nfsu2/nfsu2.aarch64` (stock binary backed up as `/roms/ports/nfsu2/nfsu2.aarch64.v10.bak_pre_vao`).

Measured on physical R36H with `nfsu2.cfg` set to `NFSU2_GRAPHICS=performance`, `NFSU2_FPS=30`, `NFSU2_LANG=Thai`, `NFSU2_DRAW_DIST=200` (city driving at matched `~610–618` draws/frame):

| Metric | Session B (`NFSU2_VAO=0`, `611–618 draws`) | Session A1 (`NFSU2_VAO=1`, `614 draws`) | Session A2 (`NFSU2_VAO=1`, `610 draws`) |
| :--- | :--- | :--- | :--- |
| **FPS / Wall Time** | `28.0–28.8 fps` (`34.73–35.78 ms`) | **`29.9–30.0 fps` (`33.35–33.48 ms`)** | `28.3–29.5 fps` (`34.0–35.3 ms` @ `511–610` draws) |
| **GL state calls (requested)** | `21,707–22,604 / frame` | **`17,146–17,150 / frame`** | **`15,471 / frame`** (`-31.6%`) |
| **GL state calls (reached driver)** | `3,235–3,345 / frame` | **`2,284–2,286 / frame`** | **`2,281 / frame`** (`-31.8%`) |
| **CPU inside Mali GL driver** | `16.37–16.50 ms/frame` | **`15.22–15.48 ms/frame`** | `16.82 ms/frame` |

**Notes from testing:**
- `nfsu2.cfg` on R36H has been restored (removed temporary `export NFSU2_VAO=0`).
- Exiting via `Hotkey Select+Start` triggers `Clean exit` without printing `[D3D9 GLES] VAO cache stats`; quit via in-game menu if hit/miss/eviction counters are needed.
- Raw logs from these runs are saved in conversation scratch (`/home/svvv/.gemini/antigravity/brain/0cf42457-dc08-47e2-bb75-59bf0250a4bb/scratch/log_session_a_vao1.txt`, `log_session_b_vao0.txt`, `log_session_a2_vao1.txt`).

---

## 3. Environment & Operational Notes for Next Session

- **Linux Build Host (Linux Mint 22.3 / GLIBC 2.39):**
  - Do **not** use `--native` on this host (`glibc 2.39` triggers `imports too-new symbol versions`). Always use Docker image `nfsu2-r36s-build` via `sg docker -c '...'`.
  - **Fast runtime-only relink (< 1 minute):** For Phase 2 C/C++ edits in `src/runtime/` or `ports/r36s/`, link directly against `/work/guest-v10` inside `nfsu2-r36s-work` (or `build/r36s/guest-objs-v10.tar.gz`) using `ports/r36s/build_cross.sh` inside `nfsu2-r36s-build`, or re-run `sg docker -c 'python3 build.py "/media/svvv/7D81CFD971486FB1/Game/Need for Speed Underground 2/SPEED2.EXE"'` (which skips Steps 1–4 when cached).
- **R36H Device Access:**
  - Credentials: `ark` / `ark` via SSH/SCP (note: `sshpass` and `paramiko` are not installed on host; use Python `pty` helper).
  - Observed DHCP IPs: `192.168.1.37` / `192.168.1.230` (`playos.local` mDNS did not resolve). Wi-Fi disconnects while the game is running and reconnects after exiting.

---

## 4. Next Steps (Phase 2 — Game-Thread CPU Optimization)

1. **Optional:** Benchmark `NFSU2_GRAPHICS=ultra_performance` (`NFSU2_DRAW_DIST=120`) if direct comparison against the original `HANDOFF.md` baseline table is needed.
2. **Backlog #16:** Implement native C early-exit / full port of `sub_005D62C0` (scenery object setup, already in `WRAP_V10`) in `src/runtime/native_scene.c` with `NFSU2_NATIVE_SCENE=2` verification mode.
3. **Backlog #3:** Implement replay / commit runs for the second render path (`sub_005B9940`, ~7% of game-thread busy time).
4. **Relink & A/B Verify:** Build updated `nfsu2.aarch64.v10` using the cached guest objects and verify on R36H.
