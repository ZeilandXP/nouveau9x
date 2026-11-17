/*
 * lkpi_core.c - OS-independent parts of the Linux-API shim: red-black tree,
 * deferred work, and the firmware registry. Freestanding.
 */
#include <lkpi.h>
#include <lkpi_backend.h>

/* --------------------------------------------------------- red-black tree */
/*
 * A conventional red-black tree with parent pointers and NULL leaves,
 * implemented from the textbook algorithm. Linux callers do the descent
 * themselves, link the new node with rb_link_node(), then call
 * rb_insert_color() to rebalance.
 */
static bool is_red(const struct rb_node *n) { return n && n->rb_red; }

static void rotate_left(struct rb_root *root, struct rb_node *x)
{
    struct rb_node *y = x->rb_right;
    x->rb_right = y->rb_left;
    if (y->rb_left) y->rb_left->rb_parent = x;
    y->rb_parent = x->rb_parent;
    if (!x->rb_parent) root->rb_node = y;
    else if (x == x->rb_parent->rb_left) x->rb_parent->rb_left = y;
    else x->rb_parent->rb_right = y;
    y->rb_left = x;
    x->rb_parent = y;
}

static void rotate_right(struct rb_root *root, struct rb_node *x)
{
    struct rb_node *y = x->rb_left;
    x->rb_left = y->rb_right;
    if (y->rb_right) y->rb_right->rb_parent = x;
    y->rb_parent = x->rb_parent;
    if (!x->rb_parent) root->rb_node = y;
    else if (x == x->rb_parent->rb_right) x->rb_parent->rb_right = y;
    else x->rb_parent->rb_left = y;
    y->rb_right = x;
    x->rb_parent = y;
}

void rb_insert_color(struct rb_node *z, struct rb_root *root)
{
    while (is_red(z->rb_parent)) {
        struct rb_node *parent = z->rb_parent, *grand = parent->rb_parent;
        if (parent == grand->rb_left) {
            struct rb_node *uncle = grand->rb_right;
            if (is_red(uncle)) {
                parent->rb_red = 0; uncle->rb_red = 0; grand->rb_red = 1;
                z = grand;
            } else {
                if (z == parent->rb_right) { z = parent; rotate_left(root, z); parent = z->rb_parent; }
                parent->rb_red = 0; grand->rb_red = 1;
                rotate_right(root, grand);
            }
        } else {
            struct rb_node *uncle = grand->rb_left;
            if (is_red(uncle)) {
                parent->rb_red = 0; uncle->rb_red = 0; grand->rb_red = 1;
                z = grand;
            } else {
                if (z == parent->rb_left) { z = parent; rotate_right(root, z); parent = z->rb_parent; }
                parent->rb_red = 0; grand->rb_red = 1;
                rotate_left(root, grand);
            }
        }
    }
    root->rb_node->rb_red = 0;
}

/* Replace the subtree rooted at u with the one rooted at v (v may be NULL). */
static void transplant(struct rb_root *root, struct rb_node *u, struct rb_node *v)
{
    if (!u->rb_parent) root->rb_node = v;
    else if (u == u->rb_parent->rb_left) u->rb_parent->rb_left = v;
    else u->rb_parent->rb_right = v;
    if (v) v->rb_parent = u->rb_parent;
}

/* x (possibly NULL, with parent xp) carries an extra black; push it up. */
static void erase_fixup(struct rb_root *root, struct rb_node *x, struct rb_node *xp)
{
    while (x != root->rb_node && !is_red(x)) {
        if (x == xp->rb_left) {
            struct rb_node *w = xp->rb_right;
            if (is_red(w)) {
                w->rb_red = 0; xp->rb_red = 1;
                rotate_left(root, xp);
                w = xp->rb_right;
            }
            if (!is_red(w->rb_left) && !is_red(w->rb_right)) {
                w->rb_red = 1;
                x = xp; xp = x->rb_parent;
            } else {
                if (!is_red(w->rb_right)) {
                    w->rb_left->rb_red = 0; w->rb_red = 1;
                    rotate_right(root, w);
                    w = xp->rb_right;
                }
                w->rb_red = xp->rb_red; xp->rb_red = 0; w->rb_right->rb_red = 0;
                rotate_left(root, xp);
                x = root->rb_node;
                break;
            }
        } else {
            struct rb_node *w = xp->rb_left;
            if (is_red(w)) {
                w->rb_red = 0; xp->rb_red = 1;
                rotate_right(root, xp);
                w = xp->rb_left;
            }
            if (!is_red(w->rb_left) && !is_red(w->rb_right)) {
                w->rb_red = 1;
                x = xp; xp = x->rb_parent;
            } else {
                if (!is_red(w->rb_left)) {
                    w->rb_right->rb_red = 0; w->rb_red = 1;
                    rotate_left(root, w);
                    w = xp->rb_left;
                }
                w->rb_red = xp->rb_red; xp->rb_red = 0; w->rb_left->rb_red = 0;
                rotate_right(root, xp);
                x = root->rb_node;
                break;
            }
        }
    }
    if (x) x->rb_red = 0;
}

void rb_erase(struct rb_node *z, struct rb_root *root)
{
    struct rb_node *x, *xp;
    int removed_red = z->rb_red;
    if (!z->rb_left) {
        x = z->rb_right; xp = z->rb_parent;
        transplant(root, z, z->rb_right);
    } else if (!z->rb_right) {
        x = z->rb_left; xp = z->rb_parent;
        transplant(root, z, z->rb_left);
    } else {
        struct rb_node *y = z->rb_right;        /* successor takes z's place */
        while (y->rb_left) y = y->rb_left;
        removed_red = y->rb_red;
        x = y->rb_right;
        if (y->rb_parent == z) {
            xp = y;
        } else {
            xp = y->rb_parent;
            transplant(root, y, y->rb_right);
            y->rb_right = z->rb_right;
            y->rb_right->rb_parent = y;
        }
        transplant(root, z, y);
        y->rb_left = z->rb_left;
        y->rb_left->rb_parent = y;
        y->rb_red = z->rb_red;
    }
    if (!removed_red) erase_fixup(root, x, xp);
}

struct rb_node *rb_first(const struct rb_root *root)
{
    struct rb_node *n = root->rb_node;
    if (!n) return NULL;
    while (n->rb_left) n = n->rb_left;
    return n;
}

struct rb_node *rb_last(const struct rb_root *root)
{
    struct rb_node *n = root->rb_node;
    if (!n) return NULL;
    while (n->rb_right) n = n->rb_right;
    return n;
}

struct rb_node *rb_next(const struct rb_node *node)
{
    struct rb_node *n = (struct rb_node *)node, *parent;
    if (RB_EMPTY_NODE(n)) return NULL;
    if (n->rb_right) {
        n = n->rb_right;
        while (n->rb_left) n = n->rb_left;
        return n;
    }
    while ((parent = n->rb_parent) && n == parent->rb_right) n = parent;
    return parent;
}

struct rb_node *rb_prev(const struct rb_node *node)
{
    struct rb_node *n = (struct rb_node *)node, *parent;
    if (RB_EMPTY_NODE(n)) return NULL;
    if (n->rb_left) {
        n = n->rb_left;
        while (n->rb_right) n = n->rb_right;
        return n;
    }
    while ((parent = n->rb_parent) && n == parent->rb_left) n = parent;
    return parent;
}

/* ---------------------------------------------------------- deferred work */
/*
 * One global FIFO. Something must call lkpi_work_run_pending() from a
 * context that may sleep: a worker thread on Windows, the test harness on
 * the host. lkpi_work_kick() tells the backend there is work to run.
 */
static DEFINE_SPINLOCK(g_work_lock);
static LIST_HEAD(g_work_list);
static struct work_struct *g_work_running;
struct workqueue_struct { int unused; };
static struct workqueue_struct g_wq;
struct workqueue_struct *system_wq = &g_wq;
struct workqueue_struct *lkpi_wq(void) { return &g_wq; }

bool lkpi_queue_work(struct work_struct *work)
{
    unsigned long flags;
    bool queued = false;
    spin_lock_irqsave(&g_work_lock, flags);
    if (!atomic_read(&work->pending)) {
        atomic_set(&work->pending, 1);
        list_add_tail(&work->entry, &g_work_list);
        queued = true;
    }
    spin_unlock_irqrestore(&g_work_lock, flags);
    if (queued) lkpi_work_kick();
    return queued;
}

static void run_one(struct work_struct *work)
{
    unsigned long flags;
    work->func(work);
    spin_lock_irqsave(&g_work_lock, flags);
    g_work_running = NULL;
    spin_unlock_irqrestore(&g_work_lock, flags);
}

int lkpi_work_run_pending(void)
{
    int ran = 0;
    for (;;) {
        struct work_struct *work = NULL;
        unsigned long flags;
        spin_lock_irqsave(&g_work_lock, flags);
        if (!list_empty(&g_work_list)) {
            work = list_first_entry(&g_work_list, struct work_struct, entry);
            list_del_init(&work->entry);
            /* Cleared before the call so the handler can requeue itself. */
            atomic_set(&work->pending, 0);
            g_work_running = work;
        }
        spin_unlock_irqrestore(&g_work_lock, flags);
        if (!work) return ran;
        run_one(work);
        ran++;
    }
}

/* On return the work is neither queued nor executing. A queued item is run
 * here, in the caller's context, which is what "wait for it" amounts to. */
void lkpi_flush_work(struct work_struct *work)
{
    for (;;) {
        unsigned long flags;
        bool mine = false, busy;
        spin_lock_irqsave(&g_work_lock, flags);
        if (atomic_read(&work->pending)) {
            list_del_init(&work->entry);
            atomic_set(&work->pending, 0);
            mine = true;
        }
        busy = (g_work_running == work);
        spin_unlock_irqrestore(&g_work_lock, flags);
        if (mine) { work->func(work); continue; }
        if (!busy) return;
        lkpi_msleep(1);
    }
}

/* --------------------------------------------------------------- firmware */
#define LKPI_MAX_FIRMWARE 96
static struct { const char *name; const u8 *data; size_t size; } g_firmware[LKPI_MAX_FIRMWARE];
static unsigned int g_firmware_count;

int lkpi_firmware_register(const char *name, const void *data, size_t size)
{
    if (g_firmware_count == LKPI_MAX_FIRMWARE) return -ENOSPC;
    g_firmware[g_firmware_count].name = name;
    g_firmware[g_firmware_count].data = data;
    g_firmware[g_firmware_count].size = size;
    g_firmware_count++;
    return 0;
}

void lkpi_firmware_clear(void) { g_firmware_count = 0; }

int request_firmware(const struct firmware **pfw, const char *name, struct device *dev)
{
    struct firmware *fw;
    unsigned int i;
    (void)dev;
    *pfw = NULL;
    for (i = 0; i < g_firmware_count; i++) {
        if (!lkpi_strcmp(g_firmware[i].name, name)) {
            fw = kzalloc(sizeof(*fw), GFP_KERNEL);
            if (!fw) return -ENOMEM;
            fw->data = g_firmware[i].data;
            fw->size = g_firmware[i].size;
            *pfw = fw;
            return 0;
        }
    }
    return -ENOENT;
}

void release_firmware(const struct firmware *fw)
{
    kfree(fw);          /* image bytes belong to whoever registered them */
}

/* nvkm only references this when its own bit-banging is disabled. */
const struct i2c_algorithm i2c_bit_algo = { NULL, NULL };
