/* jack_loopback.c - control case: a native JACK client with the same measuring
 * logic as asio_loopback.c. It shows what the JACK-internal loop out -> in costs
 * by itself (expected: exactly one period), and with that checks the method.
 *
 * Usage: jack_loopback <arm-file> [measurements]
 *        LB_WRITE_FIRST=1: write the output first, then read the input
 * Build: gcc -O2 -Wall -o jack_loopback jack_loopback.c -ljack -lm
 * Driven by run.sh. */

#include <jack/jack.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "lb_common.h"

static jack_port_t *in_port, *out_port;
static int write_first;

static int process(jack_nframes_t nframes, void *arg)
{
    const float *in;
    float *out;
    long c;
    (void)arg;
    /* The same order of jack_port_get_buffer() calls as WineASIO: for a
     * connection between ports of the same client, the input returns the
     * output as it is at the time of its get_buffer() call. */
    if (write_first)
    {
        out = jack_port_get_buffer(out_port, nframes);
        c = ++lb_calls;
        lb_send(out, nframes, c);
        in = jack_port_get_buffer(in_port, nframes);
        lb_scan(in, nframes, c);
    }
    else
    {
        in = jack_port_get_buffer(in_port, nframes);
        out = jack_port_get_buffer(out_port, nframes);
        lb_process(in, out, nframes);
    }
    return 0;
}

int main(int argc, char **argv)
{
    jack_status_t status;
    jack_client_t *client;
    int waited;

    if (argc < 2) { fprintf(stderr, "Usage: %s <arm-file> [measurements]\n", argv[0]); return 2; }
    if (argc > 2) lb_nmeas = atoi(argv[2]);
    if (lb_nmeas < 1 || lb_nmeas > LB_MAX_MEAS) lb_nmeas = 50;
    setvbuf(stdout, NULL, _IONBF, 0);
    write_first = getenv("LB_WRITE_FIRST") && atoi(getenv("LB_WRITE_FIRST"));

    client = jack_client_open("lb-control", JackNoStartServer, &status);
    if (!client) { printf("ERROR jack_client_open status=0x%x\n", status); return 1; }
    in_port = jack_port_register(client, "in_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    out_port = jack_port_register(client, "out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    lb_bufsize = jack_get_buffer_size(client);
    jack_set_process_callback(client, process, NULL);
    if (jack_activate(client)) { printf("ERROR jack_activate\n"); return 1; }
    printf("INFO client=%s bufsize=%ld rate=%u\n", jack_get_client_name(client), lb_bufsize,
           jack_get_sample_rate(client));
    printf("READY\n");

    for (waited = 0; access(argv[1], F_OK) != 0; waited += 50)
    {
        if (waited > 30000) { printf("NOT_ARMED\n"); jack_client_close(client); return 3; }
        usleep(50000);
    }
    lb_armed = 1;
    for (waited = 0; !lb_done && waited < 20000; waited += 20) usleep(20000);

    jack_deactivate(client);
    printf("INFO order=%s\n", write_first ? "write-first" : "read-first");
    lb_report("control", jack_get_sample_rate(client));
    jack_client_close(client);
    return lb_meas ? 0 : 1;
}
