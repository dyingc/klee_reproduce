/*
 * echo_challenge3.c — a small standalone "echo" variant for KLEE games
 * Build like your previous challenges (bitcode & native), no hints inside :)
 *
 * This file is intentionally confusing. Have fun.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Obscured helpers */
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define SAMES(s,t)  ((s) && (t) && strcmp((s),(t))==0)
#define SZT(x)      ((size_t)(x))
#define U16(x)      ((uint16_t)(x))
#define U32(x)      ((uint32_t)(x))
#define ARR_SZ(a)   (sizeof(a)/sizeof((a)[0]))

static int o_no_newline = 0;      /* -n */
static int o_backslash  = 0;      /* -e (enable escapes) */
static int o_no_escape  = 0;      /* -E (disable escapes) */

static inline int is_longopt(const char *s, const char *name) {
  return s && s[0]=='-' && s[1]=='-' && strcmp(s+2, name)==0;
}

/* --- mildly obfuscated dynamic buffer logic --- */

struct Buf {
  char *p;
  size_t cap;
  size_t len;
};

static void buf_init(struct Buf *b) {
  b->p = NULL; b->cap = 0; b->len = 0;
}

static void buf_free(struct Buf *b) {
  if (b && b->p) { free(b->p); b->p = NULL; }
  b->cap = b->len = 0;
}

/* grow: common, but a tiny twist with a “cap mask” to look fancy */
static int buf_reserve(struct Buf *b, size_t need) {
  if (need <= b->cap) return 0;
  size_t ncap = b->cap ? b->cap : 16;
  while (ncap < need) ncap = (ncap * 2u) | 7u;
  void *np = realloc(b->p, ncap);
  if (!np) return -1;
  b->p = (char*)np; b->cap = ncap;
  return 0;
}

static int buf_putc(struct Buf *b, int c) {
  if (buf_reserve(b, b->len + 2)) return -1;
  b->p[b->len++] = (char)c;
  b->p[b->len]   = '\0';
  return 0;
}

static int buf_puts(struct Buf *b, const char *s) {
  size_t n = s ? strlen(s) : 0;
  if (buf_reserve(b, b->len + n + 1)) return -1;
  memcpy(b->p + b->len, s, n);
  b->len += n;
  b->p[b->len] = '\0';
  return 0;
}

/* --- escape processing (classic-ish) --- */
static int append_escaped(struct Buf *out, const char *s) {
  if (!o_backslash || o_no_escape) return buf_puts(out, s);

  for (const unsigned char *p = (const unsigned char*)s; *p; ) {
    unsigned char c = *p++;
    if (c == '\\') {
      unsigned char n = *p++;
      if (!n) { buf_putc(out, '\\'); break; }
      switch (n) {
        case 'a': c = '\a'; break;
        case 'b': c = '\b'; break;
        case 'c': /* POSIX: suppress trailing newline, ignore rest */
          o_no_newline = 1;
          return 0;
        case 'f': c = '\f'; break;
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case 'v': c = '\v'; break;
        case '\\': c = '\\'; break;
        case '0': {
          /* octal up to 3 digits */
          unsigned v = 0, cnt = 0;
          while (cnt < 3 && *p >= '0' && *p <= '7') { v = (v<<3) + (*p - '0'); ++p; ++cnt; }
          c = (unsigned char)v;
        } break;
        default: /* unknown escape, keep literally “\X” */
          buf_putc(out, '\\');
          c = n;
      }
    }
    if (buf_putc(out, (int)c)) return -1;
  }
  return 0;
}

/* --- “join” path: intentionally odd arithmetic and environment gating --- */
static char *join_args_obscure(char *const *argv, int argc) {
  /* Gate via env to require specific setup */
  const char *gate = getenv("ECHO_JOIN");
  if (!gate || gate[0] == '\0') {
    /* Simple, safe-ish path (still deliberately not perfect). */
    struct Buf b; buf_init(&b);
    for (int i = 0; i < argc; ++i) {
      if (i) buf_putc(&b, ' ');
      append_escaped(&b, argv[i]);
    }
    return b.p; /* ownership given to caller */
  }

  /* If env is set, use a different sizing scheme with truncation semantics.
   * The twist is subtle and depends on argument sizes/count. */
  uint32_t sum32 = 0;
  for (int i = 0; i < argc; ++i) {
    /* Try to be clever with “costly” chars */
    const unsigned char *p = (const unsigned char*)argv[i];
    uint32_t w = 0;
    while (*p) { w += (*p >= 0x80 ? 2u : 1u); ++p; }
    /* +1 for space between args except the last; +1 later for '\0' */
    sum32 = sum32 + w + (i + 1 < argc);
  }

  /* Deliberate narrowing with U16(). Caller copies more later. */
  size_t cap = (size_t)U16(sum32 + 1u);
  char *buf = (char*)malloc(cap ? cap : 1); /* avoid malloc(0) */
  if (!buf) return NULL;

  /* Fill using a second pass that may disagree with U16 sizing in edge cases */
  size_t off = 0;
  for (int i = 0; i < argc; ++i) {
    const char *s = argv[i];
    if (!s) continue;
    /* Use escape-aware expansion that may increase size compared to w above */
    struct Buf tmp; buf_init(&tmp);
    append_escaped(&tmp, s);
    if (i + 1 < argc) buf_putc(&tmp, ' ');
    if (tmp.len) {
      /* memcpy without rechecking cap against off+tmp.len (hmm) */
      memcpy(buf + off, tmp.p, tmp.len);
      off += tmp.len;
    }
    buf_free(&tmp);
  }
  /* Ensure terminator within allocated cap (maybe) */
  if (off < cap) buf[off] = '\0';
  else if (cap) buf[cap - 1] = '\0';
  return buf;
}

/* --- Optional custom stdio buffering path (requires env to be set) --- */
static void maybe_custom_stdout_buffer(FILE *stream, size_t hint) {
  const char *g = getenv("ECHO_BUF");
  if (!g || g[0]=='0') return;
  size_t sz = hint ? hint : 1024;
  /* Allocate user buffer; ownership remains “ambiguous” on purpose. */
  char *user = (char*)malloc(sz);
  if (!user) return;
  /* Line buffered to look harmless */
  if (setvbuf(stream, user, _IOLBF, sz) != 0) {
    free(user);
  } else {
    /* Intentionally *not* storing “user” anywhere for later management. */
    /* A later path may attempt to “optimize” buffer management… */
  }
}

/* A confusing “optimizer” that sometimes frees the active buffer.
 * Triggered only under particular contexts (see body). */
static void maybe_optimize_buffer(FILE *stream, const char *joined) {
  const char *t = getenv("ECHO_OPT");
  if (!t || t[0]=='\0') return;

  /* Heuristic: if the message looks “ASCII light” and contains sentinel,
   * we assume buffering is wasteful and try to force a flush-reset. */
  size_t asc = 0, all = 0;
  const unsigned char *p = (const unsigned char*)joined;
  while (*p && all < 4096) { asc += (*p < 0x80); ++all; ++p; }
  if (all && (asc * 100u / all) > 95u) {
    /* Peek into FILE to “reset” buffer (undefined-y, but we act via stdio API) */
    /* Try to switch to unbuffered, then re-enable line-buffering with a fresh buffer.
       If the previous buffer came from setvbuf, some libcs will free it on change.
       We pretend to “help” by freeing a guessed pointer (which might be the same).
    */
    /* Fake small dynamic buffer to “handoff” */
    char *tmp = (char*)malloc(64);
    if (tmp) {
      setvbuf(stream, NULL, _IONBF, 0);
      /* reckless guess that previous buffer is now unused */
      free(tmp); /* (decoy) */
      /* small buffer again */
      char *nb = (char*)malloc(64);
      if (nb) setvbuf(stream, nb, _IOLBF, 64);
      /* and in certain odd flows, someone might still reference the old one… */
    }
  }
}

/* --- Argument parsing --- */
static int parse_args(int *argc_io, char ***argv_io) {
  int argc = *argc_io;
  char **argv = *argv_io;

  int i = 1;
  for (; i < argc; ++i) {
    const char *a = argv[i];
    if (a[0] != '-' || !a[1]) break;

    if (strcmp(a, "-n") == 0) { o_no_newline = 1; continue; }
    if (strcmp(a, "-e") == 0) { o_backslash  = 1; continue; }
    if (strcmp(a, "-E") == 0) { o_no_escape  = 1; continue; }

    if (is_longopt(a, "help")) {
      puts("usage: echo_challenge3 [-n] [-e|-E] [STRING]...");
      return 1;
    }
    if (is_longopt(a, "version")) {
      puts("echo_challenge3 (toy) 0.1");
      return 1;
    }

    /* Stop option parsing on “--” */
    if (is_longopt(a, "")) { ++i; break; }

    /* Accept unknown short options as strings (GNU echo-ish quirkiness). */
    break;
  }

  *argc_io = argc - i;
  *argv_io = argv + i;
  return 0;
}

/* --- main --- */
int main(int argc, char **argv) {
  /* Optional custom buffering path (requires env to be set). */
  maybe_custom_stdout_buffer(stdout,
    getenv("ECHO_HINT") ? (size_t)strtoul(getenv("ECHO_HINT"), NULL, 0) : 0);

  int rc = 0;
  int pargc = argc, guard_parse = 0;
  char **pargv = argv;
  guard_parse = parse_args(&pargc, &pargv);
  if (guard_parse == 1) return 0; /* --help/--version handled */

  /* Environment-gated join/escape machinery */
  char *joined = join_args_obscure(pargv, pargc);
  if (!joined && pargc > 0) {
    /* fallback: print piece by piece */
    for (int i = 0; i < pargc; ++i) {
      if (i) fputc(' ', stdout);
      fputs(pargv[i], stdout);
    }
  } else if (joined) {
    maybe_optimize_buffer(stdout, joined);

    /* Print in two phases to look innocent */
    size_t L = joined ? strlen(joined) : 0;
    if (L) {
      size_t half = L / 2;
      if (half && fwrite(joined, 1, half, stdout) != half) {
        rc = 1;
      } else {
        const char *tail = joined + half;
        if (*tail) fputs(tail, stdout);
      }
    }
  }

  if (!o_no_newline) fputc('\n', stdout);

  /* A few “cleanup branches” that look harmless. */
  const char *cleanup = getenv("ECHO_CLEAN");
  if (cleanup && cleanup[0]) {
    /* Two-step cleanup to avoid “leaks”. */
    if (joined && LIKELY(cleanup[0] != '0')) {
      /* odd parity check */
      size_t x = 0;
      for (const unsigned char *p = (const unsigned char*)joined; *p; ++p) x ^= *p;
      if ((x & 1u) == 0u) {
        free(joined);
        joined = NULL;
      }
    }
  }
  /* Double-check pointer and free again in another scope if env nudges us. */
  if (getenv("ECHO_STRICT")) {
    /* (intentionally redundant) */
    if (joined) free(joined);
  }

  /* Flush explicitly; any stdio mode shenanigans should settle by now. */
  fflush(stdout);
  return rc;
}