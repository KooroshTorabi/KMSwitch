"""Fabrication outputs for one variant: gerbers+drill zip, BOM, JLCPCB CPL, schematic PDF, board renders.
usage: python3 export.py a|b
"""
import sys, os, csv, subprocess, zipfile, glob, importlib, shutil
sys.path.insert(0, os.path.dirname(__file__))
from build import VARIANTS, ROOT


# LCSC / JLCPCB part numbers for parts whose design entry has no LCSC field.
# key: (value, footprint name without library) -> (LCSC #, note)
LCSC = {
    ('100uF 10V', 'CP_Elec_6.3x7.7'): ('C294520', 'VT1V101M-CRE77, 100uF 35V 6.3x7.7 (higher voltage is fine)'),
    ('100nF', 'C_0805_2012Metric'): ('C49678', ''),
    ('10uF', 'C_0805_2012Metric'): ('C15850', ''),
    ('100nF', 'C_0603_1608Metric'): ('C14663', ''),
    ('15pF', 'C_0603_1608Metric'): ('C1644', ''),
    ('1uF', 'C_0603_1608Metric'): ('C15849', ''),
    ('B5819W', 'D_SOD-123'): ('C8598', ''),
    ('green', 'LED_0603_1608Metric'): ('C12624', 'KT-0603G'),
    ('2A hold (1812)', 'Fuse_1812_4532Metric'): ('C545213', 'SMD1812P200TF/16, 2A hold 16V'),
    ('104031-0811', 'microSD_HC_Molex_104031-0811'): ('C585350', ''),
    ('5.1k', 'R_0805_2012Metric'): ('C27834', ''),
    ('22R', 'R_0805_2012Metric'): ('C17561', ''),
    ('10k', 'R_0805_2012Metric'): ('C17414', ''),
    ('330R', 'R_0805_2012Metric'): ('C17630', ''),
    ('22R', 'R_0603_1608Metric'): ('C23345', ''),
    ('10k', 'R_0603_1608Metric'): ('C25804', ''),
    ('330R', 'R_0603_1608Metric'): ('C23138', ''),
    ('1k', 'R_0603_1608Metric'): ('C21190', ''),
    ('27R', 'R_0603_1608Metric'): ('C25190', '0603WAF270JT5E'),
    ('5.1k', 'R_0603_1608Metric'): ('C23186', ''),
    ('20k', 'R_0603_1608Metric'): ('C4184', ''),
    ('BOOTSEL', 'SW_SPST_PTS810'): ('C116501', 'PTS810SJM250SMTRLFS'),
    ('RESET', 'SW_SPST_PTS810'): ('C116501', 'PTS810SJM250SMTRLFS'),
}


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        raise SystemExit('failed: ' + ' '.join(cmd))
    return r.stdout


def main(v):
    folder, name, title = VARIANTS[v]
    mod = importlib.import_module('design_' + v)
    d = os.path.join(ROOT, 'hardware', folder)
    pcb, sch = os.path.join(d, name + '.kicad_pcb'), os.path.join(d, name + '.kicad_sch')
    fab = os.path.join(d, 'fab')
    shutil.rmtree(fab, ignore_errors=True)
    g = os.path.join(fab, 'gerbers')
    os.makedirs(g)
    run(['kicad-cli', 'pcb', 'export', 'gerbers', '--layers',
         'F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts', '-o', g + '/', pcb])
    run(['kicad-cli', 'pcb', 'export', 'drill', '--format', 'excellon', '--excellon-separate-th',
         '-o', g + '/', pcb])
    zp = os.path.join(fab, name + '-gerbers.zip')
    with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(glob.glob(os.path.join(g, '*'))):
            z.write(f, os.path.basename(f))
    # placement
    pos = os.path.join(fab, name + '-pos.csv')
    run(['kicad-cli', 'pcb', 'export', 'pos', '--format', 'csv', '--units', 'mm', '--side', 'both', '-o', pos, pcb])
    rows = list(csv.DictReader(open(pos)))
    smd_refs = set()
    with open(os.path.join(fab, name + '-cpl-jlcpcb.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
        for r in rows:
            if r['Ref'].startswith(('H', 'TP')):
                continue
            w.writerow([r['Ref'], r['PosX'] + 'mm', r['PosY'] + 'mm', 'Top' if r['Side'] == 'top' else 'Bottom', r['Rot']])
            smd_refs.add(r['Ref'])
    # BOM grouped by value + footprint
    groups = {}
    for c in mod.comps:
        if c['ref'].startswith(('H', 'TP')):
            continue
        key = (c['value'], c['footprint'], c.get('fields', {}).get('LCSC', ''))
        groups.setdefault(key, []).append(c['ref'])
    def natkey(r):
        import re
        m = re.match(r'([A-Z]+)(\d+)', r)
        return (m.group(1), int(m.group(2))) if m else (r, 0)
    with open(os.path.join(fab, name + '-bom.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Comment', 'Designator', 'Footprint', 'Quantity', 'LCSC Part #', 'Notes'])
        for (val, fp, lcsc), refs in sorted(groups.items(), key=lambda kv: natkey(sorted(kv[1], key=natkey)[0])):
            refs = sorted(refs, key=natkey)
            note = ''
            fpn = fp.split(':')[1]
            if not lcsc and (val, fpn) in LCSC:
                lcsc, note = LCSC[(val, fpn)]
            if 'RP2040-Zero' in fp:
                lcsc, note = 'n/a', 'Waveshare RP2040-Zero module (not on LCSC) + 2x 1x9 pin headers, hand-solder'
            elif 'USB_A' in fp:
                lcsc, note = 'n/a', 'Molex 67643-0910 through-hole (Mouser/DigiKey/TME), hand-solder'
            elif 'PinHeader' in fp:
                lcsc, note = 'n/a', 'optional 3-pin header, not assembled (DNP)'
            if not lcsc:
                raise SystemExit('no LCSC number for %s %s %s' % (val, fpn, refs))
            w.writerow([val, ','.join(refs), fp.split(':')[1], len(refs), lcsc, note])
    # schematic pdf + renders
    run(['kicad-cli', 'sch', 'export', 'pdf', '-o', os.path.join(fab, name + '-schematic.pdf'), sch])
    for side, layers in (('front', 'F.Cu,F.SilkS,F.Mask,Edge.Cuts'), ('back', 'B.Cu,B.SilkS,B.Mask,Edge.Cuts')):
        svg = os.path.join(fab, '%s-%s.svg' % (name, side))
        args = ['kicad-cli', 'pcb', 'export', 'svg', '--layers', layers, '--page-size-mode', '2',
                '--exclude-drawing-sheet', '-o', svg, pcb]
        if side == 'back':
            args.insert(4, '--mirror')
        run(args)
        run(['rsvg-convert', '-b', 'white', '-w', '1600', svg, '-o', svg.replace('.svg', '.png')])
        os.remove(svg)
    shutil.rmtree(g)
    print('exported', fab, sorted(os.listdir(fab)))


if __name__ == '__main__':
    main(sys.argv[1])
