#undef NDEBUG
#include <dispatch/dispatch.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/* Swift's 64-bit ObjC-interoperable job prefix, not a Swift runtime task.
 * SchedulerPrivate[0] supplies Dispatch's intrusive queue link. */
struct job;
struct metadata {
    void *header[5];
    unsigned long type;
    void (*invoke)(struct job *, void *, uint32_t);
};
struct __attribute__((aligned(16))) job {
    const struct metadata *metadata;
    uintptr_t refcounts;
    void *scheduler[2];
    uint32_t flags, id;
    void *voucher, *reserved;
    dispatch_semaphore_t done;
    dispatch_queue_t queue;
    unsigned remaining, invocations, ordered;
};
static unsigned completed;
static void (*enqueue)(dispatch_queue_t, void *, unsigned);
static void invoke(struct job *job, void *context, uint32_t flags)
{
    assert(context == NULL && flags == 0);
    assert(job->flags == 0x12345678);
    assert(job->refcounts == 0x98765432 && job->reserved == job);
    ++job->invocations;
    if (--job->remaining) {
        /* Ownership returns to the scheduler here. Do not touch the job after
         * enqueue: a concurrent queue may begin its next invocation at once. */
        enqueue(job->queue, job, 21);
        return;
    }
    unsigned position = __atomic_fetch_add(&completed, 1, __ATOMIC_RELAXED);
    if (job->ordered) assert(job->id == position);
    dispatch_semaphore_signal(job->done);
}
int main(void)
{
    setbuf(stdout, NULL);
    alarm(10);
    enqueue = dlsym(RTLD_DEFAULT, "dispatch_async_swift_job");
    assert(enqueue);
    static const struct metadata metadata = {.type=1, .invoke=invoke};
    struct job jobs[128] = {0};
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    dispatch_queue_t queue = dispatch_queue_create("swift.abi.queue.probe", NULL);
    dispatch_suspend(queue);
    for (unsigned i=0; i<128; ++i) {
        jobs[i].metadata = &metadata;
        jobs[i].refcounts = 0x98765432;
        jobs[i].flags = 0x12345678;
        jobs[i].id = i;
        jobs[i].reserved = &jobs[i];
        jobs[i].done = done;
        jobs[i].queue = queue;
        jobs[i].remaining = 1;
        jobs[i].ordered = 1;
        enqueue(queue, &jobs[i], 21);
    }
    assert(completed == 0);
    dispatch_resume(queue);
    for (unsigned i=0; i<128; ++i)
        assert(dispatch_semaphore_wait(done,
            dispatch_time(DISPATCH_TIME_NOW, 2000000000LL)) == 0);
    assert(completed == 128);
    dispatch_release(queue);
    puts("CHECK serial Swift-shaped jobs delivered");
    queue = dispatch_queue_create("swift.abi.concurrent.probe", DISPATCH_QUEUE_CONCURRENT);
    __atomic_store_n(&completed, 0, __ATOMIC_RELAXED);
    for (unsigned i=0; i<128; ++i) {
        jobs[i].queue = queue;
        jobs[i].remaining = 8;
        jobs[i].invocations = 0;
        jobs[i].ordered = 0;
        enqueue(queue, &jobs[i], 21);
    }
    for (unsigned i=0; i<128; ++i)
        assert(dispatch_semaphore_wait(done,
            dispatch_time(DISPATCH_TIME_NOW, 2000000000LL)) == 0);
    assert(__atomic_load_n(&completed, __ATOMIC_RELAXED) == 128);
    for (unsigned i=0; i<128; ++i) assert(jobs[i].invocations == 8);
    dispatch_release(queue);
    dispatch_release(done);
    alarm(0);
    puts("PASS ABI-shaped Swift jobs: serial order, concurrent delivery and re-enqueue");
}
