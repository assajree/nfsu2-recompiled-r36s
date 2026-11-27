/*
 * hud_half.c - draw the front-end/HUD renderer at half rate while driving (R36S).
 *
 * sub_005361B0 (called from sub_00537550, the FEng render) draws the HUD: ~200 textured quads, ~8% of
 * the game thread in a race. While driving it runs every other frame; on the frames in between, the
 * D3D9 calls it made last time are replayed natively (d3d9_render_thread.inc, hudRecordBegin, hudReplay), so
 * the HUD is still drawn every frame but its contents update at 15 Hz. Menus are not affected.
 * NFSU2_HUD_HALF=0 turns it off.
 */
#include "recomp_types.h"
#include "nfs_exe_addr.h"   /* v10 addresses -> the lifted exe's (config/exe_functions.txt) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void sub_005361B0_orig(void);
extern void nfsHudRecordBegin(void);
extern void nfsHudRecordEnd(void);
extern int nfsHudReplay(void);
extern int nfsGuestDriving(void);
extern uint32_t getD3D9PresentCount(void);

/*
 * ---- Lazy effect passes in the HUD (NFSU2_HUD_LAZY: 1 default, 0 off, 2 verify) ----
 * Every HUD quad (sub_005C4CF0) does CommitChanges, BeginPass(0), DrawPrimitiveUP, EndPass on the same
 * D3DX effect; BeginPass re-emits every state and constant of the pass (~2/3 of the HUD's time). While the
 * HUD runs, EndPass is deferred, and a following BeginPass of the same effect and pass becomes a
 * CommitChanges inside the still-open pass, unless a device state that the pass sets was changed in
 * between (the bridge watches them: nfsPassWatch) or anything else happened (another effect, Begin/End,
 * synchronous calls, Present). Verify (2): after each such CommitChanges the pass is re-begun and its device
 * calls must all be redundant (counted otherwise). Outside the HUD the overrides call the D3DX methods.
 */
extern void sub_006644A6(void); extern void sub_0066A9F6(void); extern void sub_0066A93B(void);
extern void sub_0065AAEC(void); extern void sub_0065AA06(void);
extern int nfs_fx_setup_mode(void);
extern int nfs_fx_commit_changes(uint32_t fx);
__attribute__((weak)) void nfsPassWatch(int mode) { (void)mode; }   /* d3d9_render_thread.inc */
__attribute__((weak)) int nfsPassConflict(void) { return 1; }
extern uint64_t nfsD3dPushedCount(void);
static int lazyMode = -1, lazyActive;
static uint32_t pFx, pPass; static int pOpen, pPending;
static uint32_t stLazyCommits, stLazyReal, stLazyConflicts, stLazyChecked, stLazyMissed;
int nfs_fx_lazy_enabled(void) {
    if (lazyMode < 0) { const char* e = getenv("NFSU2_HUD_LAZY"); lazyMode = e ? atoi(e) : 1; }
    return lazyMode != 0;
}
static inline void push32h(uint32_t v) { g_esp -= 4; *(uint32_t*)ADDR(g_esp) = v; }
static void realEndPass(uint32_t fx) { push32h(fx); push32h(RECOMP_RETADDR); sub_0065AAEC(); }
static void flushPending(void) {
    if (pPending) { pPending = 0; nfsPassWatch(0); realEndPass(pFx); }
    pOpen = 0;
}
void nfs_fx_lazy_begin_pass(void) {
    const uint32_t fx = *(const uint32_t*)ADDR(g_esp + 4), pass = *(const uint32_t*)ADDR(g_esp + 8);
    if (lazyActive && pPending && fx == pFx && pass == pPass) {
        pPending = 0;
        const int conflict = nfsPassConflict();
        nfsPassWatch(0);
        if (!conflict) {
            if (nfs_fx_setup_mode() != 1 || !nfs_fx_commit_changes(fx)) {
                push32h(fx); push32h(RECOMP_RETADDR); sub_0066A93B();          /* CommitChanges */
            }
            if (lazyMode == 2) {
                realEndPass(fx);
                const uint64_t p0 = nfsD3dPushedCount();
                push32h(pass); push32h(fx); push32h(RECOMP_RETADDR); sub_0066A9F6();
                const uint32_t missed = (uint32_t)(nfsD3dPushedCount() - p0);
                ++stLazyChecked; stLazyMissed += missed;
                if (missed) { static int shown; if (shown++ < 10) printf("[NFSU2 HUD] lazy pass: BeginPass after CommitChanges still changed %u device states\n", missed); }
            }
            ++stLazyCommits;
            g_eax = 0; g_esp += 12; return;
        }
        ++stLazyConflicts;
        realEndPass(pFx); pOpen = 0;
    } else if (pPending) flushPending();
    if (!lazyActive) { sub_0066A9F6(); return; }
    nfsPassWatch(1);
    sub_0066A9F6();
    nfsPassWatch(0);
    ++stLazyReal;
    pFx = fx; pPass = pass; pOpen = g_eax == 0;
}
void nfs_fx_lazy_end_pass(void) {
    const uint32_t fx = *(const uint32_t*)ADDR(g_esp + 4);
    if (lazyActive && pOpen && !pPending && fx == pFx) { pPending = 1; nfsPassWatch(2); g_eax = 0; g_esp += 8; return; }
    if (pPending) flushPending();
    pOpen = 0;
    sub_0065AAEC();
}
void nfs_fx_lazy_commit(void) {
    /* inside the deferred pass: the effect's own device calls are not conflicts */
    const uint32_t fx = *(const uint32_t*)ADDR(g_esp + 4);
    if (pPending) {
        nfsPassWatch(0);
        if (nfs_fx_setup_mode() == 1 && nfs_fx_commit_changes(fx)) { g_eax = 0; g_esp += 8; nfsPassWatch(3); return; }
        sub_0066A93B();
        nfsPassWatch(3);
        return;
    }
    if (nfs_fx_setup_mode() == 1 && nfs_fx_commit_changes(fx)) { g_eax = 0; g_esp += 8; return; }
    sub_0066A93B();
}
void nfs_fx_lazy_begin(void) { if (pPending || pOpen) flushPending(); sub_006644A6(); }
void nfs_fx_lazy_end(void) { if (pPending || pOpen) flushPending(); sub_0065AA06(); }
static void lazyRegion(int on) {
    if (on) { lazyActive = lazyMode > 0; return; }
    if (pPending || pOpen) flushPending();
    lazyActive = 0;
    static uint32_t n;
    if (++n % 240 == 0 && lazyMode > 0) {
        printf("[NFSU2 HUD] lazy passes per HUD run: committed %.1f, real %.1f (conflicts %.1f)", stLazyCommits / 240.0, stLazyReal / 240.0, stLazyConflicts / 240.0);
        if (lazyMode == 2) printf("; verified %u, missed device states %u", stLazyChecked, stLazyMissed);
        printf("\n");
        stLazyCommits = stLazyReal = stLazyConflicts = 0;
    }
}
static void hudOrig(void) { nfs_fx_lazy_enabled(); lazyRegion(1); sub_005361B0_orig(); lazyRegion(0); }

static uint32_t lastFrame = 0xffffffffu, lastRecorded = 0xffffffffu, replays, runs;
void sub_005361B0(void) {
    static int mode = -1;
    if (mode < 0) { const char* e = getenv("NFSU2_HUD_HALF"); mode = e ? atoi(e) : 1; }
    const uint32_t frame = getD3D9PresentCount();
    const int firstCallThisFrame = frame != lastFrame;
    lastFrame = frame;
    if (!mode || !nfsGuestDriving() || !firstCallThisFrame) { hudOrig(); return; }
    if (frame == lastRecorded + 1 && nfsHudReplay()) {
        ++replays;
        g_esp += 4; return;  /* ret */
    }
    nfsHudRecordBegin();
    hudOrig();
    nfsHudRecordEnd();
    lastRecorded = frame;
    if (++runs % 120 == 0) printf("[NFSU2 HUD] half rate: %u runs, %u replays\n", runs, replays);
}

/*
 * ---- Throttled Car Environment Reflection Map (Backlog #13) & Crowd/Particles (Backlog #15) ----
 * sub_005C363B schedules cubemap face updates (views 0x10..0x15) using the divisor table at 0x7FEE90
 * ({?, 6, 4, 1} for g_CarEnvironmentMapEnable = 1..3) and the 3x6x6 face-enable table at 0x7FEEA0.
 * NFSU2_HALF_REFLECT=0 off (default), 1 or 2 = update every 2nd driving frame, N = every Nth frame.
 * On skipped driving frames all active rows for the current level are zeroed so sub_005C363B sets
 * view->active = 0 (skipping both sub_005B9940 scenery walks and sub_005CA7A0 cubemap draws) while
 * g_CarEnvironmentMapEnable stays enabled so cars keep sampling the previous cubemap. On update
 * frames all active rows hold origTbl[row0 + (step % div)] so every cubemap face rotates evenly.
 * NFSU2_CROWD=0|1 and NFSU2_PARTICLES=0|1 optionally override g_CrowdEnable (0x870CD0) and
 * g_ParticleSystemEnable (0x870CEC) at runtime.
 */
static int reflTableMatch(uint32_t base) {
    static const uint32_t row0[6] = {1, 0, 0, 0, 0, 0};
    static const uint32_t row1[6] = {0, 1, 0, 0, 0, 0};
    static const uint32_t row6[6] = {1, 1, 0, 0, 1, 0};
    static const uint32_t row12[6] = {1, 1, 1, 1, 1, 1};
    const uint32_t* d = (const uint32_t*)ADDR(base);
    if (d[1] != 6u || d[2] != 4u || d[3] != 1u) return 0;
    const uint32_t* t = (const uint32_t*)ADDR(base + 0x10u);
    return !memcmp(t + 0 * 6, row0, 24) && !memcmp(t + 1 * 6, row1, 24) &&
           !memcmp(t + 6 * 6, row6, 24) && !memcmp(t + 12 * 6, row12, 24);
}

void nfs_reflect_on_present(void) {
    static int init = 0, rate = 0, crowdOvr = -1, partOvr = -1;
    static uint32_t tblAddr = 0;
    static uint32_t origTbl[13][6];
    static int dirty = 0;
    static uint32_t reflFrame = 0, reflStep = 0, stUpd = 0, stSkip = 0;

    if (!init) {
        init = 1;
        const char* e = getenv("NFSU2_HALF_REFLECT");
        if (e && *e) {
            int v = atoi(e);
            if (v == 1) v = 2;
            if (v > 16) v = 16;
            rate = v >= 2 ? v : 0;
        }
        const char* ec = getenv("NFSU2_CROWD");
        if (ec && *ec) crowdOvr = atoi(ec) ? 1 : 0;
        const char* ep = getenv("NFSU2_PARTICLES");
        if (ep && *ep) partOvr = atoi(ep) ? 1 : 0;
        if (rate >= 2) {
            uint32_t found = 0;
            if (reflTableMatch(0x007FEE90u)) {
                found = 0x007FEE90u;
            } else {
                for (uint32_t va = 0x007F0000u; va < 0x00810000u; va += 4u) {
                    if (reflTableMatch(va)) { found = va; break; }
                }
            }
            if (found) {
                tblAddr = found + 0x10u;
                memcpy(origTbl, (const void*)ADDR(tblAddr), sizeof origTbl);
                printf("[NFSU2 REFLECT] enabled: rate 1/%d, schedule table @ 0x%08x\n", rate, tblAddr);
            } else {
                printf("[NFSU2 REFLECT] schedule table signature not found; disabled\n");
                rate = 0;
            }
        }
        if (crowdOvr >= 0 || partOvr >= 0) {
            printf("[NFSU2 FX] overrides: crowd=%d particles=%d\n", crowdOvr, partOvr);
        }
    }

    if (crowdOvr >= 0) *(uint32_t*)ADDR(0x00870CD0u) = (uint32_t)crowdOvr;
    if (partOvr >= 0)  *(uint32_t*)ADDR(0x00870CECu) = (uint32_t)partOvr;

    if (rate < 2 || !tblAddr) return;

    const int driving = nfsGuestDriving();
    const uint32_t lvl = *(const uint32_t*)ADDR(0x00870CB8u);
    if (!driving || lvl < 1u || lvl > 3u) {
        if (dirty) {
            memcpy((void*)ADDR(tblAddr), origTbl, sizeof origTbl);
            dirty = 0;
        }
        reflFrame = 0;
        return;
    }

    const uint32_t div = lvl == 1u ? 6u : (lvl == 2u ? 4u : 1u);
    const uint32_t row0 = (lvl * 3u - 3u) * 2u;
    const int doUpdate = (reflFrame++ % (uint32_t)rate) == 0u;
    uint32_t* dst = (uint32_t*)ADDR(tblAddr + row0 * 24u);
    if (doUpdate) {
        const uint32_t* src = origTbl[row0 + (reflStep++ % div)];
        for (uint32_t i = 0; i < div; ++i) memcpy(dst + i * 6u, src, 24u);
        ++stUpd;
    } else {
        memset(dst, 0, div * 24u);
        ++stSkip;
    }
    dirty = 1;

    if ((stUpd + stSkip) % 300u == 0u) {
        printf("[NFSU2 REFLECT] rate 1/%d (lvl=%u): %u updated, %u skipped\n", rate, lvl, stUpd, stSkip);
    }
}

/*
 * ---- Distant AI Car Physics & Controller Throttling (Backlog #14) ----
 * NFSU2_DISTANT_AI_RATE=0 off (default), 1 or 2 = 1/2 rate, 3 = 1/3 rate, 4 = 1/4 rate.
 * NFSU2_DISTANT_AI_DIST=45 (metres from human player car; cars within this radius stay full rate).
 *
 * DriverClass at [Car + 0x24]: 1 = RealController (human player, NEVER throttled),
 * 2 = DragController (sub_0042DF50), 3 = RaceCarController (sub_0042C100),
 * 4 = TrafficController (sub_00429DD0). RigidBody* is at [Car + 0x20] / [PhysicsMover + 0x20],
 * with world (x, y, z) at [RigidBody + 0x20..0x28].
 *
 * For distant AI cars (dist > threshold):
 *   1. PhysicsMover::Update (sub_005B6FF0) runs an internal substep loop of
 *      floor(dt / [0x7FC070]) + 1 steps ([0x7FC070] = 0.0044s = 227 Hz -> 8 substeps/frame at 30 FPS).
 *      Temporarily scaling [0x7FC070] by rate (capped at 0.015s >= 66 Hz) cuts distant AI physics
 *      substeps (e.g. 8 -> 4 or 3) while integrating the exact same dt every frame (zero slowdown/jitter).
 *   2. AI controllers (Drag/Race/Traffic) skip updates on staggered frames ((frame + hash) % rate != 0),
 *      holding last frame's steering/throttle/brake for 33ms.
 */
#if defined(NFS_R36S) && NFS_EXE_VARIANT == NFS_EXE_V10
extern void sub_005B6FF0(void);
extern void sub_0042DF50(void);
extern void sub_0042C100(void);
extern void sub_00429DD0(void);

static int aiRate = -1;
static float aiDist2 = 45.0f * 45.0f, aiDistM = 45.0f;
static float aiPlayerPos[3];
static uint32_t aiPlayerFrame = 0xffffffffu;
static uint32_t stAiPhysTot, stAiPhysScaled, stAiCtrlTot, stAiCtrlSkipped, stAiLogFrame;

static int aiValidGuest(uint32_t va) { return va >= 0x400000u && va < 0x30000000u; }

int nfs_distant_ai_rate(void) {
    if (aiRate < 0) {
        const char* e = getenv("NFSU2_DISTANT_AI_RATE");
        int v = (e && *e) ? atoi(e) : 0;
        if (v == 1) v = 2;
        if (v > 4) v = 4;
        aiRate = v >= 2 ? v : 0;
        const char* ed = getenv("NFSU2_DISTANT_AI_DIST");
        if (ed && *ed) {
            float d = (float)atof(ed);
            if (d >= 10.0f && d <= 500.0f) { aiDistM = d; aiDist2 = d * d; }
        }
        if (aiRate >= 2) {
            printf("[NFSU2 AI] distant car throttle enabled: rate 1/%d beyond %.0fm\n", aiRate, aiDistM);
        }
    }
    return aiRate;
}

static int aiCarIsDistant(uint32_t rb, uint32_t frame) {
    if (aiPlayerFrame == 0xffffffffu || frame - aiPlayerFrame > 4u) return 0;
    const float* p = (const float*)ADDR(rb + 0x20u);
    const float dx = p[0] - aiPlayerPos[0];
    const float dy = p[1] - aiPlayerPos[1];
    const float dz = p[2] - aiPlayerPos[2];
    return (dx * dx + dy * dy + dz * dz) > aiDist2;
}

static void aiMaybeLog(uint32_t frame) {
    if (frame != stAiLogFrame && frame > 0u && (frame % 300u) == 0u && (stAiPhysTot || stAiCtrlTot)) {
        stAiLogFrame = frame;
        printf("[NFSU2 AI] rate 1/%d >%.0fm: phys %u/%u scaled, ctrl %u/%u skipped\n",
               aiRate, aiDistM, stAiPhysScaled, stAiPhysTot, stAiCtrlSkipped, stAiCtrlTot);
        stAiPhysScaled = stAiPhysTot = stAiCtrlSkipped = stAiCtrlTot = 0;
    }
}

void nfs_ov_physics_update(void) {
    const uint32_t pm = g_ecx;
    const uint32_t frame = getD3D9PresentCount();
    aiMaybeLog(frame);
    if (aiValidGuest(pm)) {
        const uint32_t car = *(const uint32_t*)ADDR(pm + 0x5Cu);
        const uint32_t rb  = *(const uint32_t*)ADDR(pm + 0x20u);
        if (aiValidGuest(car) && aiValidGuest(rb)) {
            const uint32_t cls = *(const uint32_t*)ADDR(car + 0x24u);
            if (cls == 1u) {
                const float* p = (const float*)ADDR(rb + 0x20u);
                aiPlayerPos[0] = p[0]; aiPlayerPos[1] = p[1]; aiPlayerPos[2] = p[2];
                aiPlayerFrame = frame;
                sub_005B6FF0();
                return;
            }
            if (cls >= 2u && cls <= 4u) {
                ++stAiPhysTot;
                if (aiCarIsDistant(rb, frame)) {
                    float* stepPtr = (float*)ADDR(0x007FC070u);
                    const float origStep = *stepPtr;
                    if (origStep > 0.001f && origStep < 0.01f) {
                        float scaled = origStep * (float)aiRate;
                        if (scaled > 0.015f) scaled = 0.015f;
                        *stepPtr = scaled;
                        ++stAiPhysScaled;
                        sub_005B6FF0();
                        *stepPtr = origStep;
                        return;
                    }
                }
            }
        }
    }
    sub_005B6FF0();
}

static int aiShouldSkipCtrl(uint32_t ctrl) {
    if (!aiValidGuest(ctrl)) return 0;
    const uint32_t car = *(const uint32_t*)ADDR(ctrl + 0x4u);
    if (!aiValidGuest(car)) return 0;
    const uint32_t cls = *(const uint32_t*)ADDR(car + 0x24u);
    if (cls < 2u || cls > 4u) return 0;
    const uint32_t rb = *(const uint32_t*)ADDR(car + 0x20u);
    if (!aiValidGuest(rb)) return 0;
    const uint32_t frame = getD3D9PresentCount();
    ++stAiCtrlTot;
    if (aiCarIsDistant(rb, frame) && ((frame + (ctrl >> 4)) % (uint32_t)aiRate) != 0u) {
        ++stAiCtrlSkipped;
        return 1;
    }
    return 0;
}

void nfs_ov_ai_drag(void)    { if (aiShouldSkipCtrl(g_ecx)) { g_esp += 8; return; } sub_0042DF50(); }
void nfs_ov_ai_race(void)    { if (aiShouldSkipCtrl(g_ecx)) { g_esp += 8; return; } sub_0042C100(); }
void nfs_ov_ai_traffic(void) { if (aiShouldSkipCtrl(g_ecx)) { g_esp += 8; return; } sub_00429DD0(); }
#else
int nfs_distant_ai_rate(void) { return 0; }
#endif
