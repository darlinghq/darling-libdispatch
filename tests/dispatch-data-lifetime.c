#undef NDEBUG
#include <assert.h>
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    setbuf(stdout, NULL);
    alarm(10);
    dispatch_queue_t queue = dispatch_queue_create("data.destructor", NULL);
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    char *bytes = malloc(6);
    assert(bytes);
    memcpy(bytes, "abcdef", 6);
    __block unsigned destroyed = 0;
    dispatch_data_t original = dispatch_data_create(bytes, 6, queue, ^{
        ++destroyed;
        free(bytes);
        dispatch_semaphore_signal(done);
    });
    assert(original);
    dispatch_data_t slice = dispatch_data_create_subrange(original, 1, 4);
    dispatch_data_t joined = dispatch_data_create_concat(slice, slice);
    assert(slice && joined && dispatch_data_get_size(joined) == 8);
    dispatch_release(original);
    dispatch_release(slice);
    assert(dispatch_semaphore_wait(done, DISPATCH_TIME_NOW) != 0);
    const void *mapped = NULL;
    size_t size = 0;
    dispatch_data_t map = dispatch_data_create_map(joined, &mapped, &size);
    assert(map && size == 8 && memcmp(mapped, "bcdebcde", 8) == 0);
    dispatch_release(map);
    dispatch_release(joined);
    assert(dispatch_semaphore_wait(done,
        dispatch_time(DISPATCH_TIME_NOW, 2000000000LL)) == 0);
    dispatch_sync(queue, ^{ assert(destroyed == 1); });
    dispatch_release(done);
    dispatch_release(queue);
    alarm(0);
    puts("PASS dispatch data mapping and shared-storage destruction");
    return 0;
}
