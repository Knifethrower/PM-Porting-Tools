/* DirectDraw test for the Box64 + Wine runtime (32-bit), the classic late-90s 2D game paths:
 *  1. fullscreen exclusive 640x480x8, palette animation, flip chain
 *  2. fullscreen exclusive 640x480x16, Lock/Unlock pixel writes, flip chain
 *  3. windowed: offscreen 320x240 surface stretched to the primary with Blt + clipper
 * Text via the surface's GetDC. 6 s per phase, Esc/Enter skips a phase.
 * Every DirectDraw call's result goes to stdout + results.txt.
 * Build: i686-w64-mingw32-gcc -O2 -mwindows -o ddrawtest.exe ddrawtest.c -lddraw -lgdi32 */
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <stdarg.h>

#define SECONDS 6

static int skip, quit;
static HWND hwnd;
static LPDIRECTDRAW dd;

static void logf_(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("ddrawtest: %s\n", buf);
    fflush(stdout);
    FILE *f = fopen("results.txt", "a");
    if (f) { fprintf(f, "ddrawtest: %s\n", buf); fclose(f); }
}

/* log a call's HRESULT, return nonzero on failure */
static int check(const char *what, HRESULT hr)
{
    if (FAILED(hr)) logf_("  FAIL %s: 0x%08lx", what, (unsigned long)hr);
    else logf_("  ok   %s", what);
    return FAILED(hr);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN) skip = 1;
        return 0;
    case WM_DESTROY: quit = 1; return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static int pump(void)
{
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
    return !skip && !quit;
}

static void text(LPDIRECTDRAWSURFACE s, const char *str, COLORREF c)
{
    HDC dc;
    if (SUCCEEDED(IDirectDrawSurface_GetDC(s, &dc))) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, c);
        TextOutA(dc, 10, 10, str, lstrlenA(str));
        IDirectDrawSurface_ReleaseDC(s, dc);
    }
}

static void log_mode(void)
{
    DDSURFACEDESC sd = { sizeof sd };
    if (SUCCEEDED(IDirectDraw_GetDisplayMode(dd, &sd)))
        logf_("  display mode now %lux%lu %lu bpp", sd.dwWidth, sd.dwHeight,
              sd.ddpfPixelFormat.dwRGBBitCount);
}

static void fullscreen(int bpp)
{
    LPDIRECTDRAWSURFACE prim = NULL, back = NULL;
    LPDIRECTDRAWPALETTE pal = NULL;
    PALETTEENTRY pe[256];
    DDSURFACEDESC sd;
    char str[128];

    logf_("phase fullscreen 640x480x%d", bpp);
    SetWindowLongA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 640, 480, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    if (check("SetCooperativeLevel EXCLUSIVE|FULLSCREEN",
              IDirectDraw_SetCooperativeLevel(dd, hwnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN))) return;
    if (check("SetDisplayMode", IDirectDraw_SetDisplayMode(dd, 640, 480, bpp))) goto out;
    log_mode();

    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    sd.dwBackBufferCount = 1;
    if (check("CreateSurface primary+backbuffer", IDirectDraw_CreateSurface(dd, &sd, &prim, NULL))) goto out;
    DDSCAPS caps = { DDSCAPS_BACKBUFFER };
    if (check("GetAttachedSurface", IDirectDrawSurface_GetAttachedSurface(prim, &caps, &back))) goto out;

    if (bpp == 8) {
        for (int i = 0; i < 256; i++) {
            pe[i].peRed = i; pe[i].peGreen = 255 - i; pe[i].peBlue = (i * 4) & 255; pe[i].peFlags = 0;
        }
        pe[255].peRed = pe[255].peGreen = pe[255].peBlue = 255;
        if (check("CreatePalette", IDirectDraw_CreatePalette(dd, DDPCAPS_8BIT | DDPCAPS_ALLOW256, pe, &pal, NULL))) goto out;
        if (check("SetPalette", IDirectDrawSurface_SetPalette(prim, pal))) goto out;
    }

    DWORD t0 = GetTickCount(), frames = 0, lockfail = 0, flipfail = 0, lost = 0;
    int is565 = 0;
    double fps = 0;
    while (pump() && GetTickCount() - t0 < SECONDS * 1000) {
        DWORD t = GetTickCount() - t0;
        memset(&sd, 0, sizeof sd);
        sd.dwSize = sizeof sd;
        HRESULT hr = IDirectDrawSurface_Lock(back, NULL, &sd, DDLOCK_WAIT, NULL);
        if (hr == DDERR_SURFACELOST) { lost++; IDirectDrawSurface_Restore(prim); continue; }
        if (FAILED(hr)) { if (!lockfail++) check("Lock backbuffer", hr); continue; }
        if (frames == 0) {
            is565 = sd.ddpfPixelFormat.dwGBitMask == 0x07e0;
            logf_("  backbuffer %lux%lu pitch %ld, %lu bpp, masks R %lx G %lx B %lx", sd.dwWidth, sd.dwHeight,
                  sd.lPitch, sd.ddpfPixelFormat.dwRGBBitCount, sd.ddpfPixelFormat.dwRBitMask,
                  sd.ddpfPixelFormat.dwGBitMask, sd.ddpfPixelFormat.dwBBitMask);
        }
        int bx = (int)(t / 3) % 560;
        for (int y = 0; y < 480; y++) {
            BYTE *row = (BYTE *)sd.lpSurface + y * sd.lPitch;
            for (int x = 0; x < 640; x++) {
                int box = x >= bx && x < bx + 80 && y >= 200 && y < 280;
                if (bpp == 8) {
                    row[x] = box ? 255 : (BYTE)(((x * x + y * y) >> 8) % 255);
                } else {
                    int r = box ? 31 : (x + t / 10) & 31, g = box ? 63 : (y >> 3) & 63, b = box ? 31 : (x ^ y) & 31;
                    ((WORD *)row)[x] = is565 ? (WORD)(r << 11 | g << 5 | b) : (WORD)(r << 10 | (g >> 1) << 5 | b);
                }
            }
        }
        IDirectDrawSurface_Unlock(back, NULL);
        snprintf(str, sizeof str, "DirectDraw 640x480x%d  %.1f fps  %lus", bpp, fps,
                 (unsigned long)(SECONDS - t / 1000));
        text(back, str, bpp == 8 ? PALETTEINDEX(255) : RGB(255, 255, 255));
        if (bpp == 8) {  /* palette animation: rotate entries 0..254 */
            PALETTEENTRY first = pe[0];
            memmove(pe, pe + 1, 254 * sizeof *pe);
            pe[254] = first;
            IDirectDrawPalette_SetEntries(pal, 0, 0, 256, pe);
        }
        hr = IDirectDrawSurface_Flip(prim, NULL, DDFLIP_WAIT);
        if (hr == DDERR_SURFACELOST) { lost++; IDirectDrawSurface_Restore(prim); }
        else if (FAILED(hr) && !flipfail++) check("Flip", hr);
        frames++;
        if (t > 1000) fps = frames * 1000.0 / t;
    }
    DWORD ms = GetTickCount() - t0;
    logf_("  result: %lu frames in %lu ms = %.1f fps (lock fails %lu, flip fails %lu, lost %lu)%s",
          frames, ms, frames * 1000.0 / (ms ? ms : 1), lockfail, flipfail, lost, skip ? ", skipped" : "");
out:
    if (pal) IDirectDrawPalette_Release(pal);
    if (prim) IDirectDrawSurface_Release(prim);  /* releases the attached back buffer too */
    check("RestoreDisplayMode", IDirectDraw_RestoreDisplayMode(dd));
    IDirectDraw_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    skip = 0;
}

static void windowed(void)
{
    LPDIRECTDRAWSURFACE prim = NULL, off = NULL;
    LPDIRECTDRAWCLIPPER clip = NULL;
    DDSURFACEDESC sd;
    char str[128];

    logf_("phase windowed, 320x240 offscreen stretched with Blt");
    /* 3/4 of the screen, centred: a 640x480 window plus frame would not fit a 640x480 screen */
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    RECT wr = { 0, 0, sw * 3 / 4, sh * 3 / 4 };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    SetWindowLongA(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    SetWindowPos(hwnd, HWND_TOP, (sw - ww) / 2, (sh - wh) / 2, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    if (check("SetCooperativeLevel NORMAL", IDirectDraw_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL))) return;
    log_mode();

    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS;
    sd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if (check("CreateSurface primary", IDirectDraw_CreateSurface(dd, &sd, &prim, NULL))) goto out;
    if (check("CreateClipper", IDirectDraw_CreateClipper(dd, 0, &clip, NULL))) goto out;
    if (check("Clipper SetHWnd", IDirectDrawClipper_SetHWnd(clip, 0, hwnd))) goto out;
    if (check("SetClipper", IDirectDrawSurface_SetClipper(prim, clip))) goto out;
    memset(&sd, 0, sizeof sd);
    sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    sd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    sd.dwWidth = 320;
    sd.dwHeight = 240;
    if (check("CreateSurface offscreen 320x240", IDirectDraw_CreateSurface(dd, &sd, &off, NULL))) goto out;

    DWORD t0 = GetTickCount(), frames = 0, bltfail = 0;
    double fps = 0;
    while (pump() && GetTickCount() - t0 < SECONDS * 1000) {
        DWORD t = GetTickCount() - t0;
        memset(&sd, 0, sizeof sd);
        sd.dwSize = sizeof sd;
        if (FAILED(IDirectDrawSurface_Lock(off, NULL, &sd, DDLOCK_WAIT, NULL))) { IDirectDrawSurface_Restore(off); continue; }
        DWORD bits = sd.ddpfPixelFormat.dwRGBBitCount;
        if (frames == 0) logf_("  offscreen format %lu bpp", bits);
        for (int y = 0; y < 240; y++) {
            BYTE *row = (BYTE *)sd.lpSurface + y * sd.lPitch;
            for (int x = 0; x < 320; x++) {
                int r = (x + t / 8) & 255, g = (y + t / 16) & 255, b = (x ^ y) & 255;
                if (bits == 32) ((DWORD *)row)[x] = r << 16 | g << 8 | b;
                else if (bits == 16) ((WORD *)row)[x] = (WORD)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
            }
        }
        IDirectDrawSurface_Unlock(off, NULL);
        snprintf(str, sizeof str, "windowed Blt %.1f fps %lus", fps, (unsigned long)(SECONDS - t / 1000));
        text(off, str, RGB(255, 255, 255));
        RECT dst;  /* client area in screen coordinates */
        GetClientRect(hwnd, &dst);
        ClientToScreen(hwnd, (POINT *)&dst.left);
        ClientToScreen(hwnd, (POINT *)&dst.right);
        if (frames == 0) logf_("  Blt target %ld,%ld-%ld,%ld", dst.left, dst.top, dst.right, dst.bottom);
        HRESULT hr = IDirectDrawSurface_Blt(prim, &dst, off, NULL, DDBLT_WAIT, NULL);
        if (hr == DDERR_SURFACELOST) IDirectDrawSurface_Restore(prim);
        else if (FAILED(hr) && !bltfail++) check("Blt", hr);
        frames++;
        if (t > 1000) fps = frames * 1000.0 / t;
    }
    DWORD ms = GetTickCount() - t0;
    logf_("  result: %lu frames in %lu ms = %.1f fps (blt fails %lu)%s",
          frames, ms, frames * 1000.0 / (ms ? ms : 1), bltfail, skip ? ", skipped" : "");
out:
    if (off) IDirectDrawSurface_Release(off);
    if (clip) IDirectDrawClipper_Release(clip);
    if (prim) IDirectDrawSurface_Release(prim);
    skip = 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    logf_("start, screen %dx%d", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "ddrawtest";
    RegisterClassA(&wc);
    hwnd = CreateWindowExA(0, "ddrawtest", "DirectDraw test", WS_POPUP | WS_VISIBLE,
                           0, 0, 640, 480, NULL, NULL, inst, NULL);
    if (!hwnd) { logf_("FAIL CreateWindow %lu", GetLastError()); return 1; }
    if (check("DirectDrawCreate", DirectDrawCreate(NULL, &dd, NULL))) return 1;

    fullscreen(8);
    if (!quit) fullscreen(16);
    if (!quit) windowed();

    IDirectDraw_Release(dd);
    DestroyWindow(hwnd);
    logf_("done");
    return 0;
}
