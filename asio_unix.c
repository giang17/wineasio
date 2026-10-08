/*
 * WineASIO Unix Library - JACK Audio Connection
 * Copyright (C) 2006 Robert Reif
 * Portions copyright (C) 2007-2013 Various contributors
 * Copyright (C) 2024 WineASIO contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

/* config.h is not needed - we define what we need directly */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"

/* Don't use Wine's debug.h - it requires symbols that may not be available
 * when the unix library is loaded. Use our own debug macros instead. */
#ifdef WINEASIO_DEBUG
#define TRACE(fmt, ...) do { fprintf(stderr, "wineasio:trace: " fmt, ##__VA_ARGS__); } while(0)
#define WARN(fmt, ...) do { fprintf(stderr, "wineasio:warn: " fmt, ##__VA_ARGS__); } while(0)
#define ERR(fmt, ...) do { fprintf(stderr, "wineasio:err: " fmt, ##__VA_ARGS__); } while(0)
#else
#define TRACE(fmt, ...) do { } while(0)
#define WARN(fmt, ...) do { } while(0)
#define ERR(fmt, ...) do { fprintf(stderr, "wineasio:err: " fmt, ##__VA_ARGS__); } while(0)
#endif

#include "wine/unixlib.h"

#include "unixlib.h"

/* Define C_ASSERT if not available */
#ifndef C_ASSERT
#define C_ASSERT(e) typedef char __C_ASSERT__[(e)?1:-1]
#endif

/* Define ARRAYSIZE if not available */
#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

/* JACK types and definitions */
typedef uint32_t jack_nframes_t;
typedef struct _jack_client jack_client_t;
typedef struct _jack_port jack_port_t;
typedef float jack_default_audio_sample_t;
typedef uint64_t jack_uuid_t;
typedef uint32_t jack_port_id_t;

typedef enum {
    JackTransportStopped = 0,
    JackTransportRolling = 1,
    JackTransportStarting = 3,
} jack_transport_state_t;

typedef enum {
    JackNullOption = 0x00,
    JackNoStartServer = 0x01,
} jack_options_t;

typedef enum {
    JackCaptureLatency,
    JackPlaybackLatency
} jack_latency_callback_mode_t;

typedef struct {
    jack_nframes_t min;
    jack_nframes_t max;
} jack_latency_range_t;

typedef int jack_status_t;

typedef struct {
    jack_nframes_t frame;
    uint32_t valid;
    /* ... more fields we don't use */
} jack_position_t;

/* JACK function pointers */
static void *jack_handle = NULL;

static jack_client_t* (*pjack_client_open)(const char*, jack_options_t, jack_status_t*, ...);
static int (*pjack_client_close)(jack_client_t*);
static const char* (*pjack_get_client_name)(jack_client_t*);
static jack_nframes_t (*pjack_get_sample_rate)(jack_client_t*);
static jack_nframes_t (*pjack_get_buffer_size)(jack_client_t*);
static int (*pjack_set_buffer_size)(jack_client_t*, jack_nframes_t);
static jack_port_t* (*pjack_port_register)(jack_client_t*, const char*, const char*, unsigned long, unsigned long);
static int (*pjack_port_unregister)(jack_client_t*, jack_port_t*);
static void* (*pjack_port_get_buffer)(jack_port_t*, jack_nframes_t);
static const char* (*pjack_port_name)(const jack_port_t*);
static int (*pjack_connect)(jack_client_t*, const char*, const char*);
static int (*pjack_disconnect)(jack_client_t*, const char*, const char*);
static const char** (*pjack_get_ports)(jack_client_t*, const char*, const char*, unsigned long);
static void (*pjack_free)(void*);
static int (*pjack_activate)(jack_client_t*);
static int (*pjack_deactivate)(jack_client_t*);
static int (*pjack_set_process_callback)(jack_client_t*, int (*)(jack_nframes_t, void*), void*);
static int (*pjack_set_buffer_size_callback)(jack_client_t*, int (*)(jack_nframes_t, void*), void*);
static int (*pjack_set_sample_rate_callback)(jack_client_t*, int (*)(jack_nframes_t, void*), void*);
static int (*pjack_set_latency_callback)(jack_client_t*, void (*)(jack_latency_callback_mode_t, void*), void*);
static void (*pjack_port_get_latency_range)(jack_port_t*, jack_latency_callback_mode_t, jack_latency_range_t*);
static jack_transport_state_t (*pjack_transport_query)(const jack_client_t*, jack_position_t*);
static void (*pjack_on_shutdown)(jack_client_t*, void (*)(void*), void*);
static int (*pjack_is_realtime)(jack_client_t*);
static int (*pjack_client_real_time_priority)(jack_client_t*);

#define JACK_DEFAULT_AUDIO_TYPE "32 bit float mono audio"
#define JackPortIsInput  0x1
#define JackPortIsOutput 0x2
#define JackPortIsPhysical 0x4

#define MAX_CHANNELS 128
#define MAX_NAME_LENGTH 64

/* Channel state */
typedef struct {
    jack_port_t *port;
    char name[MAX_NAME_LENGTH];
    BOOL active;
    jack_default_audio_sample_t *audio_buffer;  /* Double buffer (legacy, Unix-allocated) */
    jack_default_audio_sample_t *pe_buffer[2];  /* PE-side allocated buffers (Wine 11 WoW64 fix) */
} IOChannel;

/* Stream state - lives on Unix side */
typedef struct {
    jack_client_t *client;
    char client_name[MAX_NAME_LENGTH];
    
    /* JACK info */
    double sample_rate;
    LONG buffer_size;
    LONG input_latency;
    LONG output_latency;
    
    /* Channels */
    int num_inputs;
    int num_outputs;
    IOChannel inputs[MAX_CHANNELS];
    IOChannel outputs[MAX_CHANNELS];
    
    /* Physical port counts */
    int jack_num_input_ports;
    int jack_num_output_ports;
    const char **jack_input_ports;
    const char **jack_output_ports;
    
    /* State */
    int state;  /* 0=Loaded, 1=Initialized, 2=Prepared, 3=Running */
    BOOL active_inputs;
    BOOL active_outputs;
    
    /* Double buffering */
    LONG buffer_index;
    jack_default_audio_sample_t *callback_audio_buffer;
    
    /* Callback notification (polled by PE side)
     * Fields written by JACK realtime thread use atomics to avoid
     * mutex in the realtime path (mutex would cause priority inversion / xruns).
     * Non-realtime callbacks (buffer_size, sample_rate, latency) still use the mutex. */
    pthread_mutex_t callback_lock;          /* protects non-realtime fields only */
    volatile LONG buffer_switch_pending;    /* written atomically by RT thread */
    volatile INT64 pending_cycle;           /* (cycle seq << 1) | buffer index, written by RT thread */
    volatile INT64 sample_position;         /* written atomically by RT thread */
    volatile INT64 system_time;             /* written atomically by RT thread */
    BOOL sample_rate_changed;               /* protected by callback_lock */
    double new_sample_rate;                 /* protected by callback_lock */
    BOOL reset_request;                     /* protected by callback_lock */
    BOOL latency_changed;                   /* protected by callback_lock */
    
    /* Event signaling (replaces Sleep-polling) */
    int event_fd;                           /* eventfd for RT→PE notification */

    /* Synchronous hand-off: the RT thread waits until the host has finished
     * bufferSwitch() for the current cycle before it copies the output half,
     * so the host's output leaves in the same JACK cycle. */
    UINT64 cycle_seq;                       /* RT thread only, cycles since Start() */
    volatile UINT32 done_seq;               /* futex word, last cycle the host finished */
    volatile LONG host_attached;            /* callback thread is taking cycles */
    volatile LONG late_cycles;              /* cycles output as silence, host too late */
    LONG output_buffering;                  /* periods between bufferSwitch() and JACK output */

    /* Config */
    BOOL autoconnect;
    BOOL fixed_bufsize;
    LONG preferred_bufsize;

} AsioStream;

enum { Loaded = 0, Initialized, Prepared, Running };

static BOOL jack_loaded = FALSE;

/* Library constructor - called when .so is loaded */
__attribute__((constructor))
static void wineasio_unix_init(void)
{
    /* Library loaded */
}

/* Load JACK library */
static BOOL load_jack(void)
{
    if (jack_loaded) {
        return TRUE;
    }
    
    jack_handle = dlopen("libjack.so.0", RTLD_NOW);
    if (!jack_handle) {
        ERR("Could not load JACK library: %s\n", dlerror());
        return FALSE;
    }
    
    #define LOAD_SYM(name) \
        p##name = dlsym(jack_handle, #name); \
        if (!p##name) { WARN("Missing symbol: " #name "\n"); }
    
    LOAD_SYM(jack_client_open)
    LOAD_SYM(jack_client_close)
    LOAD_SYM(jack_get_client_name)
    LOAD_SYM(jack_get_sample_rate)
    LOAD_SYM(jack_get_buffer_size)
    LOAD_SYM(jack_set_buffer_size)
    LOAD_SYM(jack_port_register)
    LOAD_SYM(jack_port_unregister)
    LOAD_SYM(jack_port_get_buffer)
    LOAD_SYM(jack_port_name)
    LOAD_SYM(jack_connect)
    LOAD_SYM(jack_disconnect)
    LOAD_SYM(jack_get_ports)
    LOAD_SYM(jack_free)
    LOAD_SYM(jack_activate)
    LOAD_SYM(jack_deactivate)
    LOAD_SYM(jack_set_process_callback)
    LOAD_SYM(jack_set_buffer_size_callback)
    LOAD_SYM(jack_set_sample_rate_callback)
    LOAD_SYM(jack_set_latency_callback)
    LOAD_SYM(jack_port_get_latency_range)
    LOAD_SYM(jack_transport_query)
    LOAD_SYM(jack_on_shutdown)
    LOAD_SYM(jack_is_realtime)
    LOAD_SYM(jack_client_real_time_priority)

    #undef LOAD_SYM
    
    if (!pjack_client_open || !pjack_client_close) {
        ERR("JACK library missing critical symbols\n");
        dlclose(jack_handle);
        jack_handle = NULL;
        return FALSE;
    }
    
    jack_loaded = TRUE;
    return TRUE;
}

/* Current time in nanoseconds for ASIOTime.systemTime and GetSamplePosition().
 * The ASIO SDK requires systemTime to be derived from timeGetTime() on Windows.
 * Wine's timeGetTime() is QueryPerformanceCounter() based, which reads
 * CLOCK_BOOTTIME (ntdll monotonic_counter()); CLOCK_MONOTONIC would fall behind
 * it by the time the system spent suspended. */
static INT64 get_system_time(void)
{
    struct timespec ts;
#ifdef CLOCK_BOOTTIME
    if (!clock_gettime(CLOCK_BOOTTIME, &ts))
        return (INT64)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (INT64)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* Wait until the host has finished bufferSwitch() for cycle 'seq'.
 * Runs in the JACK realtime thread: no locks, only a futex wait with a
 * deadline of two periods (at least 5 ms), so a stalled host costs xruns
 * instead of getting the client kicked out of the graph. */
static BOOL wait_for_host(AsioStream *stream, UINT32 seq, jack_nframes_t nframes)
{
    struct timespec now, ts;
    INT64 timeout, start, left;
    UINT32 done;

    timeout = stream->sample_rate > 0 ? (INT64)(2e9 * nframes / stream->sample_rate) : 0;
    if (timeout < 5000000) timeout = 5000000;
    clock_gettime(CLOCK_MONOTONIC, &now);
    start = (INT64)now.tv_sec * 1000000000LL + now.tv_nsec;

    for (;;) {
        done = __atomic_load_n(&stream->done_seq, __ATOMIC_ACQUIRE);
        if ((INT32)(done - seq) >= 0)
            return TRUE;
        if (!__atomic_load_n(&stream->host_attached, __ATOMIC_ACQUIRE))
            return FALSE;
        clock_gettime(CLOCK_MONOTONIC, &now);
        left = start + timeout - ((INT64)now.tv_sec * 1000000000LL + now.tv_nsec);
        if (left <= 0)
            return FALSE;
        ts.tv_sec = left / 1000000000LL;
        ts.tv_nsec = left % 1000000000LL;
        syscall(SYS_futex, &stream->done_seq, FUTEX_WAIT_PRIVATE, done, &ts, NULL, 0);
    }
}

static void wake_rt_thread(AsioStream *stream)
{
    syscall(SYS_futex, &stream->done_seq, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}

/* Hand cycle 'seq' (input already in its half) to the PE callback thread. */
static void signal_cycle(AsioStream *stream, UINT32 seq)
{
    uint64_t val = 1;

    /* pending_cycle must be visible before buffer_switch_pending is set. */
    __atomic_store_n(&stream->pending_cycle, ((INT64)seq << 1) | stream->buffer_index, __ATOMIC_RELAXED);
    __atomic_store_n(&stream->buffer_switch_pending, TRUE, __ATOMIC_RELEASE);

    /* Wake PE-side wait thread via eventfd (RT-safe: eventfd_write is a single write syscall) */
    eventfd_write(stream->event_fd, val);
}

/* Copy output half 'index' to JACK once the host has finished cycle 'seq'.
 * A host that misses the deadline gets silence for this cycle rather than a
 * half it may still be writing; so does a cycle that does not exist yet
 * ('valid' FALSE, the first output_buffering cycles after start). */
static void send_output(AsioStream *stream, UINT32 seq, BOOL valid, LONG index, jack_nframes_t nframes)
{
    BOOL attached = __atomic_load_n(&stream->host_attached, __ATOMIC_ACQUIRE);
    BOOL host_done = valid && attached && wait_for_host(stream, seq, nframes);
    int i;

    if (valid && attached && !host_done)
        __atomic_add_fetch(&stream->late_cycles, 1, __ATOMIC_RELAXED);

    /* Wine 11 WoW64 fix: Use pe_buffer[] instead of audio_buffer */
    for (i = 0; i < stream->num_outputs; i++) {
        if (stream->outputs[i].active && stream->outputs[i].port) {
            void *jack_buf = pjack_port_get_buffer(stream->outputs[i].port, nframes);
            jack_default_audio_sample_t *pe_buf = stream->outputs[i].pe_buffer[index];
            if (jack_buf && pe_buf && host_done) {
                memcpy(jack_buf, pe_buf, sizeof(jack_default_audio_sample_t) * nframes);
            } else if (jack_buf) {
                memset(jack_buf, 0, sizeof(jack_default_audio_sample_t) * nframes);
            }
            /* No logging in realtime callback - causes xruns */
        }
    }
}

/* JACK process callback - runs in realtime thread */
static int jack_process_callback(jack_nframes_t nframes, void *arg)
{
    AsioStream *stream = (AsioStream *)arg;
    UINT64 seq;
    LONG n, out_index;
    int i;

    if (stream->state != Running) {
        /* Output silence */
        for (i = 0; i < stream->num_outputs; i++) {
            if (stream->outputs[i].port) {
                void *buf = pjack_port_get_buffer(stream->outputs[i].port, nframes);
                if (buf) memset(buf, 0, sizeof(jack_default_audio_sample_t) * nframes);
            }
        }
        return 0;
    }

    /* The half sent to JACK in this cycle is the one the host filled
     * output_buffering cycles ago: 0 waits for this cycle's bufferSwitch(),
     * 1 and 2 give the host one or two more periods. With two halves, 2 sends
     * the half this cycle is about to refill, so that half goes out (and the
     * host is done with it) before new input lands in it; 0 and 1 hand the
     * cycle to the host first. */
    seq = ++stream->cycle_seq;
    n = stream->output_buffering;
    out_index = stream->buffer_index ^ (n & 1);
    if (n == 2)
        send_output(stream, (UINT32)(seq - 2), seq > 2, out_index, nframes);

    /* Copy JACK input buffers to PE-side buffer
     * Wine 11 WoW64 fix: Use pe_buffer[] instead of audio_buffer
     * pe_buffer[0] and pe_buffer[1] are pointers to PE-allocated memory */
    for (i = 0; i < stream->num_inputs; i++) {
        if (stream->inputs[i].active && stream->inputs[i].port) {
            void *jack_buf = pjack_port_get_buffer(stream->inputs[i].port, nframes);
            jack_default_audio_sample_t *pe_buf = stream->inputs[i].pe_buffer[stream->buffer_index];
            if (jack_buf && pe_buf) {
                memcpy(pe_buf, jack_buf, sizeof(jack_default_audio_sample_t) * nframes);
            }
            /* No logging in realtime callback - causes xruns */
        }
    }
    
    /* Update sample position and time atomically (no mutex in RT thread!) */
    __atomic_store_n(&stream->sample_position, stream->sample_position + nframes, __ATOMIC_RELAXED);
    __atomic_store_n(&stream->system_time, get_system_time(), __ATOMIC_RELAXED);

    signal_cycle(stream, (UINT32)seq);
    if (n != 2)
        send_output(stream, (UINT32)(seq - n), seq > (UINT64)n, out_index, nframes);

    /* Switch buffers */
    stream->buffer_index = stream->buffer_index ? 0 : 1;
    
    return 0;
}

/* JACK buffer size callback */
static int jack_buffer_size_callback(jack_nframes_t nframes, void *arg)
{
    AsioStream *stream = (AsioStream *)arg;
    
    stream->buffer_size = nframes;
    
    pthread_mutex_lock(&stream->callback_lock);
    stream->reset_request = TRUE;
    pthread_mutex_unlock(&stream->callback_lock);
    
    return 0;
}

/* JACK sample rate callback */
static int jack_sample_rate_callback(jack_nframes_t nframes, void *arg)
{
    AsioStream *stream = (AsioStream *)arg;
    
    pthread_mutex_lock(&stream->callback_lock);
    stream->sample_rate_changed = TRUE;
    stream->new_sample_rate = (double)nframes;
    pthread_mutex_unlock(&stream->callback_lock);
    
    stream->sample_rate = (double)nframes;
    
    return 0;
}

/* JACK latency callback */
static void jack_latency_callback(jack_latency_callback_mode_t mode, void *arg)
{
    AsioStream *stream = (AsioStream *)arg;
    
    pthread_mutex_lock(&stream->callback_lock);
    stream->latency_changed = TRUE;
    pthread_mutex_unlock(&stream->callback_lock);
}

/* JACK shutdown callback — called when the JACK server shuts down unexpectedly.
 * Without this, WineASIO would deadlock or hang indefinitely. */
static void jack_shutdown_callback(void *arg)
{
    AsioStream *stream = (AsioStream *)arg;

    ERR("JACK server shut down!\n");
    stream->state = Loaded;
    stream->client = NULL;  /* client handle is invalid after shutdown */

    /* Signal reset to PE side so the host can react */
    pthread_mutex_lock(&stream->callback_lock);
    stream->reset_request = TRUE;
    pthread_mutex_unlock(&stream->callback_lock);

    /* Wake the PE wait thread so it sees the reset */
    if (stream->event_fd >= 0) {
        uint64_t val = 1;
        eventfd_write(stream->event_fd, val);
    }
}

static AsioStream *handle_to_stream(asio_handle h)
{
    return (AsioStream *)(UINT_PTR)h;
}

/*
 * Unix function implementations
 */

static NTSTATUS asio_init(void *args)
{
    struct asio_init_params *params = args;
    AsioStream *stream;
    jack_status_t status;
    jack_options_t options;
    int i;
    
    if (!load_jack()) {
        ERR("Could not load JACK library\n");
        params->result = ASE_NotPresent;
        return STATUS_SUCCESS;
    }
    
    stream = calloc(1, sizeof(*stream));
    if (!stream) {
        params->result = ASE_NoMemory;
        return STATUS_SUCCESS;
    }
    
    /* Copy config */
    stream->num_inputs = params->config.num_inputs > 0 ? params->config.num_inputs : 2;
    stream->num_outputs = params->config.num_outputs > 0 ? params->config.num_outputs : 2;
    stream->preferred_bufsize = params->config.preferred_bufsize > 0 ? params->config.preferred_bufsize : 1024;
    stream->fixed_bufsize = params->config.fixed_bufsize;
    stream->autoconnect = params->config.autoconnect;
    stream->output_buffering = params->config.output_buffering >= 0 && params->config.output_buffering <= 2
                               ? params->config.output_buffering : 0;
    
    if (stream->num_inputs > MAX_CHANNELS) stream->num_inputs = MAX_CHANNELS;
    if (stream->num_outputs > MAX_CHANNELS) stream->num_outputs = MAX_CHANNELS;
    
    /* Set client name */
    if (params->config.client_name[0]) {
        memcpy(stream->client_name, params->config.client_name, MAX_NAME_LENGTH - 1);
        stream->client_name[MAX_NAME_LENGTH - 1] = '\0';
    } else {
        memcpy(stream->client_name, "WineASIO", 9);
    }
    
    /* Open JACK client */
    /* Start no server unless "Autostart server" asks for it (as upstream) */
    options = params->config.autostart ? JackNullOption : JackNoStartServer;
    stream->client = pjack_client_open(stream->client_name, options, &status);
    if (!stream->client) {
        ERR("Could not open JACK client '%s' (status=0x%x)\n", stream->client_name, status);
        free(stream);
        params->result = ASE_NotPresent;
        return STATUS_SUCCESS;
    }
    
    /* Get JACK info */
    stream->sample_rate = pjack_get_sample_rate(stream->client);
    stream->buffer_size = pjack_get_buffer_size(stream->client);
    
    /* Initialize mutex and eventfd for callback signaling */
    pthread_mutex_init(&stream->callback_lock, NULL);
    stream->event_fd = eventfd(0, EFD_NONBLOCK);
    if (stream->event_fd < 0) {
        ERR("Could not create eventfd: %s\n", strerror(errno));
        pjack_client_close(stream->client);
        free(stream);
        params->result = ASE_HWMalfunction;
        return STATUS_SUCCESS;
    }
    
    /* Register ports */
    for (i = 0; i < stream->num_inputs; i++) {
        snprintf(stream->inputs[i].name, MAX_NAME_LENGTH, "in_%d", i + 1);
        stream->inputs[i].port = pjack_port_register(stream->client,
            stream->inputs[i].name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        if (!stream->inputs[i].port) {
            ERR("Failed to register JACK input port %d\n", i);
            pjack_client_close(stream->client);
            pthread_mutex_destroy(&stream->callback_lock);
            if (stream->event_fd >= 0) close(stream->event_fd);
            free(stream);
            params->result = ASE_HWMalfunction;
            return STATUS_SUCCESS;
        }
        stream->inputs[i].active = FALSE;
    }

    for (i = 0; i < stream->num_outputs; i++) {
        snprintf(stream->outputs[i].name, MAX_NAME_LENGTH, "out_%d", i + 1);
        stream->outputs[i].port = pjack_port_register(stream->client,
            stream->outputs[i].name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (!stream->outputs[i].port) {
            ERR("Failed to register JACK output port %d\n", i);
            pjack_client_close(stream->client);
            pthread_mutex_destroy(&stream->callback_lock);
            if (stream->event_fd >= 0) close(stream->event_fd);
            free(stream);
            params->result = ASE_HWMalfunction;
            return STATUS_SUCCESS;
        }
        stream->outputs[i].active = FALSE;
    }
    
    /* Get physical ports */
    stream->jack_input_ports = pjack_get_ports(stream->client, NULL, NULL,
        JackPortIsPhysical | JackPortIsOutput);
    for (stream->jack_num_input_ports = 0;
         stream->jack_input_ports && stream->jack_input_ports[stream->jack_num_input_ports];
         stream->jack_num_input_ports++);
    
    stream->jack_output_ports = pjack_get_ports(stream->client, NULL, NULL,
        JackPortIsPhysical | JackPortIsInput);
    for (stream->jack_num_output_ports = 0;
         stream->jack_output_ports && stream->jack_output_ports[stream->jack_num_output_ports];
         stream->jack_num_output_ports++);
    
    /* Set callbacks */
    pjack_set_process_callback(stream->client, jack_process_callback, stream);
    pjack_set_buffer_size_callback(stream->client, jack_buffer_size_callback, stream);
    pjack_set_sample_rate_callback(stream->client, jack_sample_rate_callback, stream);
    if (pjack_set_latency_callback)
        pjack_set_latency_callback(stream->client, jack_latency_callback, stream);
    if (pjack_on_shutdown)
        pjack_on_shutdown(stream->client, jack_shutdown_callback, stream);
    
    /* Activate JACK client */
    if (pjack_activate(stream->client)) {
        ERR("Could not activate JACK client\n");
        pjack_client_close(stream->client);
        free(stream);
        params->result = ASE_HWMalfunction;
        return STATUS_SUCCESS;
    }
    
    /* Auto-connect if requested */
    if (stream->autoconnect) {
        for (i = 0; i < stream->num_inputs && i < stream->jack_num_input_ports; i++) {
            if (stream->inputs[i].port && stream->jack_input_ports[i]) {
                pjack_connect(stream->client, stream->jack_input_ports[i],
                    pjack_port_name(stream->inputs[i].port));
            }
        }
        for (i = 0; i < stream->num_outputs && i < stream->jack_num_output_ports; i++) {
            if (stream->outputs[i].port && stream->jack_output_ports[i]) {
                pjack_connect(stream->client, pjack_port_name(stream->outputs[i].port),
                    stream->jack_output_ports[i]);
            }
        }
    }
    
    stream->state = Initialized;
    
    /* Return info */
    params->handle = (asio_handle)(UINT_PTR)stream;
    params->input_channels = stream->num_inputs;
    params->output_channels = stream->num_outputs;
    params->sample_rate = stream->sample_rate;
    params->result = ASE_OK;
    
    /* Single success message - useful to see WineASIO loaded */
    fprintf(stderr, "[WineASIO] Initialized: %d in, %d out, %.0f Hz, %d samples\n",
          stream->num_inputs, stream->num_outputs, stream->sample_rate, stream->buffer_size);
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_exit(void *args)
{
    struct asio_exit_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    int i;
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    /* Deactivate and close */
    if (stream->client) {
        pjack_deactivate(stream->client);
        
        /* Unregister audio ports */
        for (i = 0; i < stream->num_inputs; i++) {
            if (stream->inputs[i].port)
                pjack_port_unregister(stream->client, stream->inputs[i].port);
        }
        for (i = 0; i < stream->num_outputs; i++) {
            if (stream->outputs[i].port)
                pjack_port_unregister(stream->client, stream->outputs[i].port);
        }
        
        pjack_client_close(stream->client);
    }
    
    /* Free port lists */
    if (stream->jack_input_ports)
        pjack_free(stream->jack_input_ports);
    if (stream->jack_output_ports)
        pjack_free(stream->jack_output_ports);
    
    /* Free audio buffers */
    for (i = 0; i < stream->num_inputs; i++)
        free(stream->inputs[i].audio_buffer);
    for (i = 0; i < stream->num_outputs; i++)
        free(stream->outputs[i].audio_buffer);
    
    free(stream->callback_audio_buffer);

    pthread_mutex_destroy(&stream->callback_lock);
    if (stream->event_fd >= 0)
        close(stream->event_fd);

    free(stream);
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_start(void *args)
{
    struct asio_start_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    int i;
    
    if (!stream || stream->state != Prepared) {
        ERR("Invalid stream or state for start: stream=%p, state=%d\n", stream, stream ? stream->state : -1);
        params->result = ASE_InvalidMode;
        return STATUS_SUCCESS;
    }
    
    /* Zero buffers */
    for (i = 0; i < stream->num_inputs; i++) {
        if (stream->inputs[i].audio_buffer)
            memset(stream->inputs[i].audio_buffer, 0,
                   sizeof(jack_default_audio_sample_t) * stream->buffer_size * 2);
    }
    for (i = 0; i < stream->num_outputs; i++) {
        if (stream->outputs[i].audio_buffer)
            memset(stream->outputs[i].audio_buffer, 0,
                   sizeof(jack_default_audio_sample_t) * stream->buffer_size * 2);
    }
    
    stream->buffer_index = 0;
    stream->sample_position = 0;
    stream->system_time = get_system_time();
    stream->buffer_switch_pending = FALSE;
    stream->pending_cycle = 0;
    stream->cycle_seq = 0;
    stream->done_seq = 0;
    stream->late_cycles = 0;
    /* The PE side starts its callback thread after this call returns; until
     * that thread asks for a cycle, the RT thread does not wait for it. */
    stream->host_attached = FALSE;

    /* Clear stale flags from JACK activation callbacks.
     * jack_activate() triggers sample_rate_callback and buffer_size_callback
     * which set these flags. With blocking poll (asio_wait_callback), the
     * callback thread would deliver them with the first buffer switch,
     * causing the host to reset the driver in a loop. */
    pthread_mutex_lock(&stream->callback_lock);
    stream->sample_rate_changed = FALSE;
    stream->reset_request = FALSE;
    stream->latency_changed = FALSE;
    stream->new_sample_rate = 0;
    pthread_mutex_unlock(&stream->callback_lock);

    stream->state = Running;
    params->result = ASE_OK;

    return STATUS_SUCCESS;
}

static NTSTATUS asio_stop(void *args)
{
    struct asio_stop_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    
    if (!stream || stream->state != Running) {
        params->result = ASE_InvalidMode;
        return STATUS_SUCCESS;
    }

    stream->state = Prepared;
    __atomic_store_n(&stream->host_attached, FALSE, __ATOMIC_RELEASE);
    wake_rt_thread(stream);
    if (stream->late_cycles)
        WARN("%d cycle(s) output as silence: host did not finish bufferSwitch in time\n",
             (int)stream->late_cycles);
    params->result = ASE_OK;

    return STATUS_SUCCESS;
}

static NTSTATUS asio_get_channels(void *args)
{
    struct asio_get_channels_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    params->num_inputs = stream->num_inputs;
    params->num_outputs = stream->num_outputs;
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_get_latencies(void *args)
{
    struct asio_get_latencies_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    jack_latency_range_t range;
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    /* Get latency from JACK if available */
    stream->input_latency = stream->buffer_size;
    stream->output_latency = stream->buffer_size * 2;
    
    if (pjack_port_get_latency_range && stream->num_inputs > 0 && stream->inputs[0].port) {
        pjack_port_get_latency_range(stream->inputs[0].port, JackCaptureLatency, &range);
        stream->input_latency = range.max > 0 ? range.max : stream->buffer_size;
    }
    
    if (pjack_port_get_latency_range && stream->num_outputs > 0 && stream->outputs[0].port) {
        pjack_port_get_latency_range(stream->outputs[0].port, JackPlaybackLatency, &range);
        stream->output_latency = range.max > 0 ? range.max : stream->buffer_size * 2;
    }

    /* Pipelined output leaves output_buffering periods after bufferSwitch(). */
    stream->output_latency += stream->output_buffering * stream->buffer_size;

    params->input_latency = stream->input_latency;
    params->output_latency = stream->output_latency;
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_get_buffer_size(void *args)
{
    struct asio_get_buffer_size_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    if (stream->fixed_bufsize) {
        params->min_size = stream->buffer_size;
        params->max_size = stream->buffer_size;
        params->preferred_size = stream->buffer_size;
        params->granularity = 0;
    } else {
        params->min_size = 16;
        params->max_size = 8192;
        params->preferred_size = stream->preferred_bufsize;
        params->granularity = 1;
    }
    
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_can_sample_rate(void *args)
{
    struct asio_can_sample_rate_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    /* JACK typically only supports one sample rate at a time */
    if ((int)params->sample_rate == (int)stream->sample_rate) {
        params->result = ASE_OK;
    } else {
        params->result = ASE_NoClock;
    }
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_get_sample_rate(void *args)
{
    struct asio_get_sample_rate_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    params->sample_rate = stream->sample_rate;
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_set_sample_rate(void *args)
{
    struct asio_set_sample_rate_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    /* JACK controls the sample rate, we can't change it */
    if ((int)params->sample_rate == (int)stream->sample_rate) {
        params->result = ASE_OK;
    } else {
        params->result = ASE_NoClock;
    }
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_get_channel_info(void *args)
{
    struct asio_get_channel_info_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    int channel = params->info.channel;
    BOOL is_input = params->info.is_input;
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    if (is_input) {
        if (channel < 0 || channel >= stream->num_inputs) {
            params->result = ASE_InvalidParameter;
            return STATUS_SUCCESS;
        }
        params->info.is_active = stream->inputs[channel].active;
        params->info.channel_group = 0;
        params->info.sample_type = ASIOSTFloat32LSB;
        memcpy(params->info.name, stream->inputs[channel].name, 31);
        params->info.name[31] = '\0';
    } else {
        if (channel < 0 || channel >= stream->num_outputs) {
            params->result = ASE_InvalidParameter;
            return STATUS_SUCCESS;
        }
        params->info.is_active = stream->outputs[channel].active;
        params->info.channel_group = 0;
        params->info.sample_type = ASIOSTFloat32LSB;
        memcpy(params->info.name, stream->outputs[channel].name, 31);
        params->info.name[31] = '\0';
    }
    
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_create_buffers(void *args)
{
    struct asio_create_buffers_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    struct asio_buffer_info *infos = params->buffer_infos;
    int i, j;
    
    if (!stream || stream->state < Initialized) {
        ERR("Invalid stream or state for CreateBuffers\n");
        params->result = ASE_InvalidMode;
        return STATUS_SUCCESS;
    }
    
    if (!infos || params->num_channels <= 0) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    /* Set buffer size if different and not fixed */
    if (params->buffer_size != stream->buffer_size && !stream->fixed_bufsize) {
        if (pjack_set_buffer_size)
            pjack_set_buffer_size(stream->client, params->buffer_size);
        stream->buffer_size = pjack_get_buffer_size(stream->client);
    }
    
    /*
     * WINE 11 WoW64 FIX:
     * Buffer pointers are now allocated on the PE (Windows) side and passed to us.
     * This is necessary because in Wine 11 WoW64, the Unix side runs in 64-bit
     * address space while 32-bit PE code runs in emulated 32-bit space.
     * Buffers allocated here with calloc() would have 64-bit addresses that
     * cannot be accessed by 32-bit Windows code.
     *
     * The PE side has already set buffer_ptr[0] and buffer_ptr[1] to valid
     * 32-bit addresses. We just need to store these pointers and mark channels active.
     */
    
    /* Process each channel - use PE-allocated buffer pointers */
    for (i = 0; i < params->num_channels; i++) {
        int ch = infos[i].channel_num;
        
        if (infos[i].is_input) {
            if (ch < 0 || ch >= stream->num_inputs) {
                params->result = ASE_InvalidParameter;
                return STATUS_SUCCESS;
            }
            
            /* Free old Unix-side buffer if exists (legacy cleanup) */
            if (stream->inputs[ch].audio_buffer) {
                free(stream->inputs[ch].audio_buffer);
                stream->inputs[ch].audio_buffer = NULL;
            }
            
            /* Store PE-side buffer pointers for JACK callback use */
            stream->inputs[ch].pe_buffer[0] = (jack_default_audio_sample_t *)(UINT_PTR)infos[i].buffer_ptr[0];
            stream->inputs[ch].pe_buffer[1] = (jack_default_audio_sample_t *)(UINT_PTR)infos[i].buffer_ptr[1];
            stream->inputs[ch].active = TRUE;
        } else {
            if (ch < 0 || ch >= stream->num_outputs) {
                params->result = ASE_InvalidParameter;
                return STATUS_SUCCESS;
            }
            
            /* Free old Unix-side buffer if exists (legacy cleanup) */
            if (stream->outputs[ch].audio_buffer) {
                free(stream->outputs[ch].audio_buffer);
                stream->outputs[ch].audio_buffer = NULL;
            }
            
            /* Store PE-side buffer pointers for JACK callback use */
            stream->outputs[ch].pe_buffer[0] = (jack_default_audio_sample_t *)(UINT_PTR)infos[i].buffer_ptr[0];
            stream->outputs[ch].pe_buffer[1] = (jack_default_audio_sample_t *)(UINT_PTR)infos[i].buffer_ptr[1];
            stream->outputs[ch].active = TRUE;
        }
    }
    
    /* Count active channels */
    stream->active_inputs = FALSE;
    stream->active_outputs = FALSE;
    for (j = 0; j < stream->num_inputs; j++)
        if (stream->inputs[j].active) stream->active_inputs = TRUE;
    for (j = 0; j < stream->num_outputs; j++)
        if (stream->outputs[j].active) stream->active_outputs = TRUE;
    
    stream->state = Prepared;
    params->result = ASE_OK;

    return STATUS_SUCCESS;
}

static NTSTATUS asio_dispose_buffers(void *args)
{
    struct asio_dispose_buffers_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    int i;
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    if (stream->state == Running) {
        stream->state = Prepared;
    }
    
    /* Free buffers and mark inactive */
    for (i = 0; i < stream->num_inputs; i++) {
        free(stream->inputs[i].audio_buffer);
        stream->inputs[i].audio_buffer = NULL;
        stream->inputs[i].active = FALSE;
    }
    for (i = 0; i < stream->num_outputs; i++) {
        free(stream->outputs[i].audio_buffer);
        stream->outputs[i].audio_buffer = NULL;
        stream->outputs[i].active = FALSE;
    }
    
    stream->state = Initialized;
    params->result = ASE_OK;

    return STATUS_SUCCESS;
}

static NTSTATUS asio_output_ready(void *args)
{
    struct asio_output_ready_params *params = args;
    
    /* We don't need this optimization since JACK handles timing */
    params->result = ASE_NotPresent;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_get_sample_position(void *args)
{
    /* Note: No TRACE - called frequently during playback */
    struct asio_get_sample_position_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    params->sample_position = stream->sample_position;
    params->system_time = stream->system_time;
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_get_callback(void *args)
{
    /* Note: No TRACE here - this is called ~1000x/sec and would cause xruns */
    struct asio_get_callback_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    /* Read RT-thread fields with acquire semantics (lock-free).
     * buffer_switch_pending acts as the release/acquire synchronization point. */
    params->buffer_switch_ready = __atomic_load_n(&stream->buffer_switch_pending, __ATOMIC_ACQUIRE);
    params->buffer_index = (LONG)(__atomic_load_n(&stream->pending_cycle, __ATOMIC_RELAXED) & 1);
    params->direct_process = TRUE;

    params->time_info.speed = 1.0;
    params->time_info.system_time = __atomic_load_n(&stream->system_time, __ATOMIC_RELAXED);
    params->time_info.sample_position = __atomic_load_n(&stream->sample_position, __ATOMIC_RELAXED);
    params->time_info.sample_rate = stream->sample_rate;
    params->time_info.flags = 0x7;  /* Valid flags */

    /* Clear pending flag atomically */
    if (params->buffer_switch_ready)
        __atomic_store_n(&stream->buffer_switch_pending, FALSE, __ATOMIC_RELAXED);

    /* Non-RT fields still protected by mutex */
    pthread_mutex_lock(&stream->callback_lock);
    params->sample_rate_changed = stream->sample_rate_changed;
    params->new_sample_rate = stream->new_sample_rate;
    params->reset_request = stream->reset_request;
    params->resync_request = FALSE;
    params->latency_changed = stream->latency_changed;

    stream->sample_rate_changed = FALSE;
    stream->reset_request = FALSE;
    stream->latency_changed = FALSE;
    pthread_mutex_unlock(&stream->callback_lock);
    
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

/* Give the callback thread the scheduling of the JACK process thread.
 * The RT thread now waits for the host's bufferSwitch() every cycle; WineASIO
 * upstream runs that callback inside the JACK thread itself. Runs once per
 * thread, on the thread that calls asio_wait_callback (the PE callback thread,
 * also under WoW64). */
static void promote_callback_thread(AsioStream *stream)
{
    static __thread BOOL tried;
    struct sched_param sp;
    int prio, err;

    if (tried) return;
    tried = TRUE;
    if (!pjack_is_realtime || !pjack_client_real_time_priority || !pjack_is_realtime(stream->client))
        return;
    prio = pjack_client_real_time_priority(stream->client);
    if (prio <= 0) return;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = prio;
    err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (err)
        WARN("callback thread stays at normal priority, SCHED_FIFO %d: %s\n", prio, strerror(err));
    else
        TRACE("callback thread runs at SCHED_FIFO %d\n", prio);
}

static NTSTATUS asio_wait_callback(void *args)
{
    struct asio_wait_callback_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    INT64 cycle;

    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }

    promote_callback_thread(stream);
    __atomic_store_n(&stream->host_attached, TRUE, __ATOMIC_RELEASE);

    /* Block until eventfd is signaled or timeout expires.
     * This replaces the Sleep(1) polling loop on the PE side. */
    {
        struct pollfd pfd = { .fd = stream->event_fd, .events = POLLIN };
        int ret = poll(&pfd, 1, params->timeout_ms > 0 ? params->timeout_ms : 10);

        if (ret > 0) {
            /* Consume the eventfd counter */
            uint64_t val;
            eventfd_read(stream->event_fd, &val);
        }
    }

    /* Read RT-thread fields with acquire semantics; index and cycle number
     * come from one word so they always belong to the same cycle. */
    params->buffer_switch_ready = __atomic_load_n(&stream->buffer_switch_pending, __ATOMIC_ACQUIRE);
    cycle = __atomic_load_n(&stream->pending_cycle, __ATOMIC_RELAXED);
    params->buffer_index = (LONG)(cycle & 1);
    params->seq = (UINT32)(cycle >> 1);
    params->direct_process = TRUE;

    params->time_info.speed = 1.0;
    params->time_info.system_time = __atomic_load_n(&stream->system_time, __ATOMIC_RELAXED);
    params->time_info.sample_position = __atomic_load_n(&stream->sample_position, __ATOMIC_RELAXED);
    params->time_info.sample_rate = stream->sample_rate;
    params->time_info.flags = 0x7;

    if (params->buffer_switch_ready)
        __atomic_store_n(&stream->buffer_switch_pending, FALSE, __ATOMIC_RELAXED);

    /* Non-RT fields still protected by mutex */
    pthread_mutex_lock(&stream->callback_lock);
    params->sample_rate_changed = stream->sample_rate_changed;
    params->new_sample_rate = stream->new_sample_rate;
    params->reset_request = stream->reset_request;
    params->resync_request = FALSE;
    params->latency_changed = stream->latency_changed;

    stream->sample_rate_changed = FALSE;
    stream->reset_request = FALSE;
    stream->latency_changed = FALSE;
    pthread_mutex_unlock(&stream->callback_lock);

    params->result = ASE_OK;
    return STATUS_SUCCESS;
}

/* The host has returned from bufferSwitch() for cycle 'seq' (or the callback
 * thread is leaving, 'detach'): release the RT thread waiting in wait_for_host. */
static NTSTATUS asio_callback_done(void *args)
{
    struct asio_callback_done_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);

    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }

    if (params->detach)
        __atomic_store_n(&stream->host_attached, FALSE, __ATOMIC_RELEASE);
    else
        __atomic_store_n(&stream->done_seq, params->seq, __ATOMIC_RELEASE);
    wake_rt_thread(stream);

    params->result = ASE_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS asio_control_panel(void *args)
{
    struct asio_control_panel_params *params = args;
    pid_t pid;
    
    pid = fork();
    if (pid == 0) {
        /* Child process */
        static char arg0[] = "wineasio-settings";
        static char *arg_list[] = { arg0, NULL };
        
        /* Try to execute wineasio-settings from PATH */
        execvp(arg0, arg_list);
        
        /* If that fails, try common locations */
        execl("/usr/bin/wineasio-settings", "wineasio-settings", NULL);
        execl("/usr/local/bin/wineasio-settings", "wineasio-settings", NULL);
        
        /* If all fail, exit silently */
        _exit(1);
    } else if (pid < 0) {
        /* Fork failed */
        WARN("Failed to fork for control panel\n");
        params->result = ASE_NotPresent;
        return STATUS_SUCCESS;
    }
    
    /* Parent process — ignore SIGCHLD to auto-reap child and avoid zombies */
    {
        struct sigaction sa;
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_NOCLDWAIT;
        sigaction(SIGCHLD, &sa, NULL);
    }
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_future(void *args)
{
    struct asio_future_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    switch (params->selector) {
    case kAsioCanTimeInfo:
    case kAsioCanTimeCode:
        params->result = ASE_SUCCESS;
        break;
        
    case kAsioEnableTimeCodeRead:
    case kAsioDisableTimeCodeRead:
        params->result = ASE_SUCCESS;
        break;
        
    case kAsioCanInputMonitor:
    case kAsioCanTransport:
    case kAsioCanInputGain:
    case kAsioCanInputMeter:
    case kAsioCanOutputGain:
    case kAsioCanOutputMeter:
        params->result = ASE_NotPresent;
        break;
        
    default:
        params->result = ASE_NotPresent;
        break;
    }
    
    return STATUS_SUCCESS;
}

/* Unix function table */
__attribute__((visibility("default")))
const unixlib_entry_t __wine_unix_call_funcs[] =
{
    asio_init,
    asio_exit,
    asio_start,
    asio_stop,
    asio_get_channels,
    asio_get_latencies,
    asio_get_buffer_size,
    asio_can_sample_rate,
    asio_get_sample_rate,
    asio_set_sample_rate,
    asio_get_channel_info,
    asio_create_buffers,
    asio_dispose_buffers,
    asio_output_ready,
    asio_get_sample_position,
    asio_get_callback,
    asio_wait_callback,
    asio_callback_done,
    asio_control_panel,
    asio_future,
};

C_ASSERT(ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count);

#ifdef _WIN64
/* WoW64 thunks would go here if needed */
__attribute__((visibility("default")))
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    asio_init,
    asio_exit,
    asio_start,
    asio_stop,
    asio_get_channels,
    asio_get_latencies,
    asio_get_buffer_size,
    asio_can_sample_rate,
    asio_get_sample_rate,
    asio_set_sample_rate,
    asio_get_channel_info,
    asio_create_buffers,
    asio_dispose_buffers,
    asio_output_ready,
    asio_get_sample_position,
    asio_get_callback,
    asio_wait_callback,
    asio_callback_done,
    asio_control_panel,
    asio_future,
};

C_ASSERT(ARRAYSIZE(__wine_unix_call_wow64_funcs) == unix_funcs_count);
#endif