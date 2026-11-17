/*
 * lkpi_string.c - strings, number parsing and printf for the Linux-API shim.
 * Freestanding; identical on every target.
 */
#include <lkpi.h>

void *lkpi_memchr(const void *s, int c, size_t n)
{
    const u8 *p = s;
    for (; n; n--, p++)
        if (*p == (u8)c) return (void *)p;
    return NULL;
}

size_t lkpi_strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

size_t lkpi_strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

int lkpi_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}

int lkpi_strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (int)(u8)*a - (int)(u8)*b;
        if (!*a) break;
    }
    return 0;
}

int lkpi_strcasecmp(const char *a, const char *b)
{
    while (*a && tolower((u8)*a) == tolower((u8)*b)) { a++; b++; }
    return tolower((u8)*a) - tolower((u8)*b);
}

int lkpi_strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int ca = tolower((u8)*a), cb = tolower((u8)*b);
        if (ca != cb) return ca - cb;
        if (!ca) break;
    }
    return 0;
}

char *lkpi_strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

/* C semantics: pads with NULs, and does not terminate when src is too long. */
char *lkpi_strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

size_t lkpi_strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    for (; s[n]; n++)
        if (lkpi_strchr(reject, s[n])) break;
    return n;
}

/* Always terminates; returns the length copied or -E2BIG on truncation. */
ssize_t lkpi_strscpy(char *dst, const char *src, size_t size)
{
    size_t i;
    if (!size) return -E2BIG;
    for (i = 0; i < size - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
    return src[i] ? -E2BIG : (ssize_t)i;
}

static int digit_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    return 99;
}

/* Parses an unsigned number. Returns digits consumed; sets *overflow. */
static size_t parse_ull(const char *s, unsigned int base, unsigned long long *out,
                        bool *overflow)
{
    size_t skipped = 0, n = 0;
    unsigned long long v = 0;
    *overflow = false;
    if (base == 0) {
        if (s[0] == '0' && tolower(s[1]) == 'x' && digit_value(s[2]) < 16) { base = 16; skipped = 2; }
        else if (s[0] == '0') base = 8;
        else base = 10;
    } else if (base == 16 && s[0] == '0' && tolower(s[1]) == 'x' && digit_value(s[2]) < 16) {
        skipped = 2;
    }
    s += skipped;
    while (digit_value(s[n]) < (int)base) {
        unsigned long long next = v * base + (unsigned int)digit_value(s[n]);
        if (v > (~0ULL - (unsigned int)digit_value(s[n])) / base) *overflow = true;
        v = next;
        n++;
    }
    *out = v;
    return n ? n + skipped : 0;
}

unsigned long lkpi_simple_strtoul(const char *s, char **end, unsigned int base)
{
    unsigned long long v;
    bool overflow;
    size_t n = parse_ull(s, base, &v, &overflow);
    if (end) *end = (char *)s + n;
    return (unsigned long)v;
}

long lkpi_simple_strtol(const char *s, char **end, unsigned int base)
{
    if (*s == '-') return -(long)lkpi_simple_strtoul(s + 1, end, base);
    return (long)lkpi_simple_strtoul(s, end, base);
}

/* Whole-string conversion: optional sign, digits, optional single newline. */
int lkpi_kstrtol(const char *s, unsigned int base, long *res)
{
    unsigned long long v;
    bool overflow, negative = false;
    size_t n;
    if (*s == '-') { negative = true; s++; }
    else if (*s == '+') s++;
    n = parse_ull(s, base, &v, &overflow);
    if (!n) return -EINVAL;
    s += n;
    if (*s == '\n') s++;
    if (*s) return -EINVAL;
    if (overflow) return -ERANGE;
    if (negative) {
        if (v > (unsigned long long)LONG_MAX + 1) return -ERANGE;
        *res = (long)(0 - v);
    } else {
        if (v > (unsigned long long)LONG_MAX) return -ERANGE;
        *res = (long)v;
    }
    return 0;
}

void *kmemdup(const void *src, size_t len, gfp_t gfp)
{
    void *p = kmalloc(len, gfp);
    if (p) memcpy(p, src, len);
    return p;
}

char *kstrndup(const char *s, size_t max, gfp_t gfp)
{
    size_t len;
    char *p;
    if (!s) return NULL;
    len = lkpi_strnlen(s, max);
    p = kmalloc(len + 1, gfp);
    if (p) { memcpy(p, s, len); p[len] = '\0'; }
    return p;
}

char *kstrdup(const char *s, gfp_t gfp)
{
    return s ? kstrndup(s, lkpi_strlen(s), gfp) : NULL;
}

/* ---------------------------------------------------------------- printf */
struct out { char *buf; size_t size, pos; };

static void put(struct out *o, char c)
{
    if (o->pos + 1 < o->size) o->buf[o->pos] = c;
    o->pos++;
}

static void pad(struct out *o, int n, char c) { while (n-- > 0) put(o, c); }

#define F_LEFT 1
#define F_ZERO 2
#define F_PLUS 4
#define F_SPACE 8
#define F_ALT 16
#define F_UPPER 32
#define F_SIGNED 64

static void number(struct out *o, unsigned long long v, bool negative, unsigned int base,
                   int flags, int width, int precision)
{
    char digits[24];
    const char *set = (flags & F_UPPER) ? "0123456789ABCDEF" : "0123456789abcdef";
    char sign = 0;
    const char *prefix = "";
    int n = 0, zeros, total;
    if (negative) sign = '-';
    else if (flags & F_SIGNED) {
        if (flags & F_PLUS) sign = '+';
        else if (flags & F_SPACE) sign = ' ';
    }
    if ((flags & F_ALT) && base == 16 && v) prefix = (flags & F_UPPER) ? "0X" : "0x";
    if (!v && precision != 0) digits[n++] = '0';
    while (v) { digits[n++] = set[v % base]; v /= base; }
    if ((flags & F_ALT) && base == 8 && (n == 0 || digits[n - 1] != '0')) digits[n++] = '0';
    zeros = precision > n ? precision - n : 0;
    total = n + zeros + (sign ? 1 : 0) + (int)lkpi_strlen(prefix);
    /* An explicit precision disables zero padding, as in C. */
    if ((flags & F_ZERO) && !(flags & F_LEFT) && precision < 0 && width > total) {
        zeros += width - total;
        total = width;
    }
    if (!(flags & F_LEFT)) pad(o, width - total, ' ');
    if (sign) put(o, sign);
    while (*prefix) put(o, *prefix++);
    pad(o, zeros, '0');
    while (n) put(o, digits[--n]);
    if (flags & F_LEFT) pad(o, width - total, ' ');
}

static void string(struct out *o, const char *s, int flags, int width, int precision)
{
    int len;
    if (!s) s = "(null)";
    len = (int)(precision >= 0 ? lkpi_strnlen(s, (size_t)precision) : lkpi_strlen(s));
    if (!(flags & F_LEFT)) pad(o, width - len, ' ');
    for (int i = 0; i < len; i++) put(o, s[i]);
    if (flags & F_LEFT) pad(o, width - len, ' ');
}

/*
 * Supports the conversions nvkm uses: d i u x X o c s p %, flags - 0 + space
 * #, width and precision (including *), and length modifiers hh h l ll z t.
 * Kernel pointer extensions: %pa/%pad print the pointed-to address value and
 * %pe prints an error pointer's number; any other %p suffix letters are
 * consumed and the plain pointer is printed.
 */
int lkpi_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct out o = { buf, size, 0 };
    for (; *fmt; fmt++) {
        int flags = 0, width = -1, precision = -1, length = 0;
        if (*fmt != '%') { put(&o, *fmt); continue; }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-') flags |= F_LEFT;
            else if (*fmt == '0') flags |= F_ZERO;
            else if (*fmt == '+') flags |= F_PLUS;
            else if (*fmt == ' ') flags |= F_SPACE;
            else if (*fmt == '#') flags |= F_ALT;
            else break;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) { flags |= F_LEFT; width = -width; }
            fmt++;
        } else if (*fmt >= '0' && *fmt <= '9') {
            width = 0;
            while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            if (*fmt == '*') { precision = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') precision = precision * 10 + (*fmt++ - '0');
        }
        if (*fmt == 'h') { length = -1; fmt++; if (*fmt == 'h') { length = -2; fmt++; } }
        else if (*fmt == 'l') { length = 1; fmt++; if (*fmt == 'l') { length = 2; fmt++; } }
        else if (*fmt == 'z' || *fmt == 't') { length = 3; fmt++; }

        switch (*fmt) {
        case 'd': case 'i': {
            long long v;
            if (length == 2) v = va_arg(ap, long long);
            else if (length == 1) v = va_arg(ap, long);
            else if (length == 3) v = va_arg(ap, ssize_t);
            else v = va_arg(ap, int);
            if (length == -1) v = (short)v;
            else if (length == -2) v = (signed char)v;
            number(&o, v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v, v < 0,
                   10, flags | F_SIGNED, width, precision);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long long v;
            if (length == 2) v = va_arg(ap, unsigned long long);
            else if (length == 1) v = va_arg(ap, unsigned long);
            else if (length == 3) v = va_arg(ap, size_t);
            else v = va_arg(ap, unsigned int);
            if (length == -1) v = (unsigned short)v;
            else if (length == -2) v = (unsigned char)v;
            if (*fmt == 'X') flags |= F_UPPER;
            number(&o, v, false, *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16, flags, width, precision);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (!(flags & F_LEFT)) pad(&o, width - 1, ' ');
            put(&o, c);
            if (flags & F_LEFT) pad(&o, width - 1, ' ');
            break;
        }
        case 's':
            string(&o, va_arg(ap, const char *), flags, width, precision);
            break;
        case 'p': {
            const void *p = va_arg(ap, const void *);
            if (fmt[1] == 'a') {            /* %pa, %pad, %pap: address by reference */
                fmt++;
                if (fmt[1] == 'd' || fmt[1] == 'p') fmt++;
                number(&o, p ? *(const u64 *)p : 0, false, 16, flags | F_ALT | F_ZERO,
                       18, -1);
            } else if (fmt[1] == 'e') {     /* %pe: error pointer */
                fmt++;
                if (IS_ERR(p)) number(&o, (unsigned long long)-PTR_ERR(p), true, 10, 0, width, -1);
                else number(&o, (uintptr_t)p, false, 16, F_ALT, width, -1);
            } else {
                while ((fmt[1] >= 'a' && fmt[1] <= 'z') || (fmt[1] >= 'A' && fmt[1] <= 'Z')) fmt++;
                number(&o, (uintptr_t)p, false, 16, flags | F_ZERO,
                       width >= 0 ? width : (int)(2 * sizeof(void *)), -1);
            }
            break;
        }
        case '%':
            put(&o, '%');
            break;
        case '\0':
            fmt--;                          /* dangling '%': stop cleanly */
            break;
        default:                            /* unknown: emit verbatim */
            put(&o, '%');
            put(&o, *fmt);
            break;
        }
    }
    if (size) buf[o.pos < size ? o.pos : size - 1] = '\0';
    return (int)o.pos;
}

/* The kernel's snprintf family returns the would-be length like C99; nvkm
 * only uses the result as "characters written" with buffers that fit. */
int lkpi_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = lkpi_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int lkpi_sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = lkpi_vsnprintf(buf, (size_t)INT_MAX, fmt, ap);
    va_end(ap);
    return n;
}

/* ---------------------------------------------------------------- logging */
static void emit(int level, const char *prefix, const char *fmt, va_list ap)
{
    char line[512];
    int n = 0;
    if (prefix) n = lkpi_snprintf(line, sizeof(line), "%s: ", prefix);
    if (n < 0 || n >= (int)sizeof(line)) n = 0;
    lkpi_vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
    lkpi_log(level, line);
}

void lkpi_printk(int level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(level, NULL, fmt, ap);
    va_end(ap);
}

/* printk(): an optional "\001<digit>" prefix selects the level. */
void lkpi_printk_raw(const char *fmt, ...)
{
    va_list ap;
    int level = 6;
    if (fmt[0] == '\001' && fmt[1]) {
        if (fmt[1] >= '0' && fmt[1] <= '7') level = fmt[1] - '0';
        fmt += 2;
    }
    va_start(ap, fmt);
    emit(level, NULL, fmt, ap);
    va_end(ap);
}

void lkpi_dev_printk(int level, const struct device *dev, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(level, dev_name(dev), fmt, ap);
    va_end(ap);
}

void lkpi_warn(const char *file, int line, const char *what)
{
    lkpi_printk(4, "WARNING: %s at %s:%d\n", what, file, line);
}

void lkpi_bug(const char *file, int line, const char *what)
{
    char text[256];
    lkpi_snprintf(text, sizeof(text), "BUG: %s at %s:%d", what, file, line);
    lkpi_panic(text);
}
