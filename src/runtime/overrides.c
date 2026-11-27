#include <pthread.h>
/*
 * overrides.c - guest functions replaced by hand-written C.
 *
 * Every entry needs a reason that is a fact about the hardware or the host,
 * not "it crashed": an override hides whatever the original did.
 *
 * Calling convention inside the lifted model: the caller has pushed a return
 * slot, arguments are at g_esp+4.., the body leaves its result in g_eax and
 * pops what `ret N` would (g_esp += 4 + N).
 */
#include <string.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#include "recomp_types.h"
#include "nfs_runtime.h"
#include "nfs_exe_addr.h"   /* v10 addresses -> the lifted exe's (config/exe_functions.txt) */

/* VA of the indirect call in flight. The guest CPU state is one struct (g_cpu) when a build defines
 * RECOMP_TLS_STRUCT, otherwise separate thread-local variables; guest_runtime.cpp defines both. */
#ifdef RECOMP_TLS_STRUCT
#define ov_icall_va (g_cpu.icall_va)
#else
extern RECOMP_TLS uint32_t nfs_icall_va;
#define ov_icall_va (nfs_icall_va)
#endif

/* 0x006F5D25 (v12 0x006F5FC5): Cyrix detection.
 *   xor eax,eax; sahf; mov eax,5; mov ebx,2; div bl; lahf; cmp ah,2; ...
 * Cyrix parts leave the flags untouched across DIV, so AH reads back 2 (the
 * value SAHF loaded, plus the always-set bit 1); Intel/AMD do not. DIV's flags
 * are architecturally undefined and the lifter does not model them, so the
 * lifted probe always answers "Cyrix". The host is never a Cyrix: return 0. */
static void ov_is_cyrix(void) {
    g_eax = 0;
    g_esp += 4;
}

#if defined(NFS_R36S) || defined(NFS_FX_PRESHADER_ONLY)
/* Diagnostics (NFSU2_FX_STATS=1): D3DX effect BeginPass (vtable method 0x0066A9F6, args effect, pass):
 * calls per frame, distinct effects and how often the same effect+pass is applied twice in a row. */
#include <stdio.h>
#include <stdlib.h>
extern void sub_0066A9F6(void);
extern void nfs_fx_param_set(uint32_t handle);
extern void nfs_fx_global_dirty(void);
extern int nfs_fx_cache_mode(void);
extern uint32_t getD3D9PresentCount(void);
static __attribute__((unused)) void ov_fx_begin_pass(void) {  /* R36S only (NFSU2_FX_STATS) */
    static uint32_t frame, calls, repeats, lastFx, lastPass, frames;
    static uint32_t fx[64]; static uint32_t nfx;
    const uint32_t effect = *(const uint32_t*)ADDR(g_esp + 4), pass = *(const uint32_t*)ADDR(g_esp + 8);
    const uint32_t now = getD3D9PresentCount();
    { static uint32_t dumped[16]; static unsigned nd; unsigned k; const uint32_t vt = *(const uint32_t*)ADDR(effect);
      for (k = 0; k < nd && dumped[k] != vt; ++k) {}
      if (k == nd && nd < 16) { dumped[nd++] = vt; printf("[NFSU2 FX] effect %08x vtable %08x:", effect, vt);
        for (k = 0; k < 80; ++k) printf(" %u=%08x", k, *(const uint32_t*)ADDR(vt + 4 * k)); printf("\n"); } }
    if (now != frame) {
        if (++frames % 60 == 0 && calls)
            printf("[NFSU2 FX] BeginPass %u/frame, same effect+pass as previous %u/frame, %u distinct effects\n", calls / 60, repeats / 60, nfx);
        if (frames % 60 == 0) { calls = repeats = 0; nfx = 0; }
        frame = now;
    }
    ++calls;
    const int wasRepeat = effect == lastFx && pass == lastPass;
    if (wasRepeat) ++repeats;
    lastFx = effect; lastPass = pass;
    unsigned i; for (i = 0; i < nfx && fx[i] != effect; ++i) {}
    if (i == nfx && nfx < 64) fx[nfx++] = effect;
    static int skip = -1; if (skip < 0) { const char* e = getenv("NFSU2_FX_SKIP"); skip = e && e[0] == '1'; }
    if (skip && calls > 1 && effect == lastFx && pass == lastPass && wasRepeat) { g_eax = 0; g_esp += 12; return; }
    sub_0066A9F6();
}

/* Experiment: which effect methods 20..58 the game calls per frame, with which handles. */
static const struct { int idx; uint32_t va; } k_fxMethods[] = {
    {20, NFS_VA(0x0066C619)},
    {21, NFS_VA(0x0066C685)},
    {22, NFS_VA(0x0066DE12)},
    {23, NFS_VA(0x0066DF0C)},
    {24, NFS_VA(0x0066DF9E)},
    {25, NFS_VA(0x0066E2DE)},
    {26, NFS_VA(0x0066DA93)},
    {27, NFS_VA(0x0066DC20)},
    {28, NFS_VA(0x0066E5BD)},
    {29, NFS_VA(0x0066E912)},
    {30, NFS_VA(0x0066F215)},
    {31, NFS_VA(0x0066F33F)},
    {32, NFS_VA(0x0066EBDC)},
    {33, NFS_VA(0x0066EF72)},
    {34, NFS_VA(0x0066D017)},
    {35, NFS_VA(0x0066D277)},
    {36, NFS_VA(0x0066C74C)},
    {37, NFS_VA(0x0066C90F)},
    {38, NFS_VA(0x0066CE4E)},
    {39, NFS_VA(0x0066CEE6)},
    {40, NFS_VA(0x0066CA52)},
    {41, NFS_VA(0x0066CAF2)},
    {42, NFS_VA(0x0066CB51)},
    {43, NFS_VA(0x0066CBF1)},
    {44, NFS_VA(0x0066CF2E)},
    {45, NFS_VA(0x0066CFCF)},
    {46, NFS_VA(0x0066CC50)},
    {47, NFS_VA(0x0066CCF0)},
    {48, NFS_VA(0x0066CD4F)},
    {49, NFS_VA(0x0066CDEF)},
    {50, NFS_VA(0x0066D3CA)},
    {51, NFS_VA(0x0066D465)},
    {52, NFS_VA(0x0066D4FD)},
    {53, NFS_VA(0x0066D5D1)},
    {54, NFS_VA(0x0066D673)},
    {55, NFS_VA(0x0066D7C8)},
    {56, NFS_VA(0x0066D883)},
    {57, NFS_VA(0x0066D9D8)},
    {58, NFS_VA(0x0066C6E1)},
    {59, NFS_VA(0x0065C819)},
    {60, NFS_VA(0x00661BA0)},
    {61, NFS_VA(0x0065C015)},
    {62, NFS_VA(0x00660462)},
    {63, NFS_VA(0x00660319)},
    {64, NFS_VA(0x0066BD01)},
    {70, NFS_VA(0x0065C848)},
    {71, NFS_VA(0x0065C5AE)},
    {72, NFS_VA(0x0065E257)},
    {73, NFS_VA(0x00662075)},
    {74, NFS_VA(0x0066208C)},
    {75, NFS_VA(0x00665DD6)},
    {76, NFS_VA(0x0065E571)},
    {77, NFS_VA(0x0065E6D0)},
    {78, NFS_VA(0x0067036F)},
    {79, NFS_VA(0x00670B57)}
};
static inline __attribute__((unused)) void recomp_call_va(uint32_t va) { recomp_func_t f = recomp_lookup(va); if (f) f(); }
static uint32_t fxMethodCalls[80], fxFrame, fxFrames;
static void ov_fx_method(void) {
    /* Hot (~4k calls per frame): method index and lifted target from a small direct-mapped cache. */
    enum { NC = 256 };
    static uint32_t cVa[NC]; static int cIdx[NC]; static recomp_func_t cFn[NC];
    const uint32_t va = ov_icall_va;
    const unsigned h = ((va >> 1) ^ (va >> 9)) & (NC - 1);
    if (cVa[h] != va) {
        unsigned i;
        for (i = 0; i < sizeof k_fxMethods / sizeof k_fxMethods[0] && k_fxMethods[i].va != va; ++i) {}
        cIdx[h] = i < sizeof k_fxMethods / sizeof k_fxMethods[0] ? k_fxMethods[i].idx : -1;
        cFn[h] = recomp_lookup(va);
        cVa[h] = va;
    }
    const int idx = cIdx[h];
    if (idx >= 0) {
        ++fxMethodCalls[idx];
        if (!(idx & 1) && idx <= 56) nfs_fx_param_set(*(const uint32_t*)ADDR(g_esp + 8));  /* Set*(this, handle, ...) */
        else if (idx >= 56) nfs_fx_global_dirty();
    }
    static int stats = -1; if (stats < 0) { const char* e = getenv("NFSU2_FX_STATS"); stats = e && e[0] == '1'; }
    if (stats) {
        const uint32_t now = getD3D9PresentCount();
        if (now != fxFrame) {
            fxFrame = now;
            if (++fxFrames % 60 == 0) { printf("[NFSU2 FXM] effect method calls/frame:"); for (int k = 0; k < 80; ++k) if (fxMethodCalls[k]) printf(" m%d=%u", k, fxMethodCalls[k] / 60); printf("\n"); for (int k = 0; k < 80; ++k) fxMethodCalls[k] = 0; }
        }
    }
    if (cFn[h]) cFn[h]();
}
#endif /* NFS_R36S || NFS_FX_PRESHADER_ONLY */

/* 0x0075CCF0 (v12 0x0075CFD0): the static CRT memcpy/memmove(dst, src, n) (cdecl, returns dst).
 * It handles overlap by copying backward, and reaches its unrolled dword moves
 * through computed jumps into the middle of straight-line blocks (tables at
 * v12 0x75D11C, 0x75D268, 0x75D2B8...). Not every target is a lifted entry, so an
 * overlapping move with 8+ dwords stopped at "ITAIL unresolved 0x0075D27C" (v12).
 * Its contract is exactly the host memmove over guest memory. */
static void ov_crt_memmove(void) {
    uint32_t dst = MEM32(g_esp + 4), src = MEM32(g_esp + 8), n = MEM32(g_esp + 12);
    if (n) memmove((void *)ADDR(dst), (const void *)ADDR(src), n);
    g_eax = dst;
    g_esp += 4;
}

/* 0x0070CCE8 (v12 0x0070CF98): VP6 CPU feature detection (cdecl: int *has_mmx, int *has_xmm, int *has_wmt).
 * The lifted MMX/SSE2 paths emulate 64/128-bit SIMD through stack V128 unions (~10x slower than VP6's
 * built-in 32-bit scalar C routines on ARM64). Report 0 for all three so VP6_InitFunctionPointers selects
 * native 32-bit scalar C functions. */
static void ov_vp6_cpu_features(void) {
    const uint32_t esp = g_esp;
    const uint32_t p_mmx = MEM32(esp + 4);
    const uint32_t p_xmm = MEM32(esp + 8);
    const uint32_t p_wmt = MEM32(esp + 12);
    if (p_mmx) MEM32(p_mmx) = 0;
    if (p_xmm) MEM32(p_xmm) = 0;
    if (p_wmt) MEM32(p_wmt) = 0;
    g_eax = 0;
    g_esp = esp + 4;
}

/* 0x006FFAA7 (v12 0x006FFD4A): YUV420 -> ARGB8888 scanline converter (2 pixels per iteration).
 * Uses precomputed 16-bit signed lookup tables initialized by sub_00700254 at:
 *   Y_tab = 0x008149E0, U_tab = 0x008151E0, V_tab = 0x008159E0 (identical in v10 and v12). */
static void ov_vp6_yuv420_to_argb(void) {
    const uint32_t esp = g_esp;
    uint32_t y_va       = MEM32(esp + 4);
    uint32_t u_va       = MEM32(esp + 8);
    uint32_t v_va       = MEM32(esp + 12);
    uint32_t dst_va     = MEM32(esp + 16);
    uint32_t dst_end_va = MEM32(esp + 20);

    if (dst_end_va > dst_va) {
        const uint32_t count = (dst_end_va - dst_va) >> 3;
        const uint8_t* y_ptr = (const uint8_t*)ADDR(y_va);
        const uint8_t* u_ptr = (const uint8_t*)ADDR(u_va);
        const uint8_t* v_ptr = (const uint8_t*)ADDR(v_va);
        uint8_t* dst_ptr     = (uint8_t*)ADDR(dst_va);

        const int16_t* y_tab = (const int16_t*)ADDR(0x008149E0u);
        const int16_t* u_tab = (const int16_t*)ADDR(0x008151E0u);
        const int16_t* v_tab = (const int16_t*)ADDR(0x008159E0u);

        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t u = *u_ptr++;
            const uint8_t v = *v_ptr++;
            const uint8_t y0 = y_ptr[0];
            const uint8_t y1 = y_ptr[1];
            y_ptr += 2;
#if defined(__aarch64__)
            int16x4_t vu  = vld1_s16(&u_tab[(uint32_t)u * 4]);
            int16x4_t vv  = vld1_s16(&v_tab[(uint32_t)v * 4]);
            int16x4_t vuv = vqadd_s16(vu, vv);
            int16x4_t vy0 = vld1_s16(&y_tab[(uint32_t)y0 * 4]);
            int16x4_t vy1 = vld1_s16(&y_tab[(uint32_t)y1 * 4]);
            int16x8_t vp  = vcombine_s16(vqadd_s16(vy0, vuv), vqadd_s16(vy1, vuv));
            uint8x8_t out = vqmovun_s16(vp);
            vst1_u8(dst_ptr, out);
#else
            for (int c = 0; c < 4; ++c) {
                int uv = (int)u_tab[u * 4 + c] + (int)v_tab[v * 4 + c];
                int p0 = (int)y_tab[y0 * 4 + c] + uv;
                int p1 = (int)y_tab[y1 * 4 + c] + uv;
                dst_ptr[c]     = (uint8_t)(p0 < 0 ? 0 : (p0 > 255 ? 255 : p0));
                dst_ptr[4 + c] = (uint8_t)(p1 < 0 ? 0 : (p1 > 255 ? 255 : p1));
            }
#endif
            dst_ptr += 8;
        }
        g_ecx = y_va + count * 2;
        g_edx = u_va + count;
    }
    g_eax = 0;
    g_esp = esp + 4;
}

void sub_006FFAA7(void) { ov_vp6_yuv420_to_argb(); }
void sub_0070CCE8(void) { ov_vp6_cpu_features(); }

/* ---- On2 VP6 6.1.0.5 native block kernels (called indirectly via 0x8B9C54..0x8B9EAC) ---- */
static const uint8_t k_vp6_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};

static const int32_t k_vp6_bicubic[8][4] = {
    {  0, 128,   0,  0 },
    { -4, 118,  16, -2 },
    { -7, 106,  34, -5 },
    { -8,  90,  53, -7 },
    { -8,  72,  72, -8 },
    { -7,  53,  90, -8 },
    { -5,  34, 106, -7 },
    { -2,  16, 118, -4 },
};

static const int32_t k_vp6_bilinear[8][2] = {
    { 128,   0 },
    { 112,  16 },
    {  96,  32 },
    {  80,  48 },
    {  64,  64 },
    {  48,  80 },
    {  32,  96 },
    {  16, 112 },
};

static inline uint8_t vp6_clamp_u8(int32_t v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* 0x00703BB3 (v12 0x00703E63): ClearSysState (emms no-op on scalar/ARM64) */
static void ov_vp6_clear_sys_state(void) {
    g_esp += 4;
}

/* 0x0071DE86 (v12 0x0071E136): IDct1 (DC-only 8x8 IDCT) */
static void ov_vp6_idct1(void) {
    const uint32_t esp = g_esp;
    const int16_t* in  = (const int16_t*)ADDR(MEM32(esp + 4));
    const int16_t* q   = (const int16_t*)ADDR(MEM32(esp + 8));
    int16_t* out       = (int16_t*)ADDR(MEM32(esp + 12));
    const int16_t val  = (int16_t)((((int32_t)in[0] * (int32_t)q[0]) + 15) >> 5);
#if defined(__aarch64__)
    int16x8_t v = vdupq_n_s16(val);
    for (int i = 0; i < 64; i += 8) vst1q_s16(out + i, v);
#else
    for (int i = 0; i < 64; ++i) out[i] = val;
#endif
    g_esp = esp + 4;
}

static inline void vp6_idct_1d_row(int32_t* p) {
    const int32_t p0 = p[0], p1 = p[1], p2 = p[2], p3 = p[3];
    const int32_t p4 = p[4], p5 = p[5], p6 = p[6], p7 = p[7];
    if ((p0 | p1 | p2 | p3 | p4 | p5 | p6 | p7) == 0) return;

    int32_t A  = ((64277 * p1) >> 16) + ((12785 * p7) >> 16);
    int32_t B  = ((12785 * p1) >> 16) - ((64277 * p7) >> 16);
    int32_t C  = ((54491 * p3) >> 16) + ((36410 * p5) >> 16);
    int32_t D  = ((54491 * p5) >> 16) - ((36410 * p3) >> 16);
    int32_t Ad = (46341 * (A - C)) >> 16;
    int32_t Bd = (46341 * (B - D)) >> 16;
    int32_t Cd = A + C;
    int32_t Dd = B + D;
    int32_t E  = (46341 * (p0 + p4)) >> 16;
    int32_t F  = (46341 * (p0 - p4)) >> 16;
    int32_t G  = ((60547 * p2) >> 16) + ((25080 * p6) >> 16);
    int32_t H  = ((25080 * p2) >> 16) - ((60547 * p6) >> 16);
    int32_t Ed = E - G;
    int32_t Gd = E + G;
    int32_t Add = F + Ad;
    int32_t Bdd = Bd - H;
    int32_t Fd = F - Ad;
    int32_t Hd = Bd + H;

    p[0] = (int32_t)(int16_t)(Gd + Cd);
    p[7] = (int32_t)(int16_t)(Gd - Cd);
    p[1] = (int32_t)(int16_t)(Add + Hd);
    p[2] = (int32_t)(int16_t)(Add - Hd);
    p[3] = (int32_t)(int16_t)(Ed + Dd);
    p[4] = (int32_t)(int16_t)(Ed - Dd);
    p[5] = (int32_t)(int16_t)(Fd + Bdd);
    p[6] = (int32_t)(int16_t)(Fd - Bdd);
}

static inline void vp6_idct_1d_col(const int32_t* p, int16_t* out) {
    const int32_t p0 = p[0],  p1 = p[8],  p2 = p[16], p3 = p[24];
    const int32_t p4 = p[32], p5 = p[40], p6 = p[48], p7 = p[56];
    if ((p0 | p1 | p2 | p3 | p4 | p5 | p6 | p7) == 0) {
        out[0] = out[8] = out[16] = out[24] = out[32] = out[40] = out[48] = out[56] = 0;
        return;
    }

    int32_t A  = ((64277 * p1) >> 16) + ((12785 * p7) >> 16);
    int32_t B  = ((12785 * p1) >> 16) - ((64277 * p7) >> 16);
    int32_t C  = ((54491 * p3) >> 16) + ((36410 * p5) >> 16);
    int32_t D  = ((54491 * p5) >> 16) - ((36410 * p3) >> 16);
    int32_t Ad = (46341 * (A - C)) >> 16;
    int32_t Bd = (46341 * (B - D)) >> 16;
    int32_t Cd = A + C;
    int32_t Dd = B + D;
    int32_t E  = (46341 * (p0 + p4)) >> 16;
    int32_t F  = (46341 * (p0 - p4)) >> 16;
    int32_t G  = ((60547 * p2) >> 16) + ((25080 * p6) >> 16);
    int32_t H  = ((25080 * p2) >> 16) - ((60547 * p6) >> 16);
    int32_t Ed = E - G;
    int32_t Gd = E + G;
    int32_t Add = F + Ad;
    int32_t Bdd = Bd - H;
    int32_t Fd = F - Ad;
    int32_t Hd = Bd + H;

    out[0]  = (int16_t)((Gd + Cd + 8) >> 4);
    out[56] = (int16_t)((Gd - Cd + 8) >> 4);
    out[8]  = (int16_t)((Add + Hd + 8) >> 4);
    out[16] = (int16_t)((Add - Hd + 8) >> 4);
    out[24] = (int16_t)((Ed + Dd + 8) >> 4);
    out[32] = (int16_t)((Ed - Dd + 8) >> 4);
    out[40] = (int16_t)((Fd + Bdd + 8) >> 4);
    out[48] = (int16_t)((Fd - Bdd + 8) >> 4);
}

/* 0x0071D757 (v12 0x0071DA07): IDctSlow (full 8x8 VP6 IDCT) */
static void ov_vp6_idct_slow(void) {
    const uint32_t esp = g_esp;
    const int16_t* in  = (const int16_t*)ADDR(MEM32(esp + 4));
    const int16_t* q   = (const int16_t*)ADDR(MEM32(esp + 8));
    int16_t* out       = (int16_t*)ADDR(MEM32(esp + 12));
    int32_t ip[64];
    for (int i = 0; i < 64; ++i)
        ip[k_vp6_zigzag[i]] = (int32_t)in[i] * (int32_t)q[i];
    for (int r = 0; r < 8; ++r)
        vp6_idct_1d_row(ip + r * 8);
    for (int c = 0; c < 8; ++c)
        vp6_idct_1d_col(ip + c, out + c);
    g_esp = esp + 4;
}

/* 0x0071DBCC (v12 0x0071DE7C): IDct10 (top-left 10 zigzag coeffs 8x8 VP6 IDCT) */
static void ov_vp6_idct10(void) {
    const uint32_t esp = g_esp;
    const int16_t* in  = (const int16_t*)ADDR(MEM32(esp + 4));
    const int16_t* q   = (const int16_t*)ADDR(MEM32(esp + 8));
    int16_t* out       = (int16_t*)ADDR(MEM32(esp + 12));
    int32_t ip[64];
    memset(ip, 0, sizeof(ip));
    for (int i = 0; i < 10; ++i)
        ip[k_vp6_zigzag[i]] = (int32_t)in[i] * (int32_t)q[i];
    for (int r = 0; r < 4; ++r)
        vp6_idct_1d_row(ip + r * 8);
    for (int c = 0; c < 8; ++c)
        vp6_idct_1d_col(ip + c, out + c);
    g_esp = esp + 4;
}

/* 0x0071D0EE (v12 0x0071D39E): ReconIntra(tmp, recon, change, stride) */
static void ov_vp6_recon_intra(void) {
    const uint32_t esp   = g_esp;
    int16_t* tmp         = (int16_t*)ADDR(MEM32(esp + 4));
    uint8_t* recon       = (uint8_t*)ADDR(MEM32(esp + 8));
    const int16_t* chg   = (const int16_t*)ADDR(MEM32(esp + 12));
    const uint32_t strd  = MEM32(esp + 16);
#if defined(__aarch64__)
    const int16x8_t v128 = vdupq_n_s16(128);
    for (int r = 0; r < 8; ++r, tmp += 8, chg += 8, recon += strd) {
        int16x8_t v = vaddq_s16(vld1q_s16(chg), v128);
        vst1q_s16(tmp, v);
        vst1_u8(recon, vqmovun_s16(v));
    }
#else
    for (int r = 0; r < 8; ++r, tmp += 8, chg += 8, recon += strd) {
        for (int c = 0; c < 8; ++c) {
            int16_t v = (int16_t)(chg[c] + 128);
            tmp[c] = v;
            recon[c] = vp6_clamp_u8(v);
        }
    }
#endif
    g_esp = esp + 4;
}

/* 0x0071D17A (v12 0x0071D42A): ReconInter(tmp, recon, ref, change, stride) */
static void ov_vp6_recon_inter(void) {
    const uint32_t esp   = g_esp;
    int16_t* tmp         = (int16_t*)ADDR(MEM32(esp + 4));
    uint8_t* recon       = (uint8_t*)ADDR(MEM32(esp + 8));
    const uint8_t* ref   = (const uint8_t*)ADDR(MEM32(esp + 12));
    const int16_t* chg   = (const int16_t*)ADDR(MEM32(esp + 16));
    const uint32_t strd  = MEM32(esp + 20);
#if defined(__aarch64__)
    for (int r = 0; r < 8; ++r, tmp += 8, chg += 8, recon += strd, ref += strd) {
        int16x8_t vref = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(ref)));
        int16x8_t v = vaddq_s16(vref, vld1q_s16(chg));
        vst1q_s16(tmp, v);
        vst1_u8(recon, vqmovun_s16(v));
    }
#else
    for (int r = 0; r < 8; ++r, tmp += 8, chg += 8, recon += strd, ref += strd) {
        for (int c = 0; c < 8; ++c) {
            int16_t v = (int16_t)((int16_t)ref[c] + chg[c]);
            tmp[c] = v;
            recon[c] = vp6_clamp_u8(v);
        }
    }
#endif
    g_esp = esp + 4;
}

/* 0x0071D216 (v12 0x0071D4C6): ReconInterHalfPixel2(tmp, recon, ref1, ref2, change, stride) */
static void ov_vp6_recon_inter_half2(void) {
    const uint32_t esp   = g_esp;
    int16_t* tmp         = (int16_t*)ADDR(MEM32(esp + 4));
    uint8_t* recon       = (uint8_t*)ADDR(MEM32(esp + 8));
    const uint8_t* ref1  = (const uint8_t*)ADDR(MEM32(esp + 12));
    const uint8_t* ref2  = (const uint8_t*)ADDR(MEM32(esp + 16));
    const int16_t* chg   = (const int16_t*)ADDR(MEM32(esp + 20));
    const uint32_t strd  = MEM32(esp + 24);
#if defined(__aarch64__)
    for (int r = 0; r < 8; ++r, tmp += 8, chg += 8, recon += strd, ref1 += strd, ref2 += strd) {
        uint8x8_t avg = vhadd_u8(vld1_u8(ref1), vld1_u8(ref2));
        int16x8_t v = vaddq_s16(vreinterpretq_s16_u16(vmovl_u8(avg)), vld1q_s16(chg));
        vst1q_s16(tmp, v);
        vst1_u8(recon, vqmovun_s16(v));
    }
#else
    for (int r = 0; r < 8; ++r, tmp += 8, chg += 8, recon += strd, ref1 += strd, ref2 += strd) {
        for (int c = 0; c < 8; ++c) {
            int16_t v = (int16_t)((((int32_t)ref1[c] + (int32_t)ref2[c]) >> 1) + chg[c]);
            tmp[c] = v;
            recon[c] = vp6_clamp_u8(v);
        }
    }
#endif
    g_esp = esp + 4;
}

/* 0x0071D2FC (v12 0x0071D5AC): ReconBlock(pred, change, recon, stride) */
static void ov_vp6_recon_block(void) {
    const uint32_t esp   = g_esp;
    int16_t* pred        = (int16_t*)ADDR(MEM32(esp + 4));
    const int16_t* chg   = (const int16_t*)ADDR(MEM32(esp + 8));
    uint8_t* recon       = (uint8_t*)ADDR(MEM32(esp + 12));
    const uint32_t strd  = MEM32(esp + 16);
#if defined(__aarch64__)
    for (int r = 0; r < 8; ++r, pred += 8, chg += 8, recon += strd) {
        int16x8_t v = vaddq_s16(vld1q_s16(pred), vld1q_s16(chg));
        vst1q_s16(pred, v);
        vst1_u8(recon, vqmovun_s16(v));
    }
#else
    for (int r = 0; r < 8; ++r, pred += 8, chg += 8, recon += strd) {
        for (int c = 0; c < 8; ++c) {
            int16_t v = (int16_t)(pred[c] + chg[c]);
            pred[c] = v;
            recon[c] = vp6_clamp_u8(v);
        }
    }
#endif
    g_esp = esp + 4;
}

/* 0x00704122 (v12 0x007043D2): FilterBlock2d(src1, src2, dst_i16, stride, hfrac, vfrac, bicubic) */
static void ov_vp6_filter2d(void) {
    const uint32_t esp  = g_esp;
    uint32_t src1_va    = MEM32(esp + 4);
    uint32_t src2_va    = MEM32(esp + 8);
    int16_t* dst        = (int16_t*)ADDR(MEM32(esp + 12));
    const int32_t strd  = (int32_t)MEM32(esp + 16);
    const int32_t hfrac = (int32_t)MEM32(esp + 20);
    const int32_t vfrac = (int32_t)MEM32(esp + 24);
    const int32_t bicub = (int32_t)MEM32(esp + 28);

    int32_t diff = (int32_t)(src2_va - src1_va);
    uint32_t src_va = src1_va;
    if (diff < 0) { src_va = src2_va; diff = -diff; }

    if (diff == 1 || diff == strd) {
        const uint8_t* src = (const uint8_t*)ADDR(src_va);
        const int32_t step = (diff == 1) ? 1 : strd;
        const int32_t frac = (diff == 1) ? hfrac : vfrac;
        if (bicub) {
            const int32_t* f = k_vp6_bicubic[frac & 7];
            for (int r = 0; r < 8; ++r, src += strd, dst += 8) {
                for (int c = 0; c < 8; ++c) {
                    int32_t v = (int32_t)src[c - step] * f[0] + (int32_t)src[c] * f[1]
                              + (int32_t)src[c + step] * f[2] + (int32_t)src[c + 2 * step] * f[3];
                    dst[c] = (int16_t)vp6_clamp_u8((v + 64) >> 7);
                }
            }
        } else {
            const int32_t* f = k_vp6_bilinear[frac & 7];
            for (int r = 0; r < 8; ++r, src += strd, dst += 8) {
                for (int c = 0; c < 8; ++c) {
                    int32_t v = (int32_t)src[c] * f[0] + (int32_t)src[c + step] * f[1];
                    dst[c] = (int16_t)((v + 64) >> 7);
                }
            }
        }
    } else if (diff == strd - 1 || diff == strd + 1) {
        if (diff == strd - 1) src_va -= 1;
        const uint8_t* src = (const uint8_t*)ADDR(src_va);
        if (bicub) {
            int32_t tmp[11 * 8];
            const int32_t* fh = k_vp6_bicubic[hfrac & 7];
            const int32_t* fv = k_vp6_bicubic[vfrac & 7];
            const uint8_t* s = src - strd;
            for (int r = 0; r < 11; ++r, s += strd) {
                for (int c = 0; c < 8; ++c) {
                    int32_t v = (int32_t)s[c - 1] * fh[0] + (int32_t)s[c] * fh[1]
                              + (int32_t)s[c + 1] * fh[2] + (int32_t)s[c + 2] * fh[3];
                    tmp[r * 8 + c] = (int32_t)vp6_clamp_u8((v + 64) >> 7);
                }
            }
            const int32_t* t = tmp + 8;
            for (int r = 0; r < 8; ++r, t += 8, dst += 8) {
                for (int c = 0; c < 8; ++c) {
                    int32_t v = t[c - 8] * fv[0] + t[c] * fv[1] + t[c + 8] * fv[2] + t[c + 16] * fv[3];
                    dst[c] = (int16_t)vp6_clamp_u8((v + 64) >> 7);
                }
            }
        } else {
            int32_t tmp[9 * 8];
            const int32_t* fh = k_vp6_bilinear[hfrac & 7];
            const int32_t* fv = k_vp6_bilinear[vfrac & 7];
            const uint8_t* s = src;
            for (int r = 0; r < 9; ++r, s += strd) {
                for (int c = 0; c < 8; ++c) {
                    tmp[r * 8 + c] = ((int32_t)s[c] * fh[0] + (int32_t)s[c + 1] * fh[1] + 64) >> 7;
                }
            }
            const int32_t* t = tmp;
            for (int r = 0; r < 8; ++r, t += 8, dst += 8) {
                for (int c = 0; c < 8; ++c) {
                    dst[c] = (int16_t)((t[c] * fv[0] + t[c + 8] * fv[1] + 64) >> 7);
                }
            }
        }
    }
    g_esp = esp + 4;
}

/* 0x00704325 (v12 0x007045D5): FilterBlock2dBilid(src1, src2, dst_u8, stride, hfrac, vfrac) */
static void ov_vp6_filter2d_bilid(void) {
    const uint32_t esp  = g_esp;
    uint32_t src1_va    = MEM32(esp + 4);
    uint32_t src2_va    = MEM32(esp + 8);
    uint8_t* dst        = (uint8_t*)ADDR(MEM32(esp + 12));
    const int32_t strd  = (int32_t)MEM32(esp + 16);
    const int32_t hfrac = (int32_t)MEM32(esp + 20);
    const int32_t vfrac = (int32_t)MEM32(esp + 24);

    int32_t diff = (int32_t)(src2_va - src1_va);
    uint32_t src_va = src1_va;
    if (diff < 0) { src_va = src2_va; diff = -diff; }

    if (diff == 1 || diff == strd) {
        const uint8_t* src = (const uint8_t*)ADDR(src_va);
        const int32_t step = (diff == 1) ? 1 : strd;
        const int32_t* f = k_vp6_bilinear[((diff == 1) ? hfrac : vfrac) & 7];
        for (int r = 0; r < 8; ++r, src += strd, dst += 8) {
            for (int c = 0; c < 8; ++c) {
                dst[c] = (uint8_t)(((int32_t)src[c] * f[0] + (int32_t)src[c + step] * f[1] + 64) >> 7);
            }
        }
    } else if (diff == strd - 1 || diff == strd + 1) {
        if (diff == strd - 1) src_va -= 1;
        const uint8_t* src = (const uint8_t*)ADDR(src_va);
        int32_t tmp[9 * 8];
        const int32_t* fh = k_vp6_bilinear[hfrac & 7];
        const int32_t* fv = k_vp6_bilinear[vfrac & 7];
        const uint8_t* s = src;
        for (int r = 0; r < 9; ++r, s += strd) {
            for (int c = 0; c < 8; ++c) {
                tmp[r * 8 + c] = ((int32_t)s[c] * fh[0] + (int32_t)s[c + 1] * fh[1] + 64) >> 7;
            }
        }
        const int32_t* t = tmp;
        for (int r = 0; r < 8; ++r, t += 8, dst += 8) {
            for (int c = 0; c < 8; ++c) {
                dst[c] = (uint8_t)((t[c] * fv[0] + t[c + 8] * fv[1] + 64) >> 7);
            }
        }
    }
    g_esp = esp + 4;
}

static const struct { uint32_t va; nfs_override_fn fn; } k_overrides[] = {
    { NFS_VA(0x006F5D25), ov_is_cyrix },
#if defined(NFS_R36S)
    { NFS_VA(0x0066A9F6), ov_fx_begin_pass },
#endif
    { NFS_VA(0x0075CCF0), ov_crt_memmove },
    { NFS_VA(0x006FFAA7), ov_vp6_yuv420_to_argb },
    { NFS_VA(0x0070CCE8), ov_vp6_cpu_features },
    { NFS_VA(0x00703BB3), ov_vp6_clear_sys_state },
    { NFS_VA(0x00704122), ov_vp6_filter2d },
    { NFS_VA(0x00704325), ov_vp6_filter2d_bilid },
    { NFS_VA(0x0071D0EE), ov_vp6_recon_intra },
    { NFS_VA(0x0071D17A), ov_vp6_recon_inter },
    { NFS_VA(0x0071D216), ov_vp6_recon_inter_half2 },
    { NFS_VA(0x0071D2FC), ov_vp6_recon_block },
    { NFS_VA(0x0071D757), ov_vp6_idct_slow },
    { NFS_VA(0x0071DBCC), ov_vp6_idct10 },
    { NFS_VA(0x0071DE86), ov_vp6_idct1 },
};

/* Called on every indirect call that misses its call-site cache (polymorphic vtable sites), and for every
 * effect-method call (overrides are never site-cached): a hash of the ~55 overridden VAs instead of a linear
 * scan of the method table for any target in the D3DX range. Built once; read-only afterwards (the sound
 * threads call this too). */
#if defined(NFS_R36S)
extern int nfs_fx_lazy_enabled(void);
extern void nfs_fx_lazy_begin(void); extern void nfs_fx_lazy_begin_pass(void); extern void nfs_fx_lazy_commit(void);
extern void nfs_fx_lazy_end_pass(void); extern void nfs_fx_lazy_end(void);
#endif
enum { OV_SLOTS = 256 };
static uint32_t ovKey[OV_SLOTS]; static nfs_override_fn ovFn[OV_SLOTS];
static volatile int ovReady;
static unsigned ov_slot(uint32_t va) { return (va * 0x9e3779b1u) >> 24; }
static void ov_put(uint32_t va, nfs_override_fn fn) {
    unsigned h = ov_slot(va);
    while (ovKey[h] && ovKey[h] != va) h = (h + 1) & (OV_SLOTS - 1);
    if (!ovKey[h]) { ovKey[h] = va; ovFn[h] = fn; }  /* first entry wins, like the scans did */
}
static void ov_build(void) {
#if defined(NFS_R36S)
    const char* e = getenv("NFSU2_FX_STATS");
    const int stats = e && e[0] == '1';
    if (stats || nfs_fx_cache_mode() != 0)
        for (unsigned i = 0; i < sizeof k_fxMethods / sizeof k_fxMethods[0]; ++i)
            if (k_fxMethods[i].va != NFS_VA(0x0065E26C)) ov_put(k_fxMethods[i].va, ov_fx_method);
    for (unsigned i = 0; i < sizeof k_overrides / sizeof k_overrides[0]; i++)
        if (k_overrides[i].va != NFS_VA(0x0066A9F6) || stats) ov_put(k_overrides[i].va, k_overrides[i].fn);
    /* lazy effect passes (hud_half.c): Begin, BeginPass, CommitChanges, EndPass, End of the D3DX effect */
    if (!stats && nfs_fx_lazy_enabled()) {
        ov_put(NFS_VA(0x006644A6), nfs_fx_lazy_begin); ov_put(NFS_VA(0x0066A9F6), nfs_fx_lazy_begin_pass);
        ov_put(NFS_VA(0x0066A93B), nfs_fx_lazy_commit); ov_put(NFS_VA(0x0065AAEC), nfs_fx_lazy_end_pass); ov_put(NFS_VA(0x0065AA06), nfs_fx_lazy_end);
    }
#else
    /* Android: the effect Set* hooks feed the preshader cache (fx_cache.c, NFS_FX_PRESHADER_ONLY). */
#if defined(NFS_FX_PRESHADER_ONLY)
    if (nfs_fx_cache_mode() != 0)
        for (unsigned i = 0; i < sizeof k_fxMethods / sizeof k_fxMethods[0]; ++i)
            if (k_fxMethods[i].va != NFS_VA(0x0065E26C)) ov_put(k_fxMethods[i].va, ov_fx_method);
#endif
    for (unsigned i = 0; i < sizeof k_overrides / sizeof k_overrides[0]; i++)
        ov_put(k_overrides[i].va, k_overrides[i].fn);
#endif
    __atomic_store_n(&ovReady, 1, __ATOMIC_RELEASE);
}
nfs_override_fn nfs_override_lookup(uint32_t va) {
    if (__builtin_expect(!__atomic_load_n(&ovReady, __ATOMIC_ACQUIRE), 0)) {
        static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&m); if (!ovReady) ov_build(); pthread_mutex_unlock(&m);
    }
    if (!va) return 0;
    for (unsigned h = ov_slot(va);; h = (h + 1) & (OV_SLOTS - 1)) {
        const uint32_t k = __atomic_load_n(&ovKey[h], __ATOMIC_ACQUIRE);
        if (!k) break;
        if (k == va) return ovFn[h];
    }
    return 0;
}

int nfs_override_count(void) { return (int)(sizeof k_overrides / sizeof k_overrides[0]); }
