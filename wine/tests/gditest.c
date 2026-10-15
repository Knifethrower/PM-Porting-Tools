/* GDI test for the Box64 + Wine runtime (32-bit).
 * Fullscreen popup window; each frame: software-rendered 320x240 DIB stretched with StretchDIBits
 * (the usual path of 2D software-rendered games), GDI shapes, TrueType text, BitBlt to the window.
 * Shows mouse/key input. Runs 20 s, Esc/Enter ends it. Results: stdout + results.txt.
 * Build: i686-w64-mingw32-gcc -O2 -mwindows -o gditest.exe gditest.c -lgdi32 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#define W 320
#define H 240
#define SECONDS 20

static int running = 1, lclicks, rclicks, keys, lastkey;
static POINT mouse;

static void logf_(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("gditest: %s\n", buf);
    fflush(stdout);
    FILE *f = fopen("results.txt", "a");
    if (f) { fprintf(f, "gditest: %s\n", buf); fclose(f); }
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
        keys++; lastkey = (int)wp;
        if (wp == VK_ESCAPE || wp == VK_RETURN) running = 0;
        return 0;
    case WM_LBUTTONDOWN: lclicks++; return 0;
    case WM_RBUTTONDOWN: rclicks++; return 0;
    case WM_MOUSEMOVE: mouse.x = (short)LOWORD(lp); mouse.y = (short)HIWORD(lp); return 0;
    case WM_DESTROY: running = 0; return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    logf_("start, screen %dx%d", sw, sh);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "gditest";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "gditest", "GDI test", WS_POPUP | WS_VISIBLE,
                                0, 0, sw, sh, NULL, NULL, inst, NULL);
    if (!hwnd) { logf_("FAIL CreateWindow %lu", GetLastError()); return 1; }

    /* 32-bit top-down DIB, the game's "framebuffer" */
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    static DWORD fb[W * H];

    HDC wdc = GetDC(hwnd);
    HDC mdc = CreateCompatibleDC(wdc);
    HBITMAP back = CreateCompatibleBitmap(wdc, sw, sh);
    SelectObject(mdc, back);
    HFONT font = CreateFontA(sh / 16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             ANTIALIASED_QUALITY, 0, "Tahoma");
    SelectObject(mdc, font);
    SetBkMode(mdc, TRANSPARENT);
    HBRUSH red = CreateSolidBrush(RGB(230, 40, 40)), blue = CreateSolidBrush(RGB(40, 90, 230));

    DWORD t0 = GetTickCount(), tl = t0, frames = 0, sec_frames = 0;
    double fps = 0;
    char text[160];
    while (running && GetTickCount() - t0 < SECONDS * 1000) {
        MSG m;
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
        DWORD t = GetTickCount() - t0;

        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                fb[y * W + x] = ((x + t / 8) & 255) << 16 | ((y + t / 16) & 255) << 8 | ((x ^ y) & 255);
        StretchDIBits(mdc, 0, 0, sw, sh, 0, 0, W, H, fb, &bi, DIB_RGB_COLORS, SRCCOPY);

        int bx = (int)((t / 4) % (sw - sh / 5)), by = sh / 2;
        RECT r = { bx, by, bx + sh / 5, by + sh / 5 };
        FillRect(mdc, &r, red);
        SelectObject(mdc, blue);
        Ellipse(mdc, mouse.x - 12, mouse.y - 12, mouse.x + 12, mouse.y + 12);

        snprintf(text, sizeof text, "%.0f fps  %ld,%ld  L%d R%d  key %d  %lus",
                 fps, mouse.x, mouse.y, lclicks, rclicks, lastkey, (unsigned long)(SECONDS - t / 1000));
        SetTextColor(mdc, RGB(0, 0, 0));
        TextOutA(mdc, 12, 12, text, lstrlenA(text));
        SetTextColor(mdc, RGB(255, 255, 255));
        TextOutA(mdc, 10, 10, text, lstrlenA(text));

        BitBlt(wdc, 0, 0, sw, sh, mdc, 0, 0, SRCCOPY);
        frames++; sec_frames++;
        if (GetTickCount() - tl >= 1000) {
            fps = sec_frames * 1000.0 / (GetTickCount() - tl);
            tl = GetTickCount(); sec_frames = 0;
        }
    }
    DWORD ms = GetTickCount() - t0;
    logf_("done: %lu frames in %lu ms = %.1f fps, left clicks %d, right clicks %d, keys %d",
          frames, ms, frames * 1000.0 / (ms ? ms : 1), lclicks, rclicks, keys);
    DestroyWindow(hwnd);
    return 0;
}
