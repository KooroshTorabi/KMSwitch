#include "kms_layout.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define EPS 0.01f

int kms_pc_monitors(const kms_pc_cfg_t *pc, kms_rect_t *out) {
    if (pc->nmon > 0) {
        for (int i = 0; i < pc->nmon; i++)
            out[i] = (kms_rect_t){pc->mon[i].x, pc->mon[i].y, pc->mon[i].w, pc->mon[i].h};
        return pc->nmon;
    }
    // one screen from `aspect`, as the settings page draws it: 1920 wide, wider for ultra-wide shapes
    double r = (double)pc->aspect_w / pc->aspect_h;
    if (!(r > 0)) r = 16.0 / 9.0;
    double w = 1920.0 * fmax(1.0, r / (16.0 / 9.0));
    out[0] = (kms_rect_t){0, 0, (int32_t)lround(w), (int32_t)lround(w / r)};
    return 1;
}

static kms_rect_t bbox_of(const kms_rect_t *r, int n) {
    int32_t x0 = r[0].x, y0 = r[0].y, x1 = r[0].x + r[0].w, y1 = r[0].y + r[0].h;
    for (int i = 1; i < n; i++) {
        if (r[i].x < x0) x0 = r[i].x;
        if (r[i].y < y0) y0 = r[i].y;
        if (r[i].x + r[i].w > x1) x1 = r[i].x + r[i].w;
        if (r[i].y + r[i].h > y1) y1 = r[i].y + r[i].h;
    }
    return (kms_rect_t){x0, y0, x1 - x0, y1 - y0};
}

static int64_t overlap_area(kms_rect_t a, kms_rect_t b) {
    int64_t w = (int64_t)fmin(a.x + a.w, b.x + b.w) - fmax(a.x, b.x);
    int64_t h = (int64_t)fmin(a.y + a.h, b.y + b.h) - fmax(a.y, b.y);
    return w > 0 && h > 0 ? w * h : 0;
}

int kms_layout_contacts(const kms_layout_t *L, int32_t ox, int32_t oy) {
    int n = 0;
    for (int i = 0; i < L->nmon[0]; i++)
        for (int j = 0; j < L->nmon[1]; j++) {
            kms_rect_t a = L->mon[0][i], b = L->mon[1][j];
            b.x += ox; b.y += oy;
            if (b.x == a.x + a.w || b.x + b.w == a.x) {
                if (fmin(a.y + a.h, b.y + b.h) > fmax(a.y, b.y)) n++;
            }
            if (b.y == a.y + a.h || b.y + b.h == a.y) {
                if (fmin(a.x + a.w, b.x + b.w) > fmax(a.x, b.x)) n++;
            }
        }
    return n;
}

static bool placement_ok(const kms_layout_t *L, int32_t ox, int32_t oy) {
    for (int i = 0; i < L->nmon[0]; i++)
        for (int j = 0; j < L->nmon[1]; j++) {
            kms_rect_t b = L->mon[1][j];
            b.x += ox; b.y += oy;
            if (overlap_area(L->mon[0][i], b)) return false;
        }
    return true;
}

static void preset_right(const kms_layout_t *L, int32_t *ox, int32_t *oy) {
    kms_rect_t A = L->bbox[0], B = L->bbox[1];
    *ox = A.x + A.w - B.x;
    *oy = A.y - B.y;
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

void kms_layout_snap(const kms_layout_t *L, int32_t want_x, int32_t want_y, int32_t *ox, int32_t *oy) {
    double best = -1;
    int32_t bx = 0, by = 0;
    for (int i = 0; i < L->nmon[0]; i++)
        for (int j = 0; j < L->nmon[1]; j++) {
            kms_rect_t a = L->mon[0][i], b = L->mon[1][j];
            // b right / left of a
            double xs[2] = {a.x + a.w - b.x, a.x - b.w - b.x};
            for (int k = 0; k < 2; k++) {
                double lo = a.y - b.h + KMS_MIN_TOUCH - b.y, hi = a.y + a.h - KMS_MIN_TOUCH - b.y;
                if (lo > hi) continue;
                double y = clampd(want_y, lo, hi);
                double t[3] = {a.y - b.y, a.y + a.h - b.h - b.y, a.y + a.h / 2.0 - b.h / 2.0 - b.y};
                for (int q = 0; q < 3; q++)
                    if (fabs(y - t[q]) < a.h * 0.04 && t[q] >= lo && t[q] <= hi) y = t[q];
                int32_t cx = (int32_t)lround(xs[k]), cy = (int32_t)lround(y);
                if (!placement_ok(L, cx, cy)) continue;
                double d = hypot(cx - want_x, cy - want_y);
                if (best < 0 || d < best) { best = d; bx = cx; by = cy; }
            }
            // b below / above a
            double ys[2] = {a.y + a.h - b.y, a.y - b.h - b.y};
            for (int k = 0; k < 2; k++) {
                double lo = a.x - b.w + KMS_MIN_TOUCH - b.x, hi = a.x + a.w - KMS_MIN_TOUCH - b.x;
                if (lo > hi) continue;
                double x = clampd(want_x, lo, hi);
                double t[3] = {a.x - b.x, a.x + a.w - b.w - b.x, a.x + a.w / 2.0 - b.w / 2.0 - b.x};
                for (int q = 0; q < 3; q++)
                    if (fabs(x - t[q]) < a.w * 0.04 && t[q] >= lo && t[q] <= hi) x = t[q];
                int32_t cx = (int32_t)lround(x), cy = (int32_t)lround(ys[k]);
                if (!placement_ok(L, cx, cy)) continue;
                double d = hypot(cx - want_x, cy - want_y);
                if (best < 0 || d < best) { best = d; bx = cx; by = cy; }
            }
        }
    if (best < 0) preset_right(L, &bx, &by);
    *ox = bx; *oy = by;
}

void kms_layout_build(kms_layout_t *L, const kms_config_t *c) {
    memset(L, 0, sizeof *L);
    for (int p = 0; p < 2; p++) {
        L->nmon[p] = kms_pc_monitors(&c->pc[p], L->mon[p]);
        L->bbox[p] = bbox_of(L->mon[p], L->nmon[p]);
    }
    if (c->has_arrangement) {
        L->off_x = c->arr_x; L->off_y = c->arr_y;
        if (!placement_ok(L, L->off_x, L->off_y) || !kms_layout_contacts(L, L->off_x, L->off_y)) {
            kms_layout_snap(L, c->arr_x, c->arr_y, &L->off_x, &L->off_y);
            L->arrangement_moved = true;
        }
    } else {
        preset_right(L, &L->off_x, &L->off_y);
    }
}

// ------------------------------------------------------------------ pointer
static void offset_of(const kms_layout_t *L, int pc, float *x, float *y) {
    *x = pc ? (float)L->off_x : 0.0f;
    *y = pc ? (float)L->off_y : 0.0f;
}

static bool inside(kms_rect_t m, float x, float y) {
    return x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h;
}

static int find_mon(const kms_layout_t *L, int pc, float x, float y) {
    for (int i = 0; i < L->nmon[pc]; i++) if (inside(L->mon[pc][i], x, y)) return i;
    return -1;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void kms_pointer_clamp(kms_pointer_t *p, const kms_layout_t *L) {
    if (find_mon(L, p->pc, p->x, p->y) >= 0) return;
    float bd = 0, bx = 0, by = 0;
    for (int i = 0; i < L->nmon[p->pc]; i++) {
        kms_rect_t m = L->mon[p->pc][i];
        float cx = clampf(p->x, (float)m.x, m.x + m.w - EPS), cy = clampf(p->y, (float)m.y, m.y + m.h - EPS);
        float d = (cx - p->x) * (cx - p->x) + (cy - p->y) * (cy - p->y);
        if (i == 0 || d < bd) { bd = d; bx = cx; by = cy; }
    }
    p->x = bx; p->y = by;
}

void kms_pointer_init(kms_pointer_t *p, const kms_layout_t *L, int pc) {
    memset(p, 0, sizeof *p);
    p->pc = pc;
    int i = find_mon(L, pc, 0, 0);
    kms_rect_t m = L->mon[pc][i < 0 ? 0 : i];
    p->x = m.x + m.w / 2.0f;
    p->y = m.y + m.h / 2.0f;
}

// one axis of a move. axis 0 = x, 1 = y. Returns true on a switch.
static bool move_axis(kms_pointer_t *p, const kms_layout_t *L, int axis, float d, uint32_t now,
                      const kms_move_opts_t *o) {
    if (d == 0) return false;
    float *pos = axis ? &p->y : &p->x;
    float v = axis ? p->x : p->y;   // the other coordinate
    float t = *pos + d;
    const int A = p->pc, B = !A;
    if (find_mon(L, A, axis ? v : t, axis ? t : v) >= 0) {
        *pos = t;
        p->push_since = 0;
        return false;
    }
    int mi = find_mon(L, A, p->x, p->y);
    if (mi < 0) { kms_pointer_clamp(p, L); mi = find_mon(L, A, p->x, p->y); if (mi < 0) return false; }
    kms_rect_t M = L->mon[A][mi];
    int32_t lo = axis ? M.y : M.x, size = axis ? M.h : M.w;
    int32_t olo = axis ? M.x : M.y, osize = axis ? M.w : M.h;
    int32_t edge = d > 0 ? lo + size : lo;
    // another monitor of the same PC on the other side of this edge
    for (int i = 0; i < L->nmon[A]; i++) {
        kms_rect_t N = L->mon[A][i];
        int32_t nlo = axis ? N.y : N.x, nsize = axis ? N.h : N.w;
        int32_t nolo = axis ? N.x : N.y, nosize = axis ? N.w : N.h;
        if ((d > 0 ? nlo == edge : nlo + nsize == edge) && v >= nolo && v < nolo + nosize) {
            *pos = clampf(t, (float)nlo, nlo + nsize - EPS);
            p->push_since = 0;
            return false;
        }
    }
    *pos = d > 0 ? edge - EPS : (float)edge;
    // the other PC: a monitor touching this edge at this point (shared pixel space)
    float ax, ay, bx, by;
    offset_of(L, A, &ax, &ay);
    offset_of(L, B, &bx, &by);
    float ge = edge + (axis ? ay : ax), gv = v + (axis ? ax : ay);
    int pj = -1;
    for (int j = 0; j < L->nmon[B]; j++) {
        kms_rect_t P = L->mon[B][j];
        float plo = (axis ? P.y + by : P.x + bx), psize = (float)(axis ? P.h : P.w);
        float polo = (axis ? P.x + bx : P.y + by), posize = (float)(axis ? P.w : P.h);
        if ((d > 0 ? plo == ge : plo + psize == ge) && gv >= polo && gv < polo + posize) { pj = j; break; }
    }
    if (pj < 0 || !o->edge_switch || o->relative) { p->push_since = 0; return false; }
    if (o->corner_block && (v - olo < KMS_CORNER_PX || olo + osize - v <= KMS_CORNER_PX)) {
        p->push_since = 0;
        return false;
    }
    int dir = axis ? (d > 0 ? 3 : 4) : (d > 0 ? 1 : 2);
    if (!p->push_since || p->push_dir != dir) { p->push_since = now ? now : 1; p->push_dir = dir; }
    if ((uint32_t)(now - p->push_since) < (uint32_t)o->edge_delay_ms) return false;
    // switch: keep the position along the shared edge
    kms_rect_t P = L->mon[B][pj];
    int32_t plo = axis ? P.y : P.x, psize = axis ? P.h : P.w;
    float nv = gv - (axis ? bx : by);
    float np = d > 0 ? (float)plo : plo + psize - EPS;
    p->pc = B;
    if (axis) { p->y = np; p->x = nv; } else { p->x = np; p->y = nv; }
    p->push_since = 0;
    return true;
}

bool kms_pointer_move(kms_pointer_t *p, const kms_layout_t *L, float dx, float dy, uint32_t now_ms,
                      const kms_move_opts_t *o) {
    if (move_axis(p, L, 0, dx, now_ms, o)) return true;
    return move_axis(p, L, 1, dy, now_ms, o);
}

void kms_pointer_abs(const kms_pointer_t *p, const kms_layout_t *L, uint16_t *ax, uint16_t *ay) {
    kms_rect_t b = L->bbox[p->pc];
    double x = (floor(p->x) - b.x + 0.5) * 32768.0 / b.w, y = (floor(p->y) - b.y + 0.5) * 32768.0 / b.h;   // pixel centre
    *ax = (uint16_t)clampd(floor(x), 0, KMS_ABS_MAX);
    *ay = (uint16_t)clampd(floor(y), 0, KMS_ABS_MAX);
}

void kms_scale_motion(int32_t cx, int32_t cy, uint32_t dt_ms, float speed_x, float speed_y, kms_accel_t accel,
                      float *px, float *py) {
    // base 1.0: with speed 1.0 one count is one pixel (800 DPI mouse on a 1080p screen without acceleration)
    static const float gain[4] = {0.0f, 0.25f, 0.5f, 1.0f};
    if (dt_ms < 1) dt_ms = 1;
    if (dt_ms > 20) dt_ms = 20;
    float v = sqrtf((float)cx * cx + (float)cy * cy) / (float)dt_ms;   // counts per ms
    float f = 1.0f + gain[accel] * clampf(v - 0.3f, 0.0f, 4.0f);
    *px = cx * speed_x * f;
    *py = cy * speed_y * f;
}
