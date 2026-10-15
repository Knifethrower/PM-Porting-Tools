// C interface to the astcenc library for gmastc.c: one context, images compressed by
// all threads.
#include <pthread.h>
#include "astcenc.h"

static astcenc_context *ctx;
static unsigned threads;

struct job {
    astcenc_image *img;
    unsigned char *out;
    size_t len;
    unsigned idx;
    astcenc_error err;
};

static void *compress_thread(void *arg)
{
    job *j = static_cast<job *>(arg);
    static const astcenc_swizzle swz = {ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
    j->err = astcenc_compress_image(ctx, j->img, &swz, j->out, j->len, j->idx);
    return nullptr;
}

extern "C" const char *astc_setup(int block, float quality, unsigned nthreads)
{
    astcenc_config cfg;
    astcenc_error e = astcenc_config_init(ASTCENC_PRF_LDR, block, block, 1, quality, 0, &cfg);
    if (e == ASTCENC_SUCCESS)
        e = astcenc_context_alloc(&cfg, nthreads, &ctx);
    threads = nthreads;
    return e == ASTCENC_SUCCESS ? nullptr : astcenc_get_error_string(e);
}

extern "C" const char *astc_compress(unsigned char *rgba, int w, int h, unsigned char *out, size_t len)
{
    void *slice = rgba;
    astcenc_image img = {(unsigned)w, (unsigned)h, 1, ASTCENC_TYPE_U8, &slice};
    pthread_t tid[64];
    job jobs[64];
    for (unsigned i = 0; i < threads; i++) {
        jobs[i] = {&img, out, len, i, ASTCENC_SUCCESS};
        pthread_create(&tid[i], nullptr, compress_thread, &jobs[i]);
    }
    const char *err = nullptr;
    for (unsigned i = 0; i < threads; i++) {
        pthread_join(tid[i], nullptr);
        if (jobs[i].err != ASTCENC_SUCCESS)
            err = astcenc_get_error_string(jobs[i].err);
    }
    astcenc_compress_reset(ctx);
    return err;
}
