// Real ARB programs from games (extract_arb.py writes one .vp / .fp file per program): each fragment program
// runs over the fixed-function vertex stage, each vertex program through the output probes (arb.c).
#include "gt.h"
#include <dirent.h>

void gt_arb_fp(gt_test *t, const char *src, int seed, int enables);
void gt_arb_vp(gt_test *t, const char *src, int seed, int nattribs, const int *attribs);

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *s = malloc(n + 1); if (fread(s, 1, n, f) != (size_t)n) { fclose(f); free(s); return NULL; }
    s[n] = 0; fclose(f); return s;
}
static void t_corpus(gt_test *t) {
    const char *src = t->p;
    if (t->a) gt_arb_fp(t, src, 11, 1);
    else {
        int attribs[16], n = 0; const char *p = src;
        while ((p = strstr(p, "vertex.attrib[")) && n < 16) {
            int a = atoi(p + 14), dup = 0; for (int i = 0; i < n; i++) dup |= attribs[i] == a;
            if (!dup && a != 0) attribs[n++] = a;
            p += 14;
        }
        gt_arb_vp(t, src, 11, n, attribs);
    }
}
void reg_corpus(const char *dir) {
    DIR *d = opendir(dir); if (!d) { fprintf(stderr, "corpus %s: cannot open\n", dir); return; }
    struct dirent *e; char path[1024], nm[160];
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name); if (l < 4) continue;
        int fp = !strcmp(e->d_name + l - 3, ".fp"), vp = !strcmp(e->d_name + l - 3, ".vp");
        if (!fp && !vp) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        char *s = slurp(path); if (!s) continue;
        snprintf(nm, sizeof nm, "corpus/%.*s", (int)(l - 3), e->d_name);
        strcat(nm, fp ? "/fp" : "/vp");
        gt_add(nm, t_corpus, fp, 0, 0, 0, s);
    }
    closedir(d);
}
