#undef NDEBUG
#include <assert.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Pinned to Swift 6.2.4 Task.cpp, ABI/Task.h and Runtime/Concurrency.h.
 * Non-future detached task; completion releases its running ownership. */
struct context;
typedef void __attribute__((swiftasynccall)) completion_function(
    struct context * __attribute__((swift_async_context)),
    void * __attribute__((swift_context)));
struct context { struct context *parent; completion_function *resume; };
typedef void __attribute__((swiftasynccall)) entry_function(
    struct context * __attribute__((swift_async_context)),
    void * __attribute__((swift_context)));
struct task_context { void *task; struct context *context; };
typedef struct task_context __attribute__((swiftcall)) create_function(
    size_t, void *, void *, entry_function *, void *, size_t);
static dispatch_semaphore_t done;
struct executor { void *identity; uintptr_t implementation; };
typedef void __attribute__((swiftcall)) run_function(void *, struct executor);
static void __attribute__((swiftasynccall)) entry(
    struct context *context __attribute__((swift_async_context)),
    void *closure __attribute__((swift_context)))
{
    puts("CHECK entered Swift async callback");
    assert(context && context->resume && closure == NULL);
    dispatch_semaphore_signal(done);
    puts("CHECK returning through Swift completion continuation");
    /* The initial completion reads AsyncContextPrefix immediately before this
     * context; Parent is NULL for this detached task, not its completion input. */
    return context->resume(context, NULL);
}
static void barrier(void *unused) { (void)unused; }
int main(void)
{
    setbuf(stdout, NULL);
    alarm(10);
    void *runtime = dlopen("/usr/lib/swift/libswift_Concurrency.dylib", RTLD_NOW|RTLD_LOCAL);
    if (!runtime) { puts(dlerror()); return 1; }
    create_function *create = (create_function *)dlsym(runtime, "swift_task_create_common");
    void (*enqueue)(dispatch_queue_t, void *, unsigned) =
        dlsym(RTLD_DEFAULT, "dispatch_async_swift_job");
    assert(create && enqueue);
    done = dispatch_semaphore_create(0);
    dispatch_queue_t queue = dispatch_queue_create("swift.runtime.task", NULL);
    struct task_context task = create(21 | (1u<<14), NULL, NULL, entry, NULL,
        sizeof(struct context));
    assert(task.task && task.context);
    puts("CHECK real Swift task created");
    if (getenv("TEST_SWIFT_DIRECT")) {
        run_function *run = (run_function *)dlsym(runtime, "swift_job_run");
        assert(run);
        run(task.task, (struct executor){NULL, 0});
    } else {
        enqueue(queue, task.task, 21);
    }
    assert(dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 2000000000LL)) == 0);
    dispatch_sync_f(queue, NULL, barrier);
    dispatch_release(queue);
    dispatch_release(done);
    alarm(0);
    puts("PASS runtime-created Swift task executed and completed");
}
