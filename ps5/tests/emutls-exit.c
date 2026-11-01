// Exercise the emutls lifetime needed by libc++abi's pthread-key fallback.
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

// compiler-rt's public emutls control ABI on the 64-bit build hosts and PS5.
typedef struct {
    uintptr_t size, align, index;
    void *initial;
} Control;
extern void *__emutls_get_address(Control *control);

static int initial = 7;
static Control value = {sizeof(int), _Alignof(int), 0, &initial};
static Control zero = {sizeof(int), _Alignof(int), 0, NULL};
static pthread_key_t cleanup_key;

typedef struct {
    int *address;
    int expected;
    int cleaned;
} Result;

static void cleanup(void *pointer) {
    Result *result = pointer;
    // The emutls key was created first. A later pthread-key destructor must
    // still see the same object and its current value after TLS cleanup starts.
    assert(__emutls_get_address(&value) == result->address);
    assert(*result->address == result->expected);
    ++result->cleaned;
}

static void *warmup(void *unused) {
    (void)unused;
    assert(*(int *)__emutls_get_address(&value) == initial);
    return NULL;
}

static void *worker(void *pointer) {
    Result *result = pointer;
    int *local = __emutls_get_address(&value);
    int *local_zero = __emutls_get_address(&zero);
    assert(*local == initial && *local_zero == 0);
    *local = result->expected;
    *local_zero = 123;
    result->address = local;
    assert(pthread_setspecific(cleanup_key, result) == 0);
    pthread_exit(result); // Matches the capture thread's explicit thread exit.
}

int main(void) {
    pthread_t thread;
    assert(pthread_create(&thread, NULL, warmup, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(pthread_key_create(&cleanup_key, cleanup) == 0);
    for (int batch = 0; batch < 16; ++batch) {
        pthread_t threads[8];
        Result results[8] = {0};
        for (int i = 0; i < 8; ++i) {
            results[i].expected = batch * 8 + i + 100;
            assert(pthread_create(&threads[i], NULL, worker, &results[i]) == 0);
        }
        for (int i = 0; i < 8; ++i) {
            void *returned = NULL;
            assert(pthread_join(threads[i], &returned) == 0);
            assert(returned == &results[i]);
            assert(results[i].cleaned == 1);
        }
    }
    assert(pthread_key_delete(cleanup_key) == 0);
}
