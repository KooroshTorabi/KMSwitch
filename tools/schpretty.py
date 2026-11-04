"""Readable KiCad 7 schematic writer.

The layout of every block is written by hand in sch_<variant>.py: parts are placed with put(),
obvious connections are drawn as real wires with link()/wire(), and finish() gives every remaining
pin a power symbol (supply and ground nets), a net label (signals) or a no-connect flag.
Symbol UUIDs are reused from the previous schematic so the PCB stays linked to it.
"""
import math, re, os
from schgen import SymbolLibs, uid, S, dump, parse, find, findall

G = 1.27
POWER_NET = re.compile(r'^(\+|GND_|VBUS_PC)')


def sn(v):
    return round(round(v / G) * G, 3)


def rotv(vx, vy, rot):
    a = math.radians(rot)
    c, s = round(math.cos(a)), round(math.sin(a))
    return vx * c + vy * s, -vx * s + vy * c


def power_symbols(nets):
    """Power-port symbols named after each power net (KiCad takes the net name from the pin name)."""
    from schgen import SymbolLibs
    import copy
    sys_libs = SymbolLibs()
    out = []
    for net in sorted(nets):
        base = 'GND' if net.startswith('GND') else '+5V'
        sym = copy.deepcopy(sys_libs._raw('power', base))
        def walk(e):
            if isinstance(e, list) and e:
                if e[0] == 'symbol' and isinstance(e[1], str) and e[1].startswith(base + '_'):
                    e[1] = net + e[1][len(base):]
                if e[0] == 'property' and e[1] == 'Value':
                    e[2] = net
                if e[0] == 'property' and e[1] == 'Description':
                    e[2] = 'Power symbol for net ' + net
                if e[0] == 'pin':
                    for c in e:
                        if isinstance(c, list) and c and c[0] == 'name':
                            c[1] = net
                for c in e:
                    walk(c)
        for c in sym:
            walk(c)
        sym[1] = net
        out.append(sym)
    return out


def all_power_nets():
    import importlib
    nets = set()
    for v in ('a', 'b'):
        for c in importlib.import_module('design_' + v).comps:
            nets |= {n for n in c['pins'].values() if n and POWER_NET.match(n)}
    return nets


def old_uuids(path):
    """ref -> symbol uuid and the root uuid of an existing schematic (keeps the PCB link)."""
    if not os.path.exists(path):
        return {}, None
    doc = parse(open(path).read())
    root = find(doc, 'uuid')[1]
    out = {}
    for s in findall(doc, 'symbol'):
        ref = [p[2] for p in findall(s, 'property') if p[1] == 'Reference']
        u = find(s, 'uuid')
        if ref and u and not ref[0].startswith('#'):
            out[ref[0]] = u[1]
    return out, root


class Sheet:
    def __init__(self, project, libs, comps, title, paper, size, keep_path=None):
        self.project, self.libs, self.title, self.paper = project, libs, title, paper
        self.w, self.h = size
        self.comps = {c['ref']: c for c in comps}
        self.keep, root = old_uuids(keep_path) if keep_path else ({}, None)
        self.root = root or uid()
        self.items, self.used = [], {}
        self.pins = {}          # (ref, num) -> dict(x, y, dx, dy)
        self.done = set()       # pin locations already handled (x, y)
        self.groups = []        # [set of (ref,num), list of segments] for link()ed pins
        self.pwr_n = 0
        self.symbol_uuids = {}
        self.wires = []
        self.points = set()     # pin points of power symbols / flags
        self.junctions = set()
        self.labelled = set()
        self.quiet = set()      # wired-only nets that need no label

    # ---------------------------------------------------------------- symbols
    def _lib(self, lib_id):
        if lib_id not in self.used:
            self.used[lib_id] = self.libs.get(lib_id)

    def _symbol(self, lib_id, ref, value, x, y, rot, fields, footprint='', hidden_ref=False, field_pos=None, su=None, hidden_value=False):
        self._lib(lib_id)
        su = su or uid()
        power = ref.startswith('#')
        sym = [S('symbol'), [S('lib_id'), lib_id], [S('at'), x, y, rot], [S('unit'), 1],
               [S('in_bom'), S('no' if power else 'yes')], [S('on_board'), S('no' if power else 'yes')],
               [S('dnp'), S('no')], [S('uuid'), su]]
        fa = 90 if rot % 180 else 0
        vis = [S('effects'), [S('font'), [S('size'), 1.27, 1.27]]]
        hid = [S('effects'), [S('font'), [S('size'), 1.27, 1.27]], S('hide')]
        (rx, ry, rj), (vx, vy, vj) = field_pos
        def eff(j, hidden=False):
            e = [S('effects'), [S('font'), [S('size'), 1.27, 1.27]]]
            if j:
                e.append([S('justify'), S(j)])
            if hidden:
                e.append(S('hide'))
            return e
        sym.append([S('property'), 'Reference', ref, [S('at'), rx, ry, fa], eff(rj, hidden_ref)])
        sym.append([S('property'), 'Value', value, [S('at'), vx, vy, fa], eff(vj, hidden_value)])
        sym.append([S('property'), 'Footprint', footprint, [S('at'), x, y, 0], hid])
        sym.append([S('property'), 'Datasheet', '~', [S('at'), x, y, 0], hid])
        for k, v in (fields or {}).items():
            sym.append([S('property'), k, v, [S('at'), x, y, 0], hid])
        seen = set()
        for p in self.libs.pins(lib_id):
            if p['number'] not in seen:
                seen.add(p['number'])
                sym.append([S('pin'), p['number'], [S('uuid'), uid()]])
        sym.append([S('instances'), [S('project'), self.project,
                    [S('path'), '/' + self.root, [S('reference'), ref], [S('unit'), 1]]]])
        self.items.append(sym)
        return su

    def put(self, ref, x, y, rot=0, fields_at=None):
        """Place component `ref` with its origin at (x, y), rotated `rot` degrees counter-clockwise."""
        c = self.comps[ref]
        x, y = sn(x), sn(y)
        lib_id = c['lib_id']
        x0, x1, y0, y1 = self.libs.body_bbox(lib_id)
        # rotated bbox on screen
        pts = [rotv(px, -py, rot) for px in (x0, x1) for py in (y0, y1)]
        bx0, bx1 = min(p[0] for p in pts), max(p[0] for p in pts)
        by0, by1 = min(p[1] for p in pts), max(p[1] for p in pts)
        small = len({p['number'] for p in self.libs.pins(lib_id)}) <= 4 and (bx1 - bx0) < 12 and (by1 - by0) < 12
        if fields_at == 'right' or (fields_at is None and small and (by1 - by0) > (bx1 - bx0)):
            fp = ((x + bx1 + 0.8, y - 1.27, 'left'), (x + bx1 + 0.8, y + 1.27, 'left'))
        elif fields_at == 'top':
            fp = ((x + (bx0 + bx1) / 2, y + by0 - 4.4, None), (x + (bx0 + bx1) / 2, y + by0 - 1.8, None))
        elif fields_at == 'corners':      # ref above-right, value below-right (parts with pins on all sides)
            fp = ((x + bx1 + 0.6, y + by0 - 0.9, 'left'), (x + bx1 + 0.6, y + by1 + 1.3, 'left'))
        elif fields_at == 'left':
            fp = ((x + bx0 - 0.8, y - 1.27, 'right'), (x + bx0 - 0.8, y + 1.27, 'right'))
        else:
            fp = ((x + (bx0 + bx1) / 2, y + by0 - 1.6, None), (x + (bx0 + bx1) / 2, y + by1 + 1.6, None))
        su = self.keep.get(ref)
        su = self._symbol(lib_id, ref, c['value'], x, y, rot, c.get('fields'), c.get('footprint', ''),
                          field_pos=fp, su=su)
        self.symbol_uuids[ref] = su
        for p in self.libs.pins(lib_id):
            px, py = rotv(p['x'], -p['y'], rot)
            a = math.radians(p['angle'])
            # direction pointing away from the body (stub direction)
            dx, dy = rotv(-math.cos(a), math.sin(a), rot)
            self.pins[(ref, p['number'])] = dict(x=sn(x + px), y=sn(y + py), dx=round(dx), dy=round(dy))
        return self

    def P(self, ref, num):
        return self.pins[(ref, num)]

    def net(self, ref, num):
        return self.comps[ref]['pins'].get(num)

    # ---------------------------------------------------------------- drawing
    def wire(self, *pts):
        segs = []
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            ax, ay, bx, by = sn(ax), sn(ay), sn(bx), sn(by)
            if (ax, ay) == (bx, by):
                continue
            segs.append(((ax, ay), (bx, by)))
            self.wires.append(((ax, ay), (bx, by)))
        pinlocs = {(p['x'], p['y']) for p in self.pins.values()}
        for q in (pts[0], pts[-1]):
            q = (sn(q[0]), sn(q[1]))
            if q in pinlocs:
                self.done.add(q)
        return segs

    def mark(self, *pts):
        for x, y in pts:
            self.done.add((sn(x), sn(y)))

    def junction(self, x, y):
        self.junctions.add((sn(x), sn(y)))
        return
        self.items.append([S('junction'), [S('at'), sn(x), sn(y)], [S('diameter'), 0], [S('color'), 0, 0, 0, 0],
                           [S('uuid'), uid()]])

    def link(self, a, b, via=None, first=None):
        """Wire pin a=(ref,num) to pin b. via: list of intermediate points. first: 'h' or 'v' for an L route."""
        pa, pb = self.P(*a), self.P(*b)
        pts = [(pa['x'], pa['y'])]
        if via:
            pts += via
        elif pa['x'] != pb['x'] and pa['y'] != pb['y']:
            f = first or ('h' if pa['dx'] else 'v')
            pts.append((pb['x'], pa['y']) if f == 'h' else (pa['x'], pb['y']))
        pts.append((pb['x'], pb['y']))
        segs = self.wire(*pts)
        self.mark((pa['x'], pa['y']), (pb['x'], pb['y']))
        self.groups.append(({a, b}, segs))
        return segs

    def _label(self, net, x, y, dx, dy):
        self.labelled.add(net)
        if dx > 0:
            ang, just = 0, [S('justify'), S('left'), S('bottom')]
        elif dx < 0:
            ang, just = 180, [S('justify'), S('right'), S('bottom')]
        elif dy < 0:
            ang, just = 90, [S('justify'), S('left'), S('bottom')]
        else:
            ang, just = 270, [S('justify'), S('right'), S('bottom')]
        self.items.append([S('label'), net, [S('at'), sn(x), sn(y), ang], [S('fields_autoplaced')],
                           [S('effects'), [S('font'), [S('size'), 1.27, 1.27]], just], [S('uuid'), uid()]])

    def power(self, net, x, y, d=(0, 1)):
        """Power symbol for `net` with its pin at (x, y), body pointing in direction d."""
        self.pwr_n += 1
        gnd = net.startswith('GND')
        dx, dy = d
        if gnd:
            rot = {(0, 1): 0, (1, 0): 90, (0, -1): 180, (-1, 0): 270}[(dx, dy)]
            lib = 'kmswitch:' + net
        else:
            rot = {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}[(dx, dy)]
            lib = 'kmswitch:' + net
        x, y = sn(x), sn(y)
        # value text just beyond the symbol body
        tx, ty, j = x + dx * 4.0, y + dy * 4.0, None
        if dx > 0:
            tx, j = x + 3.3, 'left'
        elif dx < 0:
            tx, j = x - 3.3, 'right'
        self.points.add((x, y))
        if not gnd and rot == 90 and j:
            j = {'left': 'right', 'right': 'left'}[j]
        ref = '#PWR%03d' % self.pwr_n
        self._symbol(lib, ref, net, x, y, rot, None, hidden_ref=True,
                     field_pos=((x, y, None), (tx, ty if dx == 0 else y, j)))

    def pwr(self, ref, num, length=0.0, d=None):
        p = self.P(ref, num)
        d = d or (p['dx'], p['dy'])
        ex, ey = p['x'] + p['dx'] * length, p['y'] + p['dy'] * length
        if length:
            self.wire((p['x'], p['y']), (ex, ey))
        self.power(self.net(ref, num), ex, ey, d)
        self.done.add((p['x'], p['y']))

    def lab(self, ref, num, length=2.54):
        p = self.P(ref, num)
        ex, ey = p['x'] + p['dx'] * length, p['y'] + p['dy'] * length
        self.wire((p['x'], p['y']), (ex, ey))
        self._label(self.net(ref, num), ex, ey, p['dx'], p['dy'])
        self.done.add((p['x'], p['y']))

    def nc(self, ref, num):
        p = self.P(ref, num)
        self.items.append([S('no_connect'), [S('at'), p['x'], p['y']], [S('uuid'), uid()]])
        self.done.add((p['x'], p['y']))

    def text(self, txt, x, y, size=2.0, bold=True):
        eff = [S('font'), [S('size'), size, size]] + ([S('bold')] if bold else [])
        self.items.append([S('text'), txt, [S('at'), x, y, 0],
                           [S('effects'), eff, [S('justify'), S('left'), S('top')]], [S('uuid'), uid()]])

    def frame(self, title, x0, y0, x1, y1, note=None):
        self.items.append([S('rectangle'), [S('start'), x0, y0], [S('end'), x1, y1],
                           [S('stroke'), [S('width'), 0.25], [S('type'), S('dash')], [S('color'), 72, 72, 160, 1]],
                           [S('fill'), [S('type'), S('none')]], [S('uuid'), uid()]])
        self.text(title, x0 + 2, y0 + 2, 2.0)
        if note:
            self.text(note, x0 + 2, y0 + 6, 1.27, bold=False)

    # ---------------------------------------------------------------- finish
    def finish(self):
        """Label / power-symbol / no-connect every pin that is not wired yet; name wired groups."""
        named = set()
        for a, b in self.wires:
            self.done.add(a); self.done.add(b)
        for p in self.pins.values():
            x, y = p['x'], p['y']
            for (ax, ay), (bx, by) in self.wires:
                if (ax == bx == x and min(ay, by) <= y <= max(ay, by)) or (ay == by == y and min(ax, bx) <= x <= max(ax, bx)):
                    self.done.add((x, y))
                    break
        for (ref, num), p in sorted(self.pins.items()):
            loc = (p['x'], p['y'])
            if loc in self.done:
                continue
            net = self.net(ref, num)
            if ref.startswith('#'):
                continue
            if not net:
                if not self._hidden(ref, num):
                    self.nc(ref, num)
            elif POWER_NET.match(net):
                p = self.P(ref, num)
                self.pwr(ref, num, 0.0 if self._two_pin(ref) else (2.54 if p['dy'] else 2.54 * 5))
            else:
                self.lab(ref, num)
            named.add(net)
        for net in sorted({n for c in self.comps.values() for n in c['pins'].values() if n}):
            if net in self.labelled or net in self.quiet or POWER_NET.match(net):
                continue
            locs = {(p['x'], p['y']) for (r, n), p in self.pins.items() if self.net(r, n) == net}
            # every separate wired group of this net gets its own label (e.g. both sides of a pass-through part)
            for segs in self._groups_at(locs):
                hs = [w for w in segs if w[0][1] == w[1][1]] or segs
                (ax, ay), (bx, by) = max(hs, key=lambda w: abs(w[0][0] - w[1][0]) + abs(w[0][1] - w[1][1]))
                if ay == by:
                    self._label(net, min(ax, bx) + 1.27, ay, 1, 0)
                else:
                    self._label(net, ax, max(ay, by) - 1.27, 0, -1)

    def _groups_at(self, locs):
        """Connected wire groups that touch any of the pin locations."""
        def on(pt, w):
            (ax, ay), (bx, by) = w
            x, y = pt
            return (ax == bx == x and min(ay, by) <= y <= max(ay, by)) or (ay == by == y and min(ax, bx) <= x <= max(ax, bx))
        def touch(w1, w2):
            return any(on(p, w2) for p in w1) or any(on(p, w1) for p in w2)
        seen, groups = set(), []
        for i, w in enumerate(self.wires):
            if i in seen or not any(on(l, w) and l in (w[0], w[1]) for l in locs):
                continue
            grp, todo = [], [i]
            seen.add(i)
            while todo:
                k = todo.pop()
                grp.append(self.wires[k])
                for j, w2 in enumerate(self.wires):
                    if j not in seen and touch(self.wires[k], w2):
                        seen.add(j); todo.append(j)
            groups.append(grp)
        return groups

    def _hidden(self, ref, num):
        return any(p['number'] == num and p['hidden'] for p in self.libs.pins(self.comps[ref]['lib_id']))

    def flag(self, x, y, d=(0, -1)):
        """PWR_FLAG with its pin at (x, y) (on a power wire), body pointing in direction d, for ERC."""
        self.pwr_n += 1
        x, y = sn(x), sn(y)
        rot = {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}[tuple(d)]
        self.points.add((x, y))
        tx, ty, j = x + d[0] * 4.6, y + d[1] * 4.6, None
        if d[0]:
            tx, ty, j = x + d[0] * 3.4, y, ('left' if d[0] > 0 else 'right')
        self._symbol('power:PWR_FLAG', '#FLG%03d' % self.pwr_n, 'PWR_FLAG', x, y, rot, None, hidden_ref=True,
                     field_pos=((x, y, None), (tx, ty, j)), hidden_value=True)

    def _two_pin(self, ref):
        return len({p['number'] for p in self.libs.pins(self.comps[ref]['lib_id'])}) <= 2

    def _wire_items(self):
        """Split wires at every point where something connects to their middle (kicad-cli needs real
        endpoints there), and add junction dots where three or more things meet."""
        pts = set(self.points) | {(p['x'], p['y']) for p in self.pins.values()} | set(self.junctions)
        for a, b in self.wires:
            pts.add(a); pts.add(b)
        out, ends = [], {}
        for (ax, ay), (bx, by) in self.wires:
            inner = []
            for (px, py) in pts:
                if ax == bx == px and min(ay, by) < py < max(ay, by):
                    inner.append((px, py))
                elif ay == by == py and min(ax, bx) < px < max(ax, bx):
                    inner.append((px, py))
            seq = sorted([(ax, ay), (bx, by)] + inner, key=lambda q: (q[0] - ax) ** 2 + (q[1] - ay) ** 2)
            for q, r in zip(seq, seq[1:]):
                out.append([S('wire'), [S('pts'), [S('xy'), q[0], q[1]], [S('xy'), r[0], r[1]]],
                            [S('stroke'), [S('width'), 0], [S('type'), S('default')]], [S('uuid'), uid()]])
                ends[q] = ends.get(q, 0) + 1; ends[r] = ends.get(r, 0) + 1
        pinpts = {(p['x'], p['y']) for p in self.pins.values()} | self.points
        for q, n in ends.items():
            if n + (1 if q in pinpts else 0) >= 3:
                out.append([S('junction'), [S('at'), q[0], q[1]], [S('diameter'), 0], [S('color'), 0, 0, 0, 0],
                            [S('uuid'), uid()]])
        return out

    def write(self, path):
        doc = [S('kicad_sch'), [S('version'), 20230121], [S('generator'), S('eeschema')],
               [S('uuid'), self.root], [S('paper'), self.paper],
               [S('title_block'), [S('title'), self.title], [S('rev'), '1.0'], [S('company'), 'kmswitch (open hardware)']],
               [S('lib_symbols')] + list(self.used.values())]
        doc += self.items + self._wire_items()
        doc.append([S('sheet_instances'), [S('path'), '/', [S('page'), '1']]])
        with open(path, 'w') as f:
            f.write(dump(doc) + '\n')


def check(schfile, comps, workdir):
    """Compare the schematic netlist with the design: every design net must be exactly one schematic net."""
    import subprocess, xml.etree.ElementTree as ET
    xmlp = os.path.join(workdir, os.path.basename(schfile) + '.net.xml')
    r = subprocess.run(['kicad-cli', 'sch', 'export', 'netlist', '--format', 'kicadxml', '-o', xmlp, schfile],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    got = {}
    for n in ET.parse(xmlp).iter('net'):
        nm = n.get('name').lstrip('/')
        for node in n.iter('node'):
            got[(node.get('ref'), node.get('pin'))] = nm
    want = {}
    for c in comps:
        for pin, net in c['pins'].items():
            if net:
                want.setdefault(net, set()).add((c['ref'], pin))
    sch_nets = {}
    for k, v in got.items():
        sch_nets.setdefault(v, set()).add(k)
    errs = []
    for net, members in want.items():
        names = {got.get(m) for m in members}
        if len(names) != 1 or None in names:
            errs.append('design net %s split over %s' % (net, sorted(n or '-' for n in names)))
            continue
        nm = names.pop()
        extra = {m for m in sch_nets[nm] if not m[0].startswith('#')} - members
        if extra:
            errs.append('design net %s merged with %s' % (net, sorted(extra)))
        if nm != net and not nm.startswith('Net-('):
            errs.append('design net %s is named %s' % (net, nm))
    for c in comps:
        for pin, net in c['pins'].items():
            g = got.get((c['ref'], pin))
            if not net and g and not g.startswith(('unconnected-', 'Net-(')):
                errs.append('%s.%s should be unconnected, is on %s' % (c['ref'], pin, g))
    return errs
