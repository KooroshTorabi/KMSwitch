#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lwip/tcp.h"

#define NCONN 4
#define HDR_MAX 1024
#define OUT_MAX 8192

typedef struct {
    struct tcp_pcb *pcb;
    int state;                       // 0 free, 1 headers, 2 body, 3 waiting for handler, 4 sending, 5 streaming
    char hdr[HDR_MAX];
    size_t hlen;
    http_req_t req;
    uint8_t body[HTTP_BODY_MAX];
    size_t want, got;
    // response
    char head[256];
    size_t head_len, head_sent;
    const uint8_t *out;
    uint8_t out_buf[OUT_MAX];
    size_t out_len, out_sent;
    uint32_t serial;
} conn_t;

static conn_t conns[NCONN];
static http_handler_t handler;
static http_stream_t streamer;
static uint32_t serial;

static int cid(conn_t *c) { return (int)((c - conns) | (c->serial << 4)); }
static conn_t *by_id(int id) {
    conn_t *c = &conns[id & 15];
    if ((id & 15) >= NCONN || c->serial != (uint32_t)id >> 4 || !c->pcb) return NULL;
    return c;
}

static void conn_close(conn_t *c) {
    if (c->pcb) {
        tcp_arg(c->pcb, NULL);
        tcp_recv(c->pcb, NULL);
        tcp_sent(c->pcb, NULL);
        tcp_err(c->pcb, NULL);
        if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
    }
    c->pcb = NULL;
    c->state = 0;
}

static void pump(conn_t *c) {
    if (!c->pcb || c->state != 4) return;
    while (c->head_sent < c->head_len) {
        u16_t room = tcp_sndbuf(c->pcb);
        size_t n = c->head_len - c->head_sent;
        if (!room) return;
        if (n > room) n = room;
        if (tcp_write(c->pcb, c->head + c->head_sent, (u16_t)n, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE) != ERR_OK) return;
        c->head_sent += n;
    }
    while (c->out_sent < c->out_len) {
        u16_t room = tcp_sndbuf(c->pcb);
        size_t n = c->out_len - c->out_sent;
        if (!room) break;
        if (n > room) n = room;
        if (n > 1400) n = 1400;
        if (tcp_write(c->pcb, c->out + c->out_sent, (u16_t)n, c->out == c->out_buf ? TCP_WRITE_FLAG_COPY : 0) != ERR_OK) break;
        c->out_sent += n;
    }
    tcp_output(c->pcb);
    // everything queued: close; lwIP still sends the queued data before its FIN
    if (c->head_sent == c->head_len && c->out_sent == c->out_len) conn_close(c);
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)pcb; (void)len;
    conn_t *c = arg;
    if (!c) return ERR_OK;
    pump(c);
    return ERR_OK;
}

static const char *reason(int s) {
    switch (s) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

static void start_response(conn_t *c, const http_resp_t *r) {
    size_t len = r->len;
    if (r->body_static) {
        c->out = r->body;
    } else {
        if (len > OUT_MAX) len = OUT_MAX;
        if (len) memcpy(c->out_buf, r->body, len);
        c->out = c->out_buf;
    }
    c->out_len = len;
    c->out_sent = 0;
    c->head_len = (size_t)snprintf(c->head, sizeof c->head,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n%sCache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
        r->status, reason(r->status), r->ctype ? r->ctype : "text/plain", (unsigned)len,
        r->gzip ? "Content-Encoding: gzip\r\n" : "");
    c->head_sent = 0;
    c->state = 4;
    pump(c);
}

void http_respond(int id, const http_resp_t *resp) {
    conn_t *c = by_id(id);
    if (c && (c->state == 3 || c->state == 5)) start_response(c, resp);
}

void http_json(http_resp_t *r, int status, const char *json) {
    r->status = status;
    r->ctype = "application/json";
    r->body = (const uint8_t *)json;
    r->len = strlen(json);
    r->gzip = false;
    r->body_static = false;
}

static void dispatch(conn_t *c) {
    c->req.id = cid(c);
    c->req.body = c->body;
    c->req.len = c->got;
    c->state = 3;
    http_resp_t r = {0};
    if (handler(&c->req, &r)) start_response(c, &r);
}

// headers complete: parse the request line and Content-Length
static bool parse_head(conn_t *c) {
    c->hdr[c->hlen] = 0;
    char *sp = strchr(c->hdr, ' ');
    if (!sp) return false;
    size_t ml = (size_t)(sp - c->hdr);
    if (ml >= sizeof c->req.method) return false;
    memcpy(c->req.method, c->hdr, ml);
    c->req.method[ml] = 0;
    char *p = sp + 1, *e = strchr(p, ' ');
    if (!e) return false;
    size_t pl = (size_t)(e - p);
    if (pl >= sizeof c->req.path) pl = sizeof c->req.path - 1;
    memcpy(c->req.path, p, pl);
    c->req.path[pl] = 0;
    c->want = 0;
    for (char *h = strstr(c->hdr, "\r\n"); h; h = strstr(h + 2, "\r\n")) {
        if (!strncasecmp(h + 2, "Content-Length:", 15)) c->want = (size_t)strtoul(h + 17, NULL, 10);
    }
    return true;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    conn_t *c = arg;
    if (!c) { if (p) pbuf_free(p); return ERR_OK; }
    if (!p || err != ERR_OK) {   // the browser closed
        if (p) pbuf_free(p);
        if (c->state == 4 && c->out_sent < c->out_len) return ERR_OK;
        conn_close(c);
        return ERR_OK;
    }
    tcp_recved(pcb, p->tot_len);
    for (struct pbuf *q = p; q; q = q->next) {
        const uint8_t *d = q->payload;
        size_t n = q->len, i = 0;
        while (i < n) {
            if (c->state == 1) {
                if (c->hlen >= HDR_MAX - 1) { pbuf_free(p); conn_close(c); return ERR_OK; }
                c->hdr[c->hlen++] = (char)d[i++];
                if (c->hlen >= 4 && !memcmp(c->hdr + c->hlen - 4, "\r\n\r\n", 4)) {
                    if (!parse_head(c)) { pbuf_free(p); conn_close(c); return ERR_OK; }
                    c->got = 0;
                    if (c->want > HTTP_BODY_MAX) {
                        if (streamer) {
                            c->state = 5;
                            c->req.id = cid(c);
                        } else {
                            http_resp_t r;
                            http_json(&r, 413, "{\"error\":\"too large\"}");
                            start_response(c, &r);
                            pbuf_free(p);
                            return ERR_OK;
                        }
                    } else if (c->want == 0) {
                        dispatch(c);
                    } else {
                        c->state = 2;
                    }
                }
            } else if (c->state == 2) {
                size_t k = n - i;
                if (k > c->want - c->got) k = c->want - c->got;
                memcpy(c->body + c->got, d + i, k);
                c->got += k;
                i += k;
                if (c->got == c->want) dispatch(c);
            } else if (c->state == 5) {
                size_t k = n - i;
                if (k > c->want - c->got) k = c->want - c->got;
                http_resp_t r = {0};
                bool last = c->got + k == c->want;
                bool ok = streamer(c->req.id, c->req.path, c->want, c->got, d + i, k, last, &r);
                c->got += k;
                i += k;
                if (!ok || last) {
                    if (r.status) start_response(c, &r);
                    else c->state = 3;   // answer later
                    if (!ok) { pbuf_free(p); return ERR_OK; }
                }
            } else {
                i = n;   // ignore anything after the request
            }
        }
    }
    pbuf_free(p);
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    (void)err;
    conn_t *c = arg;
    if (c) { c->pcb = NULL; c->state = 0; }
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb) {
    conn_t *c = arg;
    (void)pcb;
    if (c && c->state == 4) on_sent(c, c->pcb, 0);
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !pcb) return ERR_VAL;
    conn_t *c = NULL;
    for (int i = 0; i < NCONN; i++) if (!conns[i].pcb) { c = &conns[i]; break; }
    if (!c) { tcp_abort(pcb); return ERR_ABRT; }
    memset(&c->req, 0, sizeof c->req);
    c->pcb = pcb;
    c->state = 1;
    c->hlen = 0;
    c->serial = ++serial & 0x7FFFFF;
    tcp_arg(pcb, c);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_err);
    tcp_poll(pcb, on_poll, 2);
    tcp_setprio(pcb, TCP_PRIO_MIN);
    return ERR_OK;
}

void http_init(http_handler_t h, http_stream_t s) {
    handler = h;
    streamer = s;
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    tcp_bind(pcb, IP_ANY_TYPE, 80);
    pcb = tcp_listen_with_backlog(pcb, NCONN);
    tcp_accept(pcb, on_accept);
}
