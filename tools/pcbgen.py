"""Build a KiCad 7 PCB from a design description, autoroute with Freerouting, fill zones, run DRC."""
import os, re, subprocess, math
import pcbnew
from sexpr import parse, Sym

MM = pcbnew.FromMM
OFF = (100.0, 100.0)  # board origin on the KiCad sheet
SYSFP = '/usr/share/kicad/footprints'
FREEROUTING = os.environ.get('FREEROUTING_JAR', os.path.join(os.path.dirname(__file__), 'freerouting.jar'))


def P(x, y):
    return pcbnew.VECTOR2I(MM(x + OFF[0]), MM(y + OFF[1]))


def fp_path(lib, projlib):
    if lib == 'kmswitch':
        return projlib
    return os.path.join(SYSFP, lib + '.pretty')


def build(design, outdir, name, projlib, sym_uuids=None, route=True, passes=40):
    board = pcbnew.CreateEmptyBoard()
    ds = board.GetDesignSettings()
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetClearance(MM(design.get('default_clearance', 0.2))); nc.SetTrackWidth(MM(design.get('default_track', 0.25))); nc.SetViaDiameter(MM(0.6)); nc.SetViaDrill(MM(0.3))
    ds.m_MinClearance = MM(0.15)
    ds.m_TrackMinWidth = MM(0.15)
    ds.m_ViasMinSize = MM(0.5)
    ds.m_MinThroughDrill = MM(0.3)
    ds.m_CopperEdgeClearance = MM(0.3)
    ds.SetCopperLayerCount(2)

    nets = {}
    def net(n):
        if n not in nets:
            ni = pcbnew.NETINFO_ITEM(board, n)
            board.Add(ni)
            nets[n] = ni
        return nets[n]

    W, H = design['size']
    # ---- outline with rounded corners
    r = 2.0
    def seg(a, b):
        s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(P(*a)); s.SetEnd(P(*b)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1)); board.Add(s)
    def arc(c, start, ang):
        s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_ARC)
        s.SetCenter(P(*c)); s.SetStart(P(*start)); s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(ang, pcbnew.DEGREES_T))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1)); board.Add(s)
    seg((r, 0), (W - r, 0)); seg((W, r), (W, H - r)); seg((W - r, H), (r, H)); seg((0, H - r), (0, r))
    arc((W - r, r), (W - r, 0), 90); arc((W - r, H - r), (W, H - r), 90)
    arc((r, H - r), (r, H), 90); arc((r, r), (0, r), 90)

    # ---- footprints
    missing = []
    for c in design['comps']:
        if not c.get('footprint') or 'pcb' not in c:
            continue
        lib, fpn = c['footprint'].split(':', 1)
        fp = pcbnew.FootprintLoad(fp_path(lib, projlib), fpn)
        assert fp is not None, c['footprint']
        fp.SetFPID(pcbnew.LIB_ID(lib, fpn))
        fp.SetReference(c['ref']); fp.SetValue(c['value'])
        if c['ref'][0] in 'RCDF' or (design.get('fab_refs') and c['ref'][0] not in 'JH'):
            fp.Reference().SetLayer(pcbnew.F_Fab)
        x, y, rot = c['pcb'][:3]
        side = c['pcb'][3] if len(c['pcb']) > 3 else 'F'
        fp.SetPosition(P(x, y))
        if side == 'B':
            fp.Flip(P(x, y), False)
        fp.SetOrientationDegrees(rot)
        if sym_uuids and c['ref'] in sym_uuids:
            fp.SetPath(pcbnew.KIID_PATH('/' + sym_uuids[c['ref']]))
        padnums = set()
        for pad in fp.Pads():
            n = pad.GetNumber()
            padnums.add(n)
            nn = c['pins'].get(n)
            if nn:
                pad.SetNet(net(nn))
        for pnum, nn in c['pins'].items():
            if nn and pnum not in padnums:
                missing.append((c['ref'], pnum))
        if c['ref'].startswith('H'):
            fp.Reference().SetVisible(False)
        board.Add(fp)
    assert not missing, 'pins without pads: %s' % missing

    # ---- silkscreen texts
    for t in design.get('texts', []):
        txt = pcbnew.PCB_TEXT(board)
        txt.SetText(t[0]); txt.SetPosition(P(t[1], t[2]))
        txt.SetLayer(pcbnew.F_SilkS if (len(t) < 5 or t[4] == 'F') else pcbnew.B_SilkS)
        sz = t[3] if len(t) > 3 else 1.0
        txt.SetTextSize(pcbnew.VECTOR2I(MM(sz), MM(sz))); txt.SetTextThickness(MM(sz * 0.15))
        if len(t) > 4 and t[4] == 'B':
            txt.SetMirrored(True)
        if len(t) > 5:
            txt.SetTextAngle(pcbnew.EDA_ANGLE(t[5], pcbnew.DEGREES_T))
        board.Add(txt)

    # ---- keepout (isolation gap): no tracks / vias / copper pour
    for (x0, y0, x1, y1) in design.get('keepouts', []):
        z = pcbnew.ZONE(board)
        z.SetIsRuleArea(True); z.SetDoNotAllowTracks(True); z.SetDoNotAllowVias(True)
        z.SetDoNotAllowCopperPour(True); z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
        ls = pcbnew.LSET(); ls.AddLayer(pcbnew.F_Cu); ls.AddLayer(pcbnew.B_Cu); z.SetLayerSet(ls)
        o = z.Outline(); o.NewOutline()
        for (px, py) in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            o.Append(MM(px + OFF[0]), MM(py + OFF[1]))
        board.Add(z)

    # ---- pre-routed stubs: (ref, pad, dx, dy, width, via)
    for (ref, padnum, dx, dy, width, via) in design.get('stubs', []):
        fp = board.FindFootprintByReference(ref)
        pad = [p for p in fp.Pads() if p.GetNumber() == padnum][0]
        a = pad.GetPosition()
        b = pcbnew.VECTOR2I(a.x + MM(dx), a.y + MM(dy))
        tr = pcbnew.PCB_TRACK(board); tr.SetStart(a); tr.SetEnd(b); tr.SetWidth(MM(width))
        tr.SetLayer(pcbnew.F_Cu); tr.SetNet(pad.GetNet()); board.Add(tr)
        if via:
            v = pcbnew.PCB_VIA(board); v.SetPosition(b); v.SetWidth(MM(0.6)); v.SetDrill(MM(0.3))
            v.SetNet(pad.GetNet()); v.SetViaType(pcbnew.VIATYPE_THROUGH); board.Add(v)

    pcbfile = os.path.join(outdir, name + '.kicad_pcb')
    board.SetFileName(pcbfile)

    if route:
        dsn = os.path.join(outdir, 'route', name + '.dsn')
        ses = os.path.join(outdir, 'route', name + '.ses')
        os.makedirs(os.path.dirname(dsn), exist_ok=True)
        for rnd in range(3):
            assert pcbnew.ExportSpecctraDSN(board, dsn), 'DSN export failed'
            patch_dsn(dsn, design.get('power_nets', {}))
            if os.path.exists(ses):
                os.remove(ses)
            cmd = ['xvfb-run', '-a', 'java', '-jar', os.path.abspath(FREEROUTING), '-de', dsn, '-do', ses,
                   '-mp', str(passes if rnd == 0 else 60), '-dct', '0', '-mt', '0']
            print('routing round', rnd + 1)
            try:
                subprocess.run(cmd, stdout=open(os.path.join(outdir, 'route', 'freerouting.log'), 'a'),
                               stderr=subprocess.STDOUT, timeout=600)
            except subprocess.TimeoutExpired:
                subprocess.run(['pkill', '-9', '-f', 'freerouting.jar -de'])
                print('freerouting timed out in round', rnd + 1)
            if not os.path.exists(ses):
                assert rnd > 0, 'freerouting produced no session file'
                break
            for tr in list(board.GetTracks()):
                board.Remove(tr)
            import_ses(board, ses, dsn, net)
            cleanup_dangling(board)
            board.BuildConnectivity()
            left = board.GetConnectivity().GetUnconnectedCount(True)
            print('unrouted connections:', left)
            if left == 0:
                break

    # ---- ground pours (after routing so the router sees a clean board)
    for (netname, poly) in design.get('pours', []):
        for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
            z = pcbnew.ZONE(board)
            z.SetLayer(layer); z.SetNet(net(netname))
            z.SetLocalClearance(MM(0.3)); z.SetMinThickness(MM(0.25))
            z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
            z.SetThermalReliefGap(MM(0.3)); z.SetThermalReliefSpokeWidth(MM(0.4))
            z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
            o = z.Outline(); o.NewOutline()
            for (px, py) in poly:
                o.Append(MM(px + OFF[0]), MM(py + OFF[1]))
            board.Add(z)
    from make_models import attach
    attach(board)
    pcbnew.SaveBoard(pcbfile, board)
    patch_project(pcbfile.replace('.kicad_pcb', '.kicad_pro'), design)
    board = pcbnew.LoadBoard(pcbfile)
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    pcbnew.SaveBoard(pcbfile, board)
    return board, pcbfile


def patch_project(pro, design):
    import json
    d = json.load(open(pro))
    sev = d['board']['design_settings'].setdefault('rule_severities', {})
    sev['silk_edge_clearance'] = 'ignore'   # connectors sit on the board edge on purpose
    sev['lib_footprint_mismatch'] = 'ignore'  # 3D models replaced on purpose (hardware/lib/3dmodels)
    ns = d.setdefault('net_settings', {})
    classes = ns.setdefault('classes', [])
    base = dict(classes[0]) if classes else {}
    groups = {}
    for n, w in design.get('power_nets', {}).items():
        groups.setdefault(w, []).append(n)
    pats = []
    for w, names in sorted(groups.items()):
        c = dict(base); c['name'] = 'Power_%gmm' % w; c['track_width'] = w; c['clearance'] = 0.2
        classes.append(c)
        pats += [{'netclass': c['name'], 'pattern': n} for n in names]
    ns['netclass_patterns'] = pats
    json.dump(d, open(pro, 'w'), indent=2)


def patch_dsn(path, power_nets):
    """Add a wider net class for power nets so Freerouting routes them wide."""
    t = open(path).read()
    if power_nets:
        groups = {}
        for n, w in power_nets.items():
            groups.setdefault(w, []).append(n)
        cls = ''
        for i, (w, ns) in enumerate(sorted(groups.items())):
            names = ' '.join('"%s"' % n for n in ns)
            cls += '\n    (class power%d %s\n      (circuit (use_via Via[0-1]_600:300_um))\n      (rule (width %d) (clearance 200))\n    )' % (i, names, int(w * 1000))
            # remove these nets from the default class
            for n in ns:
                t = re.sub(r'(\(class kicad_default[^\n]*?)\s"?%s"?(?=[\s)])' % re.escape(n), r'\1', t, flags=re.S)
        t = re.sub(r'(\(network)', r'\1', t)
        idx = t.rfind('(class kicad_default')
        end = _match_paren(t, idx)
        t = t[:end + 1] + cls + t[end + 1:]
    open(path, 'w').write(t)


def _match_paren(t, i):
    depth = 0
    inq = False
    for j in range(i, len(t)):
        ch = t[j]
        if ch == '"':
            inq = not inq
        elif not inq:
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
                if depth == 0:
                    return j
    raise ValueError


def import_ses(board, ses, dsn, net):
    s = parse(open(ses).read())
    def first(lst, key):
        for e in lst:
            if isinstance(e, list) and e and e[0] == key:
                return e
    routes = first(s, 'routes')
    res = first(routes, 'resolution')
    scale = {'um': 1e-3, 'mil': 0.0254, 'mm': 1.0, 'inch': 25.4}[str(res[1])] / float(res[2])  # units -> mm
    vias = {}
    lib = first(routes, 'library_out')
    if lib:
        for ps in lib[1:]:
            if isinstance(ps, list) and ps[0] == 'padstack':
                m = re.search(r'_(\d+):(\d+)_um', ps[1])
                vias[ps[1]] = (int(m.group(1)) / 1000.0, int(m.group(2)) / 1000.0) if m else (0.6, 0.3)
    netout = first(routes, 'network_out')
    ntr = nvia = 0
    layers = {'F.Cu': pcbnew.F_Cu, 'B.Cu': pcbnew.B_Cu}
    for n in netout[1:]:
        if not (isinstance(n, list) and n[0] == 'net'):
            continue
        ni = net(str(n[1]))
        for w in n[2:]:
            if not isinstance(w, list):
                continue
            if w[0] == 'wire':
                path = first(w, 'path')
                layer = layers[str(path[1])]
                width = float(path[2]) * scale
                pts = [float(v) for v in path[3:] if not isinstance(v, list)]
                xy = [(pts[i] * scale, -pts[i + 1] * scale) for i in range(0, len(pts), 2)]
                for a, b in zip(xy, xy[1:]):
                    tr = pcbnew.PCB_TRACK(board)
                    tr.SetStart(pcbnew.VECTOR2I(MM(a[0]), MM(a[1]))); tr.SetEnd(pcbnew.VECTOR2I(MM(b[0]), MM(b[1])))
                    tr.SetWidth(MM(width)); tr.SetLayer(layer); tr.SetNet(ni); board.Add(tr); ntr += 1
            elif w[0] == 'via':
                dia, drill = vias.get(w[1], (0.6, 0.3))
                v = pcbnew.PCB_VIA(board)
                v.SetPosition(pcbnew.VECTOR2I(MM(float(w[2]) * scale), MM(-float(w[3]) * scale)))
                v.SetWidth(MM(dia)); v.SetDrill(MM(drill)); v.SetNet(ni); v.SetViaType(pcbnew.VIATYPE_THROUGH)
                board.Add(v); nvia += 1
    print('imported %d track segments, %d vias' % (ntr, nvia))


def cleanup_dangling(board):
    """Remove vias used on one layer only and track ends that lead nowhere (non-zone nets only)."""
    zone_nets = set()
    for i in range(board.GetAreaCount()):
        zone_nets.add(board.GetArea(i).GetNetname())
    def key(p):
        return (round(p.x / 1000), round(p.y / 1000))
    changed = True
    removed = 0
    while changed:
        changed = False
        ends = {}
        tracks = [t for t in board.GetTracks() if t.GetClass() == 'PCB_TRACK']
        for t in tracks:
            for p in (t.GetStart(), t.GetEnd()):
                ends.setdefault((key(p), t.GetLayer()), []).append(t)
        pads = []
        for fp in board.GetFootprints():
            for pad in fp.Pads():
                pads.append(pad)
        vias = [v for v in board.GetTracks() if v.GetClass() == 'PCB_VIA']
        for v in vias:
            if v.GetNetname() in zone_nets:
                continue
            k = key(v.GetPosition())
            layers = [l for l in (pcbnew.F_Cu, pcbnew.B_Cu) if ends.get((k, l))]
            inpad = any(pad.HitTest(v.GetPosition()) for pad in pads if pad.GetNetname() == v.GetNetname())
            if len(layers) + (1 if inpad else 0) < 2:
                board.Remove(v); removed += 1; changed = True
        if changed:
            continue
        viapos = {key(v.GetPosition()) for v in board.GetTracks() if v.GetClass() == 'PCB_VIA'}
        for t in tracks:
            if t.GetNetname() in zone_nets:
                continue
            for p in (t.GetStart(), t.GetEnd()):
                k = key(p)
                others = [o for o in ends.get((k, t.GetLayer()), []) if o is not t]
                if others or k in viapos:
                    continue
                if any(pad.HitTest(p) and pad.IsOnLayer(t.GetLayer()) for pad in pads if pad.GetNetname() == t.GetNetname()):
                    continue
                board.Remove(t); removed += 1; changed = True
                break
            if changed:
                break
    print('cleanup removed %d dangling items' % removed)


def drc(board, path):
    pcbnew.WriteDRCReport(board, path, pcbnew.EDA_UNITS_MILLIMETRES, True)
    txt = open(path).read()
    summary = re.findall(r'\*\* Found (\d+) ([^*]+?) \*\*', txt)
    return summary, txt
