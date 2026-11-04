"""Write the readable schematic for one variant and check it against the design netlist.
usage: python3 sch_build.py a|b [--png out.png]
"""
import sys, os, importlib, subprocess
sys.path.insert(0, os.path.dirname(__file__))
os.environ.setdefault('KICAD7_SYMBOL_DIR', '/usr/share/kicad/symbols')
from schgen import SymbolLibs
from schpretty import Sheet, check
from build import VARIANTS, ROOT, LIBDIR


def main(v, png=None):
    folder, name, title = VARIANTS[v]
    design = importlib.import_module('design_' + v)
    lay = importlib.import_module('sch_' + v)
    out = os.path.join(ROOT, 'hardware', folder)
    schfile = os.path.join(out, name + '.kicad_sch')
    libs = SymbolLibs({'kmswitch': os.path.join(LIBDIR, 'kmswitch.kicad_sym')})
    paper, w, h = lay.PAPER
    sh = Sheet(name, libs, design.comps, title, paper, (w, h), keep_path=schfile)
    lay.draw(sh)
    sh.finish()
    missing = [c['ref'] for c in design.comps if c['ref'] not in sh.symbol_uuids]
    assert not missing, 'not placed: %s' % missing
    tmp = schfile + '.new'
    sh.write(tmp)
    work = os.path.join(out, 'route'); os.makedirs(work, exist_ok=True)
    errs = check(tmp, design.comps, work)
    for e in errs[:40]:
        print('  ', e)
    print('netlist check: %d problems' % len(errs))
    if errs:
        raise SystemExit(1)
    os.replace(tmp, schfile)
    print('written', schfile)
    if png:
        d = '/tmp/schsvg_' + v
        subprocess.run(['kicad-cli', 'sch', 'export', 'svg', '-o', d, schfile], capture_output=True)
        subprocess.run(['rsvg-convert', '-b', 'white', '-w', '4000', os.path.join(d, name + '.svg'), '-o', png])
    return sh


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[sys.argv.index('--png') + 1] if '--png' in sys.argv else None)
