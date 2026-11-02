"""Generate a KiCad 7 schematic (.kicad_sch) from a component/net description.

Every pin gets a short wire stub and a net label, so the schematic is fully
connected by labels. Unused pins get no-connect flags.
"""
import math, uuid, os, copy
from sexpr import parse, dump, Sym, find, findall

S = Sym
GRID = 2.54
SYSLIB = '/usr/share/kicad/symbols'

def uid():
    return str(uuid.uuid4())

def snap(v, g=GRID):
    return round(v / g) * g


class SymbolLibs:
    def __init__(self, extra_libs=None):
        self.cache = {}
        self.paths = {}
        for name, path in (extra_libs or {}).items():
            self.paths[name] = path

    def _lib(self, lib):
        if lib not in self.cache:
            path = self.paths.get(lib, os.path.join(SYSLIB, lib + '.kicad_sym'))
            self.cache[lib] = parse(open(path).read())
        return self.cache[lib]

    def _raw(self, lib, name):
        for e in findall(self._lib(lib), 'symbol'):
            if e[1] == name:
                return e
        raise KeyError('%s:%s' % (lib, name))

    def get(self, lib_id):
        """Return flattened symbol s-expr named lib_id (for lib_symbols)."""
        lib, name = lib_id.split(':', 1)
        raw = copy.deepcopy(self._raw(lib, name))
        ext = find(raw, 'extends')
        if ext:
            parent = self.get(lib + ':' + ext[1])
            pname = ext[1]
            # child properties override parent's
            child_props = {p[1]: p for p in findall(raw, 'property')}
            out = [S('symbol'), lib_id]
            for e in parent[2:]:
                if isinstance(e, list) and e and e[0] == 'property' and e[1] in child_props:
                    out.append(child_props.pop(e[1]))
                elif isinstance(e, list) and e and e[0] == 'symbol':
                    sub = copy.deepcopy(e)
                    sub[1] = sub[1].replace(pname + '_', name + '_', 1)
                    out.append(sub)
                else:
                    out.append(e)
            for p in child_props.values():
                out.append(p)
            return out
        raw[1] = lib_id
        return raw

    def pins(self, lib_id):
        sym = self.get(lib_id)
        name = lib_id.split(':', 1)[1]
        pins = []
        for sub in findall(sym, 'symbol'):
            parts = sub[1].rsplit('_', 2)
            unit, conv = int(parts[-2]), int(parts[-1])
            if unit not in (0, 1) or conv not in (0, 1):
                continue
            for p in findall(sub, 'pin'):
                at = find(p, 'at')
                num = find(p, 'number')[1]
                nm = find(p, 'name')[1]
                hidden = any(isinstance(e, Sym) and e == 'hide' for e in p)
                pins.append(dict(number=num, name=nm, type=str(p[1]), x=float(at[1]), y=float(at[2]),
                                 angle=float(at[3]) if len(at) > 3 else 0.0,
                                 length=float(find(p, 'length')[1]), hidden=hidden))
        return pins

    def body_bbox(self, lib_id):
        sym = self.get(lib_id)
        xs, ys = [0], [0]
        def walk(e):
            if isinstance(e, list) and e:
                if e[0] in ('start', 'end', 'xy', 'center') and len(e) >= 3 and all(isinstance(v, (int, float)) or _isnum(v) for v in e[1:3]):
                    xs.append(float(e[1])); ys.append(float(e[2]))
                for c in e:
                    walk(c)
        for sub in findall(sym, 'symbol'):
            walk(sub)
        for p in self.pins(lib_id):
            xs.append(p['x']); ys.append(p['y'])
        return min(xs), max(xs), min(ys), max(ys)


def _isnum(v):
    try:
        float(v); return True
    except Exception:
        return False


class Schematic:
    def __init__(self, project, libs, title='', paper='A2', page_w=594, page_h=420):
        self.project = project
        self.libs = libs
        self.root = uid()
        self.items = []
        self.used_libs = {}
        self.title = title
        self.paper = paper
        self.page_w, self.page_h = page_w, page_h
        self.flag_n = 0
        self.symbol_uuids = {}

    # ------------------------------------------------------------------
    def place(self, comp, X, Y):
        """comp: dict(ref, lib_id, value, footprint, pins{num:net or None})"""
        lib_id = comp['lib_id']
        if lib_id not in self.used_libs:
            self.used_libs[lib_id] = self.libs.get(lib_id)
        pins = self.libs.pins(lib_id)
        su = uid()
        self.symbol_uuids[comp['ref']] = su
        is_power = comp['ref'].startswith('#')
        sym = [S('symbol'), [S('lib_id'), lib_id], [S('at'), X, Y, 0], [S('unit'), 1],
               [S('in_bom'), S('no' if is_power else 'yes')], [S('on_board'), S('no' if is_power else 'yes')],
               [S('dnp'), S('no')], [S('uuid'), su]]
        x0, x1, y0, y1 = self.libs.body_bbox(lib_id)
        fnt = [S('effects'), [S('font'), [S('size'), 1.27, 1.27]]]
        fnt_h = [S('effects'), [S('font'), [S('size'), 1.27, 1.27]], S('hide')]
        sym.append([S('property'), 'Reference', comp['ref'], [S('at'), X, snap(Y - y1 - 2.54, 1.27), 0], fnt_h if is_power else fnt])
        sym.append([S('property'), 'Value', comp['value'], [S('at'), X, snap(Y - y0 + 2.54, 1.27), 0], fnt_h if is_power else fnt])
        sym.append([S('property'), 'Footprint', comp.get('footprint', ''), [S('at'), X, Y, 0], fnt_h])
        sym.append([S('property'), 'Datasheet', comp.get('datasheet', '~'), [S('at'), X, Y, 0], fnt_h])
        for k, v in comp.get('fields', {}).items():
            sym.append([S('property'), k, v, [S('at'), X, Y, 0], fnt_h])
        seen = set()
        for p in pins:
            if p['number'] in seen:
                continue
            seen.add(p['number'])
            sym.append([S('pin'), p['number'], [S('uuid'), uid()]])
        sym.append([S('instances'), [S('project'), self.project,
                    [S('path'), '/' + self.root, [S('reference'), comp['ref']], [S('unit'), 1]]]])
        self.items.append(sym)

        netmap = comp['pins']
        allnums = {p['number'] for p in pins}
        unknown = set(netmap) - allnums
        assert not unknown, '%s: unknown pins %s' % (comp['ref'], unknown)
        # group pins by location (stacked pins)
        groups = {}
        for p in pins:
            key = (round(p['x'], 3), round(p['y'], 3))
            groups.setdefault(key, []).append(p)
        for key, plist in groups.items():
            nets = {netmap.get(p['number']) for p in plist}
            nets_real = {n for n in nets if n}
            assert len(nets_real) <= 1, '%s stacked pins with different nets %s' % (comp['ref'], nets)
            p = plist[0]
            px, py = X + p['x'], Y - p['y']
            if not nets_real:
                if is_power:
                    continue
                self.items.append([S('no_connect'), [S('at'), px, py], [S('uuid'), uid()]])
                continue
            net = nets_real.pop()
            a = math.radians(p['angle'])
            dx, dy = -math.cos(a), math.sin(a)
            dx, dy = round(dx), round(dy)
            ex, ey = px + dx * 2.54, py + dy * 2.54
            self.items.append([S('wire'), [S('pts'), [S('xy'), px, py], [S('xy'), ex, ey]],
                               [S('stroke'), [S('width'), 0], [S('type'), S('default')]], [S('uuid'), uid()]])
            if dx > 0:
                ang, just = 0, [S('justify'), S('left'), S('bottom')]
            elif dx < 0:
                ang, just = 180, [S('justify'), S('right'), S('bottom')]
            elif dy < 0:
                ang, just = 90, [S('justify'), S('left'), S('bottom')]
            else:
                ang, just = 270, [S('justify'), S('right'), S('bottom')]
            self.items.append([S('label'), net, [S('at'), ex, ey, ang], [S('fields_autoplaced')],
                               [S('effects'), [S('font'), [S('size'), 1.27, 1.27]], just], [S('uuid'), uid()]])

    def footprint_extent(self, comp):
        """Width/height of a placed symbol including label room, and origin offset."""
        lib_id = comp['lib_id']
        x0, x1, y0, y1 = self.libs.body_bbox(lib_id)
        pins = self.libs.pins(lib_id)
        lmax = {'l': 0, 'r': 0, 'u': 0, 'd': 0}
        for p in pins:
            net = comp['pins'].get(p['number'])
            L = 2.54 + (len(net) * 1.0 + 2 if net else 1)
            a = int(round(p['angle'])) % 360
            side = {0: 'l', 180: 'r', 90: 'd', 270: 'u'}[a]
            lmax[side] = max(lmax[side], L)
        left = -x0 + lmax['l'] + 2
        right = x1 + lmax['r'] + 2
        top = y1 + lmax['u'] + 4
        bot = -y0 + lmax['d'] + 4
        return left, right, top, bot

    def add_text(self, txt, X, Y, size=2.0):
        self.items.append([S('text'), txt, [S('at'), X, Y, 0],
                           [S('effects'), [S('font'), [S('size'), size, size], S('bold')], [S('justify'), S('left'), S('bottom')]],
                           [S('uuid'), uid()]])

    def add_note(self, txt, X, Y, size=1.5):
        self.items.append([S('text'), txt, [S('at'), X, Y, 0],
                           [S('effects'), [S('font'), [S('size'), size, size]], [S('justify'), S('left'), S('top')]],
                           [S('uuid'), uid()]])

    def layout(self, blocks, margin=20, cols=1):
        """Shelf-pack blocks into `cols` page columns; a block moves to the next column when it would overflow."""
        colw = (self.page_w - 2 * margin) / cols
        col = 0
        X0 = margin
        cur_y = margin + 10
        bottom = cur_y
        for title, comps, note in blocks:
            # measure the block first
            def pack(x0, y0, dry):
                ny = y0 + 4
                if note:
                    ny += 4 * (note.count('\n') + 1) + 2
                x, row_top, row_h = x0, ny, 0
                for c in comps:
                    l, r, tp, b = self.footprint_extent(c)
                    w, h = l + r, tp + b
                    if x + w > x0 + colw - 5 and x > x0:
                        x = x0; row_top += row_h; row_h = 0
                    if not dry:
                        self.place(c, snap(x + l), snap(row_top + tp))
                    x += w; row_h = max(row_h, h)
                return row_top + row_h + 8
            end = pack(X0, cur_y, True)
            if end > self.page_h - 40 and cur_y > margin + 10 and col < cols - 1:
                col += 1
                X0 = margin + col * colw
                cur_y = margin + 10
                end = pack(X0, cur_y, True)
            self.add_text(title, X0, cur_y)
            if note:
                self.add_note(note, X0, cur_y + 4)
            end = pack(X0, cur_y, False)
            cur_y = end
            bottom = max(bottom, cur_y)
        self.last_col_x = X0
        self.last_col_y = cur_y
        return bottom

    def add_pwr_flags(self, nets, X, Y):
        for i, net in enumerate(nets):
            self.flag_n += 1
            comp = dict(ref='#FLG%02d' % self.flag_n, lib_id='power:PWR_FLAG', value='PWR_FLAG', footprint='',
                        pins={'1': net})
            self.place(comp, snap(X + (i % 8) * 25.4), snap(Y + (i // 8) * 15.24))

    def write(self, path):
        doc = [S('kicad_sch'), [S('version'), 20230121], [S('generator'), S('eeschema')],
               [S('uuid'), self.root], [S('paper'), self.paper]]
        if self.title:
            doc.append([S('title_block'), [S('title'), self.title], [S('rev'), '1.0'],
                        [S('company'), 'kmswitch (open hardware)']])
        doc.append([S('lib_symbols')] + list(self.used_libs.values()))
        doc += self.items
        doc.append([S('sheet_instances'), [S('path'), '/', [S('page'), '1']]])
        with open(path, 'w') as f:
            f.write(dump(doc) + '\n')
