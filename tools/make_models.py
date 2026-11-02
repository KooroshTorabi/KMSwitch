"""Attach the kmswitch 3D models (STEP, built by make_models3d.py) to the footprints that have none in the KiCad library.\nThe box-model VRML writers below are kept as a lightweight fallback.
VRML units in KiCad are 0.1 inch (2.54 mm). Origin = footprint origin, z = 0 at the board top surface.
usage: python3 make_models.py            -> writes hardware/lib/3dmodels/*.wrl and updates both boards
"""
import os, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'hardware', 'lib', '3dmodels')
U = 1 / 2.54

METAL = (0.78, 0.78, 0.80)
BLACK = (0.10, 0.10, 0.11)
DARK = (0.22, 0.22, 0.24)
GOLD = (0.85, 0.70, 0.30)
PCB_BLUE = (0.10, 0.20, 0.45)
WHITE = (0.92, 0.92, 0.92)


def box(x0, y0, z0, x1, y1, z1, color):
    """Axis-aligned box in mm (KiCad footprint coords: +y = down on the board). VRML y = -footprint y."""
    xs, ys, zs = (x0 * U, x1 * U), (-y0 * U, -y1 * U), (z0 * U, z1 * U)
    pts = [(x, y, z) for z in zs for y in ys for x in xs]
    faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    p = ', '.join('%.4f %.4f %.4f' % q for q in pts)
    f = ', '.join('%d, %d, %d, %d, -1' % fc for fc in faces)
    return ('Shape {\n appearance Appearance { material Material { diffuseColor %.3f %.3f %.3f specularColor 0.3 0.3 0.3 shininess 0.4 } }\n'
            ' geometry IndexedFaceSet { solid FALSE coord Coordinate { point [ %s ] } coordIndex [ %s ] }\n}\n') % (color + (p, f))


def write(name, shapes):
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name + '.wrl'), 'w') as f:
        f.write('#VRML V2.0 utf8\n# kmswitch simplified model: %s\n' % name)
        f.write(''.join(shapes))


def usb_c():
    # HRO TYPE-C-31-M-12: 8.94 x 7.35 x 3.26 mm shell, mouth at +y, solder tails at y = -4.045
    s = [box(-4.47, -3.25, 0.0, 4.47, 4.10, 3.26, METAL),
         box(-3.4, 4.10, 0.75, 3.4, 4.15, 2.5, BLACK)]          # mouth
    for x in (-3.25, -2.45, -1.75, -1.25, -0.75, -0.25, 0.25, 0.75, 1.25, 1.75, 2.45, 3.25):
        s.append(box(x - 0.12, -4.5, 0.0, x + 0.12, -3.25, 0.15, GOLD))
    for x in (-4.32, 4.32):                                       # shell legs
        s.append(box(x - 0.3, -3.6, 0.0, x + 0.3, -2.6, 0.5, METAL))
        s.append(box(x - 0.3, 0.6, 0.0, x + 0.3, 1.5, 0.5, METAL))
    return s


def microsd():
    # Molex 104031-0811: push-pull, 1.42 mm high, card enters from +y
    s = [box(-5.9, -5.0, 0.0, 5.9, 6.0, 1.42, METAL),
         box(-5.6, 5.0, 0.4, 5.6, 6.02, 1.2, BLACK)]             # card slot
    for i in range(8):
        x = -3.105 + i * 1.1
        s.append(box(x - 0.2, -5.9, 0.0, x + 0.2, -5.0, 0.12, GOLD))
    return s


def pts810():
    # C&K PTS810: 4.2 x 3.2 mm body, 2.5 mm total height
    s = [box(-2.1, -1.6, 0.0, 2.1, 1.6, 1.5, BLACK),
         box(-0.9, -0.9, 1.5, 0.9, 0.9, 2.5, WHITE)]
    for x in (-2.35, 2.35):
        for y in (-1.075, 1.075):
            s.append(box(x - 0.3, y - 0.25, 0.0, x + 0.3, y + 0.25, 0.3, METAL))
    return s


def soic8_208():
    # SOIC-8 5.23 x 5.23 mm (208 mil) body, 1.8 mm high
    s = [box(-2.62, -2.62, 0.1, 2.62, 2.62, 1.8, BLACK),
         box(-2.2, -2.2, 1.8, -1.6, -1.6, 1.81, DARK)]          # pin-1 mark
    for i in range(4):
        y = -1.905 + i * 1.27
        for x0, x1 in ((-4.0, -2.62), (2.62, 4.0)):
            s.append(box(x0, y - 0.21, 0.0, x1, y + 0.21, 0.6, METAL))
    return s


def rp2040_zero():
    # Waveshare RP2040-Zero (18 x 23.5 mm) on 2x9 headers, 15.24 mm row spacing; USB-C at the top edge
    hz = 2.54
    s = []
    for x in (-7.62, 7.62):
        s.append(box(x - 1.27, -11.43, 0.0, x + 1.27, 11.43, hz, BLACK))       # header plastic
        for i in range(9):
            y = -10.16 + i * 2.54
            s.append(box(x - 0.32, y - 0.32, -3.0, x + 0.32, y + 0.32, hz + 1.0 + 2.0, GOLD))
    s.append(box(-9.0, -11.75, hz, 9.0, 11.75, hz + 1.0, PCB_BLUE))           # module PCB
    s.append(box(-4.47, -12.3, hz + 1.0, 4.47, -4.95, hz + 1.0 + 3.26, METAL))  # USB-C
    s.append(box(-3.5, -1.5, hz + 1.0, 3.5, 5.5, hz + 1.0 + 0.9, BLACK))       # RP2040
    s.append(box(-1.0, 7.0, hz + 1.0, 1.0, 9.0, hz + 1.0 + 0.8, WHITE))        # WS2812
    return s


MODELS = {
    'USB_C_Receptacle_HRO_TYPE-C-31-M-12': ('kmswitch_USB-C_HRO_TYPE-C-31-M-12', usb_c),
    'microSD_HC_Molex_104031-0811': ('kmswitch_microSD_Molex_104031-0811', microsd),
    'SW_SPST_PTS810': ('kmswitch_SW_PTS810', pts810),
    'SOIC-8_5.23x5.23mm_P1.27mm': ('kmswitch_SOIC-8_5.23x5.23mm', soic8_208),
    'RP2040-Zero_Module': ('kmswitch_RP2040-Zero_on_headers', rp2040_zero),
}


def model_path(fpname):
    return '${KIPRJMOD}/../lib/3dmodels/%s.step' % MODELS[fpname][0]


def attach(board):
    """Replace the 3D model of every footprint listed in MODELS with our model."""
    import pcbnew
    n = 0
    for fp in board.GetFootprints():
        name = str(fp.GetFPID().GetLibItemName())
        if name in MODELS:
            fp.Models().clear()
            m = pcbnew.FP_3DMODEL()
            m.m_Filename = model_path(name)
            fp.Models().push_back(m)
            n += 1
    return n


if __name__ == '__main__':
    # detailed STEP models come from make_models3d.py; this script only attaches them
    print('attaching models from', OUT)
    import pcbnew
    for f in ('hardware/variant-a-rp2040-zero/kmswitch-a.kicad_pcb', 'hardware/variant-b-rp2040/kmswitch-b.kicad_pcb'):
        p = os.path.join(ROOT, f)
        b = pcbnew.LoadBoard(p)
        print(f, 'models attached:', attach(b))
        pcbnew.SaveBoard(p, b)
