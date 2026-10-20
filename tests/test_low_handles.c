/* Guest-visible handles below 1 TiB (runtime_low_alloc) and the direct-memory pool fallback.
 * The game keeps lock owners in 40 bits (issue #51), so thread, TLS, mutex, cond and rwlock
 * records must stay below 1 TiB. Build (MSYS2 CLANG64 or Linux):
 *   cc -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_low_handles.c \
 *      src/runtime_memory.c src/runtime_mutex.c -o out/low-handles-test
 *   (Windows: add src/compat_win.c -lonecore -lbcrypt -Wl,--disable-high-entropy-va, as bb-probe)
 * Run without arguments; it re-runs itself for the cases that need another environment. */
#define _GNU_SOURCE
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define TIB (UINT64_C(1)<<40)
/* runtime_memory.c and runtime_mutex.c resolve through these; the test calls them by NID. */
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,nid)) return (uintptr_t)table[i].function;
    return 0;
}
typedef uint64_t (ABI *DirectSize)(void);
typedef int32_t (ABI *MutexInit)(void **, void **, const char *);
typedef int32_t (ABI *HandleOp)(void **);

static void check_block(void *p, size_t size) {
    assert(p);
    assert(!((uintptr_t)p & 63));
    for (size_t i=0;i<size;++i) assert(!((unsigned char *)p)[i]);
}
static void low_handles(void) {
    void *first=runtime_low_alloc(200);
    check_block(first,200);
    /* Between the image range and the PS4 user range, so below 1 TiB. */
    assert((uintptr_t)first>=UINT64_C(0x0f00000000) && (uintptr_t)first<UINT64_C(0x1000000000));
    memset(first,0xab,200);
    runtime_low_free(first);
    void *again=runtime_low_alloc(200); /* same size: the freed block, zeroed again */
    assert(again==first);
    check_block(again,200);
    runtime_low_free(again);
    /* More than one slab's worth, every size class and a large block. */
    enum { COUNT=20000 };
    static void *blocks[COUNT];
    for (int i=0;i<COUNT;++i) {
        size_t size=(size_t)16<<(i%8);
        blocks[i]=runtime_low_alloc(size);
        check_block(blocks[i],size);
        assert((uintptr_t)blocks[i]+size<TIB);
        memset(blocks[i],i,size);
    }
    for (int i=0;i<COUNT;++i) runtime_low_free(blocks[i]);
    void *large=runtime_low_alloc(256*1024);
    check_block(large,256*1024);
    assert((uintptr_t)large<TIB);
    runtime_low_free(large);
    /* Guest mutex and cond handles come from the same slabs. */
    MutexInit init=(MutexInit)runtime_mutex_resolve("cmo1RIYva9o#p#J");
    HandleOp destroy=(HandleOp)runtime_mutex_resolve("2Of0f+3mhhE#p#J");
    assert(init && destroy);
    void *mutex=NULL;
    assert(init(&mutex,NULL,"test")==0);
    assert((uintptr_t)mutex<UINT64_C(0x1000000000));
    assert(destroy(&mutex)==0);
    puts("PASS: guest handles below 1 TiB");
}
static void host_heap(void) {
    /* BB_LOW_HANDLES=0: the host heap, still aligned, zeroed and freeable. */
    void *p=runtime_low_alloc(300);
    check_block(p,300);
    runtime_low_free(p);
    puts("PASS: BB_LOW_HANDLES=0 uses the host heap");
}
static void pool_fallback(void) {
    DirectSize size=(DirectSize)runtime_memory_resolve("pO96TwzOm5E#p#J");
    assert(size);
    uint64_t granted=size();
    const char *refuse=getenv("BB_TEST_REFUSE_DMEM");
    if (refuse && refuse[0]=='1') assert(granted==UINT64_C(5056)<<20);
    else assert(granted==UINT64_C(6144)<<20);
    printf("PASS: direct memory pool %llu MiB\n",(unsigned long long)(granted>>20));
}
static int run_self(const char *self, const char *mode, const char *env) {
    char command[1024];
    snprintf(command,sizeof(command),"%s\"%s\" %s",env,self,mode);
#ifdef _WIN32
    /* cmd.exe: set VAR=value&& command */
    char wrapped[1100]; snprintf(wrapped,sizeof(wrapped),"cmd /c \"%s\"",command);
    return system(wrapped);
#else
    return system(command);
#endif
}
int main(int argc, char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    if (argc>1 && !strcmp(argv[1],"--low")) { low_handles(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--host")) { host_heap(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--pool")) { pool_fallback(); return 0; }
#ifdef _WIN32
#define ENV(a) "set " a "&& "
#else
#define ENV(a) a " "
#endif
    int failed=0;
    failed|=run_self(argv[0],"--low","")!=0;
    failed|=run_self(argv[0],"--host",ENV("BB_LOW_HANDLES=0"))!=0;
    failed|=run_self(argv[0],"--pool",ENV("BB_DMEM_MB=6144"))!=0;
    failed|=run_self(argv[0],"--pool",ENV("BB_DMEM_MB=6144") ENV("BB_TEST_REFUSE_DMEM=1"))!=0;
    if (failed) { puts("FAIL"); return 1; }
    puts("PASS: low handles and direct memory fallback");
    return 0;
}
