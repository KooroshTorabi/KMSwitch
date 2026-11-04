// Minimal HTTP/1.1 server on lwIP (raw TCP API): one request per connection, Connection: close.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTTP_BODY_MAX 8192

typedef struct {
    int id;                 // connection id, for http_respond()
    char method[8];
    char path[96];
    const uint8_t *body;
    size_t len;
} http_req_t;

typedef struct {
    int status;             // 200, 202, 400, 404, ...
    const char *ctype;      // "application/json", "text/plain; charset=utf-8", "text/html; charset=utf-8"
    const uint8_t *body;
    size_t len;
    bool gzip;              // body is gzip-compressed (the settings page)
    bool body_static;       // body stays valid (flash): sent without copying
} http_resp_t;

// return true when *out holds the answer; false = the answer comes later through http_respond(id, ...)
typedef bool (*http_handler_t)(const http_req_t *r, http_resp_t *out);

// optional: large uploads (POST /api/firmware) are streamed instead of buffered. Return false to refuse.
typedef bool (*http_stream_t)(int id, const char *path, size_t total, size_t offset, const uint8_t *data, size_t n,
                              bool last, http_resp_t *out);

void http_init(http_handler_t handler, http_stream_t stream);
void http_respond(int id, const http_resp_t *resp);   // the body is copied unless body_static
void http_json(http_resp_t *r, int status, const char *json);   // helper: points r at json (must stay valid)
