/*
 * lkpi.h - the slice of the Linux kernel API that nouveau's nvkm core uses,
 * for building nvkm unmodified outside Linux (host test harness, Windows
 * XP video miniport, Windows 98 VxD).
 *
 * Written from scratch for this project (MIT). No Linux source is copied:
 * the data structures follow the documented kernel API, not its code.
 *
 * Every <linux/...> header nvkm includes forwards here. Translation units
 * that include this are built freestanding, so the only system headers used
 * are the compiler's own. Everything OS-specific is behind the lkpi_*
 * functions declared in the "backend" section and implemented once per OS.
 *
 * LLP64 note: on Windows x64 'long' is 32 bits. Nothing in this file stores
 * a pointer in a long; pointer/errno conversions go through intptr_t.
 */
#ifndef LKPI_H
#define LKPI_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ types */
typedef uint8_t  u8;   typedef int8_t  s8;
typedef uint16_t u16;  typedef int16_t s16;
typedef uint32_t u32;  typedef int32_t s32;
typedef unsigned long long u64;  typedef long long s64;   /* as the kernel: %llx */
typedef u8 __u8;   typedef s8 __s8;
typedef u16 __u16; typedef s16 __s16;
typedef u32 __u32; typedef s32 __s32;
typedef u64 __u64; typedef s64 __s64;
typedef u16 __le16; typedef u32 __le32; typedef u64 __le64;
typedef u16 __be16; typedef u32 __be32; typedef u64 __be64;
typedef u64 dma_addr_t;
typedef u64 phys_addr_t;
typedef u64 resource_size_t;
typedef unsigned int gfp_t;
typedef s64 loff_t;
typedef s64 ktime_t;
typedef intptr_t ssize_t;
typedef unsigned int fmode_t;
typedef uintptr_t pgprot_t;

#define __iomem
#define __user
#define __force
#define __must_check __attribute__((warn_unused_result))
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __printf(a, b) __attribute__((format(gnu_printf, a, b)))
#define __maybe_unused __attribute__((unused))
#define __always_unused __attribute__((unused))
#define __always_inline inline __attribute__((always_inline))
#define noinline __attribute__((noinline))
#define __init
#define __exit
#define __pure __attribute__((pure))
#define fallthrough __attribute__((fallthrough))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define __stringify_1(x...) #x
#define __stringify(x...) __stringify_1(x)
#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#define __LITTLE_ENDIAN 1234
/* IS_ENABLED(CONFIG_X) is 1 when CONFIG_X is defined to 1 and 0 when it is
 * not defined at all, usable in both #if and C expressions. */
#define __LKPI_PLACEHOLDER_1 0,
#define __lkpi_second(ignored, val, ...) val
#define __lkpi_defined2(arg1_or_junk) __lkpi_second(arg1_or_junk 1, 0)
#define __lkpi_defined1(val) __lkpi_defined2(__LKPI_PLACEHOLDER_##val)
#define IS_ENABLED(option) __lkpi_defined1(option)
#define IS_REACHABLE(option) __lkpi_defined1(option)
#define __acquires(x)
#define __releases(x)
#define __must_hold(x)
#define INT_MAX 0x7fffffff
#define INT_MIN (-INT_MAX - 1)
#define UINT_MAX 0xffffffffU
#define LONG_MAX ((long)(~0UL >> 1))
#define ULONG_MAX (~0UL)
#define U8_MAX 0xffU
#define U16_MAX 0xffffU
#define U32_MAX 0xffffffffU
#define S32_MAX 0x7fffffff
#define U64_MAX 0xffffffffffffffffULL
#define S64_MAX 0x7fffffffffffffffLL

/* Kernel configuration as nvkm sees it. Anything unlisted is off. */
#define CONFIG_X86 1
#define CONFIG_NOUVEAU_DEBUG 5
#define CONFIG_NOUVEAU_DEBUG_DEFAULT 3
#define CONFIG_NOUVEAU_I2C_INTERNAL 1           /* nvkm's own bit-banging */
#define CONFIG_NOUVEAU_I2C_INTERNAL_DEFAULT 1

/* ----------------------------------------------------------------- errno */
#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define ENODEV 19
#define EINVAL 22
#define ENOTTY 25
#define ENOSPC 28
#define ERANGE 34
#define EDEADLK 35
#define ENOSYS 38
#define ENODATA 61
#define EPROTO 71
#define EOVERFLOW 75
#define EILSEQ 84
#define EOPNOTSUPP 95
#define ENOBUFS 105
#define ENOTCONN 107
#define ETIMEDOUT 110
#define EREMOTEIO 121
#define ERESTARTSYS 512
#define EPROBE_DEFER 517
#define ENOTSUPP 524
#define MAX_ERRNO 4095

/* errno values fit in a long on every target; the pointer side always goes
 * through intptr_t so the value is sign-extended, never truncated. */
static inline void *ERR_PTR(long error) { return (void *)(intptr_t)error; }
static inline long PTR_ERR(const void *ptr) { return (long)(intptr_t)ptr; }
static inline bool IS_ERR(const void *ptr)
{ return (uintptr_t)ptr >= (uintptr_t)-MAX_ERRNO; }
static inline bool IS_ERR_OR_NULL(const void *ptr) { return !ptr || IS_ERR(ptr); }
static inline void *ERR_CAST(const void *ptr) { return (void *)ptr; }
static inline int PTR_ERR_OR_ZERO(const void *ptr)
{ return IS_ERR(ptr) ? (int)PTR_ERR(ptr) : 0; }

/* ------------------------------------------------------------ primitives */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#define sizeof_field(t, f) sizeof(((t *)0)->f)
#define struct_size(p, member, n) \
    (sizeof(*(p)) + sizeof((p)->member[0]) * (size_t)(n))
/* Saturating size arithmetic: an overflowed size allocates nothing. */
static inline size_t array_size(size_t a, size_t b)
{ size_t r; return __builtin_mul_overflow(a, b, &r) ? SIZE_MAX : r; }
static inline size_t array3_size(size_t a, size_t b, size_t c)
{ return array_size(array_size(a, b), c); }
#define BITS_PER_BYTE 8
#define BITS_PER_LONG ((int)(sizeof(long) * 8))
#define BITS_PER_LONG_LONG 64
#define BIT(n) (1UL << (n))
#define BIT_ULL(n) (1ULL << (n))
#define BITS_TO_LONGS(n) (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define DECLARE_BITMAP(name, bits) unsigned long name[BITS_TO_LONGS(bits)]
#define GENMASK(h, l) \
    (((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK_ULL(h, l) \
    (((~0ULL) << (l)) & (~0ULL >> (63 - (h))))
#define upper_32_bits(n) ((u32)(((u64)(n)) >> 32))
#define lower_32_bits(n) ((u32)((u64)(n) & 0xffffffffU))
#define min(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a < _b ? _a : _b; })
#define max(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a > _b ? _a : _b; })
#define min_t(t, a, b) ({ t _a = (t)(a); t _b = (t)(b); _a < _b ? _a : _b; })
#define max_t(t, a, b) ({ t _a = (t)(a); t _b = (t)(b); _a > _b ? _a : _b; })
#define min3(a, b, c) min(min(a, b), c)
#define max3(a, b, c) max(max(a, b), c)
#define clamp(v, lo, hi) min(max(v, lo), hi)
#define clamp_t(t, v, lo, hi) min_t(t, max_t(t, v, lo), hi)
#define swap(a, b) do { typeof(a) _t = (a); (a) = (b); (b) = _t; } while (0)
#define abs(x) ({ typeof(x) _x = (x); _x < 0 ? -_x : _x; })
#define ALIGN(x, a) (((x) + ((typeof(x))(a) - 1)) & ~((typeof(x))(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((typeof(x))(a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((typeof(x))(a) - 1)) == 0)
#define PTR_ALIGN(p, a) ((typeof(p))ALIGN((uintptr_t)(p), (a)))
#define round_up(x, y) ((((x) - 1) | ((typeof(x))(y) - 1)) + 1)
#define round_down(x, y) ((x) & ~((typeof(x))(y) - 1))
#define roundup(x, y) ({ typeof(y) _y = (y); (((x) + (_y - 1)) / _y) * _y; })
#define rounddown(x, y) ({ typeof(x) _x = (x); _x - (_x % (y)); })
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_UP_ULL(n, d) DIV_ROUND_UP((unsigned long long)(n), (unsigned long long)(d))
#define DIV_ROUND_CLOSEST(x, d) ({ typeof(x) _x = (x); typeof(d) _d = (d); \
    (((typeof(x))-1) > 0 || ((typeof(d))-1) > 0 || ((_x > 0) == (_d > 0))) ? \
        ((_x + (_d / 2)) / _d) : ((_x - (_d / 2)) / _d); })
#define DIV_ROUND_CLOSEST_ULL(x, d) (((unsigned long long)(x) + (d) / 2) / (d))
#define do_div(n, base) ({ u32 _base = (base); u32 _rem = (u32)((u64)(n) % _base); \
    (n) = (u64)(n) / _base; _rem; })
static inline u64 div_u64(u64 dividend, u32 divisor) { return dividend / divisor; }
static inline s64 div_s64(s64 dividend, s32 divisor) { return dividend / divisor; }
static inline u64 div64_u64(u64 dividend, u64 divisor) { return dividend / divisor; }
static inline s64 div64_s64(s64 dividend, s64 divisor) { return dividend / divisor; }
static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *rem)
{ *rem = (u32)(dividend % divisor); return dividend / divisor; }

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define PAGE_ALIGN(x) ALIGN(x, PAGE_SIZE)
#define PAGE_KERNEL ((pgprot_t)0)
#define offset_in_page(p) ((uintptr_t)(p) & ~PAGE_MASK)

#define BUILD_BUG_ON(cond) _Static_assert(!(cond), "BUILD_BUG_ON: " #cond)
#define BUILD_BUG_ON_ZERO(e) ((int)(sizeof(struct { int : (-!!(e)); })))
#define static_assert(expr, ...) _Static_assert(expr, #expr)

/* -------------------------------------------------------------- bit ops */
static inline int ffs(int x) { return __builtin_ffs(x); }
static inline int fls(unsigned int x) { return x ? 32 - __builtin_clz(x) : 0; }
static inline int fls64(u64 x) { return x ? 64 - __builtin_clzll(x) : 0; }
static inline unsigned long __ffs(unsigned long x) { return (unsigned long)__builtin_ctzl(x); }
static inline unsigned long __fls(unsigned long x)
{ return (unsigned long)(BITS_PER_LONG - 1 - __builtin_clzl(x)); }
static inline unsigned long __ffs64(u64 x) { return (unsigned long)__builtin_ctzll(x); }
static inline unsigned long ffz(unsigned long x) { return __ffs(~x); }
static inline unsigned int hweight8(unsigned int x) { return (unsigned int)__builtin_popcount(x & 0xff); }
static inline unsigned int hweight16(unsigned int x) { return (unsigned int)__builtin_popcount(x & 0xffff); }
static inline unsigned int hweight32(unsigned int x) { return (unsigned int)__builtin_popcount(x); }
static inline unsigned int hweight64(u64 x) { return (unsigned int)__builtin_popcountll(x); }
#define ilog2(n) ((int)(sizeof(n) <= 4 ? fls((unsigned int)(n)) - 1 : fls64((u64)(n)) - 1))
static inline bool is_power_of_2(u64 n) { return n && !(n & (n - 1)); }
static inline int order_base_2(u64 n) { return n > 1 ? fls64(n - 1) : 0; }
static inline u64 roundup_pow_of_two(u64 n) { return 1ULL << order_base_2(n); }
static inline u64 rounddown_pow_of_two(u64 n) { return 1ULL << (fls64(n) - 1); }

static inline bool test_bit(unsigned int nr, const unsigned long *addr)
{ return (addr[nr / BITS_PER_LONG] >> (nr % BITS_PER_LONG)) & 1; }
static inline void __set_bit(unsigned int nr, unsigned long *addr)
{ addr[nr / BITS_PER_LONG] |= 1UL << (nr % BITS_PER_LONG); }
static inline void __clear_bit(unsigned int nr, unsigned long *addr)
{ addr[nr / BITS_PER_LONG] &= ~(1UL << (nr % BITS_PER_LONG)); }
/* The atomic forms are atomic with respect to other bit operations on the
 * same word, as the kernel guarantees. */
static inline void set_bit(unsigned int nr, unsigned long *addr)
{ __atomic_fetch_or(&addr[nr / BITS_PER_LONG], 1UL << (nr % BITS_PER_LONG), __ATOMIC_SEQ_CST); }
static inline void clear_bit(unsigned int nr, unsigned long *addr)
{ __atomic_fetch_and(&addr[nr / BITS_PER_LONG], ~(1UL << (nr % BITS_PER_LONG)), __ATOMIC_SEQ_CST); }
static inline bool test_and_set_bit(unsigned int nr, unsigned long *addr)
{
    unsigned long mask = 1UL << (nr % BITS_PER_LONG);
    return (__atomic_fetch_or(&addr[nr / BITS_PER_LONG], mask, __ATOMIC_SEQ_CST) & mask) != 0;
}
static inline bool test_and_clear_bit(unsigned int nr, unsigned long *addr)
{
    unsigned long mask = 1UL << (nr % BITS_PER_LONG);
    return (__atomic_fetch_and(&addr[nr / BITS_PER_LONG], ~mask, __ATOMIC_SEQ_CST) & mask) != 0;
}
static inline unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
                                          unsigned long offset)
{
    for (; offset < size; offset++)
        if (test_bit((unsigned int)offset, addr)) return offset;
    return size;
}
static inline unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
                                               unsigned long offset)
{
    for (; offset < size; offset++)
        if (!test_bit((unsigned int)offset, addr)) return offset;
    return size;
}
#define find_first_bit(addr, size) find_next_bit(addr, size, 0)
#define find_first_zero_bit(addr, size) find_next_zero_bit(addr, size, 0)
#define for_each_set_bit(bit, addr, size) \
    for ((bit) = find_first_bit(addr, size); (bit) < (size); \
         (bit) = find_next_bit(addr, size, (bit) + 1))
#define for_each_clear_bit(bit, addr, size) \
    for ((bit) = find_first_zero_bit(addr, size); (bit) < (size); \
         (bit) = find_next_zero_bit(addr, size, (bit) + 1))

/* ------------------------------------------------------------ byte order */
/* Every supported target is little-endian x86. */
#define le16_to_cpu(x) ((u16)(x))
#define le32_to_cpu(x) ((u32)(x))
#define le64_to_cpu(x) ((u64)(x))
#define cpu_to_le16(x) ((__le16)(x))
#define cpu_to_le32(x) ((__le32)(x))
#define cpu_to_le64(x) ((__le64)(x))
#define be16_to_cpu(x) __builtin_bswap16(x)
#define be32_to_cpu(x) __builtin_bswap32(x)
#define cpu_to_be16(x) __builtin_bswap16(x)
#define cpu_to_be32(x) __builtin_bswap32(x)
#define swab16(x) __builtin_bswap16(x)
#define swab32(x) __builtin_bswap32(x)
static inline u16 get_unaligned_le16(const void *p)
{ const u8 *b = p; return (u16)(b[0] | (b[1] << 8)); }
static inline u32 get_unaligned_le32(const void *p)
{ const u8 *b = p; return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24); }

/* ----------------------------------------------------- strings and print */
/* The compiler emits calls to these four on its own, so they keep their
 * standard names and come from libc or the kernel (or the backend). */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
/* Everything else is the shim's own (lkpi_string.c), so behaviour is the
 * same on every target and nothing depends on a particular C runtime. */
void *lkpi_memchr(const void *s, int c, size_t n);
size_t lkpi_strlen(const char *s);
size_t lkpi_strnlen(const char *s, size_t max);
int lkpi_strcmp(const char *a, const char *b);
int lkpi_strncmp(const char *a, const char *b, size_t n);
int lkpi_strcasecmp(const char *a, const char *b);
int lkpi_strncasecmp(const char *a, const char *b, size_t n);
char *lkpi_strchr(const char *s, int c);
char *lkpi_strncpy(char *dst, const char *src, size_t n);
size_t lkpi_strcspn(const char *s, const char *reject);
ssize_t lkpi_strscpy(char *dst, const char *src, size_t size);
int lkpi_kstrtol(const char *s, unsigned int base, long *res);
long lkpi_simple_strtol(const char *s, char **end, unsigned int base);
unsigned long lkpi_simple_strtoul(const char *s, char **end, unsigned int base);
#define memchr lkpi_memchr
#define strlen lkpi_strlen
#define strnlen lkpi_strnlen
#define strcmp lkpi_strcmp
#define strncmp lkpi_strncmp
#define strcasecmp lkpi_strcasecmp
#define strncasecmp lkpi_strncasecmp
#define strchr lkpi_strchr
#define strncpy lkpi_strncpy
#define strcspn lkpi_strcspn
#define strscpy lkpi_strscpy
#define kstrtol lkpi_kstrtol
#define simple_strtol lkpi_simple_strtol
#define simple_strtoul lkpi_simple_strtoul
static inline int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c; }
static inline int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c; }
static inline const char *str_yes_no(bool v) { return v ? "yes" : "no"; }
static inline const char *str_enabled_disabled(bool v) { return v ? "enabled" : "disabled"; }
static inline const char *str_on_off(bool v) { return v ? "on" : "off"; }
int lkpi_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int lkpi_snprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);
int lkpi_sprintf(char *buf, const char *fmt, ...) __printf(2, 3);
#define vsnprintf lkpi_vsnprintf
#define snprintf lkpi_snprintf
#define scnprintf lkpi_snprintf
#define sprintf lkpi_sprintf

#define KERN_SOH "\001"
#define KERN_EMERG KERN_SOH "0"
#define KERN_ALERT KERN_SOH "1"
#define KERN_CRIT KERN_SOH "2"
#define KERN_ERR KERN_SOH "3"
#define KERN_WARNING KERN_SOH "4"
#define KERN_NOTICE KERN_SOH "5"
#define KERN_INFO KERN_SOH "6"
#define KERN_DEBUG KERN_SOH "7"
#define KERN_CONT KERN_SOH "c"
/* 'level' is the syslog severity digit (3 = error ... 7 = debug). */
void lkpi_printk(int level, const char *fmt, ...) __printf(2, 3);
void lkpi_printk_raw(const char *fmt, ...) __printf(1, 2); /* KERN_ prefix in fmt */
#define printk(fmt, ...) lkpi_printk_raw(fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...) lkpi_printk(3, fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...) lkpi_printk(4, fmt, ##__VA_ARGS__)
#define pr_notice(fmt, ...) lkpi_printk(5, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) lkpi_printk(6, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...) lkpi_printk(7, fmt, ##__VA_ARGS__)
#define pr_cont(fmt, ...) lkpi_printk_raw(KERN_CONT fmt, ##__VA_ARGS__)

void lkpi_warn(const char *file, int line, const char *what);
#define WARN_ON(cond) ({ bool _w = !!(cond); \
    if (unlikely(_w)) lkpi_warn(__FILE__, __LINE__, #cond); _w; })
#define WARN_ON_ONCE(cond) WARN_ON(cond)
#define WARN(cond, fmt, ...) ({ bool _w = !!(cond); \
    if (unlikely(_w)) { lkpi_warn(__FILE__, __LINE__, #cond); lkpi_printk(4, fmt, ##__VA_ARGS__); } _w; })
#define WARN_ONCE(cond, fmt, ...) WARN(cond, fmt, ##__VA_ARGS__)
void lkpi_bug(const char *file, int line, const char *what) __attribute__((noreturn));
#define BUG() lkpi_bug(__FILE__, __LINE__, "BUG")
#define BUG_ON(cond) do { if (unlikely(cond)) lkpi_bug(__FILE__, __LINE__, #cond); } while (0)
#define might_sleep() do { } while (0)
#define dump_stack() do { } while (0)

/* ------------------------------------------------------------------ lists */
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)
static inline void INIT_LIST_HEAD(struct list_head *l) { l->next = l; l->prev = l; }
static inline void __list_add(struct list_head *n, struct list_head *prev, struct list_head *next)
{ next->prev = n; n->next = next; n->prev = prev; prev->next = n; }
static inline void list_add(struct list_head *n, struct list_head *head)
{ __list_add(n, head, head->next); }
static inline void list_add_tail(struct list_head *n, struct list_head *head)
{ __list_add(n, head->prev, head); }
static inline void __list_del_entry(struct list_head *e)
{ e->next->prev = e->prev; e->prev->next = e->next; }
static inline void list_del(struct list_head *e)
{ __list_del_entry(e); e->next = NULL; e->prev = NULL; }
static inline void list_del_init(struct list_head *e)
{ __list_del_entry(e); INIT_LIST_HEAD(e); }
static inline void list_move(struct list_head *e, struct list_head *head)
{ __list_del_entry(e); list_add(e, head); }
static inline void list_move_tail(struct list_head *e, struct list_head *head)
{ __list_del_entry(e); list_add_tail(e, head); }
static inline bool list_empty(const struct list_head *head) { return head->next == head; }
static inline bool list_is_last(const struct list_head *e, const struct list_head *head)
{ return e->next == head; }
static inline bool list_is_first(const struct list_head *e, const struct list_head *head)
{ return e->prev == head; }
static inline bool list_is_singular(const struct list_head *head)
{ return !list_empty(head) && head->next == head->prev; }
static inline void list_splice_init(struct list_head *list, struct list_head *head)
{
    if (!list_empty(list)) {
        struct list_head *first = list->next, *last = list->prev, *at = head->next;
        first->prev = head; head->next = first;
        last->next = at; at->prev = last;
        INIT_LIST_HEAD(list);
    }
}
static inline void list_rotate_left(struct list_head *head)
{ if (!list_empty(head)) list_move_tail(head->next, head); }
#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(ptr, type, member) list_entry((ptr)->next, type, member)
#define list_last_entry(ptr, type, member) list_entry((ptr)->prev, type, member)
#define list_first_entry_or_null(ptr, type, member) \
    (list_empty(ptr) ? NULL : list_first_entry(ptr, type, member))
#define list_next_entry(pos, member) list_entry((pos)->member.next, typeof(*(pos)), member)
#define list_prev_entry(pos, member) list_entry((pos)->member.prev, typeof(*(pos)), member)
#define list_entry_is_head(pos, head, member) (&(pos)->member == (head))
#define list_for_each(pos, head) for (pos = (head)->next; pos != (head); pos = pos->next)
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)
#define list_for_each_entry(pos, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member); \
         !list_entry_is_head(pos, head, member); pos = list_next_entry(pos, member))
#define list_for_each_entry_reverse(pos, head, member) \
    for (pos = list_last_entry(head, typeof(*pos), member); \
         !list_entry_is_head(pos, head, member); pos = list_prev_entry(pos, member))
#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member), n = list_next_entry(pos, member); \
         !list_entry_is_head(pos, head, member); pos = n, n = list_next_entry(n, member))
#define list_for_each_entry_safe_reverse(pos, n, head, member) \
    for (pos = list_last_entry(head, typeof(*pos), member), n = list_prev_entry(pos, member); \
         !list_entry_is_head(pos, head, member); pos = n, n = list_prev_entry(n, member))
#define list_for_each_entry_from(pos, head, member) \
    for (; !list_entry_is_head(pos, head, member); pos = list_next_entry(pos, member))
#define list_for_each_entry_from_reverse(pos, head, member) \
    for (; !list_entry_is_head(pos, head, member); pos = list_prev_entry(pos, member))
#define list_for_each_entry_continue(pos, head, member) \
    for (pos = list_next_entry(pos, member); !list_entry_is_head(pos, head, member); \
         pos = list_next_entry(pos, member))
#define list_for_each_entry_continue_reverse(pos, head, member) \
    for (pos = list_prev_entry(pos, member); !list_entry_is_head(pos, head, member); \
         pos = list_prev_entry(pos, member))

/* --------------------------------------------------------- red-black tree */
struct rb_node {
    struct rb_node *rb_parent;      /* self-pointer marks "not in a tree" */
    struct rb_node *rb_left, *rb_right;
    int rb_red;
};
struct rb_root { struct rb_node *rb_node; };
#define RB_ROOT (struct rb_root) { NULL }
#define rb_entry(ptr, type, member) container_of(ptr, type, member)
#define RB_EMPTY_ROOT(root) ((root)->rb_node == NULL)
#define RB_EMPTY_NODE(node) ((node)->rb_parent == (node))
#define RB_CLEAR_NODE(node) ((node)->rb_parent = (node))
static inline void rb_link_node(struct rb_node *node, struct rb_node *parent, struct rb_node **link)
{
    node->rb_parent = parent; node->rb_left = NULL; node->rb_right = NULL; node->rb_red = 1;
    *link = node;
}
void rb_insert_color(struct rb_node *node, struct rb_root *root);
void rb_erase(struct rb_node *node, struct rb_root *root);
struct rb_node *rb_first(const struct rb_root *root);
struct rb_node *rb_last(const struct rb_root *root);
struct rb_node *rb_next(const struct rb_node *node);
struct rb_node *rb_prev(const struct rb_node *node);

/* ------------------------------------------------- atomics and refcounts */
typedef struct { int counter; } atomic_t;
#define ATOMIC_INIT(i) { (i) }
static inline int atomic_read(const atomic_t *v) { return __atomic_load_n(&v->counter, __ATOMIC_SEQ_CST); }
static inline void atomic_set(atomic_t *v, int i) { __atomic_store_n(&v->counter, i, __ATOMIC_SEQ_CST); }
static inline void atomic_inc(atomic_t *v) { __atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST); }
static inline void atomic_dec(atomic_t *v) { __atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST); }
static inline int atomic_inc_return(atomic_t *v) { return __atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST); }
static inline int atomic_dec_return(atomic_t *v) { return __atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST); }
static inline bool atomic_dec_and_test(atomic_t *v) { return atomic_dec_return(v) == 0; }
static inline int atomic_xchg(atomic_t *v, int n) { return __atomic_exchange_n(&v->counter, n, __ATOMIC_SEQ_CST); }
static inline int atomic_cmpxchg(atomic_t *v, int old, int n)
{ __atomic_compare_exchange_n(&v->counter, &old, n, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); return old; }
static inline int atomic_add_return(int i, atomic_t *v) { return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST); }

typedef struct { atomic_t refs; } refcount_t;
#define REFCOUNT_INIT(n) { ATOMIC_INIT(n) }
static inline void refcount_set(refcount_t *r, int n) { atomic_set(&r->refs, n); }
static inline unsigned int refcount_read(const refcount_t *r) { return (unsigned int)atomic_read(&r->refs); }
static inline void refcount_inc(refcount_t *r) { atomic_inc(&r->refs); }
static inline bool refcount_inc_not_zero(refcount_t *r)
{
    int old = atomic_read(&r->refs);
    while (old != 0) {
        int seen = atomic_cmpxchg(&r->refs, old, old + 1);
        if (seen == old) return true;
        old = seen;
    }
    return false;
}
static inline bool refcount_dec_and_test(refcount_t *r) { return atomic_dec_and_test(&r->refs); }

struct kref { refcount_t refcount; };
static inline void kref_init(struct kref *k) { refcount_set(&k->refcount, 1); }
static inline void kref_get(struct kref *k) { refcount_inc(&k->refcount); }
static inline int kref_put(struct kref *k, void (*release)(struct kref *))
{ if (refcount_dec_and_test(&k->refcount)) { release(k); return 1; } return 0; }
static inline unsigned int kref_read(const struct kref *k) { return refcount_read(&k->refcount); }

/* ----------------------------------------------------------- OS backend */
/* Storage for the locking objects is embedded so nvkm structures need no
 * extra allocation; each backend checks at build time that its native
 * object fits. All-zero is a valid, unlocked spinlock on every backend. */
typedef struct spinlock { uintptr_t opaque[2]; } spinlock_t;
struct mutex { uintptr_t ready; uintptr_t opaque[10]; };
struct lkpi_event { uintptr_t opaque[6]; };
struct lock_class_key { int unused; };

void lkpi_spin_lock(spinlock_t *lock);
void lkpi_spin_unlock(spinlock_t *lock);
unsigned long lkpi_spin_lock_irqsave(spinlock_t *lock);
void lkpi_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags);
void lkpi_mutex_init(struct mutex *m);
void lkpi_mutex_lock(struct mutex *m);          /* initialises a zeroed mutex on first use */
bool lkpi_mutex_trylock(struct mutex *m);
void lkpi_mutex_unlock(struct mutex *m);
bool lkpi_mutex_is_locked(struct mutex *m);
void lkpi_event_init(struct lkpi_event *e);
void lkpi_event_signal(struct lkpi_event *e);   /* wake every waiter */
/* Wait until signalled or 'timeout_ms' passes. Spurious returns allowed;
 * callers always re-test their condition. */
void lkpi_event_wait(struct lkpi_event *e, unsigned int timeout_ms);

void *lkpi_alloc(size_t size, bool zero);
void lkpi_free(void *p);
u64 lkpi_time_ns(void);                         /* monotonic */
void lkpi_udelay(unsigned long usec);           /* busy wait */
void lkpi_msleep(unsigned int msec);            /* may block */
void lkpi_log(int level, const char *text);     /* one formatted message */
void lkpi_panic(const char *text) __attribute__((noreturn));

#define DEFINE_SPINLOCK(name) spinlock_t name = { { 0, 0 } }
#define DEFINE_MUTEX(name) struct mutex name = { 0, { 0 } }
#define spin_lock_init(l) do { (l)->opaque[0] = 0; (l)->opaque[1] = 0; } while (0)
#define spin_lock(l) lkpi_spin_lock(l)
#define spin_unlock(l) lkpi_spin_unlock(l)
#define spin_lock_irq(l) lkpi_spin_lock(l)
#define spin_unlock_irq(l) lkpi_spin_unlock(l)
#define spin_lock_irqsave(l, flags) do { (flags) = lkpi_spin_lock_irqsave(l); } while (0)
#define spin_unlock_irqrestore(l, flags) lkpi_spin_unlock_irqrestore(l, flags)
#define assert_spin_locked(l) do { } while (0)
#define mutex_init(m) lkpi_mutex_init(m)
#define __mutex_init(m, name, key) lkpi_mutex_init(m)
#define mutex_destroy(m) do { } while (0)
#define mutex_lock(m) lkpi_mutex_lock(m)
#define mutex_lock_nested(m, subclass) lkpi_mutex_lock(m)
#define mutex_trylock(m) lkpi_mutex_trylock(m)
#define mutex_unlock(m) lkpi_mutex_unlock(m)
#define mutex_is_locked(m) lkpi_mutex_is_locked(m)
#define lockdep_assert_held(l) do { } while (0)
#define lockdep_set_class(l, key) do { } while (0)
static inline bool refcount_dec_and_mutex_lock(refcount_t *r, struct mutex *lock)
{
    /* Fast path: not the last reference. */
    int old = atomic_read(&r->refs);
    while (old > 1) {
        int seen = atomic_cmpxchg(&r->refs, old, old - 1);
        if (seen == old) return false;
        old = seen;
    }
    mutex_lock(lock);
    if (!refcount_dec_and_test(r)) { mutex_unlock(lock); return false; }
    return true;
}

/* time */
#define HZ 1000
#define NSEC_PER_SEC 1000000000LL
#define NSEC_PER_MSEC 1000000LL
#define NSEC_PER_USEC 1000LL
#define USEC_PER_SEC 1000000LL
#define jiffies ((unsigned long)(lkpi_time_ns() / NSEC_PER_MSEC))
#define msecs_to_jiffies(m) ((unsigned long)(m))
#define usecs_to_jiffies(u) ((unsigned long)(((u) + 999) / 1000))
#define jiffies_to_msecs(j) ((unsigned int)(j))
#define time_after(a, b) ((long)((b) - (a)) < 0)
#define time_before(a, b) time_after(b, a)
#define time_after_eq(a, b) ((long)((a) - (b)) >= 0)
#define time_before_eq(a, b) time_after_eq(b, a)
static inline ktime_t ktime_get(void) { return (ktime_t)lkpi_time_ns(); }
static inline ktime_t ktime_get_raw(void) { return (ktime_t)lkpi_time_ns(); }
static inline s64 ktime_to_ns(ktime_t t) { return t; }
static inline s64 ktime_to_us(ktime_t t) { return t / NSEC_PER_USEC; }
static inline s64 ktime_to_ms(ktime_t t) { return t / NSEC_PER_MSEC; }
static inline ktime_t ktime_sub(ktime_t a, ktime_t b) { return a - b; }
static inline ktime_t ktime_add_ns(ktime_t a, u64 ns) { return a + (s64)ns; }
#define udelay(us) lkpi_udelay(us)
#define mdelay(ms) lkpi_udelay((unsigned long)(ms) * 1000UL)
#define msleep(ms) lkpi_msleep(ms)
#define usleep_range(lo, hi) lkpi_udelay(lo)
#define cond_resched() do { } while (0)
#define cpu_relax() __builtin_ia32_pause()
#define wmb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define rmb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define mb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define barrier() __asm__ __volatile__("" : : : "memory")

/* wait queues and completions, on the backend event */
typedef struct wait_queue_head { struct lkpi_event event; } wait_queue_head_t;
#define init_waitqueue_head(wq) lkpi_event_init(&(wq)->event)
#define wake_up(wq) lkpi_event_signal(&(wq)->event)
#define wake_up_all(wq) lkpi_event_signal(&(wq)->event)
#define wait_event(wq, cond) do { while (!(cond)) lkpi_event_wait(&(wq).event, 10); } while (0)
#define wait_event_timeout(wq, cond, timeout) ({ \
    u64 _end = lkpi_time_ns() + (u64)(timeout) * NSEC_PER_MSEC; long _left = 1; \
    while (!(cond)) { \
        u64 _now = lkpi_time_ns(); \
        if (_now >= _end) { _left = (cond) ? 1 : 0; break; } \
        lkpi_event_wait(&(wq).event, 10); \
    } _left; })

struct completion { atomic_t done; struct lkpi_event event; };
static inline void init_completion(struct completion *c)
{ atomic_set(&c->done, 0); lkpi_event_init(&c->event); }
static inline void reinit_completion(struct completion *c) { atomic_set(&c->done, 0); }
static inline void complete(struct completion *c)
{ atomic_inc(&c->done); lkpi_event_signal(&c->event); }
static inline void complete_all(struct completion *c)
{ atomic_set(&c->done, 0x40000000); lkpi_event_signal(&c->event); }
static inline bool lkpi_completion_take(struct completion *c)
{
    int old = atomic_read(&c->done);
    while (old > 0) {
        int seen;
        if (old >= 0x40000000) return true;       /* complete_all: stays done */
        seen = atomic_cmpxchg(&c->done, old, old - 1);
        if (seen == old) return true;
        old = seen;
    }
    return false;
}
/* Returns 0 on timeout, otherwise a non-zero remaining time, as Linux does. */
static inline unsigned long wait_for_completion_timeout(struct completion *c, unsigned long timeout)
{
    u64 end = lkpi_time_ns() + (u64)timeout * NSEC_PER_MSEC;
    for (;;) {
        u64 now;
        if (lkpi_completion_take(c)) return 1;
        now = lkpi_time_ns();
        if (now >= end) return 0;
        lkpi_event_wait(&c->event, 10);
    }
}
static inline void wait_for_completion(struct completion *c)
{ while (!lkpi_completion_take(c)) lkpi_event_wait(&c->event, 10); }

/* deferred work */
struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);
struct work_struct { work_func_t func; struct list_head entry; atomic_t pending; };
struct workqueue_struct;
extern struct workqueue_struct *system_wq;
#define INIT_WORK(w, f) do { (w)->func = (f); INIT_LIST_HEAD(&(w)->entry); \
    atomic_set(&(w)->pending, 0); } while (0)
bool lkpi_queue_work(struct work_struct *work);  /* false if already queued */
void lkpi_flush_work(struct work_struct *work);  /* returns once it is not queued or running */
#define schedule_work(w) lkpi_queue_work(w)
#define queue_work(wq, w) lkpi_queue_work(w)
#define flush_work(w) lkpi_flush_work(w)
#define cancel_work_sync(w) lkpi_flush_work(w)
struct workqueue_struct *lkpi_wq(void);
#define alloc_workqueue(name, flags, max, ...) lkpi_wq()
#define create_singlethread_workqueue(name) lkpi_wq()
#define destroy_workqueue(wq) do { } while (0)
#define flush_workqueue(wq) do { } while (0)
#define WQ_UNBOUND 0
#define WQ_HIGHPRI 0

/* ---------------------------------------------------------------- memory */
#define GFP_KERNEL 0u
#define GFP_ATOMIC 1u
#define GFP_USER 0u
#define GFP_HIGHUSER 0u
#define GFP_DMA32 0u
#define __GFP_ZERO 0x100u
#define __GFP_NOWARN 0u
#define __GFP_NORETRY 0u
#define __GFP_HIGHMEM 0u
#define __GFP_COMP 0u
static inline void *kmalloc(size_t size, gfp_t gfp) { return lkpi_alloc(size, (gfp & __GFP_ZERO) != 0); }
static inline void *kzalloc(size_t size, gfp_t gfp) { (void)gfp; return lkpi_alloc(size, true); }
static inline void *kmalloc_array(size_t n, size_t size, gfp_t gfp)
{ if (size && n > SIZE_MAX / size) return NULL; return kmalloc(n * size, gfp); }
static inline void *kcalloc(size_t n, size_t size, gfp_t gfp)
{ (void)gfp; if (size && n > SIZE_MAX / size) return NULL; return lkpi_alloc(n * size, true); }
static inline void kfree(const void *p) { lkpi_free((void *)p); }
#define kvmalloc(size, gfp) kmalloc(size, gfp)
#define kvzalloc(size, gfp) kzalloc(size, gfp)
#define kvmalloc_array(n, size, gfp) kmalloc_array(n, size, gfp)
#define kvcalloc(n, size, gfp) kcalloc(n, size, gfp)
#define kvfree(p) kfree(p)
#define vmalloc(size) kmalloc(size, GFP_KERNEL)
#define vzalloc(size) kzalloc(size, GFP_KERNEL)
#define vfree(p) kfree(p)
void *kmemdup(const void *src, size_t len, gfp_t gfp);
char *kstrdup(const char *s, gfp_t gfp);
char *kstrndup(const char *s, size_t max, gfp_t gfp);

/* ------------------------------------------------------------ device model */
struct device_driver { const char *name; };
struct device {
    const char *init_name;
    struct device *parent;
    struct device_driver *driver;
    void *driver_data;
    void *lkpi_private;     /* backend's per-device state */
};
static inline const char *dev_name(const struct device *dev)
{ return dev && dev->init_name ? dev->init_name : "nvx"; }
void lkpi_dev_printk(int level, const struct device *dev, const char *fmt, ...) __printf(3, 4);
#define dev_crit(dev, fmt, ...) lkpi_dev_printk(2, dev, fmt, ##__VA_ARGS__)
#define dev_err(dev, fmt, ...) lkpi_dev_printk(3, dev, fmt, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...) lkpi_dev_printk(4, dev, fmt, ##__VA_ARGS__)
#define dev_notice(dev, fmt, ...) lkpi_dev_printk(5, dev, fmt, ##__VA_ARGS__)
#define dev_info(dev, fmt, ...) lkpi_dev_printk(6, dev, fmt, ##__VA_ARGS__)
#define dev_dbg(dev, fmt, ...) lkpi_dev_printk(7, dev, fmt, ##__VA_ARGS__)
/* No rate limiting: these report faults a developer wants to see in full. */
#define dev_err_ratelimited dev_err
#define dev_warn_ratelimited dev_warn
#define dev_notice_ratelimited dev_notice
#define dev_info_ratelimited dev_info
#define dev_WARN(dev, fmt, ...) do { lkpi_warn(__FILE__, __LINE__, "dev_WARN"); \
    lkpi_dev_printk(4, dev, fmt, ##__VA_ARGS__); } while (0)
#define dev_WARN_ONCE(dev, cond, fmt, ...) ({ bool _w = !!(cond); \
    if (_w) dev_WARN(dev, fmt, ##__VA_ARGS__); _w; })
static inline int request_module(const char *fmt, ...) { (void)fmt; return -ENOSYS; }

/* modules: nothing to do */
struct module;
#define THIS_MODULE ((struct module *)0)
#define MODULE_FIRMWARE(x)
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_PARM_DESC(a, b)
#define module_param_named(a, b, c, d)
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)

/* firmware: the backend supplies images by their linux-firmware name */
struct firmware { size_t size; const u8 *data; void *lkpi_private; };
int request_firmware(const struct firmware **fw, const char *name, struct device *dev);
#define firmware_request_nowarn(fw, name, dev) request_firmware(fw, name, dev)
void release_firmware(const struct firmware *fw);

/* notifier chains appear only in the ACPI glue, which is compiled out */
struct notifier_block {
    int (*notifier_call)(struct notifier_block *, unsigned long, void *);
};
#define NOTIFY_DONE 0
#define NOTIFY_OK 1

/* ------------------------------------------------------------------- MMIO */
/* Registers are memory-mapped on every target; ordinary volatile access. */
void __iomem *ioremap(phys_addr_t offset, size_t size);
void __iomem *ioremap_wc(phys_addr_t offset, size_t size);
void iounmap(void __iomem *addr);
static inline u8 ioread8(const void __iomem *p) { return *(const volatile u8 *)p; }
static inline u16 ioread16(const void __iomem *p) { return *(const volatile u16 *)p; }
static inline u32 ioread32(const void __iomem *p) { return *(const volatile u32 *)p; }
static inline void iowrite8(u8 v, void __iomem *p) { *(volatile u8 *)p = v; }
static inline void iowrite16(u16 v, void __iomem *p) { *(volatile u16 *)p = v; }
static inline void iowrite32(u32 v, void __iomem *p) { *(volatile u32 *)p = v; }
static inline u16 ioread16be(const void __iomem *p) { return __builtin_bswap16(ioread16(p)); }
static inline u32 ioread32be(const void __iomem *p) { return __builtin_bswap32(ioread32(p)); }
static inline void iowrite16be(u16 v, void __iomem *p) { iowrite16(__builtin_bswap16(v), p); }
static inline void iowrite32be(u32 v, void __iomem *p) { iowrite32(__builtin_bswap32(v), p); }
static inline void memcpy_fromio(void *dst, const void __iomem *src, size_t n)
{ size_t i; for (i = 0; i < n; i++) ((u8 *)dst)[i] = ioread8((const u8 *)src + i); }
static inline void memcpy_toio(void __iomem *dst, const void *src, size_t n)
{ size_t i; for (i = 0; i < n; i++) iowrite8(((const u8 *)src)[i], (u8 *)dst + i); }
static inline void memset_io(void __iomem *dst, int c, size_t n)
{ size_t i; for (i = 0; i < n; i++) iowrite8((u8)c, (u8 *)dst + i); }

/* A write-combined view of a physical range, mapped a page at a time. */
struct io_mapping { phys_addr_t base; size_t size; void __iomem *iomem; };
struct io_mapping *io_mapping_create_wc(phys_addr_t base, size_t size);
void io_mapping_free(struct io_mapping *map);
static inline void __iomem *io_mapping_map_atomic_wc(struct io_mapping *map, size_t offset)
{ return (u8 *)map->iomem + offset; }
static inline void io_mapping_unmap_atomic(void __iomem *vaddr) { (void)vaddr; }
static inline void __iomem *io_mapping_map_local_wc(struct io_mapping *map, size_t offset)
{ return (u8 *)map->iomem + offset; }
static inline void io_mapping_unmap_local(void __iomem *vaddr) { (void)vaddr; }

/* -------------------------------------------------------- pages and DMA */
/* A page is one PAGE_SIZE block of host memory with a known bus address. */
struct page { void *virt; dma_addr_t phys; };
struct page *alloc_page(gfp_t gfp);
void __free_page(struct page *page);
static inline void *page_address(const struct page *page) { return page->virt; }
static inline void *kmap(struct page *page) { return page->virt; }
static inline void kunmap(struct page *page) { (void)page; }
static inline void *kmap_local_page(struct page *page) { return page->virt; }
static inline void kunmap_local(const void *addr) { (void)addr; }
static inline void *kmap_atomic(struct page *page) { return page->virt; }
static inline void kunmap_atomic(void *addr) { (void)addr; }
#define VM_MAP 0
void *vmap(struct page **pages, unsigned int count, unsigned long flags, pgprot_t prot);
void vunmap(const void *addr);

enum dma_data_direction { DMA_BIDIRECTIONAL = 0, DMA_TO_DEVICE = 1, DMA_FROM_DEVICE = 2 };
#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))
#define DMA_ATTR_NON_CONSISTENT 0
#define DMA_ATTR_WEAK_ORDERING 0
#define DMA_ATTR_WRITE_COMBINE 0
#define DMA_ATTR_NO_KERNEL_MAPPING 0
/* There is no IOMMU between these drivers and the card: bus == physical. */
static inline dma_addr_t dma_map_page(struct device *dev, struct page *page, size_t offset,
                                      size_t size, enum dma_data_direction dir)
{ (void)dev; (void)size; (void)dir; return page->phys + offset; }
static inline void dma_unmap_page(struct device *dev, dma_addr_t addr, size_t size,
                                  enum dma_data_direction dir)
{ (void)dev; (void)addr; (void)size; (void)dir; }
static inline int dma_mapping_error(struct device *dev, dma_addr_t addr)
{ (void)dev; (void)addr; return 0; }
static inline int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{ (void)dev; (void)mask; return 0; }
static inline int dma_set_mask(struct device *dev, u64 mask) { (void)dev; (void)mask; return 0; }
/* Physically contiguous, device-visible memory. */
void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp);
void dma_free_coherent(struct device *dev, size_t size, void *cpu, dma_addr_t handle);
#define dma_alloc_attrs(dev, size, handle, gfp, attrs) dma_alloc_coherent(dev, size, handle, gfp)
#define dma_free_attrs(dev, size, cpu, handle, attrs) dma_free_coherent(dev, size, cpu, handle)

struct scatterlist { struct page *page; unsigned int offset, length; dma_addr_t dma_address; };
struct sg_table { struct scatterlist *sgl; unsigned int nents, orig_nents; };
#define sg_dma_address(sg) ((sg)->dma_address)
#define sg_dma_len(sg) ((sg)->length)
static inline struct scatterlist *sg_next(struct scatterlist *sg) { return sg + 1; }
static inline void sg_init_one(struct scatterlist *sg, const void *buf, unsigned int len)
{ (void)buf; sg->page = NULL; sg->offset = 0; sg->length = len; sg->dma_address = 0; }
/* Only reached from nouveau's SVM (shared virtual memory) path, which needs
 * the Linux mm and has no caller in this port. */
static inline struct page *pfn_to_page(unsigned long pfn)
{ (void)pfn; lkpi_bug(__FILE__, __LINE__, "pfn_to_page: SVM is not supported"); }
#define pgprot_writecombine(prot) (prot)

/* -------------------------------------------------------------------- PCI */
struct pci_bus { unsigned char number; int domain; enum pci_bus_speed { PCI_SPEED_UNKNOWN = 0xff,
    PCIE_SPEED_2_5GT = 0x14, PCIE_SPEED_5_0GT = 0x15, PCIE_SPEED_8_0GT = 0x16 } max_bus_speed;
    struct pci_dev *self; };
struct lkpi_pci_bar { phys_addr_t start; u64 len; };
struct pci_dev {
    struct device dev;
    struct pci_bus *bus;
    unsigned int devfn;
    u16 vendor, device, subsystem_vendor, subsystem_device;
    u8 revision;
    unsigned int irq;
    bool is_pcie;
    struct lkpi_pci_bar resource[6];
    phys_addr_t rom;        /* firmware-provided ROM image, if any */
    size_t romlen;
};
#define PCI_DEVFN(slot, func) ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn) (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn) ((devfn) & 0x07)
#define PCI_ANY_ID (~0)
#define PCI_VENDOR_ID_NVIDIA 0x10de
#define PCI_CAP_ID_AGP 0x02
#define PCI_ROM_RESOURCE 6
#define to_pci_dev(d) container_of(d, struct pci_dev, dev)
static inline bool dev_is_pci(const struct device *dev) { (void)dev; return true; }
static inline phys_addr_t pci_resource_start(struct pci_dev *pdev, int bar) { return pdev->resource[bar].start; }
static inline u64 pci_resource_len(struct pci_dev *pdev, int bar) { return pdev->resource[bar].len; }
static inline bool pci_is_pcie(struct pci_dev *pdev) { return pdev->is_pcie; }
static inline int pci_domain_nr(struct pci_bus *bus) { return bus->domain; }
static inline int pci_enable_device(struct pci_dev *pdev) { (void)pdev; return 0; }
static inline void pci_disable_device(struct pci_dev *pdev) { (void)pdev; }
static inline void pci_set_master(struct pci_dev *pdev) { (void)pdev; }
/* MSI is not offered; nvkm falls back to the legacy line interrupt. */
static inline int pci_enable_msi(struct pci_dev *pdev) { (void)pdev; return -ENOSYS; }
static inline void pci_disable_msi(struct pci_dev *pdev) { (void)pdev; }
static inline int pci_find_capability(struct pci_dev *pdev, int cap) { (void)pdev; (void)cap; return 0; }
static inline struct pci_dev *pci_get_domain_bus_and_slot(int domain, unsigned int bus, unsigned int devfn)
{ (void)domain; (void)bus; (void)devfn; return NULL; }
int pci_read_config_dword(struct pci_dev *pdev, int where, u32 *val);
int pci_write_config_byte(struct pci_dev *pdev, int where, u8 val);
/* The expansion ROM is read through PROM/PRAMIN instead; no PCI ROM BAR. */
static inline int pci_enable_rom(struct pci_dev *pdev) { (void)pdev; return -ENODEV; }
static inline void pci_disable_rom(struct pci_dev *pdev) { (void)pdev; }
static inline void __iomem *pci_map_rom(struct pci_dev *pdev, size_t *size)
{ (void)pdev; *size = 0; return NULL; }
static inline void pci_unmap_rom(struct pci_dev *pdev, void __iomem *rom) { (void)pdev; (void)rom; }

/* interrupts: the backend delivers the device interrupt to 'handler' */
typedef enum irqreturn { IRQ_NONE = 0, IRQ_HANDLED = 1 } irqreturn_t;
typedef irqreturn_t (*irq_handler_t)(int irq, void *arg);
#define IRQF_SHARED 0x80
int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
                const char *name, void *arg);
void free_irq(unsigned int irq, void *arg);
static inline void synchronize_irq(unsigned int irq) { (void)irq; }
static inline void disable_irq(unsigned int irq) { (void)irq; }
static inline void enable_irq(unsigned int irq) { (void)irq; }

/* platform (Tegra) devices never exist here */
struct platform_device { struct device dev; };
struct clk; struct regulator; struct reset_control; struct iommu_domain;
struct device_node;

/* -------------------------------------------------------------------- I2C */
struct i2c_msg { u16 addr; u16 flags; u16 len; u8 *buf; };
#define I2C_M_RD 0x0001
#define I2C_M_NOSTART 0x4000
#define I2C_FUNC_I2C 0x00000001
#define I2C_FUNC_SMBUS_EMUL 0x0eff0008
#define I2C_NAME_SIZE 20
struct i2c_adapter;
struct i2c_algorithm {
    int (*master_xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
    u32 (*functionality)(struct i2c_adapter *adap);
};
struct i2c_adapter {
    struct module *owner;
    const struct i2c_algorithm *algo;
    void *algo_data;
    struct device dev;
    char name[48];
};
struct i2c_board_info { char type[I2C_NAME_SIZE]; unsigned short addr; void *platform_data; };
#define I2C_BOARD_INFO(dev_type, dev_addr) .type = dev_type, .addr = (dev_addr)
struct i2c_client { unsigned short addr; struct i2c_adapter *adapter; struct device dev; };
struct i2c_driver {
    struct device_driver driver;
    int (*detect)(struct i2c_client *client, struct i2c_board_info *info);
};
#define to_i2c_driver(d) container_of(d, struct i2c_driver, driver)
#define I2C_MODULE_PREFIX "i2c:"
extern const struct i2c_algorithm i2c_bit_algo;
struct i2c_algo_bit_data {
    void *data;
    void (*setsda)(void *data, int state);
    void (*setscl)(void *data, int state);
    int (*getsda)(void *data);
    int (*getscl)(void *data);
    int (*pre_xfer)(struct i2c_adapter *);
    void (*post_xfer)(struct i2c_adapter *);
    int udelay;
    int timeout;
};
static inline int i2c_add_adapter(struct i2c_adapter *adap) { (void)adap; return 0; }
static inline void i2c_del_adapter(struct i2c_adapter *adap) { (void)adap; }
/* nvkm's own bit-banging is always used (CONFIG_NOUVEAU_I2C_INTERNAL). */
static inline int i2c_bit_add_bus(struct i2c_adapter *adap) { (void)adap; return -ENOSYS; }
static inline int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{ return adap->algo && adap->algo->master_xfer ? adap->algo->master_xfer(adap, msgs, num) : -EOPNOTSUPP; }
/* External I2C chips (fan/thermal sensors) have Linux hwmon drivers behind
 * this call; without them nvkm simply reports no such device. */
static inline struct i2c_client *i2c_new_client_device(struct i2c_adapter *adap,
                                                       const struct i2c_board_info *info)
{ (void)adap; (void)info; return ERR_PTR(-ENODEV); }
static inline void i2c_unregister_device(struct i2c_client *client) { (void)client; }
static inline void *i2c_get_adapdata(const struct i2c_adapter *adap) { return adap->dev.driver_data; }
static inline void i2c_set_adapdata(struct i2c_adapter *adap, void *data) { adap->dev.driver_data = data; }

/* ------------------------------------------------- compiled-out subsystems */
/* ACPI, DMI, power-supply, runtime PM: absent on every target. */
typedef void *acpi_handle;
struct dmi_strmatch { unsigned char slot; char substr[79]; };
struct dmi_system_id { const char *ident; struct dmi_strmatch matches[4]; void *driver_data; };
#define DMI_MATCH(a, b) { .slot = (a), .substr = (b) }
enum { DMI_NONE, DMI_SYS_VENDOR, DMI_BOARD_VENDOR, DMI_PRODUCT_NAME, DMI_PRODUCT_VERSION,
       DMI_BOARD_NAME };
static inline bool dmi_match(int field, const char *str) { (void)field; (void)str; return false; }
static inline int dmi_check_system(const struct dmi_system_id *list) { (void)list; return 0; }

/* Tegra platform services. The Tegra chips' files are compiled so nvkm's
 * chipset table links unmodified, but no platform device ever exists. */
struct iommu_fwspec { unsigned int num_ids; u32 ids[1]; };
static inline struct iommu_fwspec *dev_iommu_fwspec_get(struct device *dev) { (void)dev; return NULL; }
#define IOMMU_READ 1
#define IOMMU_WRITE 2
static inline int iommu_map(struct iommu_domain *d, unsigned long iova, phys_addr_t paddr,
                            size_t size, int prot, gfp_t gfp)
{ (void)d; (void)iova; (void)paddr; (void)size; (void)prot; (void)gfp; return -ENOSYS; }
static inline size_t iommu_unmap(struct iommu_domain *d, unsigned long iova, size_t size)
{ (void)d; (void)iova; (void)size; return 0; }
static inline unsigned long clk_get_rate(struct clk *clk) { (void)clk; return 0; }
static inline int regulator_get_voltage(struct regulator *r) { (void)r; return -ENOSYS; }
static inline int regulator_set_voltage(struct regulator *r, int min_uV, int max_uV)
{ (void)r; (void)min_uV; (void)max_uV; return -ENOSYS; }
static inline int power_supply_is_system_supplied(void) { return 1; }
static inline void orderly_poweroff(bool force) { (void)force; }
static inline int pm_runtime_get_sync(struct device *dev) { (void)dev; return 0; }
static inline void pm_runtime_put_autosuspend(struct device *dev) { (void)dev; }
static inline void pm_runtime_mark_last_busy(struct device *dev) { (void)dev; }
static inline bool pm_runtime_suspended(struct device *dev) { (void)dev; return false; }

#endif /* LKPI_H */
