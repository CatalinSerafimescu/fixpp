/* tools/mallocnesia/mallocnesia.c
 *
 * LD_PRELOAD interceptor for the allocation discipline gate (seam #6).
 * Counts calls to libc's heap-allocating entry points between alloc_guard_start()
 * and alloc_guard_end() and exits 1 if the count exceeds MALLOCNESIA_MAX_ALLOCS.
 *
 * WHICH ENTRY POINTS (fixpp#497). The population is libc's allocator API: the exported
 * functions whose result is new heap memory handed to the caller (malloc.h, stdlib.h).
 * Each is hooked here unless one of these holds for it:
 *   - libc serves it through a function hooked here, so it is already counted;
 *   - no installed header declares it, so ordinary code cannot call it
 *     (`grep -rn <name> /usr/include`);
 *   - it returns no new memory.
 * To re-derive the set for a libc, list its exports (`nm -D --defined-only <libc.so>`)
 * and decide each against the rule. Every hooked function, and every one left unhooked
 * on the first ground, needs an arm in ci/test-check-alloc.sh's T10, which calls it in a
 * window and requires the interceptor to name the function that counted it: the first
 * ground is a fact about one libc's internals, and the arm is what notices when it
 * stops holding. Every hooked function also needs a positive control in the gate
 * population (tests/alloc_guard/planted_entry_witness.cpp, declared in
 * tools/check_mallocnesia_population.py): this file is built without coverage
 * instrumentation, so those controls are what show each hook runs on a CI lane.
 * Functions OUTSIDE the API that allocate as a side effect (strdup,
 * asprintf, ...) are counted through whichever hook they reach; T10's strdup arm is a
 * representative of that class, not a census of it. The aligned hooks matter to C++:
 * an over-aligned `new` reaches libc through one of them, not through malloc.
 *
 * Build:  it is a CMake target — `cmake --build <dir> --target mallocnesia` builds it,
 *         and an ordinary build of the test tree builds it anyway. The artifact lands at
 *         <build>/lib/libmallocnesia.so and is gitignored; only this source is tracked.
 *
 *         ⚠️ There is NO hand-build route any more. A Makefile here produced a
 *         gitignored .so in the SOURCE tree, every gate was registered inside
 *         `if(EXISTS <that path>)`, and on any machine that had not run it — every CI
 *         runner — the gates were silently never registered. fixpp#448 deleted both the
 *         Makefile and that path: a precondition someone has to remember is one CI never
 *         satisfies. Do not reintroduce them.
 *
 * Use:    the gates go through tools/check_alloc.py, which CMake invokes with
 *         --mallocnesia $<TARGET_FILE:mallocnesia>. By hand:
 *         python3 tools/check_alloc.py --binary <binary> \
 *                 --mallocnesia <build>/lib/libmallocnesia.so
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void *(*malloc_fn)(size_t);
typedef void  (*free_fn)(void *);
typedef void *(*calloc_fn)(size_t, size_t);
typedef void *(*realloc_fn)(void *, size_t);
typedef void *(*aligned_fn)(size_t, size_t);   /* aligned_alloc, memalign */
typedef int   (*posix_memalign_fn)(void **, size_t, size_t);
typedef void *(*page_fn)(size_t);              /* valloc, pvalloc */

static malloc_fn  real_malloc;
static free_fn    real_free;
static calloc_fn  real_calloc;
static realloc_fn real_realloc;
static aligned_fn        real_aligned_alloc;
static aligned_fn        real_memalign;
static posix_memalign_fn real_posix_memalign;
static page_fn           real_valloc;
static page_fn           real_pvalloc;

/* A hook called before the real functions are resolved resolves them itself: ld.so
 * runs a needed library's constructor before this interceptor's, so such a call is
 * ordinary. Only a call made WHILE resolve_fns() runs (a libc whose dlsym allocates)
 * cannot resolve again without recursing. calloc serves that call from this static
 * buffer; every other hook aborts on it, saying so. g_resolving marks the window. */
static char   bootstrap[8192];
static size_t bootstrap_pos;
static int    g_resolving;

static _Atomic int  g_active;  /* 1 while between start/end markers */
static _Atomic long g_count;   /* allocations intercepted this guard window */
static long         g_max;     /* from MALLOCNESIA_MAX_ALLOCS env var */

/* Per-thread flag to avoid re-entering our hook from fprintf inside the hook */
static __thread int g_in_hook;

/* POSIX guarantees that a dlsym() result converts to a function pointer; ISO C does
 * not, and gcc -Wpedantic rejects the cast. So the bits are copied instead (the POSIX
 * dlsym() example writes through a `void **` alias for the same reason). */
#define RESOLVE(var, name)                                   \
    do {                                                     \
        void *sym_ = dlsym(RTLD_NEXT, name);                 \
        memcpy(&(var), &sym_, sizeof(var));                  \
    } while (0)

static void resolve_fns(void) {
    g_resolving = 1;
    RESOLVE(real_malloc,         "malloc");
    RESOLVE(real_calloc,         "calloc");
    RESOLVE(real_realloc,        "realloc");
    RESOLVE(real_free,           "free");
    RESOLVE(real_aligned_alloc,  "aligned_alloc");
    RESOLVE(real_memalign,       "memalign");
    RESOLVE(real_posix_memalign, "posix_memalign");
    RESOLVE(real_valloc,         "valloc");
    RESOLVE(real_pvalloc,        "pvalloc");
    g_resolving = 0;
}

/* The aligned hooks' lazy resolve: the static buffer above is calloc's alone. */
static void resolve_or_abort(void) {
    if (g_resolving) {
        static const char msg[] =
            "[mallocnesia] FATAL: an aligned allocation re-entered resolve_fns()\n";
        (void)!write(2, msg, sizeof msg - 1);
        abort();
    }
    resolve_fns();
}

/* One count, one line on stderr naming the function: ci/test-check-alloc.sh reads the
 * name back, so a window's allocation is attributed to the entry point that made it. */
static void count_aligned(const char *fn, size_t align, size_t size) {
    if (atomic_load(&g_active) && !g_in_hook) {
        g_in_hook = 1;
        long n = atomic_fetch_add(&g_count, 1) + 1;
        fprintf(stderr, "[mallocnesia] intercepted %s(%zu, %zu) — call #%ld\n",
                fn, align, size, n);
        g_in_hook = 0;
    }
}

/* fixpp#448: PROOF OF INTERCEPTION.
 *
 * `LD_PRELOAD=/nonexistent/libmallocnesia.so` is NOT an error: ld.so prints
 * "cannot be preloaded ... ignored" and runs the binary UNINSTRUMENTED, which then
 * exits 0 and reads as a passing gate. Measured on main before this change, that is
 * how several gates were green.
 *
 * A gate cannot prove its own instrumentation from inside the parent process, so the
 * CHILD leaves evidence at MALLOCNESIA_WITNESS. THREE notes, not one, each tagged with
 * the writing process's pid:
 *
 *   loaded  this .so was actually mapped and its constructor ran
 *   start   THIS binary called alloc_guard_start, and OUR definition answered
 *   end     ... and OUR alloc_guard_end answered too
 *
 * ⚠️ "loaded" ALONE IS NOT ENOUGH, and an earlier revision required only that.
 * `alloc_guard_start`/`_end` are WEAK UNDEFINED in the test binaries, so a STRONG
 * definition anywhere in the link closure wins over this preload: the constructor still
 * runs and still writes "loaded", while `g_active` is never set and every allocation
 * sails past. The same split appears under a sanitizer, whose allocator interposes
 * ahead of these hooks. Constructor execution and symbol interposition are different
 * facts; only the start/end notes distinguish them, because only OUR definitions
 * write them.
 *
 * open()/write(), never fopen(): this runs as a malloc interposer, and the stdio
 * path allocates through the very hooks being installed.
 */
static void mallocnesia_note(const char *what) {
    const char *path = getenv("MALLOCNESIA_WITNESS");
    if (!path || !*path) return;
    /* O_APPEND, not O_TRUNC: the three notes accumulate. */
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    char buf[64];
    /* snprintf, not fprintf: this runs as a malloc interposer and the stdio path
     * allocates through the very hooks being installed. */
    int n = snprintf(buf, sizeof buf, "%s %ld\n", what, (long)getpid());
    if (n > 0) (void)!write(fd, buf, (size_t)n);
    close(fd);
}

__attribute__((constructor))
static void mallocnesia_init(void) {
    resolve_fns();
    mallocnesia_note("loaded");
}

/* --- Guard markers (override the weak no-op symbols in the test binary) --- */

void alloc_guard_start(void) {
    mallocnesia_note("start");
    const char *env = getenv("MALLOCNESIA_MAX_ALLOCS");
    g_max = env ? atol(env) : 0;
    atomic_store(&g_count, 0);
    atomic_store(&g_active, 1);
}

void alloc_guard_end(void) {
    mallocnesia_note("end");
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
    /* Bootstrap allocations live in the static buffer — nothing to free. One unsigned
     * compare: a relational compare against a pointer outside the buffer is undefined. */
    if ((uintptr_t)ptr - (uintptr_t)bootstrap < sizeof(bootstrap))
        return;
    if (!real_free) resolve_fns();
    real_free(ptr);
}

void *calloc(size_t nmemb, size_t size) {
    /* Serve bootstrap calls (dlsym init) from the static buffer */
    if (g_resolving) {
        size_t total = nmemb * size;
        if (bootstrap_pos + total <= sizeof(bootstrap)) {
            void *p = bootstrap + bootstrap_pos;
            bootstrap_pos += total;
            memset(p, 0, total);
            return p;
        }
        return NULL;
    }
    if (!real_calloc) resolve_fns();
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

/* --- Aligned and page-aligned hooks (fixpp#497) --- */

void *aligned_alloc(size_t align, size_t size) {
    if (!real_aligned_alloc) resolve_or_abort();
    count_aligned("aligned_alloc", align, size);
    return real_aligned_alloc(align, size);
}

void *memalign(size_t align, size_t size) {
    if (!real_memalign) resolve_or_abort();
    count_aligned("memalign", align, size);
    return real_memalign(align, size);
}

int posix_memalign(void **memptr, size_t align, size_t size) {
    if (!real_posix_memalign) resolve_or_abort();
    count_aligned("posix_memalign", align, size);
    return real_posix_memalign(memptr, align, size);
}

/* valloc/pvalloc take no alignment argument; the page size is the alignment. */
void *valloc(size_t size) {
    if (!real_valloc) resolve_or_abort();
    count_aligned("valloc", (size_t)sysconf(_SC_PAGESIZE), size);
    return real_valloc(size);
}

void *pvalloc(size_t size) {
    if (!real_pvalloc) resolve_or_abort();
    count_aligned("pvalloc", (size_t)sysconf(_SC_PAGESIZE), size);
    return real_pvalloc(size);
}
