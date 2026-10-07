/*
 * SDL2 OpenGL ES context for vo=gpu (PortMaster handhelds).
 *
 * The window, the GLES context and all input go through the firmware's SDL2, which picks the platform each
 * firmware needs (Mali fbdev on Knulli, Wayland/Sway on ROCKNIX, KMSDRM elsewhere): the same route as the
 * Unity ports' glxsdl. One SDL event loop on the VO thread delivers the keyboard (gptokeyb) and the game
 * controller; controller buttons become mpv's GAMEPAD_* keys, as input/sdl_gamepad.c would send them, so
 * build with sdl2-gamepad disabled (two SDL event loops would steal each other's events).
 *
 * This file is part of mpv, LGPL 2.1 or later (see the mpv sources).
 */

#include <dlfcn.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "common/common.h"
#include "common/msg.h"
#include "misc/bstr.h"
#include "input/input.h"
#include "input/keycodes.h"
#include "osdep/timer.h"
#include "video/out/vo.h"
#include "context.h"
#include "utils.h"

struct priv {
    GL gl;
    SDL_Window *window;
    SDL_GLContext context;
    SDL_GameController *pad;
    Uint32 wakeup_event;
    void *gles_lib, *egl_lib;
    int axis_state[SDL_CONTROLLER_AXIS_MAX];
};

static const int keys[][2] = {
    {SDLK_RETURN, MP_KEY_ENTER}, {SDLK_KP_ENTER, MP_KEY_ENTER}, {SDLK_ESCAPE, MP_KEY_ESC},
    {SDLK_BACKSPACE, MP_KEY_BACKSPACE}, {SDLK_TAB, MP_KEY_TAB}, {SDLK_DELETE, MP_KEY_DELETE},
    {SDLK_HOME, MP_KEY_HOME}, {SDLK_END, MP_KEY_END}, {SDLK_PAGEUP, MP_KEY_PAGE_UP},
    {SDLK_PAGEDOWN, MP_KEY_PAGE_DOWN}, {SDLK_RIGHT, MP_KEY_RIGHT}, {SDLK_LEFT, MP_KEY_LEFT},
    {SDLK_DOWN, MP_KEY_DOWN}, {SDLK_UP, MP_KEY_UP}, {SDLK_MENU, MP_KEY_MENU},
    {SDLK_VOLUMEUP, MP_KEY_VOLUME_UP}, {SDLK_VOLUMEDOWN, MP_KEY_VOLUME_DOWN}, {SDLK_MUTE, MP_KEY_MUTE},
    {SDLK_AUDIOPLAY, MP_KEY_PLAY}, {SDLK_AUDIONEXT, MP_KEY_NEXT}, {SDLK_AUDIOPREV, MP_KEY_PREV},
    {SDLK_F1, MP_KEY_F + 1}, {SDLK_F2, MP_KEY_F + 2}, {SDLK_F3, MP_KEY_F + 3}, {SDLK_F4, MP_KEY_F + 4},
    {SDLK_F5, MP_KEY_F + 5}, {SDLK_F6, MP_KEY_F + 6}, {SDLK_F7, MP_KEY_F + 7}, {SDLK_F8, MP_KEY_F + 8},
    {SDLK_F9, MP_KEY_F + 9}, {SDLK_F10, MP_KEY_F + 10}, {SDLK_F11, MP_KEY_F + 11}, {SDLK_F12, MP_KEY_F + 12},
};

/* input/sdl_gamepad.c's table: SDL names the face buttons by position, and so does mpv */
static const int buttons[][2] = {
    {SDL_CONTROLLER_BUTTON_A, MP_KEY_GAMEPAD_ACTION_DOWN},
    {SDL_CONTROLLER_BUTTON_B, MP_KEY_GAMEPAD_ACTION_RIGHT},
    {SDL_CONTROLLER_BUTTON_X, MP_KEY_GAMEPAD_ACTION_LEFT},
    {SDL_CONTROLLER_BUTTON_Y, MP_KEY_GAMEPAD_ACTION_UP},
    {SDL_CONTROLLER_BUTTON_BACK, MP_KEY_GAMEPAD_BACK},
    {SDL_CONTROLLER_BUTTON_GUIDE, MP_KEY_GAMEPAD_MENU},
    {SDL_CONTROLLER_BUTTON_START, MP_KEY_GAMEPAD_START},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, MP_KEY_GAMEPAD_LEFT_STICK},
    {SDL_CONTROLLER_BUTTON_RIGHTSTICK, MP_KEY_GAMEPAD_RIGHT_STICK},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, MP_KEY_GAMEPAD_LEFT_SHOULDER},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, MP_KEY_GAMEPAD_RIGHT_SHOULDER},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, MP_KEY_GAMEPAD_DPAD_UP},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, MP_KEY_GAMEPAD_DPAD_DOWN},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, MP_KEY_GAMEPAD_DPAD_LEFT},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, MP_KEY_GAMEPAD_DPAD_RIGHT},
};

/* per axis: the key for the negative and the positive direction (0: none) */
static const int axes[SDL_CONTROLLER_AXIS_MAX][2] = {
    [SDL_CONTROLLER_AXIS_LEFTX] = {MP_KEY_GAMEPAD_LEFT_STICK_LEFT, MP_KEY_GAMEPAD_LEFT_STICK_RIGHT},
    [SDL_CONTROLLER_AXIS_LEFTY] = {MP_KEY_GAMEPAD_LEFT_STICK_UP, MP_KEY_GAMEPAD_LEFT_STICK_DOWN},
    [SDL_CONTROLLER_AXIS_RIGHTX] = {MP_KEY_GAMEPAD_RIGHT_STICK_LEFT, MP_KEY_GAMEPAD_RIGHT_STICK_RIGHT},
    [SDL_CONTROLLER_AXIS_RIGHTY] = {MP_KEY_GAMEPAD_RIGHT_STICK_UP, MP_KEY_GAMEPAD_RIGHT_STICK_DOWN},
    [SDL_CONTROLLER_AXIS_TRIGGERLEFT] = {0, MP_KEY_GAMEPAD_LEFT_TRIGGER},
    [SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = {0, MP_KEY_GAMEPAD_RIGHT_TRIGGER},
};

static void *get_proc(void *ctx, const char *name)
{
    struct priv *p = ctx;
    /* some Mali drivers answer NULL for core GLES functions here */
    void *f = SDL_GL_GetProcAddress(name);
    if (!f && p->gles_lib)
        f = dlsym(p->gles_lib, name);
    if (!f && p->egl_lib)
        f = dlsym(p->egl_lib, name);
    return f;
}

static void sdl_swap_buffers(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    SDL_GL_SwapWindow(p->window);
}

static void sdl_uninit(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    ra_gl_ctx_uninit(ctx);
    if (p->pad)
        SDL_GameControllerClose(p->pad);
    if (p->context)
        SDL_GL_DeleteContext(p->context);
    if (p->window)
        SDL_DestroyWindow(p->window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
}

static bool create_context(struct ra_ctx *ctx, int major)
{
    struct priv *p = ctx->priv;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    p->context = SDL_GL_CreateContext(p->window);
    return p->context != NULL;
}

static bool sdl_init(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv = talloc_zero(ctx, struct priv);
    int msgl = ctx->opts.probing ? MSGL_V : MSGL_FATAL;

    /* firmware SDL2 builds may install crash handlers over the process's own */
    static const int fatal[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    struct sigaction saved[MP_ARRAY_SIZE(fatal)];
    for (int i = 0; i < MP_ARRAY_SIZE(fatal); i++)
        sigaction(fatal[i], NULL, &saved[i]);

    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER)) {
        MP_MSG(ctx, msgl, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    p->wakeup_event = SDL_RegisterEvents(1);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    /* PortMaster's control.txt names the panel size; a mode change only when it differs */
    SDL_DisplayMode mode = {0};
    SDL_GetCurrentDisplayMode(0, &mode);
    const char *ew = getenv("DISPLAY_WIDTH"), *eh = getenv("DISPLAY_HEIGHT");
    int w = ew ? atoi(ew) : 0, h = eh ? atoi(eh) : 0;
    if (w <= 0 || h <= 0) {
        w = mode.w;
        h = mode.h;
    }
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN |
        (w == mode.w && h == mode.h ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN);
    p->window = SDL_CreateWindow("mpv", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, flags);
    if (!p->window || !(create_context(ctx, 3) || create_context(ctx, 2))) {
        MP_MSG(ctx, msgl, "SDL window/GLES context failed: %s\n", SDL_GetError());
        goto fail;
    }
    for (int i = 0; i < MP_ARRAY_SIZE(fatal); i++)
        sigaction(fatal[i], &saved[i], NULL);
    SDL_GL_MakeCurrent(p->window, p->context);
    SDL_ShowCursor(SDL_DISABLE);

    p->gles_lib = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
    p->egl_lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    mpgl_load_functions2(&p->gl, get_proc, p, NULL, ctx->log);
    if (!p->gl.version && !p->gl.es) {
        MP_MSG(ctx, msgl, "No usable GLES functions.\n");
        goto fail;
    }

    p->gl.SwapInterval = SDL_GL_SetSwapInterval;
    struct ra_ctx_params params = {
        .swap_buffers = sdl_swap_buffers,
    };
    if (!ra_gl_ctx_init(ctx, &p->gl, params))
        goto fail;

    int dw, dh;
    SDL_GL_GetDrawableSize(p->window, &dw, &dh);
    MP_INFO(ctx, "SDL %s driver, %dx%d (display %dx%d @ %d Hz)\n",
               SDL_GetCurrentVideoDriver(), dw, dh, mode.w, mode.h, mode.refresh_rate);
    return true;

fail:
    sdl_uninit(ctx);
    return false;
}

static void resize(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    int w, h;
    SDL_GL_GetDrawableSize(p->window, &w, &h);
    ctx->vo->dwidth = w;
    ctx->vo->dheight = h;
    ra_gl_ctx_resize(ctx->swapchain, w, h, 0);
}

static bool sdl_reconfig(struct ra_ctx *ctx)
{
    resize(ctx);
    return true;
}

static int sdl_control(struct ra_ctx *ctx, int *events, int request, void *arg)
{
    struct priv *p = ctx->priv;
    switch (request) {
    case VOCTRL_GET_DISPLAY_FPS: {
        SDL_DisplayMode mode;
        if (SDL_GetCurrentDisplayMode(0, &mode) || mode.refresh_rate <= 0)
            break;
        *(double *)arg = mode.refresh_rate;
        return VO_TRUE;
    }
    case VOCTRL_GET_DISPLAY_RES: {
        int *res = arg;
        SDL_GL_GetDrawableSize(p->window, &res[0], &res[1]);
        return VO_TRUE;
    }
    case VOCTRL_GET_FOCUSED:
        *(bool *)arg = true;
        return VO_TRUE;
    }
    return VO_NOTIMPL;
}

static void sdl_wakeup(struct ra_ctx *ctx)
{
    struct priv *p = ctx->priv;
    SDL_Event ev = {.type = p->wakeup_event};
    SDL_PushEvent(&ev);
}

static int lookup(const int (*table)[2], int n, int key)
{
    for (int i = 0; i < n; i++) {
        if (table[i][0] == key)
            return table[i][1];
    }
    return 0;
}

static void handle_axis(struct ra_ctx *ctx, int axis, int value)
{
    struct priv *p = ctx->priv;
    if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX)
        return;
    /* same thresholds as sdl_gamepad.c: on past half way, off again below a quarter */
    int state = p->axis_state[axis];
    int next = value > 16383 ? 1 : value < -16383 ? -1 : abs(value) < 8192 ? 0 : state;
    if (next < 0 && !axes[axis][0])
        next = 0;
    if (next == state)
        return;
    struct input_ctx *ictx = ctx->vo->input_ctx;
    if (state)
        mp_input_put_key(ictx, axes[axis][state > 0] | MP_KEY_STATE_UP);
    if (next)
        mp_input_put_key(ictx, axes[axis][next > 0] | MP_KEY_STATE_DOWN);
    p->axis_state[axis] = next;
}

static void handle_event(struct ra_ctx *ctx, SDL_Event *ev)
{
    struct priv *p = ctx->priv;
    struct vo *vo = ctx->vo;
    switch (ev->type) {
    case SDL_WINDOWEVENT:
        if (ev->window.event == SDL_WINDOWEVENT_EXPOSED)
            vo->want_redraw = true;
        if (ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            resize(ctx);
            vo_event(vo, VO_EVENT_RESIZE);
        }
        break;
    case SDL_QUIT:
        mp_input_put_key(vo->input_ctx, MP_KEY_CLOSE_WIN);
        break;
    case SDL_TEXTINPUT: {
        struct bstr t = bstr0(ev->text.text);
        mp_input_put_key_utf8(vo->input_ctx, 0, t);
        break;
    }
    case SDL_KEYDOWN: {
        int mod = ev->key.keysym.mod;
        int key = lookup(keys, MP_ARRAY_SIZE(keys), ev->key.keysym.sym);
        if (!key) {
            /* printable keys normally arrive as SDL_TEXTINPUT right behind this event; some firmware
             * SDL2 builds (Knulli's mali fbdev driver) never send one, so make the character here */
            SDL_Event text;
            int sym = ev->key.keysym.sym;
            if (sym < 32 || sym > 126 || SDL_PeepEvents(&text, 1, SDL_PEEKEVENT, SDL_TEXTINPUT, SDL_TEXTINPUT) > 0)
                break;
            static const char plain[] = "1234567890-=[]\\;',./`", shifted[] = "!@#$%^&*()_+{}|:\"<>?~";
            char c[2] = {sym, 0};
            if (mod & KMOD_SHIFT) {
                const char *s = strchr(plain, sym);
                c[0] = sym >= 'a' && sym <= 'z' ? sym - 32 : s ? shifted[s - plain] : sym;
            }
            mp_input_put_key_utf8(vo->input_ctx, (mod & KMOD_CTRL ? MP_KEY_MODIFIER_CTRL : 0) |
                                  (mod & KMOD_ALT ? MP_KEY_MODIFIER_ALT : 0), bstr0(c));
            break;
        }
        if (mod & KMOD_SHIFT)
            key |= MP_KEY_MODIFIER_SHIFT;
        if (mod & KMOD_CTRL)
            key |= MP_KEY_MODIFIER_CTRL;
        if (mod & KMOD_ALT)
            key |= MP_KEY_MODIFIER_ALT;
        mp_input_put_key(vo->input_ctx, key);
        break;
    }
    case SDL_CONTROLLERDEVICEADDED:
        if (!p->pad && SDL_IsGameController(ev->cdevice.which)) {
            p->pad = SDL_GameControllerOpen(ev->cdevice.which);
            if (p->pad)
                MP_INFO(ctx, "controller: %s\n", SDL_GameControllerName(p->pad));
        }
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (p->pad && ev->cdevice.which ==
                SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p->pad))) {
            SDL_GameControllerClose(p->pad);
            p->pad = NULL;
        }
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        int key = lookup(buttons, MP_ARRAY_SIZE(buttons), ev->cbutton.button);
        if (key)
            mp_input_put_key(vo->input_ctx, key | (ev->type == SDL_CONTROLLERBUTTONDOWN ?
                                                   MP_KEY_STATE_DOWN : MP_KEY_STATE_UP));
        break;
    }
    case SDL_CONTROLLERAXISMOTION:
        handle_axis(ctx, ev->caxis.axis, ev->caxis.value);
        break;
    }
}

static void sdl_wait_events(struct ra_ctx *ctx, int64_t until_time_ns)
{
    int64_t wait_ns = until_time_ns - mp_time_ns();
    if (wait_ns > MP_TIME_US_TO_NS(100))
        wait_ns = MPMAX(wait_ns, MP_TIME_MS_TO_NS(1));
    int timeout_ms = MPCLAMP(wait_ns / MP_TIME_MS_TO_NS(1), 0, 10000);
    SDL_Event ev;
    while (SDL_WaitEventTimeout(&ev, timeout_ms)) {
        timeout_ms = 0;
        handle_event(ctx, &ev);
    }
}

const struct ra_ctx_fns ra_ctx_sdl = {
    .type           = "opengl",
    .name           = "sdl",
    .description    = "SDL2/OpenGL ES (firmware SDL2)",
    .reconfig       = sdl_reconfig,
    .control        = sdl_control,
    .wakeup         = sdl_wakeup,
    .wait_events    = sdl_wait_events,
    .init           = sdl_init,
    .uninit         = sdl_uninit,
};
