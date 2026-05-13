/* tools/mallocnesia/mallocnesia.c
 *
 * LD_PRELOAD interceptor for the allocation discipline gate (seam #6).
 * Counts malloc/calloc/realloc calls between alloc_guard_start() and
 * alloc_guard_end() and exits 1 if the count exceeds MALLOCNESIA_MAX_ALLOCS.
 *
 * Build:  make -C tools/mallocnesia
 * Use:    MALLOCNESIA_PATH=tools/mallocnesia/libmallocnesia.so
 *         python3 tools/check_alloc.py --binary <binary>
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*malloc_fn)(size_t);
typedef void  (*free_fn)(void *);
typedef void *(*calloc_fn)(size_t, size_t);
typedef void *(*realloc_fn)(void *, size_t);

static malloc_fn  real_malloc;
static free_fn    real_free;
static calloc_fn  real_calloc;
static realloc_fn real_realloc;

/* dlsym calls calloc internally before real_calloc is resolved.
 * Serve those early calls from a static buffer to break the cycle. */
static char   bootstrap[8192];
static size_t bootstrap_pos;
static int    bootstrap_done;  /* set to 1 after dlsym calls complete */

static _Atomic int  g_active;  /* 1 while between start/end markers */
static _Atomic long g_count;   /* allocations intercepted this guard window */
static long         g_max;     /* from MALLOCNESIA_MAX_ALLOCS env var */

/* Per-thread flag to avoid re-entering our hook from fprintf inside the hook */
static __thread int g_in_hook;

static void resolve_fns(void) {
    real_malloc  = (malloc_fn) dlsym(RTLD_NEXT, "malloc");
    real_calloc  = (calloc_fn) dlsym(RTLD_NEXT, "calloc");
    real_realloc = (realloc_fn)dlsym(RTLD_NEXT, "realloc");
    real_free    = (free_fn)   dlsym(RTLD_NEXT, "free");
    bootstrap_done = 1;
}

__attribute__((constructor))
static void mallocnesia_init(void) {
    resolve_fns();
}

/* --- Guard markers (override the weak no-op symbols in the test binary) --- */

void alloc_guard_start(void) {
    const char *env = getenv("MALLOCNESIA_MAX_ALLOCS");
    g_max = env ? atol(env) : 0;
    atomic_store(&g_count, 0);
    atomic_store(&g_active, 1);
}

void alloc_guard_end(void) {
    atomic_store(&g_active, 0);
    long count = atomic_load(&g_count);
    if (count > g_max) {
        fprintf(stderr,
            "[mallocnesia] FAIL: %ld allocation(s) intercepted between guard markers "
            "(max allowed: %ld)\n", count, g_max);
        exit(1);
    }
}

/* --- Allocator hooks --- */

void *malloc(size_t size) {
    if (!real_malloc) resolve_fns();
    if (atomic_load(&g_active) && !g_in_hook) {
        g_in_hook = 1;
        long n = atomic_fetch_add(&g_count, 1) + 1;
        fprintf(stderr, "[mallocnesia] intercepted malloc(%zu) — call #%ld\n", size, n);
        g_in_hook = 0;
    }
    return real_malloc(size);
}

void free(void *ptr) {
    /* Bootstrap allocations live in the static buffer — nothing to free */
    if ((char *)ptr >= bootstrap && (char *)ptr < bootstrap + sizeof(bootstrap))
        return;
    if (!real_free) resolve_fns();
    real_free(ptr);
}

void *calloc(size_t nmemb, size_t size) {
    /* Serve bootstrap calls (dlsym init) from the static buffer */
    if (!bootstrap_done) {
        size_t total = nmemb * size;
        if (bootstrap_pos + total <= sizeof(bootstrap)) {
            void *p = bootstrap + bootstrap_pos;
            bootstrap_pos += total;
            memset(p, 0, total);
            return p;
        }
        return NULL;
    }
    if (atomic_load(&g_active) && !g_in_hook) {
        g_in_hook = 1;
        long n = atomic_fetch_add(&g_count, 1) + 1;
        fprintf(stderr, "[mallocnesia] intercepted calloc(%zu, %zu) — call #%ld\n",
                nmemb, size, n);
        g_in_hook = 0;
    }
    return real_calloc(nmemb, size);
}

void *realloc(void *ptr, size_t size) {
    if (!real_realloc) resolve_fns();
    if (atomic_load(&g_active) && !g_in_hook) {
        g_in_hook = 1;
        long n = atomic_fetch_add(&g_count, 1) + 1;
        fprintf(stderr, "[mallocnesia] intercepted realloc(%p, %zu) — call #%ld\n",
                ptr, size, n);
        g_in_hook = 0;
    }
    return real_realloc(ptr, size);
}
