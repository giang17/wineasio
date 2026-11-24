/* lb_common.h - measuring logic shared by asio_loopback.c and jack_loopback.c.
 *
 * One cycle is one call of lb_process(). Each measurement writes a single
 * impulse (0.25 at sample 0) into the output buffer and looks for it in the
 * input buffer of the following cycles. Latency = (receiving cycle - sending
 * cycle) * buffer size + sample offset. The input is scanned BEFORE the output
 * is written: with JACK's zero copy for a single connection both pointers can
 * point to the same memory.
 *
 * Nothing is sent until lb_armed is set; until then the output stays silent
 * (by then the script has checked that the ports are wired to each other and
 * to nothing else). */

#ifndef LB_COMMON_H
#define LB_COMMON_H

#include <math.h>
#include <stdio.h>
#include <string.h>

#define LB_MAX_MEAS      1000
#define LB_IMPULSE       0.25f
#define LB_THRESHOLD     0.1f
#define LB_INTERVAL      16     /* cycles between two impulses */
#define LB_WARMUP        100    /* cycles after arming before the first impulse */
#define LB_TIMEOUT_CALLS 12     /* no hit after this many cycles: lost */

static volatile int lb_armed;
static volatile int lb_done;
static int  lb_nmeas = 50;
static long lb_bufsize;
static long lb_calls;
static long lb_armed_at = -1;
static long lb_sent = -1;
static int  lb_meas, lb_lost, lb_stray;
static long lb_lat[LB_MAX_MEAS];
static long lb_cycles[LB_MAX_MEAS];

/* A cycle scans the input and then writes the output. The order matters for a
 * client wired to itself: the input shows the client's own output as it is at
 * the jack_port_get_buffer() call for the input. Fetching the input before the
 * output is written gives the previous cycle's output (the loop costs 1 period),
 * fetching it afterwards gives the current one (the loop costs 0). */
static void lb_scan(const float *in, long n, long c)
{
    long i;

    if (lb_sent >= 0)
    {
        for (i = 0; i < n; i++)
            if (fabsf(in[i]) > LB_THRESHOLD) break;
        if (i < n)
        {
            if (lb_meas < LB_MAX_MEAS)
            {
                lb_cycles[lb_meas] = c - lb_sent;
                lb_lat[lb_meas] = (c - lb_sent) * n + i;
            }
            lb_meas++;
            lb_sent = -1;
        }
        else if (c - lb_sent > LB_TIMEOUT_CALLS)
        {
            lb_lost++;
            lb_sent = -1;
        }
    }
    else if (lb_armed)
    {
        /* Signal without an impulse in flight: the wiring is not what it should be. */
        for (i = 0; i < n; i++)
            if (fabsf(in[i]) > LB_THRESHOLD) { lb_stray++; break; }
    }
}

static void lb_send(float *out, long n, long c)
{
    memset(out, 0, sizeof(float) * n);
    if (!lb_armed) return;
    if (lb_armed_at < 0) lb_armed_at = c;
    if (lb_meas + lb_lost >= lb_nmeas) { lb_done = 1; return; }
    if (lb_sent < 0 && c - lb_armed_at >= LB_WARMUP && c % LB_INTERVAL == 0)
    {
        out[0] = LB_IMPULSE;
        lb_sent = c;
    }
}

/* Read before write (as WineASIO does with Output buffering 0 and 1). */
static void lb_process(const float *in, float *out, long n)
{
    long c = ++lb_calls;
    lb_scan(in, n, c);
    lb_send(out, n, c);
}

static int lb_cmp_long(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    return (x > y) - (x < y);
}

/* The result as one machine-readable RESULT line plus the distribution. */
static void lb_report(const char *label, double rate)
{
    int  n = lb_meas < LB_MAX_MEAS ? lb_meas : LB_MAX_MEAS;
    long sorted[LB_MAX_MEAS];
    int  k;

    memcpy(sorted, lb_lat, sizeof(long) * n);
    qsort(sorted, n, sizeof(long), lb_cmp_long);
    for (k = 0; k < n; k++)
        printf("M %s %d cycles=%ld lat=%ld\n", label, k, lb_cycles[k], lb_lat[k]);
    if (n)
        printf("RESULT %s bufsize=%ld rate=%.0f n=%d lost=%d stray=%d "
               "lat_min=%ld lat_med=%ld lat_max=%ld periods_med=%.2f ms_med=%.3f calls=%ld\n",
               label, lb_bufsize, rate, n, lb_lost, lb_stray,
               sorted[0], sorted[n / 2], sorted[n - 1],
               (double)sorted[n / 2] / lb_bufsize, 1000.0 * sorted[n / 2] / rate, lb_calls);
    else
        printf("RESULT %s bufsize=%ld rate=%.0f n=0 lost=%d stray=%d calls=%ld\n",
               label, lb_bufsize, rate, lb_lost, lb_stray, lb_calls);
}

#endif
