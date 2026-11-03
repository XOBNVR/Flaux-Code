/* ===================================================================
 * Flaux runtime, included at the top of every compiled Flaux program.
 *
 * Kept deliberately small: text, safe maths, friendly errors and a few
 * helpers. List types are generated per item type by the compiler.
 * =================================================================== */
#define _GNU_SOURCE
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <ctype.h>

#define FX_INLINE static inline __attribute__((always_inline))

typedef struct { const char *p; int64_t n; } fx_text;
#define FX_LIT(s) ((fx_text){ (s), (int64_t)(sizeof(s) - 1) })

static const char *fx_file = "?";
static const char *const *fx_lines = 0;
static int fx_nlines = 0;

/* ---------------------------------------------------------- errors */

/* Stop the program with a message that says what went wrong and how to fix
 * it. `fix` is plain text (lines starting with spaces are shown as code). */
__attribute__((noinline, cold, noreturn, format(printf, 3, 4)))
static void fx_fail(int line, const char *fix, const char *fmt, ...) {
    fflush(stdout);
    bool color = isatty(2);
    const char *red = color ? "\033[1;31m" : "", *bold = color ? "\033[1m" : "";
    const char *dim = color ? "\033[2m" : "", *off = color ? "\033[0m" : "";
    if (line > 0) {
        fprintf(stderr, "\n%sProblem%s%s while running %s, line %d:%s\n\n", red, off, bold, fx_file, line, off);
        if (line <= fx_nlines)
            fprintf(stderr, "  %s%d |%s %s\n\n", dim, line, off, fx_lines[line - 1]);
    } else {
        fprintf(stderr, "\n%sProblem%s%s while running %s:%s\n\n", red, off, bold, fx_file, off);
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (fix && *fix) fprintf(stderr, "\n%sHow to fix it:%s\n  %s\n", bold, off, fix);
    exit(1);
}

__attribute__((noinline, cold, noreturn))
static void fx_index_fail(int64_t i, int64_t len, int line, const char *what) {
    const char *thing = what[0] == 'i' ? "list" : "text";
    char fix[600];
    if (i == 0) {
        snprintf(fix, sizeof fix,
                 "In Flaux, the first %s is number 1 (not 0), and the last is number `length of` the %s.",
                 what, thing);
        fx_fail(line, fix, "There is no %s number 0.", what);
    }
    if (len == 0) {
        snprintf(fix, sizeof fix,
                 "The %s has nothing in it yet. Make sure it has something in it before reading from it,\n"
                 "  or check its length first:\n\n"
                 "      if length of my_%s > 0\n"
                 "          say my_%s[1]\n"
                 "      end", thing, thing, thing);
        fx_fail(line, fix, "You asked for %s number %" PRId64 ", but the %s is empty.", what, i, thing);
    }
    snprintf(fix, sizeof fix,
             "%ss are numbered from 1 up to `length of` the %s. Check the position is in range\n"
             "  before you use it:\n\n"
             "      if position >= 1 and position <= length of my_%s\n"
             "          say my_%s[position]\n"
             "      end", what, thing, thing, thing);
    fix[0] = (char)toupper((unsigned char)fix[0]);
    fx_fail(line, fix, "You asked for %s number %" PRId64 ", but there %s only %" PRId64 ".",
            what, i, len == 1 ? "is" : "are", len);
}

static const char FX_FIX_DIVIDE[] =
    "Check that the number you divide by isn't zero first, like this:\n\n"
    "      if count != 0\n"
    "          say total / count\n"
    "      end";

static void *fx_alloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) fx_fail(0, "Check for a `repeat` that keeps adding to a list or text and never stops.",
                    "The computer ran out of memory.");
    return p;
}

static void *fx_realloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) fx_fail(0, "Check for a `repeat` that keeps adding to a list or text and never stops.",
                    "The computer ran out of memory.");
    return p;
}

/* ------------------------------------------------------------ text */

static char fx_chars[512]; /* every single-byte text, so letters don't allocate */

FX_INLINE fx_text fx_byte_text(unsigned char c) { return (fx_text){ &fx_chars[2 * c], 1 }; }

static fx_text fx_text_make(const char *src, int64_t n) {
    char *p = fx_alloc((size_t)n + 1);
    memcpy(p, src, (size_t)n);
    p[n] = 0;
    return (fx_text){ p, n };
}

static fx_text fx_concat(fx_text a, fx_text b) {
    if (b.n == 0) return a;
    if (a.n == 0) return b;
    char *p = fx_alloc((size_t)(a.n + b.n) + 1);
    memcpy(p, a.p, (size_t)a.n);
    memcpy(p + a.n, b.p, (size_t)b.n);
    p[a.n + b.n] = 0;
    return (fx_text){ p, a.n + b.n };
}

static fx_text fx_join(const fx_text *parts, int count) {
    int64_t n = 0;
    for (int i = 0; i < count; i++) n += parts[i].n;
    char *p = fx_alloc((size_t)n + 1), *q = p;
    for (int i = 0; i < count; i++) { memcpy(q, parts[i].p, (size_t)parts[i].n); q += parts[i].n; }
    *q = 0;
    return (fx_text){ p, n };
}

static int fx_fmt_float(char *buf, size_t size, double v) {
    if (isnan(v)) return snprintf(buf, size, "not a number");
    if (isinf(v)) return snprintf(buf, size, v > 0 ? "infinity" : "-infinity");
    /* find the fewest digits that read back as exactly the same number */
    int digits = 17;
    for (int d = 1; d <= 17; d++) {
        snprintf(buf, size, "%.*e", d - 1, v);
        if (strtod(buf, NULL) == v) { digits = d; break; }
    }
    snprintf(buf, size, "%.*e", digits - 1, v);
    int exp = atoi(strchr(buf, 'e') + 1);
    if (exp < -5 || exp >= 17) return (int)strlen(buf); /* tiny or huge: 1.5e+20 */
    int decimals = digits - 1 - exp;
    return snprintf(buf, size, "%.*f", decimals > 0 ? decimals : 0, v);
}

static fx_text fx_int_text(int64_t v) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%" PRId64, v);
    return fx_text_make(buf, n);
}

static fx_text fx_float_text(double v) {
    char buf[64];
    int n = fx_fmt_float(buf, sizeof buf, v);
    return fx_text_make(buf, n);
}

FX_INLINE fx_text fx_bool_text(bool b) { return b ? FX_LIT("true") : FX_LIT("false"); }

static fx_text fx_text_quote(fx_text t) {
    fx_text parts[3] = { FX_LIT("\""), t, FX_LIT("\"") };
    return fx_join(parts, 3);
}

FX_INLINE bool fx_text_eq(fx_text a, fx_text b) {
    return a.n == b.n && (a.p == b.p || memcmp(a.p, b.p, (size_t)a.n) == 0);
}

static int fx_text_cmp(fx_text a, fx_text b) {
    int64_t m = a.n < b.n ? a.n : b.n;
    int c = memcmp(a.p, b.p, (size_t)m);
    if (c) return c;
    return (a.n > b.n) - (a.n < b.n);
}

static bool fx_text_contains(fx_text hay, fx_text needle) {
    if (needle.n == 0) return true;
    for (int64_t i = 0; i + needle.n <= hay.n; i++)
        if (memcmp(hay.p + i, needle.p, (size_t)needle.n) == 0) return true;
    return false;
}

FX_INLINE fx_text fx_text_get(fx_text t, int64_t i, int line) {
    if ((uint64_t)(i - 1) >= (uint64_t)t.n) fx_index_fail(i, t.n, line, "letter");
    return fx_byte_text((unsigned char)t.p[i - 1]);
}

static fx_text fx_text_upper(fx_text t) {
    char *p = fx_alloc((size_t)t.n + 1);
    for (int64_t i = 0; i < t.n; i++) p[i] = (char)toupper((unsigned char)t.p[i]);
    p[t.n] = 0;
    return (fx_text){ p, t.n };
}

static fx_text fx_text_lower(fx_text t) {
    char *p = fx_alloc((size_t)t.n + 1);
    for (int64_t i = 0; i < t.n; i++) p[i] = (char)tolower((unsigned char)t.p[i]);
    p[t.n] = 0;
    return (fx_text){ p, t.n };
}

/* ------------------------------------------------------------- say */

static void fx_say_text(fx_text t) { fwrite(t.p, 1, (size_t)t.n, stdout); putchar('\n'); }
static void fx_say_int(int64_t v) { printf("%" PRId64 "\n", v); }
static void fx_say_bool(bool b) { puts(b ? "true" : "false"); }
static void fx_say_float(double v) { char buf[64]; fx_fmt_float(buf, sizeof buf, v); puts(buf); }

/* ------------------------------------------------------------ maths */

FX_INLINE int64_t fx_idiv(int64_t a, int64_t b, int line) {
    if (b == 0) fx_fail(line, FX_FIX_DIVIDE, "You can't divide by zero.");
    if (b == -1) return -a;
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--; /* round down, like maths */
    return q;
}

FX_INLINE int64_t fx_mod(int64_t a, int64_t b, int line) {
    if (b == 0) fx_fail(line, "`mod` gives the remainder after dividing, so the number after it can't be 0.",
                        "You can't use mod with zero.");
    if (b == -1) return 0;
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b; /* result has the sign of b */
    return r;
}

/* `a // b` and `a mod b` when b is a positive number written in the program */
FX_INLINE int64_t fx_idiv_pos(int64_t a, int64_t b) {
    int64_t q = a / b;
    return (a % b < 0) ? q - 1 : q;
}

FX_INLINE int64_t fx_mod_pos(int64_t a, int64_t b) {
    int64_t r = a % b;
    return r < 0 ? r + b : r;
}

FX_INLINE double fx_fdiv(double a, double b, int line) {
    if (b == 0.0) fx_fail(line, FX_FIX_DIVIDE, "You can't divide by zero.");
    return a / b;
}

FX_INLINE double fx_ffloordiv(double a, double b, int line) {
    if (b == 0.0) fx_fail(line, FX_FIX_DIVIDE, "You can't divide by zero.");
    return floor(a / b);
}

FX_INLINE double fx_fmod(double a, double b, int line) {
    if (b == 0.0) fx_fail(line, "`mod` gives the remainder after dividing, so the number after it can't be 0.",
                          "You can't use mod with zero.");
    double r = fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

static int64_t fx_ipow(int64_t base, int64_t e, int line) {
    if (e < 0)
        fx_fail(line, "Use a decimal number instead, like 2.0 ^ -1 (which gives 0.5).",
                "A whole number to a negative power isn't a whole number.");
    int64_t r = 1;
    while (e) {
        if (e & 1) r *= base;
        base *= base;
        e >>= 1;
    }
    return r;
}

FX_INLINE int64_t fx_iabs(int64_t v) { return v < 0 ? -v : v; }
FX_INLINE int64_t fx_imin(int64_t a, int64_t b) { return a < b ? a : b; }
FX_INLINE int64_t fx_imax(int64_t a, int64_t b) { return a > b ? a : b; }
FX_INLINE double fx_fmin(double a, double b) { return a < b ? a : b; }
FX_INLINE double fx_fmax(double a, double b) { return a > b ? a : b; }

static double fx_sqrt(double v, int line) {
    if (v < 0) fx_fail(line, "Use abs(...) to make the number positive first, like sqrt(abs(x)).",
                       "You can't take the square root of a negative number.");
    return sqrt(v);
}

/* ----------------------------------------------------------- random */

static uint64_t fx_rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t fx_rng(void) { /* xorshift64* */
    uint64_t x = fx_rng_state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    fx_rng_state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

static int64_t fx_random_int(int64_t a, int64_t b, int line) {
    if (a > b) fx_fail(line, "Put the smaller number first, like random(1, 6).",
                       "random(%" PRId64 ", %" PRId64 ") has the bigger number first.", a, b);
    uint64_t span = (uint64_t)(b - a) + 1;
    if (span == 0) return (int64_t)fx_rng();
    return a + (int64_t)(fx_rng() % span);
}

static double fx_random_float(double a, double b, int line) {
    if (a > b) fx_fail(line, "Put the smaller number first, like random(0.0, 1.0).",
                       "random(a, b) has the bigger number first.");
    return a + (b - a) * ((double)(fx_rng() >> 11) * 0x1.0p-53);
}

/* --------------------------------------------------- input and time */

static fx_text fx_ask(fx_text prompt) {
    if (prompt.n) {
        fwrite(prompt.p, 1, (size_t)prompt.n, stdout);
        if (prompt.p[prompt.n - 1] != ' ') putchar(' ');
    }
    fflush(stdout);
    size_t cap = 64, n = 0;
    char *buf = fx_alloc(cap);
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (n + 1 >= cap) { cap *= 2; buf = fx_realloc(buf, cap); }
        buf[n++] = (char)c;
    }
    if (n && buf[n - 1] == '\r') n--;
    buf[n] = 0;
    return (fx_text){ buf, (int64_t)n };
}

static double fx_number(fx_text t, int line) {
    char *s = (char *)fx_text_make(t.p, t.n).p;
    char *start = s;
    while (isspace((unsigned char)*start)) start++;
    char *end;
    double v = strtod(start, &end);
    while (isspace((unsigned char)*end)) end++;
    if (end == start || *end)
        fx_fail(line, "number(...) only works on text that looks like a number, like \"42\" or \"3.5\".\n"
                      "  If this came from ask(...), you could keep asking until you get a number:\n\n"
                      "      set answer to ask(\"How old are you?\")\n"
                      "      repeat until is_number(answer)\n"
                      "          set answer to ask(\"Please type a number:\")\n"
                      "      end",
                "I can't turn \"%s\" into a number.", s);
    return v;
}

static bool fx_is_number(fx_text t) {
    char *s = (char *)fx_text_make(t.p, t.n).p;
    char *start = s;
    while (isspace((unsigned char)*start)) start++;
    char *end;
    strtod(start, &end);
    while (isspace((unsigned char)*end)) end++;
    return end != start && !*end;
}

static int64_t fx_whole_text(fx_text t, int line) {
    char *s = (char *)fx_text_make(t.p, t.n).p;
    char *start = s;
    while (isspace((unsigned char)*start)) start++;
    char *end;
    long long v = strtoll(start, &end, 10);
    while (isspace((unsigned char)*end)) end++;
    if (end != start && !*end) return (int64_t)v;
    return (int64_t)trunc(fx_number(t, line));
}

static struct timespec fx_t0;

static double fx_clock(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)(t.tv_sec - fx_t0.tv_sec) + (double)(t.tv_nsec - fx_t0.tv_nsec) * 1e-9;
}

/* ----------------------------------------------------------- start up */

static void fx_crash(int sig) {
    (void)sig;
    static const char msg[] =
        "\nProblem while running the program: it crashed.\n\n"
        "This almost always means a function kept calling itself and never stopped.\n\n"
        "How to fix it:\n"
        "  Every function that calls itself needs an `if` with a `return` that doesn't call\n"
        "  itself, so it knows when to stop:\n\n"
        "      function count_down(n)\n"
        "          if n = 0\n"
        "              return 0\n"
        "          end\n"
        "          return count_down(n - 1)\n"
        "      end\n";
    ssize_t ignored = write(2, msg, sizeof msg - 1);
    (void)ignored;
    _exit(1);
}

static void fx_init(const char *file, const char *const *lines, int nlines) {
    fx_file = file;
    fx_lines = lines;
    fx_nlines = nlines;
    clock_gettime(CLOCK_MONOTONIC, &fx_t0);
    fx_rng_state ^= (uint64_t)fx_t0.tv_nsec * 0x9E3779B97F4A7C15ull
                  ^ (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32);
    if (!fx_rng_state) fx_rng_state = 1;
    for (int i = 0; i < 256; i++) { fx_chars[2 * i] = (char)i; fx_chars[2 * i + 1] = 0; }
    static char alt_stack[1 << 16];
    stack_t ss;
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof alt_stack;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fx_crash;
    sa.sa_flags = SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

/* ============================== end of runtime ============================= */
