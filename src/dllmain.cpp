// ACZUltrawide - ultrawide support for ACE COMBAT ZERO (Steam), loaded as a version.dll proxy.
// The exe on disk is never modified; everything is patched in memory once the code has been unpacked.
#include <windows.h>
#include <intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <share.h>
#include <math.h>
#include <string.h>
#include <initializer_list>
#include "exports.h"

static uint8_t* g_base;
static char g_dir[MAX_PATH];
static FILE* g_log;
static void Log(const char* f, ...) {
    if (!g_log) return;
    va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a); fputc('\n', g_log); fflush(g_log);
}
template <class T> static T& At(uint32_t rva) { return *(T*)(g_base + rva); }

// ---------------------------------------------------------------- config
static int  c_w, c_h;
static bool c_enabled = true, c_fov = true;
static int  c_hud = 1, c_debug = 0;

static void LoadConfig() {
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\ACZUltrawide.ini", g_dir);
    c_enabled = GetPrivateProfileIntA("Ultrawide", "Enabled", 1, p) != 0;
    c_w = GetPrivateProfileIntA("Ultrawide", "Width", 0, p);
    c_h = GetPrivateProfileIntA("Ultrawide", "Height", 0, p);
    c_fov = GetPrivateProfileIntA("Ultrawide", "FixFOV", 1, p) != 0;
    c_hud = GetPrivateProfileIntA("HUD", "FixHUD", 1, p);
    c_debug = GetPrivateProfileIntA("Debug", "Debug", 0, p);
    if (c_w <= 0 || c_h <= 0) {
        DEVMODEA m = {}; m.dmSize = sizeof m; // real pixels, independent of DPI scaling
        if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &m)) { c_w = (int)m.dmPelsWidth; c_h = (int)m.dmPelsHeight; }
    }
}

// ---------------------------------------------------------------- patch helpers
static void Write(uint32_t rva, const void* d, size_t n) {
    DWORD o; VirtualProtect(g_base + rva, n, PAGE_EXECUTE_READWRITE, &o);
    memcpy(g_base + rva, d, n);
    VirtualProtect(g_base + rva, n, o, &o);
    FlushInstructionCache(GetCurrentProcess(), g_base + rva, n);
}
static bool Match(uint32_t rva, const char* hex) {
    const uint8_t* p = g_base + rva;
    while (*hex) {
        if (*hex == ' ') { hex++; continue; }
        if (*hex == '?') { hex += 2; p++; continue; }
        unsigned v; sscanf_s(hex, "%2x", &v);
        if (*p++ != v) return false;
        hex += 2;
    }
    return true;
}
static void JmpAbs(uint8_t* at, const void* to) { // 14 bytes
    uint8_t j[14] = { 0xFF, 0x25, 0, 0, 0, 0 }; memcpy(j + 6, &to, 8); memcpy(at, j, 14);
}
static uint8_t* g_near; static size_t g_nearUsed;
static uint8_t* Near(size_t n) {
    if (!g_near) {
        for (uint64_t a = ((uint64_t)g_base + 0x4000000) & ~0xFFFFull; a < (uint64_t)g_base + 0x60000000; a += 0x10000)
            if ((g_near = (uint8_t*)VirtualAlloc((void*)a, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) != nullptr) break;
        if (!g_near) return nullptr;
    }
    uint8_t* r = g_near + g_nearUsed; g_nearUsed += (n + 15) & ~(size_t)15; return r;
}
static float* NearF(float v) { float* f = (float*)Near(4); *f = v; return f; }
// repoint the rip-relative disp32 that ends the instruction [rva, rva+len) to 'to'
static void Repoint(uint32_t rva, int len, const void* to) {
    int32_t d = (int32_t)((uint8_t*)to - (g_base + rva + len));
    Write(rva + len - 4, &d, 4);
}
// hook: steals 'len' position independent bytes (>=14), returns a trampoline
static void* Hook(uint32_t rva, int len, void* fn) {
    uint8_t* t = Near(len + 14);
    memcpy(t, g_base + rva, len); JmpAbs(t + len, g_base + rva + len);
    uint8_t b[32]; memset(b, 0x90, sizeof b); JmpAbs(b, fn);
    Write(rva, b, len);
    return t;
}

// ---------------------------------------------------------------- addresses (build 25908842)
enum : uint32_t {
    BUILD_TIMESTAMP = 0x6ac61130, // PE header TimeDateStamp of the only supported acz.exe
    RVA_resW = 0x312770, RVA_resH = 0x3206a8, RVA_is43 = 0x32f956, RVA_borderless = 0x32f955,
    RVA_dev = 0x35b9f8, RVA_hwnd = 0x35ba08, RVA_curRT = 0x30fac4,
};
static bool g_active; static int g_W, g_H; static float g_k = 1.f; // k = aspect / (16/9)

// ---------------------------------------------------------------- aspect option (replaces 0x189c60)
static void SetAspect(uint8_t* opt, int wide) {
    int H = At<int>(RVA_resH), W;
    if (g_active) { wide = 1; W = g_W; }
    else W = wide == 1 ? H * 16 / 9 : H * 4 / 3;
    *(int*)(opt + 0x24) = wide;
    At<int>(RVA_resW) = W;
    At<uint8_t>(RVA_is43) = (W / 4) * 3 == H;
    uint8_t* dev = At<uint8_t*>(RVA_dev);
    dev[0xe4] = *(int*)(dev + 0xdc) != W || *(int*)(dev + 0xe0) != H;
    *(int64_t*)(dev + 0xd4) = 0;
    *(int*)(dev + 0xdc) = W; *(int*)(dev + 0xe0) = H;
    if (!At<uint8_t>(RVA_borderless)) {
        RECT r = { 0, 0, W, H }; AdjustWindowRect(&r, 0xcb0000, FALSE);
        SetWindowPos(At<HWND>(RVA_hwnd), 0, 0, 0, r.right - r.left, r.bottom - r.top, 0x66);
    }
}

// ---------------------------------------------------------------- camera (0x1fc540)
static void (*o_CamUpdate)(uint8_t*);
static void CamUpdate(uint8_t* c) {
    float& w = *(float*)c; float& h = *(float*)(c + 4);
    // the camera used for 2D layouts never leaves the origin; it must keep its 16:9 text/sprite scale
    const float* eye = (float*)(c + 0x30);
    bool is2D = eye[0] == 0.f && eye[1] == 0.f && eye[2] == 0.f;
    if (c_fov && !is2D && *(int16_t*)(c + 0x28) == 512 && *(int16_t*)(c + 0x2a) == 448 && h > 0 && fabsf(w / h - 16.f / 9.f) < 0.002f) {
        w = h * ((float)g_W / g_H);
        *(int*)(c + 0x1a0) |= 1;
    }
    o_CamUpdate(c);
}

// The flight HUD (0xae550) projects world positions with the main camera. Since the HUD is drawn into a centered 16:9
// area it gets a copy of that camera which maps to 16:9 screen coordinates; the clip matrix (+0x150) stays ultrawide so
// that markers outside the 16:9 area remain visible.
static uint8_t* ShadowCamera(uint8_t* c) {
    static uint8_t ring[8][0x200]; static unsigned n;
    if (!c_hud || !c_fov || !c) return c;
    float w = *(float*)c, h = *(float*)(c + 4);
    if (h <= 0 || fabsf(w / h - (float)g_W / g_H) > 0.005f) return c;
    uint8_t* r = ring[n++ & 7]; memcpy(r, c, 0x1c0);
    *(float*)r = w / g_k; *(float*)(r + 0x198) *= g_k;
    for (int m : { 0x90, 0x110 }) for (int i = 0; i < 4; i++) {
        float* row = (float*)(r + m + i * 16);
        row[0] = row[0] * g_k + (1.f - g_k) * 2048.f * row[3];
    }
    return r;
}
// hook that replaces the third argument (r8) with its shadow camera and leaves every other argument alone
static void HookShadowR8(uint32_t rva, int len) {
    uint8_t* s = Near(128); uint8_t* p = s;
    auto B = [&](std::initializer_list<int> l) { for (int x : l) *p++ = (uint8_t)x; };
    B({ 0x51, 0x52, 0x41,0x51 });                                  // push rcx, rdx, r9      (entry rsp is 8 mod 16 -> now aligned)
    B({ 0x48,0x83,0xEC,0x60 });                                    // sub rsp,60h
    B({ 0xF3,0x0F,0x7F,0x44,0x24,0x20, 0xF3,0x0F,0x7F,0x4C,0x24,0x30, 0xF3,0x0F,0x7F,0x54,0x24,0x40, 0xF3,0x0F,0x7F,0x5C,0x24,0x50 });
    B({ 0x4C,0x89,0xC1, 0x48,0xB8 }); void* f = (void*)ShadowCamera; memcpy(p, &f, 8); p += 8; B({ 0xFF,0xD0, 0x49,0x89,0xC0 });
    B({ 0xF3,0x0F,0x6F,0x44,0x24,0x20, 0xF3,0x0F,0x6F,0x4C,0x24,0x30, 0xF3,0x0F,0x6F,0x54,0x24,0x40, 0xF3,0x0F,0x6F,0x5C,0x24,0x50 });
    B({ 0x48,0x83,0xC4,0x60, 0x41,0x59, 0x5A, 0x59 });
    void* t = Hook(rva, len, s);
    JmpAbs(p, t);
}

// ---------------------------------------------------------------- draw (0x20ff60)
static uint8_t* g_gs;
static void (*o_Flush)(uint8_t*);
static void Flush(uint8_t* gs) { uint8_t* old = g_gs; g_gs = gs; o_Flush(gs); g_gs = old; }
struct Vtx { float a[16]; };
typedef void (*DrawFn)(void*, int, Vtx*, int, int, float, uint8_t, float, float, float, float, uint8_t, void*, uint16_t*, int);
static DrawFn o_Draw;
static void (*o_Scissor)(int, int, int, int);
static FILE* g_dump; static int g_dumpLeft;

static void DumpDraw(uint32_t ret, void* tex, int smp, Vtx* v, int n, int prim, float a, float sx, float sy, float ox, float oy, void* sh, int nidx, int cls) {
    float lo[16], hi[16];
    for (int j = 0; j < 16; j++) { lo[j] = 1e30f; hi[j] = -1e30f; }
    for (int i = 0; i < n; i++) for (int j = 0; j < 16; j++) { lo[j] = fminf(lo[j], v[i].a[j]); hi[j] = fmaxf(hi[j], v[i].a[j]); }
    fprintf(g_dump, "%x,rt %d,tex %p,smp %d,n %d,prim %d,a %g,cls %d,s %g %g,o %g %g,0,sh %llx,ni %d,sc %d %d %d %d", ret, At<int>(RVA_curRT), tex, smp, n, prim, a, cls, sx, sy, ox, oy,
        (long long)((uint8_t*)sh - g_base), nidx, At<int>(0x3210d0), At<int>(0x3210d4), At<int>(0x3210d8), At<int>(0x3210dc));
    for (int j = 0; j < 16; j++) fprintf(g_dump, ",[%d] %g..%g", j, j == 0 ? lo[j] - ox : j == 1 ? lo[j] - oy : lo[j], j == 0 ? hi[j] - ox : j == 1 ? hi[j] - oy : hi[j]);
    fprintf(g_dump, ",GS");
    if (g_gs) for (int o = 0xc0; o < 0x350; o += 8) fprintf(g_dump, " %x:%llx", o, *(unsigned long long*)(g_gs + o));
    fprintf(g_dump, "\n");
    if (--g_dumpLeft == 0) { fclose(g_dump); g_dump = nullptr; Log("draw dump done"); }
}

// HUD and subtitles are flat 2D primitives: GS context 1, no perspective, and either pinned to an extreme depth value
// or (target boxes, which are depth tested) written with the alpha channel masked.
static bool IsHud(Vtx* v, int n, float sx, float ox) {
    uint64_t prim = *(uint64_t*)(g_gs + 0xc8);
    if (prim & 0x200) return false;
    bool masked = (prim & 7) == 6 && *(uint32_t*)(g_gs + 0x32c) == 0xff000000;
    float x0 = 1e9f, x1 = -1e9f;
    for (int i = 0; i < n; i++) {
        float z = v[i].a[2];
        if (v[i].a[3] != 1.f || !(masked || z == 0.f || z == 1.f || z >= 16777200.f)) return false;
        x0 = fminf(x0, v[i].a[0]); x1 = fmaxf(x1, v[i].a[0]);
    }
    // full screen effects and fades stay full screen
    return !(x0 - ox <= -sx * 0.5f + 1 && x1 - ox >= sx * 0.5f - 1);
}

static void Draw(void* tex, int smp, Vtx* v, int n, int prim, float a, uint8_t b, float sx, float sy, float ox, float oy,
    uint8_t b2, void* sh, uint16_t* idx, int nidx) {
    uint32_t ret = (uint32_t)((uint8_t*)_ReturnAddress() - g_base);
    int cls = 0; Vtx tmp[4];
    if (c_hud) {
        if (ret == 0x17520a || ret == 0x175399 || ret == 0x1755ab) { cls = 1; sx *= g_k; }        // info cards
        else if (ret == 0x216751 && n == 4) {                                                        // movies
            cls = 2; memcpy(tmp, v, sizeof tmp); for (auto& t : tmp) t.a[0] /= g_k; v = tmp;
        }
        else if (ret == 0x1961d0 && At<int>(RVA_curRT) == 1 && (sx == 512 || sx == 640 || sx == (float)g_W) && g_gs && IsHud(v, n, sx, ox)) { cls = 3; sx *= g_k; }
    }
    if (g_dump && g_dumpLeft > 0) DumpDraw(ret, tex, smp, v, n, prim, a, sx, sy, ox, oy, sh, nidx, cls);
    if (cls == 3) {
        int* sc = &At<int>(0x3210d0); int s0 = sc[0], s1 = sc[1], s2 = sc[2], s3 = sc[3];
        if (s0 > 0 || (s2 > 0 && s2 < g_W)) { // squeeze the scissor as well (right edge is stored as x + width by some callers)
            float c = g_W * 0.5f; auto f = [&](int x) { return (int)lroundf(c + (x - c) / g_k); };
            o_Scissor(f(s0), s1, f(s2), s3);
            o_Draw(tex, smp, v, n, prim, a, b, sx, sy, ox, oy, b2, sh, idx, nidx);
            o_Scissor(s0, s1, s2, s3);
            return;
        }
    }
    o_Draw(tex, smp, v, n, prim, a, b, sx, sy, ox, oy, b2, sh, idx, nidx);
}

// ---------------------------------------------------------------- early patches: resolution selection
static wchar_t g_label[40];
static bool Ultrawide(int w, int h) { return w * 9 > h * 16 + 9; }

static void EarlyPatches() {
    if (!Ultrawide(c_w, c_h)) { Log("%dx%d is not wider than 16:9 - nothing to do", c_w, c_h); return; }
    if (c_w > 4095) { Log("width %d is too large, the game packs it into 12 bits: set Width/Height in the ini (e.g. 3840x1080)", c_w); return; }
    // settings/setting.ini: accept the custom resolution
    uint8_t* s = Near(96); uint8_t* p = s;
    auto B = [&](std::initializer_list<int> l) { for (int x : l) *p++ = (uint8_t)x; };
    auto D = [&](int x) { memcpy(p, &x, 4); p += 4; };
    auto Q = [&](void* x) { memcpy(p, &x, 8); p += 8; };
    B({ 0x8B,0x44,0x24,0x24, 0x3D }); D(c_w); B({ 0x75, 23 });                 // mov eax,[rsp+24]; cmp eax,W; jne L
    B({ 0x8B,0x44,0x24,0x20, 0x3D }); D(c_h); B({ 0x75, 12 });                 // mov eax,[rsp+20]; cmp eax,H; jne L
    B({ 0x41,0xB8 }); D(c_w); B({ 0x41,0xB9 }); D(c_h);                        // mov r8d,W; mov r9d,H
    B({ 0x48,0xB8 }); Q(g_base + RVA_resW); B({ 0x44,0x89,0x00 });             // L: resW = r8d
    B({ 0x48,0xB8 }); Q(g_base + RVA_resH); B({ 0x44,0x89,0x08 });             // resH = r9d
    JmpAbs(p, g_base + 0x22417a);
    uint8_t j[14]; JmpAbs(j, s); Write(0x22416c, j, 14);
    // launcher list: 2048x1152 becomes the custom resolution
    swprintf_s(g_label, L"%d x %d (Ultrawide)", c_w, c_h);
    struct Entry { int w, h; const wchar_t* label; } e = { c_w, c_h, g_label };
    Write(0x2d3b30, &e, sizeof e);
    Log("resolution %dx%d added", c_w, c_h);
}

// ---------------------------------------------------------------- late patches: renderer is about to start, resolution is final
static void LatePatches() {
    static bool done; if (done) return; done = true;
    g_W = At<int>(RVA_resW); g_H = At<int>(RVA_resH);
    Log("starting renderer at %dx%d", g_W, g_H);
    if (!Ultrawide(g_W, g_H)) return;
    if (!Match(0x1fc540, "48 8b c4 48 89 58 08 48 89 70 10 48 89 78 18") || !Match(0x20ff60, "40 55 56 57 41 55 41 57 48 8d 6c 24 d0 48 81 ec 30 01 00 00")
        || !Match(0x20b78c, "c1 e1 04 f7 e9") || !Match(0x189c60, "48 83 ec 68 48 8b 05") || !Match(0x18de38, "b9 aa 02 00 00")
        || !Match(0xae550, "40 53 57 41 54 41 55 41 56 48 81 ec 80 00 00 00") || !Match(0x194ec0, "40 55 53 41 57 48 8d 6c 24 80 48 81 ec 80 01 00 00")
        || !Match(0xd9d90, "48 8b c4 41 57 48 81 ec c0 00 00 00 80 79 08 00") || !Match(0xba06b, "c5 78 2f 05") || !Match(0xba0f8, "c5 78 2f 05")
        || !Match(0xb68b5, "41 8d 81 00 90 ff ff") || !Match(0xb48c0, "41 8d 81 ff 8f ff ff") || !Match(0xb54d1, "41 8d 80 ff 8f ff ff") || !Match(0x194a90, "c5 f2 59 05") || !Match(0x194a98, "c5 f2 59 0d") || !Match(0x194b49, "c5 fa 59 0d") || At<int>(0x2d5fa0) != 566) {
        Log("unknown game build, not patching"); return;
    }
    g_active = true; g_k = (float)g_W / g_H / (16.f / 9.f);
    // internal render target width: mov edx, 2*W   (the code then halves it)
    uint8_t m[5] = { 0xBA }; int v = g_W * 2; memcpy(m + 1, &v, 4); Write(0x20b78c, m, 5);
    // virtual canvas used by menus: 682 -> 682*k
    int canvas = (int)lroundf(682.6667f * g_k) & ~1; Write(0x18de39, &canvas, 4);
    float* lo = NearF((canvas - 512) * 0.5f / canvas); float* hi = NearF((canvas + 512) * 0.5f / canvas);
    Repoint(0x194a90, 8, hi); Repoint(0x194a98, 8, lo); Repoint(0x194b49, 8, lo);
    uint8_t j[14]; JmpAbs(j, (void*)SetAspect); Write(0x189c60, j, 14);
    o_CamUpdate = (void(*)(uint8_t*))Hook(0x1fc540, 15, (void*)CamUpdate);
    o_Draw = (DrawFn)Hook(0x20ff60, 20, (void*)Draw);
    HookShadowR8(0xae550, 16);
    HookShadowR8(0xd9d90, 16);
    // markers are only drawn inside the 16:9 screen (x = 2048 +- 256, 12.4 fixed point): widen that range
    float* x0 = NearF(32768.f - 4096.f * g_k); float* x1 = NearF(32768.f + 4096.f * g_k);
    Repoint(0xba06b, 8, x0); Repoint(0xba0f8, 8, x1); Repoint(0xd9ed8, 8, x0); Repoint(0xd9f31, 8, x1);
    int half = (int)(4096 * g_k);
    auto Range = [&](uint32_t lea, int leaLen, uint32_t cmp, int e) { // lea eax,[r-lo]; cmp eax,size
        int lo = -(0x8000 - half + e), size = 2 * half - 2 * e;
        Write(lea + leaLen - 4, &lo, 4); Write(cmp + 1, &size, 4);
    };
    Range(0xb68b5, 7, 0xb68ca, 0);
    Range(0xb48c0, 7, 0xb48ca, 1); Range(0xb48d1, 6, 0xb48d7, 1);
    Range(0xb54d1, 7, 0xb54d8, 1); Range(0xb54e3, 7, 0xb54ea, 1);
    // the radar's scissor rectangles are stored for 3840x2160 and scaled by width/3840 in both directions
    float ky = 1.f / g_k; int tab[32]; memcpy(tab, g_base + 0x2d5fa0, sizeof tab);
    for (int i = 1; i < 32; i += 2) tab[i] = (int)lroundf(tab[i] * ky);
    Write(0x2d5fa0, tab, sizeof tab);
    o_Scissor = (void (*)(int, int, int, int))(g_base + 0x20f530);
    o_Flush = (void(*)(uint8_t*))Hook(0x194ec0, 17, (void*)Flush);
    Log("active, k=%.4f canvas=%d", g_k, canvas);
}

// ---------------------------------------------------------------- bootstrap through the game's import table
typedef void (WINAPI* GSIW)(LPSTARTUPINFOW); static GSIW o_startup;
typedef LPWSTR(WINAPI* GCLW)(); static GCLW o_cmdline;
typedef HRESULT(WINAPI* CDXGI)(UINT, REFIID, void**); static CDXGI o_dxgi;
// called by the CRT startup code, i.e. right after the code section was unpacked
static void TryEarly() {
    static LONG once;
    if (!once && Match(0x22416c, "44 89 05 ?? ?? ?? ?? 44 89 0d") && Match(0x22417a, "45 85 c0 79 04") && !InterlockedExchange(&once, 1)) EarlyPatches();
}
static void WINAPI h_startup(LPSTARTUPINFOW i) { TryEarly(); o_startup(i); }
static LPWSTR WINAPI h_cmdline() { TryEarly(); return o_cmdline(); }
static HRESULT WINAPI h_dxgi(UINT f, REFIID r, void** o) { LatePatches(); return o_dxgi(f, r, o); }

static bool HookImport(const char* dll, const char* fn, void* h, void** orig) {
    auto* nt = (IMAGE_NT_HEADERS*)(g_base + ((IMAGE_DOS_HEADER*)g_base)->e_lfanew);
    auto* d = (IMAGE_IMPORT_DESCRIPTOR*)(g_base + nt->OptionalHeader.DataDirectory[1].VirtualAddress);
    for (; d->Name; d++) {
        if (_stricmp((char*)g_base + d->Name, dll)) continue;
        auto* n = (IMAGE_THUNK_DATA*)(g_base + d->OriginalFirstThunk); auto* t = (IMAGE_THUNK_DATA*)(g_base + d->FirstThunk);
        for (; n->u1.AddressOfData; n++, t++) {
            if (IMAGE_SNAP_BY_ORDINAL(n->u1.Ordinal)) continue;
            if (strcmp(((IMAGE_IMPORT_BY_NAME*)(g_base + n->u1.AddressOfData))->Name, fn)) continue;
            *orig = (void*)t->u1.Function; DWORD o;
            VirtualProtect(t, 8, PAGE_READWRITE, &o); t->u1.Function = (ULONGLONG)h; VirtualProtect(t, 8, o, &o);
            return true;
        }
    }
    return false;
}

static DWORD WINAPI DebugThread(void*) {
    for (;;) {
        Sleep(100);
        if (GetAsyncKeyState(VK_F10) & 1) {
            char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\ACZUltrawide_draws.csv", g_dir);
            FILE* f = _fsopen(p, "w", 0x40); if (f) { g_dumpLeft = 900; g_dump = f; Log("dumping"); }
        }
        if (GetAsyncKeyState(VK_F9) & 1) { LoadConfig(); Log("config reloaded"); }
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD why, void*) {
    if (why != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    g_base = (uint8_t*)GetModuleHandleA(0);
    GetModuleFileNameA(h, g_dir, MAX_PATH); if (char* s = strrchr(g_dir, '\\')) *s = 0;
    char exe[MAX_PATH]; GetModuleFileNameA(0, exe, MAX_PATH);
    const char* e = strrchr(exe, '\\'); if (!e || _stricmp(e + 1, "acz.exe")) return TRUE;
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\ACZUltrawide.log", g_dir); g_log = _fsopen(p, "w", 0x40);
    LoadConfig();
    Log("ACZUltrawide, target %dx%d", c_w, c_h);
    if (!c_enabled) return TRUE;
    DWORD stamp = ((IMAGE_NT_HEADERS*)(g_base + ((IMAGE_DOS_HEADER*)g_base)->e_lfanew))->FileHeader.TimeDateStamp;
    if (stamp != BUILD_TIMESTAMP) { Log("unsupported game build (timestamp %08lx), doing nothing", stamp); return TRUE; }
    bool a = HookImport("KERNEL32.dll", "GetStartupInfoW", (void*)h_startup, (void**)&o_startup)
        && HookImport("KERNEL32.dll", "GetCommandLineW", (void*)h_cmdline, (void**)&o_cmdline);
    bool b = HookImport("dxgi.dll", "CreateDXGIFactory2", (void*)h_dxgi, (void**)&o_dxgi);
    Log("import hooks: %d %d", a, b);
    if (c_debug) CreateThread(0, 0, DebugThread, 0, 0, 0);
    return TRUE;
}
