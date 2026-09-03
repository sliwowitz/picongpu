/* Refuses a device allocation that is larger than it should be, at the place it is asked
 * for, and says where it was asked from. An outside watchdog cannot do this: the driver
 * holds the memory without it being charged to the process, so by the time the machine
 * notices, killing the process does not give it back. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GIB (1024UL * 1024UL * 1024UL)
#define OOM 2 /* cudaErrorMemoryAllocation and CUDA_ERROR_OUT_OF_MEMORY are both 2 */

static size_t g_one = 4 * GIB;   /* largest single allocation allowed */
static size_t g_all = 16 * GIB;  /* largest total allowed */
static size_t g_total = 0;
static int g_fd = 2;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

__attribute__((constructor)) static void setup(void) {
    const char* v;
    if ((v = getenv("ALLOCGUARD_ONE_GIB"))) g_one = (size_t) atoll(v) * GIB;
    if ((v = getenv("ALLOCGUARD_ALL_GIB"))) g_all = (size_t) atoll(v) * GIB;
    if ((v = getenv("ALLOCGUARD_LOG"))) {
        int fd = open(v, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) g_fd = fd;
    }
}

/* Returns non-zero when the request is refused. Logs every request, with a backtrace for
 * the large ones, so the caller that asks for too much is named. */
static int refuse(const char* what, size_t size) {
    pthread_mutex_lock(&g_lock);
    const int over = (size > g_one) || (g_total + size > g_all);
    char line[320];
    int n = snprintf(line, sizeof line, "[allocguard] %-22s %10.3f GiB   total %8.3f GiB%s\n",
                     what, (double) size / GIB, (double) g_total / GIB,
                     over ? "   REFUSED" : "");
    (void) !write(g_fd, line, (size_t) n);
    if (over || size >= GIB / 4) {
        void* bt[24];
        int m = backtrace(bt, 24);
        backtrace_symbols_fd(bt, m, g_fd);
        (void) !write(g_fd, "\n", 1);
    }
    if (!over) g_total += size;
    pthread_mutex_unlock(&g_lock);
    return over;
}

#define WRAP(name, sig, call, what, sz)                                                    \
    typedef int (*name##_t) sig;                                                           \
    int name sig {                                                                         \
        static name##_t real = NULL;                                                        \
        if (!real) real = (name##_t) dlsym(RTLD_NEXT, #name);                                \
        if (refuse(what, (size_t)(sz))) return OOM;                                          \
        return real call;                                                                    \
    }

WRAP(cudaMalloc,        (void** p, size_t s),                     (p, s),        "cudaMalloc",        s)
WRAP(cudaMallocManaged, (void** p, size_t s, unsigned f),         (p, s, f),     "cudaMallocManaged", s)
WRAP(cudaMallocHost,    (void** p, size_t s),                     (p, s),        "cudaMallocHost",    s)
WRAP(cudaHostAlloc,     (void** p, size_t s, unsigned f),         (p, s, f),     "cudaHostAlloc",     s)
WRAP(cudaMallocAsync,   (void** p, size_t s, void* q),            (p, s, q),     "cudaMallocAsync",   s)
WRAP(cuMemAlloc_v2,     (unsigned long long* p, size_t s),        (p, s),        "cuMemAlloc",        s)
WRAP(cuMemAllocManaged, (unsigned long long* p, size_t s, unsigned f), (p, s, f),"cuMemAllocManaged", s)
WRAP(cuMemAllocHost_v2, (void** p, size_t s),                     (p, s),        "cuMemAllocHost",    s)
WRAP(cuMemAddressReserve,(unsigned long long* p, size_t s, size_t a, unsigned long long ad, unsigned long long fl), (p, s, a, ad, fl), "cuMemAddressReserve", s)
