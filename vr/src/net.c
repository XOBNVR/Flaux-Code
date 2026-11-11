/* Flaux VR: rooms.  One headset hosts, others on the same Wi-Fi join, and everyone builds in the
 * same scene.  The host shouts "I'm here" on the network once a second, so rooms show up by
 * themselves; you can also type the host's address.
 *
 * It's all plain text lines over TCP (port 24727):
 *     hello <name>            joiner -> host
 *     you <id>                host -> joiner: your player number
 *     full ... endfull        host -> joiner: the whole scene, as obj lines
 *     obj <...>               an object was added or changed
 *     del <id>                an object was removed
 *     pose <id> <name> <21 numbers>   where a player's head and hands are
 * The host passes every line from one joiner on to the others. */
#include "fx.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 24727
#define BEACON_PORT 24728
#define RX_MAX 4096

Peer g_peers[MAX_PEERS];
Room g_rooms[MAX_ROOMS];
char g_net_status[96] = "Not in a room";

typedef struct { int fd; char rx[RX_MAX]; int rxn; char *tx; size_t txn, txcap; uint32_t pid; char name[20]; } Conn;

static int mode;                       /* 0 alone, 1 hosting, 2 joined, 3 joining */
static int listen_fd = -1, beacon_fd = -1, radio_fd = -1;
static Conn conns[MAX_PEERS];          /* hosting: the joiners.  joined: conns[0] is the host */
static uint32_t my_pid = 1, next_pid = 2;
static char my_name[20] = "Player", room_name[24] = "Room", local_ip[20] = "";
static double now, last_sync, last_pose, last_beacon, join_started;
static Obj *synced; static int synced_count;
static int in_full;

int net_mode(void) { return mode; }

static void nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

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
}

/* ---------- sending ---------- */

static void conn_close(Conn *c) {
    if (c->fd >= 0) close(c->fd);
    free(c->tx);
    memset(c, 0, sizeof *c);
    c->fd = -1;
}

static void conn_send(Conn *c, const char *s) {
    if (c->fd < 0) return;
    size_t n = strlen(s);
    if (c->txn + n > (4u << 20)) { conn_close(c); return; }     /* hopelessly behind */
    if (c->txn + n > c->txcap) { c->txcap = (c->txn + n) * 2; c->tx = realloc(c->tx, c->txcap); }
    memcpy(c->tx + c->txn, s, n); c->txn += n;
}

static void conn_flush(Conn *c) {
    while (c->fd >= 0 && c->txn > 0) {
        ssize_t k = send(c->fd, c->tx, c->txn, MSG_NOSIGNAL);
        if (k > 0) { memmove(c->tx, c->tx + k, c->txn - (size_t)k); c->txn -= (size_t)k; }
        else if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || (errno == ENOTCONN && mode == 3))) break;
        else { conn_close(c); break; }
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
        if (scene_parse_obj(line, &o)) { apply_obj(&o); if (mode == 1) { strcat(line, "\n"); send_all(line, c); } }
    } else if (!strncmp(line, "del ", 4)) {
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
    } else if (mode == 1 && !strncmp(line, "hello ", 6)) {
        c->pid = next_pid++;
        snprintf(c->name, sizeof c->name, "%.19s", line + 6);
        char buf[256];
        snprintf(buf, sizeof buf, "you %u\nfull\n", c->pid); conn_send(c, buf);
        for (int i = 0; i < g_obj_count; i++) { scene_write_obj(&g_obj[i], buf, sizeof buf); conn_send(c, buf); }
        conn_send(c, "endfull\n");
        snprintf(g_net_status, sizeof g_net_status, "%s joined your room", c->name);
        plat_sound(SND_OPEN);
    } else if (mode != 1 && !strncmp(line, "you ", 4)) {
        my_pid = (uint32_t)strtoul(line + 4, NULL, 10);
    } else if (mode != 1 && !strcmp(line, "full")) {
        g_obj_count = 0; synced_count = 0; in_full = 1;
    } else if (mode != 1 && !strcmp(line, "endfull")) {
        in_full = 0;
        scene_commit("Joined a room");
        snprintf(g_net_status, sizeof g_net_status, "In %s's room", room_name);
        plat_sound(SND_OPEN);
    }
}

static void conn_read(Conn *c) {
    while (c->fd >= 0) {
        ssize_t k = recv(c->fd, c->rx + c->rxn, (size_t)(RX_MAX - 2 - c->rxn), 0);
        if (k > 0) {
            c->rxn += (int)k;
            int start = 0;
            for (int i = 0; i < c->rxn; i++)
                if (c->rx[i] == '\n') {
                    char line[RX_MAX];
                    int len = i - start;
                    memcpy(line, c->rx + start, (size_t)len); line[len] = 0;
                    if (len && line[len - 1] == '\r') line[len - 1] = 0;
                    handle_line(c, line);
                    start = i + 1;
                    if (c->fd < 0) return;
                }
            memmove(c->rx, c->rx + start, (size_t)(c->rxn - start)); c->rxn -= start;
            if (c->rxn >= RX_MAX - 2) c->rxn = 0;      /* a line far too long: drop it */
        } else if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || (errno == ENOTCONN && mode == 3))) {
            break;                                  /* nothing yet (or still connecting) */
        } else {
            if (mode == 1) snprintf(g_net_status, sizeof g_net_status, "%s left", c->name[0] ? c->name : "A player");
            conn_close(c);
        }
    }
}

/* ---------- rooms ---------- */

void net_leave(void) {
    for (int i = 0; i < MAX_PEERS; i++) conn_close(&conns[i]);
    if (listen_fd >= 0) close(listen_fd);
    if (beacon_fd >= 0) close(beacon_fd);
    listen_fd = beacon_fd = -1;
    memset(g_peers, 0, sizeof g_peers);
    mode = 0; my_pid = 1; in_full = 0;
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
    mode = 1; my_pid = 1; next_pid = 2;
    mark_synced();
    snprintf(g_net_status, sizeof g_net_status, "Hosting at %s", net_local_ip());
    return 1;
}

int net_join(const char *ip) {
    net_leave();
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(PORT);
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) {
        snprintf(g_net_status, sizeof g_net_status, "\"%s\" isn't an address like 192.168.1.20", ip);
        return 0;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { snprintf(g_net_status, sizeof g_net_status, "Couldn't join (no network?)"); return 0; }
    nonblock(fd);
    int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0 && errno != EINPROGRESS) {
        snprintf(g_net_status, sizeof g_net_status, "Couldn't reach %s", ip);
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
    snprintf(g_net_status, sizeof g_net_status, "Joining %s...", ip);
    return 1;
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
    if (mode == 0) return;

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
        net_leave();
        snprintf(g_net_status, sizeof g_net_status, was == 3 ? "Couldn't join that room" : "The room closed");
        return;
    }
    if (mode == 3) {
        if (my_pid != 1) mode = 2;
        else if (now - join_started > 6.0) { net_leave(); snprintf(g_net_status, sizeof g_net_status, "No answer from that room"); return; }
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
        }
    }
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_peers[i].used && now - g_peers[i].seen > 3.0) g_peers[i].used = 0;
        conn_flush(&conns[i]);
    }
}
