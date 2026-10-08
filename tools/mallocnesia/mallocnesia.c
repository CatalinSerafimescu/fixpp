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
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* claim-ok: PR #557 Gate B r2 FQ-A — direct __libc_* forwarding; Gate B r3 FQ-E — refuse mixed allocator processes; supersedes Gate B r1 FQ-2's lazy dlsym resolution.
 *
 * Every hook forwards to glibc's own allocator by the __libc_* names glibc exports, so
 * nothing is resolved at run time: there is no function table to fill, no window in which
 * a hook runs before it is filled, and no state that a second thread or a fork could see
 * half-written. A hook called from a library constructor that runs before this one
 * (ci/test-check-alloc.sh T11) is therefore an ordinary call. The rejected alternative
 * kept dlsym(RTLD_NEXT) and synchronised it (a once-initialised table, a per-thread
 * "resolving" marker, fork handling, a static buffer for an allocating dlsym), each piece
 * needing a test seam of its own. Restoring a free hook would pair the standard names
 * only; a block from another allocator's own API would then reach glibc's free.
 *
 * The CONDITION this rests on: the libc exports __libc_malloc, __libc_calloc,
 * __libc_realloc, __libc_memalign, __libc_valloc and __libc_pvalloc as public, linkable
 * symbols; no header declares them, hence the declarations below. Re-check a libc with
 * `nm -D --defined-only <libc.so> | grep ' __libc_'`, and `objdump -T <libc.so>` to see
 * that each shares its address with the public name it stands for. A libc without them
 * gates are registered only on glibc. At run time, glibc must serve the standard names
 * this file does not define, and nothing may define malloc ahead of this file.
 *
 * glibc has no __libc_ twin for aligned_alloc or posix_memalign, so those two hooks
 * validate their arguments as glibc does and forward to __libc_memalign. T12 in
 * ci/test-check-alloc.sh compares every hook's results against unhooked glibc, which is
 * what notices if that validation and glibc's stop agreeing. */
extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void *__libc_memalign(size_t, size_t);
extern void *__libc_valloc(size_t);
extern void *__libc_pvalloc(size_t);

static _Atomic int  g_active;  /* 1 while between start/end markers */
static _Atomic long g_count;   /* allocations intercepted this guard window */
static long         g_max;     /* from MALLOCNESIA_MAX_ALLOCS env var */

/* Per-thread flag to avoid re-entering our hook from fprintf inside the hook */
static __thread int g_in_hook;

/* One count, one line on stderr naming the function: ci/test-check-alloc.sh reads the
 * name back, so a window's allocation is attributed to the entry point that made it.
 * errno is the caller's: a failed write to stderr must not show through a successful
 * allocation (posix_memalign's contract leaves errno alone). */
static void count(const char *fn, size_t a, size_t b) {
    if (atomic_load(&g_active) && !g_in_hook) {
        int saved = errno;
        g_in_hook = 1;
        long n = atomic_fetch_add(&g_count, 1) + 1;
        fprintf(stderr, "[mallocnesia] intercepted %s(%zu, %zu) — call #%ld\n", fn, a, b, n);
        g_in_hook = 0;
        errno = saved;
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

/* Only glibc's allocator may serve the standard names this file does not define (free,
 * malloc_usable_size, ...), and nothing may define malloc ahead of this file. Otherwise
 * the process allocates or frees through code these hooks never see. */
__attribute__((constructor))
static void mallocnesia_init(void) {
    Dl_info self = {0}, front = {0}, next = {0};
    void *f = dlsym(RTLD_DEFAULT, "malloc");
    void *n = dlsym(RTLD_NEXT, "malloc");
    void *(*next_malloc)(size_t);
    *(void **)&next_malloc = n;

    dladdr((void *)&g_count, &self);
    dladdr(f, &front);
    dladdr(n, &next);
    if (front.dli_fbase != self.dli_fbase || next_malloc != __libc_malloc) {
        fprintf(stderr, "[mallocnesia] REFUSED: malloc is defined in %s ahead of this "
                "interceptor and in %s behind it; only glibc may stand behind it\n",
                front.dli_fname ? front.dli_fname : "?",
                next.dli_fname ? next.dli_fname : "?");
        mallocnesia_note("refused");
        _exit(1);
    }
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
    /* Counts the allocations that happen-before this call; work on another thread must
     * be joined or awaited before the window closes. */
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
    count("malloc", size, 0);
    return __libc_malloc(size);
}

void *calloc(size_t nmemb, size_t size) {
    count("calloc", nmemb, size);
    return __libc_calloc(nmemb, size);
}

void *realloc(void *ptr, size_t size) {
    count("realloc", (size_t)(uintptr_t)ptr, size);
    return __libc_realloc(ptr, size);
}

/* --- Aligned and page-aligned hooks (fixpp#497) --- */

void *aligned_alloc(size_t align, size_t size) {
    count("aligned_alloc", align, size);
    /* glibc: EINVAL and NULL unless align is a power of two. */
    if (align == 0 || (align & (align - 1)) != 0) {
        errno = EINVAL;
        return NULL;
    }
    return __libc_memalign(align, size);
}

void *memalign(size_t align, size_t size) {
    count("memalign", align, size);
    return __libc_memalign(align, size);
}

int posix_memalign(void **memptr, size_t align, size_t size) {
    count("posix_memalign", align, size);
    /* glibc: EINVAL unless align is a power-of-two multiple of sizeof(void *); *memptr is
     * written only on success. */
    if (align == 0 || align % sizeof(void *) != 0
        || ((align / sizeof(void *)) & (align / sizeof(void *) - 1)) != 0)
        return EINVAL;
    void *p = __libc_memalign(align, size);
    if (!p) return ENOMEM;
    *memptr = p;
    return 0;
}

/* valloc/pvalloc take no alignment argument; the page size is the alignment. */
void *valloc(size_t size) {
    count("valloc", (size_t)sysconf(_SC_PAGESIZE), size);
    return __libc_valloc(size);
}

void *pvalloc(size_t size) {
    count("pvalloc", (size_t)sysconf(_SC_PAGESIZE), size);
    return __libc_pvalloc(size);
}
