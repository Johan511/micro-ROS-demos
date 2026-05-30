#include <stdint.h>
#include <stddef.h>

#define HEAP_SIZE (128 * 1024)
#define TLS_SIZE 256

static char heap_mem[HEAP_SIZE] __attribute__((aligned(16)));
static char *heap_brk = heap_mem;

static char tls_area[TLS_SIZE] __attribute__((aligned(16)));

static long syscall_dispatch(long n, long a1, long a2, long a3,
                              long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (n == 214) {
        if (a1 == 0) return (long)heap_brk;
        if ((char *)a1 >= heap_mem && (char *)a1 <= heap_mem + HEAP_SIZE) {
            heap_brk = (char *)a1;
            return a1;
        }
        return (long)heap_brk;
    }
    return -38; /* -ENOSYS */
}

extern unsigned long __sysinfo;

void heap_init(void)
{
    __sysinfo = (unsigned long)syscall_dispatch;
    __asm__ volatile("msr tpidr_el0, %0" : : "r"(tls_area + TLS_SIZE));
}
