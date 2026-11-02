"""Build one board variant: schematic, project, routed PCB, DRC, fabrication outputs.
usage: python3 build.py a|b [--no-route]
"""
import faulthandler; faulthandler.enable()
import sys, os, json, subprocess, importlib, shutil, re, xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(__file__))
from schgen import SymbolLibs, Schematic
os.environ.setdefault('KICAD7_FOOTPRINT_DIR', '/usr/share/kicad/footprints')
os.environ.setdefault('KICAD7_SYMBOL_DIR', '/usr/share/kicad/symbols')
import pcbgen

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
LIBDIR = os.path.join(ROOT, 'hardware', 'lib')
VARIANTS = {'a': ('variant-a-rp2040-zero', 'kmswitch-a', 'kmswitch - variant A (RP2040-Zero carrier)'),
            'b': ('variant-b-rp2040', 'kmswitch-b', 'kmswitch - variant B (2x RP2040 chip-down)')}


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        raise SystemExit('command failed: ' + ' '.join(cmd))
    return r.stdout


def main(v, route=True):
    folder, name, title = VARIANTS[v]
    mod = importlib.import_module('design_' + v)
    out = os.path.join(ROOT, 'hardware', folder)
    os.makedirs(out, exist_ok=True)

    # --- project files
    json.dump({"meta": {"filename": name + ".kicad_pro", "version": 1}}, open(os.path.join(out, name + '.kicad_pro'), 'w'), indent=2)
    open(os.path.join(out, 'sym-lib-table'), 'w').write(
        '(sym_lib_table\n  (lib (name "kmswitch")(type "KiCad")(uri "${KIPRJMOD}/../lib/kmswitch.kicad_sym")(options "")(descr "kmswitch project symbols"))\n)\n')
    open(os.path.join(out, 'fp-lib-table'), 'w').write(
        '(fp_lib_table\n  (lib (name "kmswitch")(type "KiCad")(uri "${KIPRJMOD}/../lib/kmswitch.pretty")(options "")(descr "kmswitch project footprints"))\n)\n')

    # --- schematic
    libs = SymbolLibs({'kmswitch': os.path.join(LIBDIR, 'kmswitch.kicad_sym')})
    paper, pw, ph = getattr(mod, 'PAPER', ('A2', 594, 420))
    sch = Schematic(name, libs, title=title, paper=paper, page_w=pw, page_h=ph)
    y = sch.layout(mod.blocks, cols=getattr(mod, 'COLS', 1))
    fx, fy = sch.last_col_x, sch.last_col_y
    sch.add_text('Power flags', fx, fy)
    sch.add_pwr_flags(mod.pwr_flags, fx + 10, fy + 10)
    y = max(y, fy + 30)
    schfile = os.path.join(out, name + '.kicad_sch')
    sch.write(schfile)
    print('schematic written', schfile, 'bottom y=%.0f of %d' % (y, ph))
    assert y < ph - 15, 'schematic does not fit the sheet'

    # --- netlist parity check (schematic vs design description)
    netxml = os.path.join(out, 'route', name + '.net.xml')
    os.makedirs(os.path.dirname(netxml), exist_ok=True)
    run(['kicad-cli', 'sch', 'export', 'netlist', '--format', 'kicadxml', '-o', netxml, schfile])
    tree = ET.parse(netxml)
    got = {}
    for n in tree.iter('net'):
        nm = n.get('name').lstrip('/')
        for node in n.iter('node'):
            got[(node.get('ref'), node.get('pin'))] = nm
    errs = []
    for c in mod.comps:
        for pin, net in c['pins'].items():
            g = got.get((c['ref'], pin))
            if net and g != net:
                errs.append((c['ref'], pin, net, g))
            if not net and g and not g.startswith('unconnected-') and not g.startswith('Net-('):
                errs.append((c['ref'], pin, None, g))
    print('netlist parity errors:', len(errs))
    for e in errs[:20]:
        print('  ', e)
    assert not errs

    # --- pcb
    board, pcbfile = pcbgen.build(mod.design, out, name, os.path.join(LIBDIR, 'kmswitch.pretty'),
                                  sym_uuids=sch.symbol_uuids, route=route, passes=getattr(mod, 'PASSES', 40))
    summary, txt = pcbgen.drc(board, os.path.join(out, 'route', 'drc.rpt'))
    print('DRC:', summary)
    return out, name


if __name__ == '__main__':
    main(sys.argv[1], route='--no-route' not in sys.argv)
