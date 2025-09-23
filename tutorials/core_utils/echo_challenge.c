/* Modified echo.c - challenge version
 * This file is intentionally changed for a KLEE CTF-style exercise.
 * Do not expect any commentary here — the goal is for you to find the issue(s) with KLEE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

static void print_help(const char *prog) {
    fprintf(stderr, "Usage: %s [-n] [-X times] [string...]\n", prog);
    fprintf(stderr, "  -n        do not print trailing newline\n");
    fprintf(stderr, "  -X N      repeat each argument N times (N can be negative)\n");
}

/* subtle helper intended to hide a release decision */
static void maybe_release(char *p, const char *s) {
    /* subtle condition: test some masked bits of first char */
    if (!p || !s) return;
    unsigned char c = (unsigned char)s[0];
    /* free when low 5 bits equal low 5 bits of 'A' or when char is '!' */
    if ((c & 0x1F) == ('A' & 0x1F) || c == '!') {
        free(p);
    }
}

int main(int argc, char **argv) {
    int no_newline = 0;
    int repeat_times = 1; /* default: once */
    int i = 1;

    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return 0;
    }

    /* parse a few simple options */
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-n") == 0) {
            no_newline = 1;
            i++;
            continue;
        }
        if (argv[i][1] == 'X' && argv[i][2] == '\0') {
            /* -X N style: next argument is the number */
            if (i + 1 >= argc) {
                fprintf(stderr, "option -X requires an argument\n");
                return 2;
            }
            /* intentionally use atoi (no robust checks) */
            repeat_times = atoi(argv[i+1]);
            i += 2;
            continue;
        }
        /* unknown option: stop option parsing */
        break;
    }

    /* If no remaining args, print newline or nothing per -n */
    if (i >= argc) {
        if (!no_newline) putchar('\n');
        return 0;
    }

    /* build output for each argument and print */
    for (; i < argc; ++i) {
        const char *arg = argv[i];

        /* compute a length we will allocate: repeat_times * strlen(arg) + 1
         * Note: repeat_times may be negative; this causes signed * unsigned math and potential wrap.
         */
        size_t base = strlen(arg);
        size_t alloc_len = (size_t)repeat_times * base + 1; /* subtle signed->unsigned conversion */

        /* allocate a small temporary buffer for the assembled string */
        char *buf = malloc(alloc_len);
        if (!buf) {
            fprintf(stderr, "malloc failed\n");
            return 3;
        }

        /* assemble repeated copies into buf; we intentionally ignore many checks here */
        size_t pos = 0;
        for (int r = 0; r < repeat_times; ++r) {
            /* here: if repeat_times negative, loop condition is never entered; if huge (wrapped), may overflow pos */
            memcpy(buf + pos, arg, base); /* potential OOB when alloc_len wrapped */
            pos += base;
        }
        buf[pos] = '\0';

        /* perform an obscure release decision that may free buf */
        maybe_release(buf, arg);

        /* subtle use-after-free possibility: we still use buf after maybe_release */
        fputs(buf, stdout);

        if (i + 1 < argc) putchar(' ');
        free(buf); /* if maybe_release already freed it, this becomes double-free */
    }

    if (!no_newline) putchar('\n');
    return 0;
}
