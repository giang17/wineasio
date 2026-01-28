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
#include <sys/mman.h>
#include <pthread.h>
#include <dlfcn.h>

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

#define JACK_DEFAULT_AUDIO_TYPE "32 bit float mono audio"
#define JACK_DEFAULT_MIDI_TYPE  "8 bit raw midi"
#define JackPortIsInput  0x1
#define JackPortIsOutput 0x2
#define JackPortIsPhysical 0x4

#define MAX_CHANNELS 128
#define MAX_NAME_LENGTH 64

/* JACK MIDI event structure */
typedef struct {
    jack_nframes_t time;
    size_t size;
    unsigned char *buffer;
} jack_midi_event_t;

/* JACK MIDI function pointers */
static jack_nframes_t (*pjack_midi_get_event_count)(void*);
static int (*pjack_midi_event_get)(jack_midi_event_t*, void*, jack_nframes_t);
static void (*pjack_midi_clear_buffer)(void*);
static int (*pjack_midi_event_write)(void*, jack_nframes_t, const unsigned char*, size_t);

/* MIDI ringbuffer for thread-safe MIDI event passing */
#define MIDI_RINGBUFFER_SIZE 256
#define MAX_MIDI_EVENT_SIZE 256

typedef struct {
    unsigned char data[MAX_MIDI_EVENT_SIZE];
    size_t size;
    jack_nframes_t time;
} MidiEvent;

typedef struct {
    MidiEvent events[MIDI_RINGBUFFER_SIZE];
    volatile int read_pos;
    volatile int write_pos;
} MidiRingBuffer;

/* MIDI port state */
typedef struct {
    jack_port_t *port;
    char name[MAX_NAME_LENGTH];
    BOOL active;
    MidiRingBuffer ringbuffer;
} MidiChannel;

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
    
    /* Callback notification (polled by PE side) */
    pthread_mutex_t callback_lock;
    BOOL buffer_switch_pending;
    LONG pending_buffer_index;
    INT64 sample_position;
    INT64 system_time;
    BOOL sample_rate_changed;
    double new_sample_rate;
    BOOL reset_request;
    BOOL latency_changed;
    
    /* Config */
    BOOL autoconnect;
    BOOL fixed_bufsize;
    LONG preferred_bufsize;
    
    /* JACK MIDI ports */
    BOOL midi_enabled;
    MidiChannel midi_input;
    MidiChannel midi_output;
} AsioStream;

enum { Loaded = 0, Initialized, Prepared, Running };

static BOOL jack_loaded = FALSE;

/* Library constructor - called when .so is loaded */
__attribute__((constructor))
static void wineasio_unix_init(void)
{
    /* Library loaded - minimal output for production */
    TRACE("Unix library loaded\n");
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
    
    #undef LOAD_SYM
    
    /* JACK MIDI functions - load manually, optional */
    pjack_midi_get_event_count = dlsym(jack_handle, "jack_midi_get_event_count");
    pjack_midi_event_get = dlsym(jack_handle, "jack_midi_event_get");
    pjack_midi_clear_buffer = dlsym(jack_handle, "jack_midi_clear_buffer");
    pjack_midi_event_write = dlsym(jack_handle, "jack_midi_event_write");
    if (!pjack_client_open || !pjack_client_close) {
        ERR("JACK library missing critical symbols\n");
        dlclose(jack_handle);
        jack_handle = NULL;
        return FALSE;
    }
    
    jack_loaded = TRUE;
    TRACE("JACK library loaded successfully\n");
    return TRUE;
}

/* Get current time in nanoseconds */
static INT64 get_system_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (INT64)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* Helper: write MIDI event to ringbuffer */
static void midi_ringbuffer_write(MidiRingBuffer *rb, const unsigned char *data, size_t size, jack_nframes_t time)
{
    int next_write = (rb->write_pos + 1) % MIDI_RINGBUFFER_SIZE;
    if (next_write == rb->read_pos) {
        /* Buffer full, drop event */
        return;
    }
    
    if (size > MAX_MIDI_EVENT_SIZE) size = MAX_MIDI_EVENT_SIZE;
    
    memcpy(rb->events[rb->write_pos].data, data, size);
    rb->events[rb->write_pos].size = size;
    rb->events[rb->write_pos].time = time;
    rb->write_pos = next_write;
}

/* JACK process callback - runs in realtime thread */
static int jack_process_callback(jack_nframes_t nframes, void *arg)
{
    AsioStream *stream = (AsioStream *)arg;
    int i;
    
    /* Process JACK MIDI input - always, even when audio not running */
    if (stream->midi_enabled && stream->midi_input.port && pjack_midi_get_event_count) {
        void *midi_buf = pjack_port_get_buffer(stream->midi_input.port, nframes);
        if (midi_buf) {
            jack_nframes_t event_count = pjack_midi_get_event_count(midi_buf);
            for (jack_nframes_t j = 0; j < event_count; j++) {
                jack_midi_event_t event;
                if (pjack_midi_event_get(&event, midi_buf, j) == 0) {
                    midi_ringbuffer_write(&stream->midi_input.ringbuffer,
                                         event.buffer, event.size, event.time);
                }
            }
        }
    }
    
    /* Process JACK MIDI output */
    if (stream->midi_enabled && stream->midi_output.port && pjack_midi_clear_buffer) {
        void *midi_buf = pjack_port_get_buffer(stream->midi_output.port, nframes);
        if (midi_buf) {
            pjack_midi_clear_buffer(midi_buf);
            
            /* Write pending MIDI events from ringbuffer */
            MidiRingBuffer *rb = &stream->midi_output.ringbuffer;
            while (rb->read_pos != rb->write_pos) {
                MidiEvent *ev = &rb->events[rb->read_pos];
                if (pjack_midi_event_write) {
                    pjack_midi_event_write(midi_buf, ev->time % nframes, ev->data, ev->size);
                }
                rb->read_pos = (rb->read_pos + 1) % MIDI_RINGBUFFER_SIZE;
            }
        }
    }
    
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
    
    /* Copy PE-side output buffer to JACK
     * Wine 11 WoW64 fix: Use pe_buffer[] instead of audio_buffer */
    for (i = 0; i < stream->num_outputs; i++) {
        if (stream->outputs[i].active && stream->outputs[i].port) {
            void *jack_buf = pjack_port_get_buffer(stream->outputs[i].port, nframes);
            jack_default_audio_sample_t *pe_buf = stream->outputs[i].pe_buffer[stream->buffer_index];
            if (jack_buf && pe_buf) {
                memcpy(jack_buf, pe_buf, sizeof(jack_default_audio_sample_t) * nframes);
            }
            /* No logging in realtime callback - causes xruns */
        }
    }
    
    /* Update sample position */
    stream->sample_position += nframes;
    stream->system_time = get_system_time();
    
    /* Signal buffer switch to PE side */
    pthread_mutex_lock(&stream->callback_lock);
    stream->pending_buffer_index = stream->buffer_index;
    stream->buffer_switch_pending = TRUE;
    pthread_mutex_unlock(&stream->callback_lock);
    
    /* Switch buffers */
    stream->buffer_index = stream->buffer_index ? 0 : 1;
    
    return 0;
}

/* JACK buffer size callback */
static int jack_buffer_size_callback(jack_nframes_t nframes, void *arg)
{
    AsioStream *stream = (AsioStream *)arg;
    
    TRACE("Buffer size changed to %u\n", nframes);
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
    
    TRACE("Sample rate changed to %u\n", nframes);
    
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
    
    TRACE("asio_init called\n");
    
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
    options = JackNoStartServer;  /* Don't start JACK if not running */
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
    
    /* Initialize mutex */
    pthread_mutex_init(&stream->callback_lock, NULL);
    
    /* Register ports */
    for (i = 0; i < stream->num_inputs; i++) {
        snprintf(stream->inputs[i].name, MAX_NAME_LENGTH, "in_%d", i + 1);
        stream->inputs[i].port = pjack_port_register(stream->client,
            stream->inputs[i].name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        stream->inputs[i].active = FALSE;
    }
    
    for (i = 0; i < stream->num_outputs; i++) {
        snprintf(stream->outputs[i].name, MAX_NAME_LENGTH, "out_%d", i + 1);
        stream->outputs[i].port = pjack_port_register(stream->client,
            stream->outputs[i].name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        stream->outputs[i].active = FALSE;
    }
    
    /* Register JACK MIDI ports */
    stream->midi_enabled = FALSE;
    if (pjack_midi_get_event_count && pjack_midi_clear_buffer) {
        /* MIDI input port */
        snprintf(stream->midi_input.name, MAX_NAME_LENGTH, "midi_in");
        stream->midi_input.port = pjack_port_register(stream->client,
            stream->midi_input.name, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        stream->midi_input.active = TRUE;
        stream->midi_input.ringbuffer.read_pos = 0;
        stream->midi_input.ringbuffer.write_pos = 0;
        
        /* MIDI output port */
        snprintf(stream->midi_output.name, MAX_NAME_LENGTH, "midi_out");
        stream->midi_output.port = pjack_port_register(stream->client,
            stream->midi_output.name, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
        stream->midi_output.active = TRUE;
        stream->midi_output.ringbuffer.read_pos = 0;
        stream->midi_output.ringbuffer.write_pos = 0;
        
        if (stream->midi_input.port && stream->midi_output.port) {
            stream->midi_enabled = TRUE;
            TRACE("JACK MIDI ports registered: %s, %s\n",
                  stream->midi_input.name, stream->midi_output.name);
        } else {
            WARN("Failed to register JACK MIDI ports\n");
        }
    } else {
        TRACE("JACK MIDI functions not available\n");
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
    TRACE("%s called\n", __func__);
    struct asio_exit_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    int i;
    
    if (!stream) {
        params->result = ASE_InvalidParameter;
        return STATUS_SUCCESS;
    }
    
    TRACE("Shutting down WineASIO\n");
    
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
        
        /* Unregister MIDI ports */
        if (stream->midi_input.port)
            pjack_port_unregister(stream->client, stream->midi_input.port);
        if (stream->midi_output.port)
            pjack_port_unregister(stream->client, stream->midi_output.port);
        
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
    
    free(stream);
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_start(void *args)
{
    TRACE("%s called\n", __func__);
    struct asio_start_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    int i;
    
    TRACE("asio_start called: stream=%p, state=%d\n", stream, stream ? stream->state : -1);
    
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
    
    stream->state = Running;
    params->result = ASE_OK;
    
    TRACE("WineASIO started\n");
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_stop(void *args)
{
    TRACE("%s called\n", __func__);
    struct asio_stop_params *params = args;
    AsioStream *stream = handle_to_stream(params->handle);
    
    if (!stream || stream->state != Running) {
        params->result = ASE_InvalidMode;
        return STATUS_SUCCESS;
    }
    
    stream->state = Prepared;
    params->result = ASE_OK;
    
    TRACE("WineASIO stopped\n");
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_get_channels(void *args)
{
    TRACE("%s called\n", __func__);
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
    TRACE("%s called\n", __func__);
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
    
    params->input_latency = stream->input_latency;
    params->output_latency = stream->output_latency;
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_get_buffer_size(void *args)
{
    TRACE("%s called\n", __func__);
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
    TRACE("%s called\n", __func__);
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
    TRACE("%s called\n", __func__);
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
    TRACE("%s called\n", __func__);
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
    TRACE("%s called\n", __func__);
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
    
    TRACE("asio_create_buffers: num_channels=%d, buffer_size=%d\n", 
          params->num_channels, params->buffer_size);
    
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
    
    TRACE("Buffers created: %d channels, %d samples\n",
          params->num_channels, stream->buffer_size);
    
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
    
    TRACE("Buffers disposed\n");
    
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
    
    pthread_mutex_lock(&stream->callback_lock);
    
    params->buffer_switch_ready = stream->buffer_switch_pending;
    params->buffer_index = stream->pending_buffer_index;
    params->direct_process = TRUE;
    
    params->time_info.speed = 1.0;
    params->time_info.system_time = stream->system_time;
    params->time_info.sample_position = stream->sample_position;
    params->time_info.sample_rate = stream->sample_rate;
    params->time_info.flags = 0x7;  /* Valid flags */
    
    params->sample_rate_changed = stream->sample_rate_changed;
    params->new_sample_rate = stream->new_sample_rate;
    params->reset_request = stream->reset_request;
    params->resync_request = FALSE;
    params->latency_changed = stream->latency_changed;
    
    /* Clear pending flags and log only on actual buffer switch */
    if (stream->buffer_switch_pending) {
        TRACE("Buffer switch ready, index=%d\n", params->buffer_index);
    }
    stream->buffer_switch_pending = FALSE;
    stream->sample_rate_changed = FALSE;
    stream->reset_request = FALSE;
    stream->latency_changed = FALSE;
    
    pthread_mutex_unlock(&stream->callback_lock);
    
    params->result = ASE_OK;
    
    return STATUS_SUCCESS;
}

static NTSTATUS asio_callback_done(void *args)
{
    struct asio_callback_done_params *params = args;
    /* Currently unused - the callback is processed synchronously */
    params->result = ASE_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS asio_control_panel(void *args)
{
    struct asio_control_panel_params *params = args;
    pid_t pid;
    
    TRACE("Control panel requested - launching wineasio-settings\n");
    
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
    
    /* Parent process - don't wait for child */
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
        TRACE("Unknown future selector: %d\n", params->selector);
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
    asio_callback_done,
    asio_control_panel,
    asio_future,
};

C_ASSERT(ARRAYSIZE(__wine_unix_call_wow64_funcs) == unix_funcs_count);
#endif