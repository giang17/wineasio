/* jack_wire.c - the JACK graph operations of run.sh, through libjack, so the
 * test needs no jack_lsp/jack_connect tools (with PipeWire's JACK they are not
 * always installed).
 *
 * Usage: jack_wire clients           print the client names, one per line
 *        jack_wire count <client>    print the number of connections of its ports
 *        jack_wire loop <client>     disconnect every port of <client>, connect
 *                                    <client>:out_1 -> <client>:in_1 and check
 *                                    that this is its only connection
 *        jack_wire relay <client>    the same loop through a second client
 *                                    (lb-relay, copies its input to its output):
 *                                    <client>:out_1 -> lb-relay:in,
 *                                    lb-relay:out -> <client>:in_1; prints
 *                                    "wired: ..." and stays until <client> is gone.
 *                                    PipeWire does not pass data over a link from
 *                                    a client to itself.
 * Exit status: 0 ok, 1 failed, 2 usage. No server is started.
 * Build: gcc -O2 -Wall -o jack_wire jack_wire.c -ljack */

#include <jack/jack.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static jack_client_t *client;

/* Does the full port name belong to client c? */
static int owned(const char *port, const char *c)
{
    size_t n = strlen(c);
    return !strncmp(port, c, n) && port[n] == ':';
}

static int count_connections(const char *c)
{
    const char **ports = jack_get_ports(client, NULL, NULL, 0), **conn;
    int i, j, total = 0;

    for (i = 0; ports && ports[i]; i++)
    {
        if (!owned(ports[i], c)) continue;
        conn = jack_port_get_all_connections(client, jack_port_by_name(client, ports[i]));
        for (j = 0; conn && conn[j]; j++) total++;
        jack_free(conn);
    }
    jack_free(ports);
    return total;
}

static void disconnect_all(const char *c)
{
    const char **ports = jack_get_ports(client, NULL, NULL, 0), **conn;
    jack_port_t *p;
    int i, j;

    for (i = 0; ports && ports[i]; i++)
    {
        if (!owned(ports[i], c)) continue;
        p = jack_port_by_name(client, ports[i]);
        conn = jack_port_get_all_connections(client, p);
        for (j = 0; conn && conn[j]; j++)
        {
            if (jack_port_flags(p) & JackPortIsOutput) jack_disconnect(client, ports[i], conn[j]);
            else jack_disconnect(client, conn[j], ports[i]);
        }
        jack_free(conn);
    }
    jack_free(ports);
}

/* Exactly out_1 -> in_1 and nothing else on any port of c. */
static int loop_ok(const char *c, const char *in, const char *out)
{
    const char **conn;
    int ok;

    if (count_connections(c) != 2) return 0;   /* both ends see the one link */
    conn = jack_port_get_all_connections(client, jack_port_by_name(client, out));
    ok = conn && conn[0] && !strcmp(conn[0], in) && !conn[1];
    jack_free(conn);
    return ok;
}

static jack_port_t *relay_in, *relay_out;

static int relay_process(jack_nframes_t nframes, void *arg)
{
    (void)arg;
    memcpy(jack_port_get_buffer(relay_out, nframes), jack_port_get_buffer(relay_in, nframes),
           sizeof(jack_default_audio_sample_t) * nframes);
    return 0;
}

/* Exactly out_1 -> lb-relay:in and lb-relay:out -> in_1, nothing else. */
static int relay_ok(const char *c, const char *in, const char *out)
{
    const char **a, **b;
    int ok;

    if (count_connections(c) != 2 || count_connections("lb-relay") != 2) return 0;
    a = jack_port_get_all_connections(client, jack_port_by_name(client, out));
    b = jack_port_get_all_connections(client, jack_port_by_name(client, in));
    ok = a && a[0] && !strcmp(a[0], "lb-relay:in") && !a[1]
      && b && b[0] && !strcmp(b[0], "lb-relay:out") && !b[1];
    jack_free(a);
    jack_free(b);
    return ok;
}

int main(int argc, char **argv)
{
    jack_status_t status;
    char in[512], out[512];
    const char **ports;
    int i, tries;

    if (argc < 2) goto usage;
    client = jack_client_open("lb-wire", JackNoStartServer, &status);
    if (!client) { fprintf(stderr, "jack_wire: no JACK server (status 0x%x)\n", status); return 1; }

    if (!strcmp(argv[1], "clients") && argc == 2)
    {
        ports = jack_get_ports(client, NULL, NULL, 0);
        for (i = 0; ports && ports[i]; i++)
        {
            const char *colon = strchr(ports[i], ':');
            int len = colon ? (int)(colon - ports[i]) : (int)strlen(ports[i]);
            if ((len == 7 && !strncmp(ports[i], "lb-wire", 7))
                || (len == 8 && !strncmp(ports[i], "lb-relay", 8))) continue;
            printf("%.*s\n", len, ports[i]);   /* run.sh sorts and dedups */
        }
        jack_free(ports);
    }
    else if (!strcmp(argv[1], "count") && argc == 3)
        printf("%d\n", count_connections(argv[2]));
    else if (!strcmp(argv[1], "loop") && argc == 3)
    {
        snprintf(in, sizeof(in), "%s:in_1", argv[2]);
        snprintf(out, sizeof(out), "%s:out_1", argv[2]);
        if (!jack_port_by_name(client, in) || !jack_port_by_name(client, out))
        {
            fprintf(stderr, "jack_wire: %s or %s not found\n", in, out);
            jack_client_close(client);
            return 1;
        }
        disconnect_all(argv[2]);
        if (jack_connect(client, out, in))
        {
            fprintf(stderr, "jack_wire: connecting %s -> %s failed\n", out, in);
            jack_client_close(client);
            return 1;
        }
        /* The graph change may take a moment to show up everywhere. */
        for (tries = 0; tries < 40 && !loop_ok(argv[2], in, out); tries++) usleep(50000);
        if (!loop_ok(argv[2], in, out))
        {
            fprintf(stderr, "jack_wire: %s has %d connections instead of only %s -> %s\n",
                    argv[2], count_connections(argv[2]), out, in);
            jack_client_close(client);
            return 1;
        }
        printf("wired: %s -> %s (no other connection)\n", out, in);
    }
    else if (!strcmp(argv[1], "relay") && argc == 3)
    {
        snprintf(in, sizeof(in), "%s:in_1", argv[2]);
        snprintf(out, sizeof(out), "%s:out_1", argv[2]);
        if (!jack_port_by_name(client, in) || !jack_port_by_name(client, out))
        {
            fprintf(stderr, "jack_wire: %s or %s not found\n", in, out);
            jack_client_close(client);
            return 1;
        }
        jack_client_close(client);
        client = jack_client_open("lb-relay", JackNoStartServer | JackUseExactName, &status);
        if (!client) { fprintf(stderr, "jack_wire: cannot open lb-relay (status 0x%x)\n", status); return 1; }
        relay_in = jack_port_register(client, "in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        relay_out = jack_port_register(client, "out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        jack_set_process_callback(client, relay_process, NULL);
        if (!relay_in || !relay_out || jack_activate(client))
        {
            fprintf(stderr, "jack_wire: cannot start lb-relay\n");
            jack_client_close(client);
            return 1;
        }
        disconnect_all(argv[2]);
        disconnect_all("lb-relay");
        if (jack_connect(client, out, "lb-relay:in") || jack_connect(client, "lb-relay:out", in))
        {
            fprintf(stderr, "jack_wire: connecting %s -> lb-relay -> %s failed\n", out, in);
            jack_client_close(client);
            return 1;
        }
        for (tries = 0; tries < 40 && !relay_ok(argv[2], in, out); tries++) usleep(50000);
        if (!relay_ok(argv[2], in, out))
        {
            fprintf(stderr, "jack_wire: %s has %d connections instead of only %s -> lb-relay -> %s\n",
                    argv[2], count_connections(argv[2]), out, in);
            jack_client_close(client);
            return 1;
        }
        printf("wired: %s -> lb-relay -> %s (no other connection)\n", out, in);
        fflush(stdout);
        /* Stay until the measured client is gone (at most 120 s). */
        for (tries = 0; tries < 1200 && jack_port_by_name(client, out); tries++) usleep(100000);
    }
    else
    {
        jack_client_close(client);
        goto usage;
    }
    jack_client_close(client);
    return 0;

usage:
    fprintf(stderr, "Usage: jack_wire clients | count <client> | loop <client> | relay <client>\n");
    return 2;
}
