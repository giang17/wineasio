/*
 * WineASIO Unix Library Interface
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

#ifndef __WINEASIO_UNIXLIB_H
#define __WINEASIO_UNIXLIB_H

#include <stdint.h>

/* Platform-specific includes */
#ifdef WINE_UNIX_LIB
/* Unix side - building the .so */
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "wine/unixlib.h"
#else
/* PE side - building with mingw or winegcc */
#ifdef _WIN32
#include <windef.h>
#include <winbase.h>
#else
#include "windef.h"
#include "winbase.h"
#endif

/* Define unixlib types for PE side if not available */
#ifndef __WINE_WINE_UNIXLIB_H
typedef uint64_t unixlib_handle_t;

/* These are imported from ntdll */
extern unixlib_handle_t __wine_unixlib_handle;
extern NTSTATUS (WINAPI *__wine_unix_call_dispatcher)(unixlib_handle_t, unsigned int, void *);
extern NTSTATUS WINAPI __wine_init_unix_call(void);

static inline NTSTATUS __wine_unix_call(unixlib_handle_t handle, unsigned int code, void *args)
{
    return __wine_unix_call_dispatcher(handle, code, args);
}
#endif /* __WINE_WINE_UNIXLIB_H */

#endif /* WINE_UNIX_LIB */

/* Maximum number of channels */
#define WINEASIO_MAX_CHANNELS 128

/* Sample types matching ASIO SDK */
typedef enum {
    ASIOSTInt16MSB   = 0,
    ASIOSTInt24MSB   = 1,
    ASIOSTInt32MSB   = 2,
    ASIOSTFloat32MSB = 3,
    ASIOSTFloat64MSB = 4,
    ASIOSTInt32MSB16 = 8,
    ASIOSTInt32MSB18 = 9,
    ASIOSTInt32MSB20 = 10,
    ASIOSTInt32MSB24 = 11,
    ASIOSTInt16LSB   = 16,
    ASIOSTInt24LSB   = 17,
    ASIOSTInt32LSB   = 18,
    ASIOSTFloat32LSB = 19,
    ASIOSTFloat64LSB = 20,
    ASIOSTInt32LSB16 = 24,
    ASIOSTInt32LSB18 = 25,
    ASIOSTInt32LSB20 = 26,
    ASIOSTInt32LSB24 = 27,
} ASIOSampleType;

/* Stream handle - opaque pointer to Unix-side stream */
typedef UINT64 asio_handle;

/* Buffer info for channel setup */
struct asio_buffer_info {
    BOOL is_input;
    LONG channel_num;
    UINT64 buffer_ptr[2];  /* Double-buffering pointers */
};

/* Channel info */
struct asio_channel_info {
    LONG channel;
    BOOL is_input;
    BOOL is_active;
    LONG channel_group;
    ASIOSampleType sample_type;
    char name[32];
};

/* Time info from JACK */
struct asio_time_info {
    double speed;
    INT64 system_time;
    INT64 sample_position;
    double sample_rate;
    UINT32 flags;
};

/* Configuration read from registry (passed to Unix side) */
struct asio_config {
    LONG num_inputs;
    LONG num_outputs;
    LONG preferred_bufsize;
    BOOL fixed_bufsize;
    BOOL autoconnect;
    char client_name[64];
};

/*
 * Parameter structures for each Unix function call
 */

struct asio_init_params {
    struct asio_config config;
    HRESULT result;
    asio_handle handle;
    LONG input_channels;
    LONG output_channels;
    double sample_rate;
};

struct asio_exit_params {
    asio_handle handle;
    HRESULT result;
};

struct asio_start_params {
    asio_handle handle;
    HRESULT result;
};

struct asio_stop_params {
    asio_handle handle;
    HRESULT result;
};

struct asio_get_channels_params {
    asio_handle handle;
    HRESULT result;
    LONG num_inputs;
    LONG num_outputs;
};

struct asio_get_latencies_params {
    asio_handle handle;
    HRESULT result;
    LONG input_latency;
    LONG output_latency;
};

struct asio_get_buffer_size_params {
    asio_handle handle;
    HRESULT result;
    LONG min_size;
    LONG max_size;
    LONG preferred_size;
    LONG granularity;
};

struct asio_can_sample_rate_params {
    asio_handle handle;
    double sample_rate;
    HRESULT result;
};

struct asio_get_sample_rate_params {
    asio_handle handle;
    HRESULT result;
    double sample_rate;
};

struct asio_set_sample_rate_params {
    asio_handle handle;
    double sample_rate;
    HRESULT result;
};

struct asio_get_channel_info_params {
    asio_handle handle;
    struct asio_channel_info info;
    HRESULT result;
};

struct asio_create_buffers_params {
    asio_handle handle;
    LONG num_channels;
    LONG buffer_size;
    struct asio_buffer_info *buffer_infos;  /* Array of buffer_info */
    HRESULT result;
};

struct asio_dispose_buffers_params {
    asio_handle handle;
    HRESULT result;
};

struct asio_output_ready_params {
    asio_handle handle;
    HRESULT result;
};

struct asio_get_sample_position_params {
    asio_handle handle;
    HRESULT result;
    INT64 sample_position;
    INT64 system_time;
};

/* Callback notification - PE side polls for this */
struct asio_get_callback_params {
    asio_handle handle;
    HRESULT result;
    BOOL buffer_switch_ready;
    LONG buffer_index;
    BOOL direct_process;
    struct asio_time_info time_info;
    BOOL sample_rate_changed;
    double new_sample_rate;
    BOOL reset_request;
    BOOL resync_request;
    BOOL latency_changed;
};

/* Acknowledge that callback was processed */
struct asio_callback_done_params {
    asio_handle handle;
    LONG buffer_index;
    HRESULT result;
};

struct asio_control_panel_params {
    asio_handle handle;
    HRESULT result;
};

struct asio_future_params {
    asio_handle handle;
    LONG selector;
    UINT64 opt;  /* Pointer to optional parameter */
    HRESULT result;
};

/*
 * Unix function IDs
 */
enum unix_funcs {
    unix_asio_init,
    unix_asio_exit,
    unix_asio_start,
    unix_asio_stop,
    unix_asio_get_channels,
    unix_asio_get_latencies,
    unix_asio_get_buffer_size,
    unix_asio_can_sample_rate,
    unix_asio_get_sample_rate,
    unix_asio_set_sample_rate,
    unix_asio_get_channel_info,
    unix_asio_create_buffers,
    unix_asio_dispose_buffers,
    unix_asio_output_ready,
    unix_asio_get_sample_position,
    unix_asio_get_callback,
    unix_asio_callback_done,
    unix_asio_control_panel,
    unix_asio_future,
    unix_funcs_count
};

/* Error codes matching ASIO SDK */
#define ASE_OK              0
#define ASE_SUCCESS         0x3f4847a0
#define ASE_NotPresent      (-1000)
#define ASE_HWMalfunction   (-999)
#define ASE_InvalidParameter (-998)
#define ASE_InvalidMode     (-997)
#define ASE_SPNotAdvancing  (-996)
#define ASE_NoClock         (-995)
#define ASE_NoMemory        (-994)

/* Future selectors */
#define kAsioEnableTimeCodeRead     1
#define kAsioDisableTimeCodeRead    2
#define kAsioSetInputMonitor        3
#define kAsioTransport              4
#define kAsioSetInputGain           5
#define kAsioGetInputMeter          6
#define kAsioSetOutputGain          7
#define kAsioGetOutputMeter         8
#define kAsioCanInputMonitor        9
#define kAsioCanTimeInfo            10
#define kAsioCanTimeCode            11
#define kAsioCanTransport           12
#define kAsioCanInputGain           13
#define kAsioCanInputMeter          14
#define kAsioCanOutputGain          15
#define kAsioCanOutputMeter         16
#define kAsioOptionalOne            17
#define kAsioSetIoFormat            0x23111961
#define kAsioGetIoFormat            0x23111983
#define kAsioCanDoIoFormat          0x23112004
#define kAsioCanReportOverload      0x24042012
#define kAsioGetInternalBufferSamples 0x25042012
#define kAsioSupportsInputResampling  0x26092017

#endif /* __WINEASIO_UNIXLIB_H */