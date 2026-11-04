"""Create the project symbol + footprint library (hardware/lib)."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from sexpr import dump, Sym as S

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
LIB = os.path.join(ROOT, 'hardware', 'lib')
os.makedirs(os.path.join(LIB, 'kmswitch.pretty'), exist_ok=True)

F = [S('effects'), [S('font'), [S('size'), 1.27, 1.27]]]
FH = [S('effects'), [S('font'), [S('size'), 1.27, 1.27]], S('hide')]

def pin(typ, x, y, ang, name, num, length=2.54):
    return [S('pin'), S(typ), S('line'), [S('at'), x, y, ang], [S('length'), length],
            [S('name'), name, F], [S('number'), num, F]]

def symbol(name, ref, value, fp, desc, rect, pins, keywords=''):
    x0, y0, x1, y1 = rect
    return [S('symbol'), name, [S('pin_names'), [S('offset'), 1.016]], [S('in_bom'), S('yes')], [S('on_board'), S('yes')],
            [S('property'), 'Reference', ref, [S('at'), x0, y0 + 1.27, 0], [S('effects'), [S('font'), [S('size'), 1.27, 1.27]], [S('justify'), S('left'), S('bottom')]]],
            [S('property'), 'Value', value, [S('at'), x0, y1 - 1.27, 0], [S('effects'), [S('font'), [S('size'), 1.27, 1.27]], [S('justify'), S('left'), S('top')]]],
            [S('property'), 'Footprint', fp, [S('at'), 0, 0, 0], FH],
            [S('property'), 'Datasheet', '~', [S('at'), 0, 0, 0], FH],
            [S('property'), 'ki_description', desc, [S('at'), 0, 0, 0], FH],
            [S('property'), 'ki_keywords', keywords, [S('at'), 0, 0, 0], FH],
            [S('symbol'), name + '_0_1', [S('rectangle'), [S('start'), x0, y0], [S('end'), x1, y1],
                                         [S('stroke'), [S('width'), 0.254], [S('type'), S('default')]], [S('fill'), [S('type'), S('background')]]]],
            [S('symbol'), name + '_1_1'] + pins]

# --- RP2040-Zero: pad numbering anticlockwise from the USB connector (NuttX board docs)
left = [('1', '5V', 'power_in'), ('2', 'GND', 'power_in'), ('3', '3V3', 'power_out'), ('4', 'GP29', 'bidirectional'),
        ('5', 'GP28', 'bidirectional'), ('6', 'GP27', 'bidirectional'), ('7', 'GP26', 'bidirectional'),
        ('8', 'GP15', 'bidirectional'), ('9', 'GP14', 'bidirectional')]
right = [('23', 'GP0'), ('22', 'GP1'), ('21', 'GP2'), ('20', 'GP3'), ('19', 'GP4'), ('18', 'GP5'),
         ('17', 'GP6'), ('16', 'GP7'), ('15', 'GP8')]
zpins = []
for i, (num, name, typ) in enumerate(left):
    zpins.append(pin(typ, -15.24, 10.16 - i * 2.54, 0, name, num))
for i, (num, name) in enumerate(right):
    zpins.append(pin('bidirectional', 15.24, 10.16 - i * 2.54, 180, name, num))
zero = symbol('RP2040-Zero', 'MOD', 'RP2040-Zero', 'kmswitch:RP2040-Zero_Module',
              'Waveshare RP2040-Zero module, 2x9 edge pads (bottom row GP9-GP13 not used)',
              (-12.7, 12.7, 12.7, -12.7), zpins, 'RP2040 module waveshare')

# --- microSD socket Molex 104031-0811 incl. card-detect switch and shield
sd_l = [('1', 'DAT2'), ('2', 'DAT3/CD'), ('3', 'CMD'), ('4', 'VDD'), ('5', 'CLK'), ('6', 'VSS'), ('7', 'DAT0'), ('8', 'DAT1')]
sdp = []
for i, (num, name) in enumerate(sd_l):
    typ = 'power_in' if name in ('VDD', 'VSS') else 'bidirectional'
    sdp.append(pin(typ, -12.7, 8.89 - i * 2.54, 0, name, num))
sdp.append(pin('passive', 12.7, 5.08, 180, 'DET_A', '9'))
sdp.append(pin('passive', 12.7, 2.54, 180, 'DET_B', '10'))
sdp.append(pin('passive', 12.7, -7.62, 180, 'SHIELD', '11'))
sd = symbol('MicroSD_Molex_104031', 'J', '104031-0811', 'Connector_Card:microSD_HC_Molex_104031-0811',
            'microSD socket with card-detect switch (pads 9/10) and shield (pad 11)', (-10.16, 11.43, 10.16, -11.43), sdp,
            'microSD card socket')

from schpretty import power_symbols, all_power_nets
lib = [S('kicad_symbol_lib'), [S('version'), 20220914], [S('generator'), S('kicad_symbol_editor')], zero, sd] + power_symbols(all_power_nets())
open(os.path.join(LIB, 'kmswitch.kicad_sym'), 'w').write(dump(lib) + '\n')

# --- RP2040-Zero footprint (module on 2.54 mm headers, 15.24 mm row spacing)
W, H = 18.0, 23.5
fp = [S('footprint'), 'RP2040-Zero_Module', [S('version'), 20221018], [S('generator'), S('pcbnew')],
      [S('layer'), 'F.Cu'],
      [S('descr'), 'Waveshare RP2040-Zero (18x23.5 mm) on 2x9 pin headers, 15.24 mm row spacing. USB-C at top edge. Verify against your module before ordering.'],
      [S('tags'), 'RP2040 Zero module'], [S('attr'), S('through_hole')],
      [S('fp_text'), S('reference'), 'REF**', [S('at'), 0, 13.0], [S('layer'), 'F.SilkS'],
       [S('effects'), [S('font'), [S('size'), 1, 1], [S('thickness'), 0.15]]]],
      [S('fp_text'), S('value'), 'RP2040-Zero', [S('at'), 0, 0], [S('layer'), 'F.Fab'],
       [S('effects'), [S('font'), [S('size'), 1, 1], [S('thickness'), 0.15]]]],
      [S('fp_text'), S('user'), 'USB-C', [S('at'), 0, -9.5], [S('layer'), 'F.SilkS'],
       [S('effects'), [S('font'), [S('size'), 0.8, 0.8], [S('thickness'), 0.12]]]]]
# silkscreen: sides and bottom only (the top edge sits on the board edge)
for (a, b) in (((-W / 2 - 0.12, -H / 2 + 1.0), (-W / 2 - 0.12, H / 2 + 0.12)), ((-W / 2 - 0.12, H / 2 + 0.12), (W / 2 + 0.12, H / 2 + 0.12)),
               ((W / 2 + 0.12, H / 2 + 0.12), (W / 2 + 0.12, -H / 2 + 1.0))):
    fp.append([S('fp_line'), [S('start'), a[0], a[1]], [S('end'), b[0], b[1]], [S('stroke'), [S('width'), 0.12], [S('type'), S('solid')]], [S('layer'), 'F.SilkS']])
for layer, inset, wdt in (('F.Fab', 0, 0.1), ('F.CrtYd', -0.25, 0.05)):
    fp.append([S('fp_rect'), [S('start'), -W / 2 + inset, -H / 2 + inset], [S('end'), W / 2 - inset, H / 2 - inset],
               [S('stroke'), [S('width'), wdt], [S('type'), S('solid')]], [S('fill'), S('none')], [S('layer'), layer]])
fp.append([S('fp_rect'), [S('start'), -4.5, -H / 2 - 1.0], [S('end'), 4.5, -H / 2 + 6.5],
           [S('stroke'), [S('width'), 0.1], [S('type'), S('solid')]], [S('fill'), S('none')], [S('layer'), 'F.Fab']])
for i, (num, name, typ) in enumerate(left):
    shape = S('rect') if num == '1' else S('circle')
    fp.append([S('pad'), num, S('thru_hole'), shape, [S('at'), -7.62, -10.16 + i * 2.54], [S('size'), 1.7, 1.7],
               [S('drill'), 1.0], [S('layers'), '*.Cu', '*.Mask']])
for i, (num, name) in enumerate(right):
    fp.append([S('pad'), num, S('thru_hole'), S('circle'), [S('at'), 7.62, -10.16 + i * 2.54], [S('size'), 1.7, 1.7],
               [S('drill'), 1.0], [S('layers'), '*.Cu', '*.Mask']])
open(os.path.join(LIB, 'kmswitch.pretty', 'RP2040-Zero_Module.kicad_mod'), 'w').write(dump(fp) + '\n')
print('library written to', LIB)
