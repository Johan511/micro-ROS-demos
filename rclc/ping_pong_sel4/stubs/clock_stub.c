#include <stdint.h>

struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

typedef int clockid_t;

int clock_gettime(clockid_t clk, struct timespec *ts)
{
    (void)clk;
    static int64_t tick;
    tick++;
    ts->tv_sec  = tick / 1000;
    ts->tv_nsec = (tick % 1000) * 1000000;
    return 0;
}
