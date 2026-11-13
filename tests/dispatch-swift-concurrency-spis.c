#undef NDEBUG
#include <assert.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/* Probes the libdispatch SPIs the Swift concurrency runtime imports
 * (private/swift_concurrency_private.h). Declarations are pinned here so the
 * probe needs no private header. The symbols are looked up with dlsym, so a
 * libdispatch that lacks them fails with a message instead of a link error.
 *
 * The override calls are checked against what Darling's libdispatch does:
 * it is built without pthread workqueue QoS support, so the thread cannot be
 * overridden (can_override 0, QOS_CLASS_UNSPECIFIED) and the calls succeed
 * without changing anything. */
struct override_info { uint32_t can_override:1, unused:31; unsigned qos_floor; };
typedef struct override_info (*get_floor_fn)(void);
typedef int (*override_self_fn)(unsigned);
typedef int (*lock_start_fn)(uint32_t *, uint32_t, unsigned);
typedef int (*lock_end_fn)(unsigned);
typedef _Bool (*verify_fn)(dispatch_queue_t);

#define LOOKUP(type, name) \
    type name = (type)dlsym(RTLD_DEFAULT, #name); \
    if (!name) { puts("FAIL missing symbol " #name); return 1; }

static verify_fn verify;
static dispatch_queue_t q1, q2, qc;
static int ran;

static void on_q1(void *unused)
{
    (void)unused;
    assert(verify(q1));
    assert(!verify(q2));
    assert(!verify(qc));
    ran++;
}

/* q1 is draining (it holds q1's drain lock) while q2 runs inline on this
 * thread: a serial dispatch_sync_f takes the queue's drain lock, so both
 * answers are true through the drain lock. */
static void on_q2_from_q1(void *unused)
{
    (void)unused;
    assert(verify(q2));
    assert(verify(q1));
    assert(!verify(qc));
    ran++;
}

static void sync_q2_from_q1(void *unused)
{
    (void)unused;
    dispatch_sync_f(q2, NULL, on_q2_from_q1);
}

/* A non-barrier dispatch_sync_f onto a concurrent queue takes no drain lock:
 * the queue is only on the thread's frame stack, so a true answer proves the
 * frame lookup. */
static void on_qc(void *unused)
{
    (void)unused;
    assert(verify(qc));
    assert(!verify(q1));
    ran++;
}

static void on_q1_async(void *ctxt)
{
    assert(verify(q1) && !verify(q2) && !verify(qc));
    dispatch_semaphore_signal((dispatch_semaphore_t)ctxt);
}

int main(void)
{
    setbuf(stdout, NULL);
    alarm(20);
    LOOKUP(get_floor_fn, dispatch_thread_get_current_override_qos_floor);
    LOOKUP(override_self_fn, dispatch_thread_override_self);
    LOOKUP(lock_start_fn, dispatch_lock_override_start_with_debounce);
    LOOKUP(lock_end_fn, dispatch_lock_override_end);
    LOOKUP(verify_fn, dispatch_verify_current_queue_4swiftonly);
    verify = dispatch_verify_current_queue_4swiftonly;

    struct override_info before = dispatch_thread_get_current_override_qos_floor();
    assert(before.can_override == 0 && before.qos_floor == 0);
    puts("CHECK floor reports a thread that cannot be overridden");
    assert(dispatch_thread_override_self(0x19) == 0);
    struct override_info after = dispatch_thread_get_current_override_qos_floor();
    assert(after.can_override == 0 && after.qos_floor == 0);
    uint32_t lock = 0;
    assert(dispatch_lock_override_start_with_debounce(&lock, 0, 0x19) == 0);
    assert(lock == 0);
    assert(dispatch_lock_override_end(0x19) == 0);
    puts("CHECK override calls return 0 and leave the thread unchanged");

    q1 = dispatch_queue_create("spi.q1", NULL);
    q2 = dispatch_queue_create("spi.q2", NULL);
    qc = dispatch_queue_create("spi.qc", DISPATCH_QUEUE_CONCURRENT);
    assert(!verify(q1) && !verify(q2) && !verify(qc));
    puts("CHECK false from a thread on none of the queues");
    dispatch_sync_f(q1, NULL, on_q1);
    puts("CHECK serial queue: true for q1 only, inside dispatch_sync_f");
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    dispatch_async_f(q1, done, on_q1_async);
    assert(dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5000000000LL)) == 0);
    puts("CHECK serial queue: true for q1 only, inside dispatch_async_f");
    dispatch_sync_f(q1, NULL, sync_q2_from_q1);
    puts("CHECK nested serial sync: both queues by drain lock");
    dispatch_sync_f(qc, NULL, on_qc);
    puts("CHECK concurrent queue: true inside a non-barrier dispatch_sync_f (frame only)");
    assert(ran == 3);
    assert(!verify(q1) && !verify(q2) && !verify(qc));
    dispatch_release(q1);
    dispatch_release(q2);
    dispatch_release(qc);
    dispatch_release(done);
    alarm(0);
    puts("PASS Swift concurrency SPIs");
    return 0;
}
