/* Flaux VR: rooms.  Everyone in a room builds in the same scene and sees each other.
 *
 * There are two ways to be in a room:
 *   - On the same Wi-Fi: one headset hosts, shouting "I'm here" on the network once a second so
 *     the room shows up on the others by itself (or you type the host's address).
 *   - Over the internet: every headset connects to a Flaux VR server (see server/) and gives a
 *     room code.  The server plays the part of the host.  Addresses look like
 *     ws://192.168.1.5:8080 (a server on your own computer) or wss://my-server.onrender.com.
 *
 * Either way it's plain text lines:
 *     room <code>             joiner -> server: which room (not used with a headset host)
 *     hello <name>            joiner -> host
 *     you <id>                host -> joiner: your player number
 *     full ... endfull        host -> joiner: the whole scene, as obj lines
 *     obj <...>               an object was added or changed
 *     del <id>                an object was removed
 *     pose <id> <name> <21 numbers>   where a player's head and hands are
 * To a headset host the lines go straight down a TCP connection (port 24727).  To a server they
 * travel inside a WebSocket, with TLS around it for wss:// addresses. */
#include "fx.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <psa/crypto.h>

#define PORT 24727
#define BEACON_PORT 24728
#define RX_MAX 4096
#define WS_MAX 65536

Peer g_peers[MAX_PEERS];
Room g_rooms[MAX_ROOMS];
char g_net_status[96] = "Not in a room";
PublicRoom g_public[MAX_PUBLIC];
int g_public_count, g_public_state;

typedef struct {
    mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context drbg; mbedtls_entropy_context entropy; mbedtls_x509_crt ca;
    int fd;
} Tls;

typedef struct {
    int fd;
    char rx[RX_MAX]; int rxn;
    char *tx; size_t txn, txcap, tls_retry;
    uint32_t pid; char name[20];
    int ws;                      /* lines travel inside WebSocket frames */
    Tls *tls;
    unsigned char *wsbuf; int wsn;
} Conn;

static int mode;                       /* 0 alone, 1 hosting, 2 joined, 3 joining */
static int via_server;
static int listen_fd = -1, beacon_fd = -1, radio_fd = -1;
static Conn conns[MAX_PEERS];          /* hosting: the joiners.  joined: conns[0] is the host or the server */
static uint32_t my_pid = 1, next_pid = 2;
static char my_name[20] = "Player", room_name[40] = "Room", local_ip[20] = "";
static double now, last_sync, last_pose, last_beacon, last_ping, join_started;
static Obj *synced; static int synced_count;
static int in_full;
static int room_locked; static uint32_t owner_pid = 1;      /* who may lock the room: the host, or whoever opened it on a server */
static struct { int on; V3 from, to; } my_aim[2];
static unsigned rng = 0x2545F491u;

int net_mode(void) { return mode; }
int net_on_server(void) { return mode >= 2 && via_server; }
int net_is_owner(void) { return mode == 1 || (mode == 2 && owner_pid == my_pid); }
int net_locked(void) { return mode != 0 && room_locked; }
int net_locked_out(void) { return mode == 2 && room_locked && owner_pid != my_pid; }
void net_set_aim(int hand, int on, V3 from, V3 to) { my_aim[hand].on = on; my_aim[hand].from = from; my_aim[hand].to = to; }

static void nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
static unsigned rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

const char *net_local_ip(void) {
    static double asked = -10;
    if (now - asked < 2.0 && now >= asked) return local_ip;      /* asking is cheap, but not every-frame cheap */
    asked = now;
    local_ip[0] = 0;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return local_ip;
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(53); inet_pton(AF_INET, "8.8.8.8", &a.sin_addr);
    socklen_t len = sizeof a;                 /* nothing is sent: this just asks which address we'd use */
    if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0 && getsockname(fd, (struct sockaddr *)&a, &len) == 0)
        inet_ntop(AF_INET, &a.sin_addr, local_ip, sizeof local_ip);
    close(fd);
    return local_ip;
}

void net_init(const char *player_name) {
    snprintf(my_name, sizeof my_name, "%s", player_name);
    for (char *p = my_name; *p; p++) if (*p == ' ') *p = '_';
    if (synced) return;                      /* just a new name */
    for (int i = 0; i < MAX_PEERS; i++) conns[i].fd = -1;
    synced = malloc(sizeof(Obj) * MAX_OBJ);
    struct timeval tv; gettimeofday(&tv, NULL);
    rng ^= (unsigned)tv.tv_usec * 2654435761u + (unsigned)tv.tv_sec;
    if (!rng) rng = 1;
}

/* ---------- TLS (for wss:// servers) ---------- */

static int tls_send(void *ctx, const unsigned char *buf, size_t len) {
    ssize_t k = send(((Tls *)ctx)->fd, buf, len, MSG_NOSIGNAL);
    if (k >= 0) return (int)k;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}
static int tls_recv(void *ctx, unsigned char *buf, size_t len) {
    ssize_t k = recv(((Tls *)ctx)->fd, buf, len, 0);
    if (k > 0) return (int)k;
    if (k == 0) return MBEDTLS_ERR_SSL_CONN_EOF;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static void tls_free(Tls *t) {
    if (!t) return;
    mbedtls_ssl_free(&t->ssl); mbedtls_ssl_config_free(&t->conf);
    mbedtls_ctr_drbg_free(&t->drbg); mbedtls_entropy_free(&t->entropy); mbedtls_x509_crt_free(&t->ca);
    free(t);
}

/* a secure connection over fd (blocking, with the socket's timeouts); NULL and a reason if it can't be trusted */
static Tls *tls_open(int fd, const char *host, char *err, size_t errn) {
    static int psa_ready;
    if (!psa_ready) { psa_crypto_init(); psa_ready = 1; }
    Tls *t = calloc(1, sizeof *t);
    t->fd = fd;
    mbedtls_ssl_init(&t->ssl); mbedtls_ssl_config_init(&t->conf);
    mbedtls_ctr_drbg_init(&t->drbg); mbedtls_entropy_init(&t->entropy); mbedtls_x509_crt_init(&t->ca);
    int r = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy, (const unsigned char *)"flauxvr", 7);
    if (!r) r = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (r) { snprintf(err, errn, "Couldn't start a secure connection"); tls_free(t); return NULL; }
    /* who to trust: the certificate authorities the system trusts */
    static const char *const dirs[] = {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"};
    static const char *const files[] = {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem"};
    int loaded = 0;
    if (getenv("FLAUX_CA_FILE") && mbedtls_x509_crt_parse_file(&t->ca, getenv("FLAUX_CA_FILE")) >= 0) loaded = 1;
    for (size_t i = 0; i < 2; i++) if (mbedtls_x509_crt_parse_path(&t->ca, dirs[i]) >= 0 && t->ca.version) loaded = 1;
    for (size_t i = 0; i < 3 && !loaded; i++) if (mbedtls_x509_crt_parse_file(&t->ca, files[i]) >= 0 && t->ca.version) loaded = 1;
    if (!loaded || !t->ca.version) { snprintf(err, errn, "No trusted certificates on this device"); tls_free(t); return NULL; }
    mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, NULL);
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);
    r = mbedtls_ssl_setup(&t->ssl, &t->conf);
    if (!r) r = mbedtls_ssl_set_hostname(&t->ssl, host);
    if (r) { snprintf(err, errn, "Couldn't start a secure connection"); tls_free(t); return NULL; }
    mbedtls_ssl_set_bio(&t->ssl, t, tls_send, tls_recv, NULL);
    while ((r = mbedtls_ssl_handshake(&t->ssl)) != 0) {
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { r = -1; }      /* the socket's timeout ran out */
            else continue;
        }
        if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) snprintf(err, errn, "That server's certificate isn't trusted");
        else snprintf(err, errn, "The secure connection failed");
        tls_free(t);
        return NULL;
    }
    return t;
}

/* ---------- connections ---------- */

static void conn_close(Conn *c) {
    if (c->tls) { mbedtls_ssl_close_notify(&c->tls->ssl); tls_free(c->tls); }
    if (c->fd >= 0) close(c->fd);
    free(c->tx); free(c->wsbuf);
    memset(c, 0, sizeof *c);
    c->fd = -1;
}

/* >0 bytes, 0 nothing right now, -1 gone */
static int io_recv(Conn *c, void *buf, size_t n) {
    if (c->tls) {
        int r = mbedtls_ssl_read(&c->tls->ssl, buf, n);
        if (r > 0) return r;
        return (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) ? 0 : -1;
    }
    ssize_t k = recv(c->fd, buf, n, 0);
    if (k > 0) return (int)k;
    if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || (errno == ENOTCONN && mode == 3))) return 0;
    return -1;
}

static void tx_append(Conn *c, const void *data, size_t n) {
    if (c->txn + n > c->txcap) { c->txcap = (c->txn + n) * 2; c->tx = realloc(c->tx, c->txcap); }
    memcpy(c->tx + c->txn, data, n); c->txn += n;
}

static void ws_frame(Conn *c, int opcode, const char *data, size_t n) {
    unsigned char head[14]; size_t h = 0;
    head[h++] = (unsigned char)(0x80 | opcode);
    if (n < 126) head[h++] = (unsigned char)(0x80 | n);
    else if (n < 65536) { head[h++] = 0x80 | 126; head[h++] = (unsigned char)(n >> 8); head[h++] = (unsigned char)n; }
    else { head[h++] = 0x80 | 127; for (int i = 7; i >= 0; i--) head[h++] = (unsigned char)((uint64_t)n >> (i * 8)); }
    unsigned char key[4]; unsigned r = rnd(); memcpy(key, &r, 4);       /* what a browser would do: mask what we send */
    memcpy(head + h, key, 4); h += 4;
    tx_append(c, head, h);
    size_t at = c->txn;
    tx_append(c, data, n);
    for (size_t i = 0; i < n; i++) c->tx[at + i] ^= (char)key[i & 3];
}

static void conn_send(Conn *c, const char *s) {
    if (c->fd < 0) return;
    size_t n = strlen(s);
    if (c->txn + n > (4u << 20)) { conn_close(c); return; }     /* hopelessly behind */
    if (c->ws) ws_frame(c, 1, s, n);
    else tx_append(c, s, n);
}

static void conn_flush(Conn *c) {
    while (c->fd >= 0 && c->txn > 0) {
        long k;
        if (c->tls) {
            size_t want = c->tls_retry ? c->tls_retry : (c->txn > 16000 ? 16000 : c->txn);    /* a retry must repeat the same bytes */
            int r = mbedtls_ssl_write(&c->tls->ssl, (const unsigned char *)c->tx, want);
            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) { c->tls_retry = want; break; }
            c->tls_retry = 0;
            k = r > 0 ? r : -1;
        } else {
            k = send(c->fd, c->tx, c->txn, MSG_NOSIGNAL);
            if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || (errno == ENOTCONN && mode == 3))) break;
        }
        if (k <= 0) { conn_close(c); break; }
        memmove(c->tx, c->tx + k, c->txn - (size_t)k); c->txn -= (size_t)k;
    }
}

static void send_all(const char *s, const Conn *except) {
    for (int i = 0; i < MAX_PEERS; i++)
        if (conns[i].fd >= 0 && &conns[i] != except && (mode == 2 || conns[i].pid)) conn_send(&conns[i], s);
}

/* ---------- the scene ---------- */

static Obj *synced_find(uint32_t id) {
    for (int i = 0; i < synced_count; i++) if (synced[i].id == id) return &synced[i];
    return NULL;
}

static void mark_synced(void) { memcpy(synced, g_obj, sizeof(Obj) * (size_t)g_obj_count); synced_count = g_obj_count; }

static void sync_scene(void) {            /* tell the others what changed here since last time */
    char line[256];
    if (net_locked_out()) {                /* not allowed to change anything: put it back as it was */
        if (g_obj_count != synced_count || memcmp(g_obj, synced, sizeof(Obj) * (size_t)synced_count) != 0) {
            memcpy(g_obj, synced, sizeof(Obj) * (size_t)synced_count); g_obj_count = synced_count;
        }
        return;
    }
    for (int i = 0; i < g_obj_count; i++) {
        const Obj *o = &g_obj[i];
        const Obj *s = (i < synced_count && synced[i].id == o->id) ? &synced[i] : synced_find(o->id);
        if (!s || memcmp(s, o, sizeof *o) != 0) { scene_write_obj(o, line, sizeof line); send_all(line, NULL); }
    }
    for (int i = 0; i < synced_count; i++)
        if (!scene_find(synced[i].id)) { snprintf(line, sizeof line, "del %u\n", synced[i].id); send_all(line, NULL); }
    mark_synced();
}

static void apply_obj(const Obj *o) {
    Obj *mine = scene_find(o->id);
    if (mine) *mine = *o;
    else if (g_obj_count < MAX_OBJ) g_obj[g_obj_count++] = *o;
    Obj *s = synced_find(o->id);
    if (s) *s = *o;
    else if (synced_count < MAX_OBJ) synced[synced_count++] = *o;
}

static void apply_del(uint32_t id) {
    scene_remove(id);
    Obj *s = synced_find(id);
    if (s) { memmove(s, s + 1, sizeof(Obj) * (size_t)(synced_count - (int)(s - synced) - 1)); synced_count--; }
}

/* ---------- players ---------- */

static Peer *peer_get(uint32_t id) {
    Peer *spare = NULL;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_peers[i].used && g_peers[i].id == id) return &g_peers[i];
        if (!g_peers[i].used && !spare) spare = &g_peers[i];
    }
    if (spare) { memset(spare, 0, sizeof *spare); spare->used = 1; spare->id = id; }
    return spare;
}

int net_peer_count(void) { int n = 0; for (int i = 0; i < MAX_PEERS; i++) n += g_peers[i].used; return n; }

static void handle_line(Conn *c, char *line) {
    if (!strncmp(line, "obj ", 4)) {
        Obj o;
        if (mode == 1 && room_locked) return;                  /* guests can't edit just now */
        if (scene_parse_obj(line, &o)) { apply_obj(&o); if (mode == 1) { strcat(line, "\n"); send_all(line, c); } }
    } else if (!strncmp(line, "del ", 4)) {
        if (mode == 1 && room_locked) return;
        apply_del((uint32_t)strtoul(line + 4, NULL, 10));
        if (mode == 1) { strcat(line, "\n"); send_all(line, c); }
    } else if (!strncmp(line, "pose ", 5)) {
        unsigned id; char name[20]; float f[21]; int n = 0;
        const char *p = line + 5;
        if (sscanf(p, "%u %19s %n", &id, name, &n) < 2) return;
        p += n;
        for (int i = 0; i < 21; i++) { char *end; f[i] = strtof(p, &end); if (end == p || !isfinite(f[i])) return; p = end; }
        if (id == my_pid) return;
        Peer *pr = peer_get(id);
        if (!pr) return;
        snprintf(pr->name, sizeof pr->name, "%s", name);
        Pose *ps[3] = {&pr->head, &pr->hand[0], &pr->hand[1]};
        for (int i = 0; i < 3; i++) {
            ps[i]->p = v3(f[i * 7], f[i * 7 + 1], f[i * 7 + 2]);
            ps[i]->q = q4norm(q4(f[i * 7 + 3], f[i * 7 + 4], f[i * 7 + 5], f[i * 7 + 6]));
        }
        pr->seen = now;
        if (mode == 1) { strcat(line, "\n"); send_all(line, c); }
    } else if (!strncmp(line, "aim ", 4)) {                 /* where a player's two lasers start and end */
        unsigned id; int n = 0;
        const char *p = line + 4;
        if (sscanf(p, "%u %n", &id, &n) < 1 || id == my_pid) return;
        p += n;
        float f[14];
        for (int i = 0; i < 14; i++) { char *end; f[i] = strtof(p, &end); if (end == p || !isfinite(f[i])) return; p = end; }
        Peer *pr = peer_get(id);
        if (!pr) return;
        for (int h = 0; h < 2; h++) {
            pr->aim_on[h] = f[h * 7] > 0.5f;
            pr->aim_from[h] = v3(f[h * 7 + 1], f[h * 7 + 2], f[h * 7 + 3]);
            pr->aim_to[h] = v3(f[h * 7 + 4], f[h * 7 + 5], f[h * 7 + 6]);
        }
        if (mode == 1) { strcat(line, "\n"); send_all(line, c); }
    } else if (mode != 1 && !strncmp(line, "owner ", 6)) {
        owner_pid = (uint32_t)strtoul(line + 6, NULL, 10);
    } else if (mode != 1 && !strncmp(line, "lock ", 5)) {
        int was = room_locked;
        room_locked = atoi(line + 5) != 0;
        if (room_locked != was && owner_pid != my_pid)
            snprintf(g_net_status, sizeof g_net_status, room_locked ? "The owner locked editing" : "Editing is unlocked again");
    } else if (mode == 1 && !strncmp(line, "hello ", 6)) {
        c->pid = next_pid++;
        snprintf(c->name, sizeof c->name, "%.19s", line + 6);
        char buf[256];
        snprintf(buf, sizeof buf, "you %u\nfull\n", c->pid); conn_send(c, buf);
        for (int i = 0; i < g_obj_count; i++) { scene_write_obj(&g_obj[i], buf, sizeof buf); conn_send(c, buf); }
        conn_send(c, "endfull\n");
        conn_send(c, room_locked ? "owner 1\nlock 1\n" : "owner 1\nlock 0\n");
        snprintf(g_net_status, sizeof g_net_status, "%s joined your room", c->name);
        plat_sound(SND_OPEN);
    } else if (mode != 1 && !strncmp(line, "you ", 4)) {
        my_pid = (uint32_t)strtoul(line + 4, NULL, 10);
    } else if (mode != 1 && !strcmp(line, "full")) {
        g_obj_count = 0; synced_count = 0; in_full = 1;
    } else if (mode != 1 && !strcmp(line, "endfull")) {
        in_full = 0;
        scene_commit("Joined a room");
        snprintf(g_net_status, sizeof g_net_status, via_server ? "In room %.40s" : "In %.40s's room", room_name);
        plat_sound(SND_OPEN);
    } else if (mode != 1 && !strncmp(line, "sorry ", 6)) {        /* the server says no, and why */
        snprintf(g_net_status, sizeof g_net_status, "%.90s", line + 6);
        conn_close(c);
    }
}

static void feed(Conn *c, const char *data, int n) {       /* bytes of lines, in whatever pieces they arrive */
    for (int i = 0; i < n && c->fd >= 0; i++) {
        char ch = data[i];
        if (ch == '\n') {
            c->rx[c->rxn] = 0;
            if (c->rxn && c->rx[c->rxn - 1] == '\r') c->rx[c->rxn - 1] = 0;
            c->rxn = 0;
            handle_line(c, c->rx);
        } else if (c->rxn < RX_MAX - 3) {
            c->rx[c->rxn++] = ch;
        }
    }
}

static void ws_feed(Conn *c, const unsigned char *data, int n) {
    if (!c->wsbuf) c->wsbuf = malloc(WS_MAX);
    if (c->wsn + n > WS_MAX) { conn_close(c); return; }
    memcpy(c->wsbuf + c->wsn, data, (size_t)n); c->wsn += n;
    while (c->fd >= 0 && c->wsn >= 2) {
        unsigned char *b = c->wsbuf;
        int op = b[0] & 15, masked = b[1] & 0x80;
        size_t len = b[1] & 127, head = 2;
        if (len == 126) { if (c->wsn < 4) return; len = ((size_t)b[2] << 8) | b[3]; head = 4; }
        else if (len == 127) { conn_close(c); return; }          /* nothing we're sent is that big */
        if (masked) head += 4;
        if ((size_t)c->wsn < head + len) return;
        unsigned char *payload = b + head;
        if (masked) for (size_t i = 0; i < len; i++) payload[i] ^= b[head - 4 + (i & 3)];
        if (op == 8) { conn_close(c); return; }
        if (op == 9) ws_frame(c, 10, (const char *)payload, len);            /* ping: answer pong */
        else if (op <= 2) feed(c, (const char *)payload, (int)len);
        if (c->fd < 0) return;
        size_t used = head + len;
        memmove(c->wsbuf, c->wsbuf + used, (size_t)c->wsn - used); c->wsn -= (int)used;
    }
}

static void conn_read(Conn *c) {
    while (c->fd >= 0) {
        unsigned char buf[4096];
        int k = io_recv(c, buf, sizeof buf);
        if (k > 0) {
            if (c->ws) ws_feed(c, buf, k); else feed(c, (const char *)buf, k);
        } else if (k == 0) {
            break;
        } else {
            if (mode == 1) snprintf(g_net_status, sizeof g_net_status, "%s left", c->name[0] ? c->name : "A player");
            conn_close(c);
        }
    }
}

/* ---------- joining a server: done on the side, so a slow network never freezes your view ---------- */

typedef struct {
    char host[128], path[128], room[24], err[96];
    int port, secure, make_public, list_only;
    PublicRoom found[MAX_PUBLIC]; int found_n;
    volatile int done, ok, cancelled;
    int fd; Tls *tls;
    unsigned char extra[2048]; int extra_n;       /* anything the server said straight after its welcome */
} Job;
static Job *job, *list_job;

static int job_write(Job *j, const char *data, size_t n) {
    while (n > 0) {
        long k = j->tls ? mbedtls_ssl_write(&j->tls->ssl, (const unsigned char *)data, n) : send(j->fd, data, n, MSG_NOSIGNAL);
        if (k <= 0) return 0;
        data += k; n -= (size_t)k;
    }
    return 1;
}
static int job_read(Job *j, unsigned char *buf, size_t n) {
    if (j->tls) {
        int r;
        do r = mbedtls_ssl_read(&j->tls->ssl, buf, n); while (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
        return r > 0 ? r : -1;
    }
    ssize_t k = recv(j->fd, buf, n, 0);
    return k > 0 ? (int)k : -1;
}

static void *job_run(void *arg) {
    Job *j = arg;
    char port[12]; snprintf(port, sizeof port, "%d", j->port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    j->fd = -1;
    if (getaddrinfo(j->host, port, &hints, &res) != 0 || !res) {
        snprintf(j->err, sizeof j->err, "Can't find %.40s. Check the address.", j->host);
    } else {
        struct timeval tv = {8, 0};
        for (struct addrinfo *a = res; a && j->fd < 0; a = a->ai_next) {
            int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (fd < 0) continue;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
            if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) j->fd = fd; else close(fd);
        }
        freeaddrinfo(res);
        if (j->fd < 0) snprintf(j->err, sizeof j->err, "No answer from %.60s", j->host);
    }
    if (j->fd >= 0 && j->secure) {
        j->tls = tls_open(j->fd, j->host, j->err, sizeof j->err);
        if (!j->tls) { close(j->fd); j->fd = -1; }
    }
    if (j->fd >= 0) {                              /* ask to switch from web page talk to a WebSocket */
        char req[512];
        int n = snprintf(req, sizeof req,
                         "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                         "Sec-WebSocket-Key: RmxhdXhWUiByb29tIGtleQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", j->path, j->host);
        unsigned char buf[2048]; int got = 0, end = -1;
        if (job_write(j, req, (size_t)n))
            while (got < (int)sizeof buf - 1 && end < 0) {
                int k = job_read(j, buf + got, sizeof buf - 1 - (size_t)got);
                if (k <= 0) break;
                got += k; buf[got] = 0;
                char *e = strstr((char *)buf, "\r\n\r\n");
                if (e) end = (int)(e - (char *)buf) + 4;
            }
        if (end > 0 && !strncmp((char *)buf, "HTTP/1.1 101", 12)) {
            j->extra_n = got - end; memcpy(j->extra, buf + end, (size_t)j->extra_n);
            int one = 1; setsockopt(j->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            struct timeval none = {0, 0};
            setsockopt(j->fd, SOL_SOCKET, SO_RCVTIMEO, &none, sizeof none); setsockopt(j->fd, SOL_SOCKET, SO_SNDTIMEO, &none, sizeof none);
            nonblock(j->fd);
            j->ok = 1;
            if (j->list_only) {                    /* ask what public rooms there are, read the answer, hang up */
                static const unsigned char ask[11] = {0x81, 0x85, 0, 0, 0, 0, 'l', 'i', 's', 't', '\n'};
                char text[4096]; int tn = 0, finished = 0;
                unsigned char raw[8192]; int rn = j->extra_n;
                memcpy(raw, j->extra, (size_t)rn);
                j->ok = job_write(j, (const char *)ask, sizeof ask);
                struct timeval wait = {8, 0};
                fcntl(j->fd, F_SETFL, fcntl(j->fd, F_GETFL, 0) & ~O_NONBLOCK);
                setsockopt(j->fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
                while (j->ok && !finished) {
                    while (rn >= 2) {              /* whole frames so far (the server never masks) */
                        size_t len = raw[1] & 127, head = 2;
                        if (len == 126) { if (rn < 4) break; len = ((size_t)raw[2] << 8) | raw[3]; head = 4; }
                        else if (len == 127) { j->ok = 0; break; }
                        if ((size_t)rn < head + len) break;
                        if ((raw[0] & 15) <= 2 && tn + (int)len < (int)sizeof text - 1) { memcpy(text + tn, raw + head, len); tn += (int)len; }
                        memmove(raw, raw + head + len, (size_t)rn - head - len); rn -= (int)(head + len);
                    }
                    text[tn] = 0;
                    if (strstr(text, "endrooms")) { finished = 1; break; }
                    int k = job_read(j, raw + rn, sizeof raw - (size_t)rn);
                    if (k <= 0) j->ok = 0; else rn += k;
                }
                if (finished)
                    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
                        PublicRoom r;
                        if (j->found_n < MAX_PUBLIC && sscanf(line, "pub %23s %d %d", r.code, &r.players, &r.shapes) == 3) j->found[j->found_n++] = r;
                    }
                else snprintf(j->err, sizeof j->err, "%.60s didn't send its list of rooms", j->host);
                if (j->tls) tls_free(j->tls);
                close(j->fd); j->fd = -1; j->tls = NULL;
            }
        } else {
            snprintf(j->err, sizeof j->err, end > 0 ? "%.60s isn't a Flaux VR server" : "%.60s stopped answering", j->host);
            if (j->tls) tls_free(j->tls);
            close(j->fd); j->fd = -1; j->tls = NULL;
        }
    }
    j->done = 1;
    if (j->cancelled) {                            /* nobody is waiting any more: tidy up here */
        if (j->tls) tls_free(j->tls);
        if (j->fd >= 0) close(j->fd);
        free(j);
    }
    return NULL;
}

/* ---------- rooms ---------- */

void net_leave(void) {
    if (job) { if (job->done) { if (job->tls) tls_free(job->tls); if (job->fd >= 0) close(job->fd); free(job); } else job->cancelled = 1; job = NULL; }
    for (int i = 0; i < MAX_PEERS; i++) conn_close(&conns[i]);
    if (listen_fd >= 0) close(listen_fd);
    if (beacon_fd >= 0) close(beacon_fd);
    listen_fd = beacon_fd = -1;
    memset(g_peers, 0, sizeof g_peers);
    mode = 0; my_pid = 1; in_full = 0; via_server = 0; room_locked = 0; owner_pid = 1;
    snprintf(g_net_status, sizeof g_net_status, "Not in a room");
}

int net_host(const char *name) {
    net_leave();
    snprintf(room_name, sizeof room_name, "%s", name);
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { snprintf(g_net_status, sizeof g_net_status, "Couldn't open a room (no network?)"); return 0; }
    int one = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_fd, (struct sockaddr *)&a, sizeof a) != 0 || listen(listen_fd, 8) != 0) {
        snprintf(g_net_status, sizeof g_net_status, "Couldn't open a room: %s", strerror(errno));
        close(listen_fd); listen_fd = -1;
        return 0;
    }
    nonblock(listen_fd);
    beacon_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (beacon_fd >= 0) { setsockopt(beacon_fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one); nonblock(beacon_fd); }
    mode = 1; my_pid = 1; next_pid = 2; owner_pid = 1; room_locked = 0;
    mark_synced();
    snprintf(g_net_status, sizeof g_net_status, "Hosting at %s", net_local_ip());
    return 1;
}

int net_join(const char *ip) {           /* a headset that's hosting on this Wi-Fi */
    net_leave();
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(PORT);
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) {
        snprintf(g_net_status, sizeof g_net_status, "\"%.40s\" isn't an address like 192.168.1.20", ip);
        return 0;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { snprintf(g_net_status, sizeof g_net_status, "Couldn't join (no network?)"); return 0; }
    nonblock(fd);
    int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0 && errno != EINPROGRESS) {
        snprintf(g_net_status, sizeof g_net_status, "Couldn't reach %.40s", ip);
        close(fd);
        return 0;
    }
    conns[0].fd = fd;
    snprintf(room_name, sizeof room_name, "%s", ip);
    for (int i = 0; i < MAX_ROOMS; i++)
        if (!strcmp(g_rooms[i].ip, ip) && g_rooms[i].name[0]) snprintf(room_name, sizeof room_name, "%s", g_rooms[i].name);
    char hello[64]; snprintf(hello, sizeof hello, "hello %s\n", my_name);
    conn_send(&conns[0], hello);
    mode = 3; join_started = now; in_full = 1;       /* don't send our scene: we're about to get theirs */
    snprintf(g_net_status, sizeof g_net_status, "Joining %.40s...", ip);
    return 1;
}

/* Works out where a server is from what was typed.  The address can be ws://host:port,
 * wss://host, host:port (taken as ws://) or just a host name (taken as wss://). */
static Job *job_for(const char *address) {
    char addr[160]; snprintf(addr, sizeof addr, "%s", address);
    char *s = addr;
    while (*s == ' ') s++;
    for (char *e = s + strlen(s); e > s && (e[-1] == ' ' || e[-1] == '/'); ) *--e = 0;
    Job *j = calloc(1, sizeof *j);
    j->secure = 1; j->port = 443;
    if (!strncmp(s, "wss://", 6)) s += 6;
    else if (!strncmp(s, "https://", 8)) s += 8;
    else if (!strncmp(s, "ws://", 5)) { s += 5; j->secure = 0; j->port = 80; }
    else if (!strncmp(s, "http://", 7)) { s += 7; j->secure = 0; j->port = 80; }
    else if (strchr(s, ':')) { j->secure = 0; j->port = 80; }
    char *slash = strchr(s, '/');
    snprintf(j->path, sizeof j->path, "%.120s", slash ? slash : "/");
    if (slash) *slash = 0;
    char *colon = strrchr(s, ':');
    if (colon) { *colon = 0; j->port = atoi(colon + 1); }
    snprintf(j->host, sizeof j->host, "%.120s", s);
    if (!j->host[0] || j->port <= 0 || j->port > 65535) { free(j); return NULL; }
    return j;
}

static int job_start(Job *j) {
    pthread_t t;
    if (pthread_create(&t, NULL, job_run, j) != 0) { free(j); return 0; }
    pthread_detach(t);
    return 1;
}

/* Join a room on a server (making it if it isn't there; make_public puts a new room on the
 * server's list).  A bare number address like 192.168.1.20 is a headset hosting on this Wi-Fi. */
int net_join_server(const char *address, const char *room, int make_public) {
    const char *a = address;
    while (*a == ' ') a++;
    struct in_addr probe;
    if (inet_pton(AF_INET, a, &probe) == 1) return net_join(a);
    net_leave();
    Job *j = job_for(address);
    if (!j) { snprintf(g_net_status, sizeof g_net_status, "Type the server's address, like my-server.onrender.com"); return 0; }
    int k = 0;
    for (const char *r = room; *r && k < 20; r++)
        if ((*r >= 'a' && *r <= 'z') || (*r >= 'A' && *r <= 'Z') || (*r >= '0' && *r <= '9') || *r == '-' || *r == '_')
            j->room[k++] = (char)((*r >= 'a' && *r <= 'z') ? *r - 32 : *r);
    if (!k) { snprintf(g_net_status, sizeof g_net_status, "Type a room code: letters and numbers, like FLAUX"); free(j); return 0; }
    j->make_public = make_public;
    char host[128]; snprintf(host, sizeof host, "%s", j->host);
    snprintf(room_name, sizeof room_name, "%s", j->room);
    if (!job_start(j)) { snprintf(g_net_status, sizeof g_net_status, "Couldn't start connecting"); return 0; }
    job = j;
    mode = 3; via_server = 1; join_started = now; in_full = 1;
    snprintf(g_net_status, sizeof g_net_status, "Connecting to %.60s...", host);
    return 1;
}

/* asks a server which public rooms it has; g_public is filled in a moment later */
void net_list_public(const char *address) {
    if (list_job) return;                          /* already asking */
    const char *a = address;
    while (*a == ' ') a++;
    struct in_addr probe;
    Job *j = inet_pton(AF_INET, a, &probe) == 1 ? NULL : job_for(address);      /* a bare number is a headset, not a server */
    if (!j) { g_public_count = 0; g_public_state = 0; return; }
    j->list_only = 1;
    if (!job_start(j)) { g_public_state = 3; return; }
    list_job = j; g_public_state = 1;
}

void net_listen_rooms(int on) {
    if (on && radio_fd < 0) {
        radio_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (radio_fd < 0) return;
        int one = 1; setsockopt(radio_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        struct sockaddr_in a; memset(&a, 0, sizeof a);
        a.sin_family = AF_INET; a.sin_port = htons(BEACON_PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(radio_fd, (struct sockaddr *)&a, sizeof a) != 0) { close(radio_fd); radio_fd = -1; return; }
        nonblock(radio_fd);
    } else if (!on && radio_fd >= 0) {
        close(radio_fd); radio_fd = -1;
    }
}

static void hear_rooms(void) {
    for (int i = 0; i < MAX_ROOMS; i++) if (g_rooms[i].ip[0] && now - g_rooms[i].seen > 4.0) g_rooms[i].ip[0] = 0;
    if (radio_fd < 0) return;
    for (;;) {
        char buf[128]; struct sockaddr_in from; socklen_t fl = sizeof from;
        ssize_t k = recvfrom(radio_fd, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
        if (k <= 0) break;
        buf[k] = 0;
        int players = 0, n = 0;
        if (sscanf(buf, "FLAUXVR2 %d %n", &players, &n) < 1 || mode == 1) continue;
        char ip[20]; inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        Room *r = NULL;
        for (int i = 0; i < MAX_ROOMS; i++) if (!strcmp(g_rooms[i].ip, ip)) r = &g_rooms[i];
        for (int i = 0; i < MAX_ROOMS && !r; i++) if (!g_rooms[i].ip[0]) r = &g_rooms[i];
        if (!r) continue;
        snprintf(r->ip, sizeof r->ip, "%s", ip);
        snprintf(r->name, sizeof r->name, "%s", buf + n);
        r->name[strcspn(r->name, "\r\n")] = 0;
        r->players = players; r->seen = now;
    }
}

void net_tick(double time, Pose head, Pose lh, Pose rh) {
    now = time;
    hear_rooms();
    if (list_job && list_job->done) {              /* the list of public rooms came back (or didn't) */
        g_public_count = list_job->ok ? list_job->found_n : 0;
        memcpy(g_public, list_job->found, sizeof g_public);
        g_public_state = list_job->ok ? 2 : 3;
        free(list_job); list_job = NULL;
    }
    if (mode == 0) return;

    if (job) {                                   /* still finding and greeting the server */
        if (!job->done) {
            if (now - join_started > 25.0) { net_leave(); snprintf(g_net_status, sizeof g_net_status, "The server took too long to answer"); }
            return;
        }
        Job *j = job; job = NULL;
        if (!j->ok) {
            char why[96]; snprintf(why, sizeof why, "%s", j->err);
            free(j); net_leave();
            snprintf(g_net_status, sizeof g_net_status, "%s", why);
            plat_sound(SND_ERROR);
            return;
        }
        Conn *c = &conns[0];
        c->fd = j->fd; c->tls = j->tls; c->ws = 1;
        if (j->extra_n) ws_feed(c, j->extra, j->extra_n);
        char hello[112]; snprintf(hello, sizeof hello, "room %s%s\nhello %s\n", j->room, j->make_public ? " public" : "", my_name);
        free(j);
        conn_send(c, hello);
        join_started = now;
        snprintf(g_net_status, sizeof g_net_status, "Joining room %.40s...", room_name);
    }

    if (mode == 1) {
        for (;;) {
            int fd = accept(listen_fd, NULL, NULL);
            if (fd < 0) break;
            Conn *c = NULL;
            for (int i = 0; i < MAX_PEERS - 1 && !c; i++) if (conns[i].fd < 0) c = &conns[i];
            if (!c) { close(fd); continue; }
            nonblock(fd);
            int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            memset(c, 0, sizeof *c); c->fd = fd;
        }
        if (beacon_fd >= 0 && now - last_beacon > 1.0) {
            last_beacon = now;
            char msg[96]; snprintf(msg, sizeof msg, "FLAUXVR2 %d %s", net_peer_count() + 1, room_name);
            struct sockaddr_in a; memset(&a, 0, sizeof a);
            a.sin_family = AF_INET; a.sin_port = htons(BEACON_PORT); a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            sendto(beacon_fd, msg, strlen(msg), MSG_NOSIGNAL, (struct sockaddr *)&a, sizeof a);
        }
    }

    for (int i = 0; i < MAX_PEERS; i++) conn_read(&conns[i]);

    if (mode >= 2 && conns[0].fd < 0) {            /* the host went away, or never answered */
        int was = mode;
        char said[96]; snprintf(said, sizeof said, "%s", g_net_status);
        int refused = strncmp(said, "Joining", 7) != 0 && strncmp(said, "In ", 3) != 0 && strncmp(said, "Connecting", 10) != 0;
        net_leave();
        if (refused) snprintf(g_net_status, sizeof g_net_status, "%s", said);       /* the server gave its reason */
        else snprintf(g_net_status, sizeof g_net_status, was == 3 ? "Couldn't join that room" : "The room closed");
        return;
    }
    if (mode == 3) {
        if (my_pid != 1) mode = 2;
        else if (now - join_started > 8.0) { net_leave(); snprintf(g_net_status, sizeof g_net_status, "No answer from that room"); return; }
    }

    if ((mode == 1 || mode == 2) && !in_full) {
        if (now - last_sync > 0.1) { last_sync = now; sync_scene(); }
        if (now - last_pose > 0.066) {
            last_pose = now;
            char line[512];
            int n = snprintf(line, sizeof line, "pose %u %s", my_pid, my_name);
            const Pose *ps[3] = {&head, &lh, &rh};
            for (int i = 0; i < 3; i++)
                n += snprintf(line + n, sizeof line - (size_t)n, " %.3f %.3f %.3f %.4f %.4f %.4f %.4f", (double)ps[i]->p.x,
                              (double)ps[i]->p.y, (double)ps[i]->p.z, (double)ps[i]->q.x, (double)ps[i]->q.y, (double)ps[i]->q.z,
                              (double)ps[i]->q.w);
            snprintf(line + n, sizeof line - (size_t)n, "\n");
            send_all(line, NULL);
            n = snprintf(line, sizeof line, "aim %u", my_pid);
            for (int h = 0; h < 2; h++)
                n += snprintf(line + n, sizeof line - (size_t)n, " %d %.3f %.3f %.3f %.3f %.3f %.3f", my_aim[h].on, (double)my_aim[h].from.x,
                              (double)my_aim[h].from.y, (double)my_aim[h].from.z, (double)my_aim[h].to.x, (double)my_aim[h].to.y, (double)my_aim[h].to.z);
            snprintf(line + n, sizeof line - (size_t)n, "\n");
            send_all(line, NULL);
        }
        if (via_server && now - last_ping > 20.0) { last_ping = now; ws_frame(&conns[0], 9, "", 0); }    /* keep the line open */
    }
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_peers[i].used && now - g_peers[i].seen > 3.0) g_peers[i].used = 0;
        conn_flush(&conns[i]);
    }
}

/* the room's owner stops (or lets) everyone else edit; a locked guest's lasers vanish for the others */
void net_set_lock(int on) {
    if (!net_is_owner()) return;
    room_locked = on != 0;
    send_all(room_locked ? "lock 1\n" : "lock 0\n", NULL);
}
