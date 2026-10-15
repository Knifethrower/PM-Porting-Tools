/* Direct3D 9 test for the Box64 + Wine runtime (32-bit): fixed-function spinning, lit, textured cube.
 * Logs the adapter (Wine reports the OpenGL renderer it found there), caps and fps per phase:
 *  1. fullscreen 640x480 X8R8G8B8, vsync     2. windowed, 3/4 of the screen
 * 8 s per phase, Esc/Enter skips. Every call's result goes to stdout + results.txt.
 * Build: i686-w64-mingw32-gcc -O2 -s -mwindows -o 3-d3d9test.exe d3d9test.c -ld3d9 */
#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>

#define SECONDS 8

static int skip, quit;
static HWND hwnd;
static IDirect3D9 *d3d;

static void logf_(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("d3d9test: %s\n", buf);
    fflush(stdout);
    FILE *f = fopen("results.txt", "a");
    if (f) { fprintf(f, "d3d9test: %s\n", buf); fclose(f); }
}

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

/* cube: 6 faces x 2 triangles, position + normal + texcoord */
typedef struct { float x, y, z, nx, ny, nz, u, v; } vertex;
#define FVF (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)
static vertex cube[36];

static void make_cube(void)
{
    static const float n[6][3] = { {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}, {0,1,0}, {0,-1,0} };
    static const float uax[6][3] = { {1,0,0}, {-1,0,0}, {0,0,-1}, {0,0,1}, {1,0,0}, {1,0,0} };
    static const float vax[6][3] = { {0,1,0}, {0,1,0}, {0,1,0}, {0,1,0}, {0,0,1}, {0,0,-1} };
    static const float c[6][2] = { {-1,-1}, {-1,1}, {1,1}, {-1,-1}, {1,1}, {1,-1} };
    int k = 0;
    for (int f = 0; f < 6; f++)
        for (int i = 0; i < 6; i++, k++) {
            float a = c[i][0], b = c[i][1];
            cube[k].x = n[f][0] + a * uax[f][0] + b * vax[f][0];
            cube[k].y = n[f][1] + a * uax[f][1] + b * vax[f][1];
            cube[k].z = n[f][2] + a * uax[f][2] + b * vax[f][2];
            cube[k].nx = n[f][0]; cube[k].ny = n[f][1]; cube[k].nz = n[f][2];
            cube[k].u = (a + 1) / 2; cube[k].v = (1 - b) / 2;
        }
}

static void matmul(D3DMATRIX *r, const D3DMATRIX *a, const D3DMATRIX *b)
{
    D3DMATRIX t;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            t.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + a->m[i][3] * b->m[3][j];
    *r = t;
}

static void set_matrices(IDirect3DDevice9 *dev, float t, int w, int h)
{
    float ay = t, ax = t * 0.7f;
    D3DMATRIX ry = {{{ cosf(ay), 0, -sinf(ay), 0,  0, 1, 0, 0,  sinf(ay), 0, cosf(ay), 0,  0, 0, 0, 1 }}};
    D3DMATRIX rx = {{{ 1, 0, 0, 0,  0, cosf(ax), sinf(ax), 0,  0, -sinf(ax), cosf(ax), 0,  0, 0, 0, 1 }}};
    D3DMATRIX tr = {{{ 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 4.5f, 1 }}};
    D3DMATRIX id = {{{ 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 }}};
    D3DMATRIX world;
    matmul(&world, &ry, &rx);
    matmul(&world, &world, &tr);
    float zn = 0.5f, zf = 20.0f, yscale = 1.0f / tanf(0.5f), xscale = yscale * h / w;
    D3DMATRIX proj = {{{ xscale, 0, 0, 0,  0, yscale, 0, 0,  0, 0, zf / (zf - zn), 1,  0, 0, -zn * zf / (zf - zn), 0 }}};
    IDirect3DDevice9_SetTransform(dev, D3DTS_WORLD, &world);
    IDirect3DDevice9_SetTransform(dev, D3DTS_VIEW, &id);
    IDirect3DDevice9_SetTransform(dev, D3DTS_PROJECTION, &proj);
}

static void phase(int fullscreen)
{
    IDirect3DDevice9 *dev = NULL;
    IDirect3DTexture9 *tex = NULL;
    int w, h;

    if (fullscreen) {
        w = 640; h = 480;
        logf_("phase fullscreen %dx%d", w, h);
        SetWindowLongA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        RECT wr = { 0, 0, sw * 3 / 4, sh * 3 / 4 };
        AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
        w = sw * 3 / 4; h = sh * 3 / 4;
        logf_("phase windowed %dx%d", w, h);
        SetWindowLongA(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, (sw - ww) / 2, (sh - wh) / 2, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }

    D3DPRESENT_PARAMETERS pp = {0};
    pp.Windowed = !fullscreen;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = fullscreen ? D3DFMT_X8R8G8B8 : D3DFMT_UNKNOWN;
    pp.BackBufferWidth = fullscreen ? w : 0;
    pp.BackBufferHeight = fullscreen ? h : 0;
    pp.BackBufferCount = 1;
    pp.hDeviceWindow = hwnd;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    HRESULT hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                         D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) {
        check("CreateDevice D24S8", hr);
        pp.AutoDepthStencilFormat = D3DFMT_D16;
        hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
        if (check("CreateDevice D16", hr)) goto out;
    } else {
        logf_("  ok   CreateDevice D24S8");
    }

    /* 64x64 checkerboard texture */
    if (check("CreateTexture", IDirect3DDevice9_CreateTexture(dev, 64, 64, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &tex, NULL)))
        goto out;
    D3DLOCKED_RECT lr;
    if (!check("LockRect", IDirect3DTexture9_LockRect(tex, 0, &lr, NULL, 0))) {
        for (int y = 0; y < 64; y++) {
            DWORD *row = (DWORD *)((BYTE *)lr.pBits + y * lr.Pitch);
            for (int x = 0; x < 64; x++)
                row[x] = ((x / 8 + y / 8) & 1) ? 0xffffc040 : 0xff3070e0;
        }
        IDirect3DTexture9_UnlockRect(tex, 0);
    }

    D3DLIGHT9 light = {0};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = light.Diffuse.a = 1;
    light.Direction.x = 0.4f; light.Direction.y = -0.6f; light.Direction.z = 0.7f;
    D3DMATERIAL9 mat = {0};
    mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 1;
    mat.Ambient = mat.Diffuse;
    IDirect3DDevice9_SetLight(dev, 0, &light);
    IDirect3DDevice9_LightEnable(dev, 0, TRUE);
    IDirect3DDevice9_SetMaterial(dev, &mat);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_AMBIENT, 0x00404040);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, D3DZB_TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);  /* the z-buffer sorts the faces */
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

    DWORD t0 = GetTickCount(), frames = 0, presentfail = 0, lost = 0, drawfail = 0;
    while (pump() && GetTickCount() - t0 < SECONDS * 1000) {
        float t = (GetTickCount() - t0) / 1000.0f;
        hr = IDirect3DDevice9_TestCooperativeLevel(dev);
        if (hr == D3DERR_DEVICELOST) { Sleep(20); continue; }
        if (hr == D3DERR_DEVICENOTRESET) { lost++; IDirect3DDevice9_Reset(dev, &pp); continue; }
        DWORD bg = D3DCOLOR_XRGB(20, 20, (int)(40 + 30 * sinf(t)));
        IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, bg, 1.0f, 0);
        IDirect3DDevice9_BeginScene(dev);
        set_matrices(dev, t, w, h);
        IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)tex);
        IDirect3DDevice9_SetFVF(dev, FVF);
        if (FAILED(IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, 12, cube, sizeof(vertex))) && !drawfail++)
            logf_("  FAIL DrawPrimitiveUP");
        IDirect3DDevice9_EndScene(dev);
        hr = IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
        if (hr == D3DERR_DEVICELOST) lost++;
        else if (FAILED(hr) && !presentfail++) check("Present", hr);
        frames++;
    }
    DWORD ms = GetTickCount() - t0;
    logf_("  result: %lu frames in %lu ms = %.1f fps (present fails %lu, draw fails %lu, lost %lu)%s",
          frames, ms, frames * 1000.0 / (ms ? ms : 1), presentfail, drawfail, lost, skip ? ", skipped" : "");
out:
    if (tex) IDirect3DTexture9_Release(tex);
    if (dev) IDirect3DDevice9_Release(dev);
    skip = 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    logf_("start, screen %dx%d", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    make_cube();
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "d3d9test";
    RegisterClassA(&wc);
    hwnd = CreateWindowExA(0, "d3d9test", "Direct3D 9 test", WS_POPUP | WS_VISIBLE,
                           0, 0, 640, 480, NULL, NULL, inst, NULL);
    if (!hwnd) { logf_("FAIL CreateWindow %lu", GetLastError()); return 1; }

    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { logf_("FAIL Direct3DCreate9 (no D3D9: Wine found no usable OpenGL?)"); return 1; }
    D3DADAPTER_IDENTIFIER9 id;
    if (!check("GetAdapterIdentifier", IDirect3D9_GetAdapterIdentifier(d3d, D3DADAPTER_DEFAULT, 0, &id)))
        logf_("  adapter \"%s\", driver \"%s\", vendor 0x%04lx device 0x%04lx", id.Description, id.Driver,
              (unsigned long)id.VendorId, (unsigned long)id.DeviceId);
    D3DCAPS9 caps;
    if (!check("GetDeviceCaps", IDirect3D9_GetDeviceCaps(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps)))
        logf_("  vs %lu.%lu, ps %lu.%lu, max texture %lux%lu",
              (unsigned long)D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion), (unsigned long)D3DSHADER_VERSION_MINOR(caps.VertexShaderVersion),
              (unsigned long)D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion), (unsigned long)D3DSHADER_VERSION_MINOR(caps.PixelShaderVersion),
              (unsigned long)caps.MaxTextureWidth, (unsigned long)caps.MaxTextureHeight);

    phase(1);
    if (!quit) phase(0);

    IDirect3D9_Release(d3d);
    DestroyWindow(hwnd);
    logf_("done");
    return 0;
}
