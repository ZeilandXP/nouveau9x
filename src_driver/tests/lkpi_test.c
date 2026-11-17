/* Unit tests for the Linux-API shim itself (freestanding, host backend). */
#include <lkpi.h>
#include <lkpi_backend.h>
#include "../lkpi/host/lkpi_host.h"

#define CHECK(cond) do { if (!(cond)) { \
    lkpi_printk(0, "lkpi_test: FAILED %s (line %d)\n", #cond, __LINE__); \
    lkpi_panic("test failure"); } } while (0)

static void fmt_is(const char *expect, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = lkpi_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (lkpi_strcmp(buf, expect) || n != (int)lkpi_strlen(expect)) {
        lkpi_printk(0, "lkpi_test: format \"%s\" gave \"%s\" (%d), want \"%s\"\n", fmt, buf, n, expect);
        lkpi_panic("format mismatch");
    }
}

static void test_printf(void)
{
    char small[8];
    u64 addr = 0x123456789abcULL;
    fmt_is("42", "%d", 42);
    fmt_is("-42", "%d", -42);
    fmt_is("   42|42   |00042", "%5d|%-5d|%05d", 42, 42, 42);
    fmt_is("-0042", "%05d", -42);
    fmt_is("+7 7", "%+d% d", 7, 7);
    fmt_is("ff FF 0xff 377", "%x %X %#x %o", 255, 255, 255, 255);
    fmt_is("0000000123456789", "%016llx", 0x123456789ULL);
    fmt_is("18446744073709551615", "%llu", ~0ULL);
    fmt_is("-9223372036854775808", "%lld", (long long)(-9223372036854775807LL - 1));
    fmt_is("4294967295", "%u", 0xffffffffU);
    fmt_is("2147483647 -2147483648", "%ld %ld", 2147483647L, -2147483647L - 1);
    fmt_is("ab", "%hhx", 0x1ab);
    fmt_is("beef", "%hx", 0xdeadbeef);
    fmt_is("008", "%.3d", 8);
    fmt_is("  008", "%5.3d", 8);
    fmt_is("", "%.0d", 0);
    fmt_is("[abc] [  abc] [abc  ] [ab]", "[%s] [%5s] [%-5s] [%.2s]", "abc", "abc", "abc", "abc");
    fmt_is("(null)", "%s", (char *)NULL);
    fmt_is("x%y", "%c%%%c", 'x', 'y');
    fmt_is("  7", "%*d", 3, 7);
    fmt_is("7  |", "%*d|", -3, 7);
    fmt_is("1234", "%zu", (size_t)1234);
    fmt_is("0x0000123456789abc", "%pad", &addr);
    fmt_is("-12", "%pe", ERR_PTR(-ENOMEM));
    fmt_is("GP104 chipset 134 rev a1", "%s chipset %03x rev %02x", "GP104", 0x134, 0xa1);
    /* The nvkm message format: literal text between conversions. */
    fmt_is("fb:6144 MiB DDR5", "%s:%d MiB %s", "fb", 6144, "DDR5");
    /* Truncation: always terminated, returns the full length. */
    CHECK(lkpi_snprintf(small, sizeof(small), "%s", "0123456789") == 10);
    CHECK(!lkpi_strcmp(small, "0123456"));
    CHECK(lkpi_snprintf(NULL, 0, "%d", 12345) == 5);
}

static void test_strings(void)
{
    char buf[8];
    long v;
    CHECK(lkpi_strlen("") == 0 && lkpi_strlen("abc") == 3);
    CHECK(lkpi_strnlen("abcdef", 3) == 3 && lkpi_strnlen("ab", 9) == 2);
    CHECK(lkpi_strcmp("a", "a") == 0 && lkpi_strcmp("a", "b") < 0 && lkpi_strcmp("b", "a") > 0);
    CHECK(lkpi_strcmp("a", "ab") < 0);
    CHECK(lkpi_strncmp("abcX", "abcY", 3) == 0 && lkpi_strncmp("abcX", "abcY", 4) < 0);
    CHECK(lkpi_strcasecmp("NvBios", "nvbios") == 0 && lkpi_strncasecmp("PRAMIN", "prom", 2) == 0);
    CHECK(lkpi_strncasecmp("PRAMIN", "prom", 3) != 0);
    CHECK(lkpi_strchr("a=b", '=') && *lkpi_strchr("a=b", '=') == '=' && !lkpi_strchr("ab", '='));
    CHECK(lkpi_strcspn("NvBios=PROM,x", "=,") == 6 && lkpi_strcspn("abc", ",") == 3);
    CHECK(lkpi_strscpy(buf, "abc", sizeof(buf)) == 3 && !lkpi_strcmp(buf, "abc"));
    CHECK(lkpi_strscpy(buf, "0123456789", sizeof(buf)) == -E2BIG && !lkpi_strcmp(buf, "0123456"));
    lkpi_strncpy(buf, "ab", 5);
    CHECK(buf[0] == 'a' && buf[2] == 0 && buf[4] == 0);
    CHECK(lkpi_memchr("abc", 'c', 3) && !lkpi_memchr("abc", 'c', 2));
    CHECK(!lkpi_kstrtol("123", 0, &v) && v == 123);
    CHECK(!lkpi_kstrtol("0x134", 0, &v) && v == 0x134);
    CHECK(!lkpi_kstrtol("-17\n", 10, &v) && v == -17);
    CHECK(!lkpi_kstrtol("010", 0, &v) && v == 8);
    CHECK(lkpi_kstrtol("12a", 10, &v) == -EINVAL && lkpi_kstrtol("", 0, &v) == -EINVAL);
    CHECK(lkpi_kstrtol("99999999999999999999999", 10, &v) == -ERANGE);
    CHECK(!lkpi_kstrtol("2147483647", 10, &v) && v == 2147483647L);
    CHECK(lkpi_simple_strtoul("ff,", NULL, 16) == 255);
    {
        char *dup = kstrndup("abcdef", 3, GFP_KERNEL);
        CHECK(dup && !lkpi_strcmp(dup, "abc"));
        kfree(dup);
    }
}

static void test_bits(void)
{
    DECLARE_BITMAP(map, 100);
    unsigned long bit, seen = 0;
    memset(map, 0, sizeof(map));
    set_bit(0, map); set_bit(33, map); __set_bit(99, map);
    CHECK(test_bit(33, map) && !test_bit(34, map));
    CHECK(find_first_bit(map, 100) == 0 && find_next_bit(map, 100, 1) == 33);
    CHECK(find_first_zero_bit(map, 100) == 1);
    for_each_set_bit(bit, map, 100) seen += bit;
    CHECK(seen == 132);
    CHECK(test_and_clear_bit(33, map) && !test_and_clear_bit(33, map));
    CHECK(!test_and_set_bit(5, map) && test_and_set_bit(5, map));
    CHECK(fls(0) == 0 && fls(1) == 1 && fls(0x80000000U) == 32 && ffs(8) == 4 && __ffs(8) == 3);
    CHECK(fls64(1ULL << 40) == 41 && __ffs64(1ULL << 40) == 40);
    CHECK(hweight32(0xf0f0) == 8 && hweight8(0x1ff) == 8);
    CHECK(ilog2(4096) == 12 && ilog2(1ULL << 35) == 35 && order_base_2(4097) == 13);
    CHECK(is_power_of_2(64) && !is_power_of_2(0) && !is_power_of_2(6));
    CHECK(roundup_pow_of_two(5) == 8 && rounddown_pow_of_two(5) == 4);
    CHECK(GENMASK(7, 4) == 0xf0 && GENMASK_ULL(39, 32) == 0xff00000000ULL && BIT_ULL(40) == 1ULL << 40);
    CHECK(ALIGN(4097, 4096) == 8192 && ALIGN((u64)0x1ffffffffULL, 0x1000) == 0x200000000ULL);
    CHECK(IS_ALIGNED(8192, 4096) && round_up(5, 4) == 8 && round_down(7, 4) == 4);
    CHECK(DIV_ROUND_UP(7, 2) == 4 && DIV_ROUND_CLOSEST(7, 2) == 4 && DIV_ROUND_CLOSEST(-7, 2) == -4);
    CHECK(upper_32_bits(0x1234567800000000ULL) == 0x12345678 && lower_32_bits(0x1ffffffffULL) == 0xffffffff);
    CHECK(min(3, 5) == 3 && max_t(u64, 3, 5) == 5 && clamp(12, 0, 10) == 10 && abs(-4) == 4);
    { u64 n = 1000000007ULL; CHECK(do_div(n, 10) == 7 && n == 100000000ULL); }
    CHECK(array_size(SIZE_MAX, 2) == SIZE_MAX && array3_size(2, 3, 4) == 24);
    /* The IS_ENABLED trick distinguishes defined-to-1 from undefined. */
    CHECK(IS_ENABLED(CONFIG_X86) == 1 && IS_ENABLED(CONFIG_ARM) == 0);
    /* Error pointers survive on a 64-bit target whose long is 32 bits. */
    CHECK(IS_ERR(ERR_PTR(-ENOMEM)) && PTR_ERR(ERR_PTR(-ENOMEM)) == -ENOMEM);
    CHECK(!IS_ERR(&seen) && !IS_ERR(NULL) && IS_ERR_OR_NULL(NULL));
    CHECK((uintptr_t)ERR_PTR(-1) == UINTPTR_MAX);
}

struct item { int key; struct list_head head; struct rb_node node; };

static void test_lists(void)
{
    struct item it[5], *pos, *tmp;
    LIST_HEAD(list); LIST_HEAD(other);
    int i, sum = 0, order = 0;
    for (i = 0; i < 5; i++) { it[i].key = i; list_add_tail(&it[i].head, &list); }
    CHECK(list_first_entry(&list, struct item, head)->key == 0);
    CHECK(list_last_entry(&list, struct item, head)->key == 4);
    list_for_each_entry(pos, &list, head) order = order * 10 + pos->key;
    CHECK(order == 1234);
    order = 0;
    list_for_each_entry_reverse(pos, &list, head) order = order * 10 + pos->key;
    CHECK(order == 43210);
    list_for_each_entry_safe(pos, tmp, &list, head) if (pos->key & 1) list_del(&pos->head);
    list_for_each_entry(pos, &list, head) sum += pos->key;
    CHECK(sum == 6);
    list_move(&it[4].head, &list);
    CHECK(list_first_entry(&list, struct item, head)->key == 4);
    list_rotate_left(&list);
    CHECK(list_first_entry(&list, struct item, head)->key == 0);
    list_splice_init(&list, &other);
    CHECK(list_empty(&list) && !list_empty(&other) && !list_is_singular(&other));
    CHECK(list_first_entry_or_null(&list, struct item, head) == NULL);
}

/* ---- red-black tree: random workload checked against the invariants ---- */
static u32 rng_state = 0x12345678;
static u32 rng(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static int black_height(const struct rb_node *n, const struct rb_node *parent)
{
    int l, r;
    if (!n) return 1;
    CHECK(n->rb_parent == parent);
    if (n->rb_red) CHECK(!(n->rb_left && n->rb_left->rb_red) && !(n->rb_right && n->rb_right->rb_red));
    l = black_height(n->rb_left, n);
    r = black_height(n->rb_right, n);
    CHECK(l == r);
    return l + (n->rb_red ? 0 : 1);
}

static void tree_insert(struct rb_root *root, struct item *it)
{
    struct rb_node **link = &root->rb_node, *parent = NULL;
    while (*link) {
        parent = *link;
        link = it->key < rb_entry(parent, struct item, node)->key ? &parent->rb_left : &parent->rb_right;
    }
    rb_link_node(&it->node, parent, link);
    rb_insert_color(&it->node, root);
}

#define NITEMS 600
static void test_rbtree(void)
{
    static struct item items[NITEMS];
    static bool in_tree[NITEMS];
    struct rb_root root = RB_ROOT;
    struct rb_node *n;
    int i, step, live = 0;
    CHECK(RB_EMPTY_ROOT(&root) && !rb_first(&root) && !rb_last(&root));
    for (i = 0; i < NITEMS; i++) { items[i].key = i; RB_CLEAR_NODE(&items[i].node); }
    CHECK(RB_EMPTY_NODE(&items[0].node) && !rb_next(&items[0].node));
    for (step = 0; step < 40000; step++) {
        int idx = (int)(rng() % NITEMS);
        if (in_tree[idx]) { rb_erase(&items[idx].node, &root); RB_CLEAR_NODE(&items[idx].node); live--; }
        else { tree_insert(&root, &items[idx]); live++; }
        in_tree[idx] = !in_tree[idx];
        if (step % 97 == 0 || step > 39900) {
            int count = 0, last = -1;
            CHECK(!root.rb_node || !root.rb_node->rb_red);
            black_height(root.rb_node, NULL);
            for (n = rb_first(&root); n; n = rb_next(n)) {
                int key = rb_entry(n, struct item, node)->key;
                CHECK(key > last && in_tree[key]);
                last = key; count++;
            }
            CHECK(count == live);
            count = 0; last = NITEMS;
            for (n = rb_last(&root); n; n = rb_prev(n)) {
                int key = rb_entry(n, struct item, node)->key;
                CHECK(key < last);
                last = key; count++;
            }
            CHECK(count == live);
        }
    }
    /* Ascending then descending insertion: the degenerate cases for an
     * unbalanced tree; depth must stay logarithmic. */
    for (i = 0; i < NITEMS; i++) if (in_tree[i]) { rb_erase(&items[i].node, &root); in_tree[i] = false; }
    CHECK(RB_EMPTY_ROOT(&root));
    for (i = 0; i < NITEMS; i++) tree_insert(&root, &items[i]);
    CHECK(black_height(root.rb_node, NULL) <= 11);   /* 2*log2(601) bound on height */
    for (i = 0; i < NITEMS; i++) { rb_erase(&items[i].node, &root); if (i % 50 == 0) black_height(root.rb_node, NULL); }
    CHECK(RB_EMPTY_ROOT(&root));
}

static int work_runs;
static struct work_struct *requeue_target;
static void work_fn(struct work_struct *w)
{
    work_runs++;
    if (requeue_target == w && work_runs < 3) schedule_work(w);
}

static void test_work_and_waits(void)
{
    struct work_struct a, b;
    struct completion done;
    refcount_t ref;
    struct mutex lock;
    struct kref kref;
    INIT_WORK(&a, work_fn); INIT_WORK(&b, work_fn);
    CHECK(schedule_work(&a) && !schedule_work(&a) && schedule_work(&b));
    CHECK(work_runs == 0);
    CHECK(lkpi_work_run_pending() == 2 && work_runs == 2);
    CHECK(lkpi_work_run_pending() == 0);
    /* flush_work runs a queued item before returning. */
    schedule_work(&a);
    flush_work(&a);
    CHECK(work_runs == 3 && lkpi_work_run_pending() == 0);
    /* A handler may requeue itself. */
    work_runs = 0; requeue_target = &b;
    schedule_work(&b);
    CHECK(lkpi_work_run_pending() == 3 && work_runs == 3);

    lkpi_host_fast_time = true;
    init_completion(&done);
    CHECK(wait_for_completion_timeout(&done, 50) == 0);    /* times out */
    complete(&done);
    CHECK(wait_for_completion_timeout(&done, 50) != 0);
    CHECK(wait_for_completion_timeout(&done, 5) == 0);     /* consumed exactly once */
    complete_all(&done);
    CHECK(wait_for_completion_timeout(&done, 5) && wait_for_completion_timeout(&done, 5));
    reinit_completion(&done);
    CHECK(wait_for_completion_timeout(&done, 5) == 0);
    {
        ktime_t t0 = ktime_get();
        msleep(250);
        CHECK(ktime_to_us(ktime_sub(ktime_get(), t0)) >= 250000);
        CHECK(time_after(jiffies + 5, jiffies) && time_before(jiffies, jiffies + 5));
    }
    lkpi_host_fast_time = false;

    mutex_init(&lock);
    refcount_set(&ref, 2);
    CHECK(!refcount_dec_and_mutex_lock(&ref, &lock) && !mutex_is_locked(&lock));
    CHECK(refcount_dec_and_mutex_lock(&ref, &lock) && mutex_is_locked(&lock));
    CHECK(!mutex_trylock(&lock));
    mutex_unlock(&lock);
    CHECK(!refcount_inc_not_zero(&ref));
    refcount_set(&ref, 1);
    CHECK(refcount_inc_not_zero(&ref) && refcount_read(&ref) == 2);
    kref_init(&kref); kref_get(&kref);
    CHECK(kref_read(&kref) == 2);
    {
        static DEFINE_MUTEX(static_lock);       /* zero-initialised, never mutex_init'd */
        mutex_lock(&static_lock);
        CHECK(mutex_is_locked(&static_lock));
        mutex_unlock(&static_lock);
    }
}

static void test_firmware(void)
{
    static const u8 blob[4] = { 1, 2, 3, 4 };
    const struct firmware *fw = NULL;
    CHECK(request_firmware(&fw, "nvidia/gp104/acr/bl.bin", NULL) == -ENOENT && !fw);
    CHECK(!lkpi_firmware_register("nvidia/gp104/acr/bl.bin", blob, sizeof(blob)));
    CHECK(!request_firmware(&fw, "nvidia/gp104/acr/bl.bin", NULL) && fw->size == 4 && fw->data[3] == 4);
    release_firmware(fw);
    CHECK(request_firmware(&fw, "nvidia/gp104/acr/bl.bi", NULL) == -ENOENT);
    lkpi_firmware_clear();
    CHECK(request_firmware(&fw, "nvidia/gp104/acr/bl.bin", NULL) == -ENOENT);
}

int main(void)
{
    test_printf();
    test_strings();
    test_bits();
    test_lists();
    test_rbtree();
    test_work_and_waits();
    test_firmware();
    lkpi_printk(0, "lkpi_test: ok\n");
    return 0;
}
