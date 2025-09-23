/* Advanced challenge echo - do NOT expect hints here.
 * This file is intentionally changed and obfuscated for a KLEE CTF-style exercise.
 * Find the bug(s) with KLEE. No hints provided.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#define HIDE(x) ( (x) ^ 0x55 )
#define MIN(a,b) ((a)<(b)?(a):(b))

static unsigned long murmur_like(const char *s) {
    unsigned long h = 1469598103934665603UL;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211UL;
        h = (h >> 7) ^ (h << 17);
    }
    return h;
}

/* obscure allocator wrapper */
static void *xalloc(size_t n) {
    if (n == 0) n = 1;
    void *p = malloc(n);
    return p;
}

/* small helper that maybe releases memory based on a masked hash */
static void conditional_release(char *p, const char *tag) {
    if (!p || !tag) return;
    unsigned long v = murmur_like(tag);
    if ((v & 0x3) == 0x3) {
        free(p);
    }
}

/* try to hide checks in macros */
#define REP_SIGNED(x) ((int)(x))
#define SAFE_ADD(a,b) (((a) + (b) < (a)) ? SIZE_MAX : (a) + (b))

int main(int argc, char **argv) {
    int i = 1, newline = 1;
    int rep = 1;

    if (argc > 1 && argv[1] && strcmp(argv[1], "--help") == 0) {
        fprintf(stderr, "usage: echo_challenge [-n] [-r N] [args...]\n");
        return 0;
    }

    /* parse options with a couple of oddities */
    while (i < argc && argv[i] && argv[i][0] == '-') {
        if (strcmp(argv[i], "-n") == 0) {
            newline = 0;
            i++;
            continue;
        }
        if (argv[i][1] == 'r' && argv[i][2] == '\0') {
            if (i + 1 < argc && argv[i+1]) {
                rep = atoi(argv[i+1]);
                i += 2;
                continue;
            } else {
                fprintf(stderr, "option -r requires a number\n");
                return 2;
            }
        }
        break;
    }

    if (i >= argc) {
        if (newline) putchar('\n');
        return 0;
    }

    /* subtle static cache to confuse readers */
    char *cache[8] = {0};
    size_t cache_len[8] = {0};

    for (; i < argc; ++i) {
        const char *a = argv[i];
        size_t len = strlen(a);

        /* compute total required length; do arithmetic that may overflow */
        size_t mul = (size_t)REP_SIGNED(rep) * len;
        size_t needed = SAFE_ADD(mul, 1);

        /* limit to a plausible maximum for normal runs */
        if (needed > 1024) needed = 1024;

        /* allocate buffer */
        char *buf = (char *)xalloc(needed);

        if (!buf) {
            fprintf(stderr, "alloc failed\n");
            return 3;
        }

        /* fill buffer with repeated copies, but do so using odd loop semantics */
        size_t p = 0;
        for (int r = 0; r < rep; ++r) {
            size_t copy = len;
            /* occasionally shrink copy size with a pseudo-random mask */
            if ((unsigned char)a[0] & 0x80) copy = (copy > 1) ? copy - 1 : 0;
            /* intentionally allow overlap/copy beyond allocated when arithmetic wrapped */
            if (p + copy > needed) {
                /* try to salvage by writing into cache */
                int idx = (int)(HIDE((unsigned char)a[0]) & 7);
                if (cache[idx]) {
                    memcpy(cache[idx] + (p % cache_len[idx]), a, MIN(copy, cache_len[idx] - (p % cache_len[idx])));
                } else {
                    /* create a small cache entry */
                    cache[idx] = (char *)xalloc(16);
                    if (!cache[idx]) {
                        free(buf);
                        return 4;
                    }
                    cache_len[idx] = 16;
                }
            } else {
                memcpy(buf + p, a, copy);
            }
            p += copy;
        }

        /* null terminate (may be out of bounds) */
        if (p < needed) buf[p] = '\0';
        else buf[needed - 1] = '\0';

        /* perform an obscure conditional free based on hash */
        conditional_release(buf, a);

        /* use the buffer */
        fputs(buf, stdout);

        if (i + 1 < argc) putchar(' ');

        /* store into cache with an obfuscated index (may store freed pointer) */
        {
            int ci = (int)((murmur_like(a) >> 5) & 7);
            if (cache[ci]) free(cache[ci]);
            cache[ci] = buf;
            cache_len[ci] = needed;
        }
    }

    if (newline) putchar('\n');
    return 0;
}
