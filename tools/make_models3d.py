"""Detailed 3D models (STEP with colours) for parts that have no model in the KiCad library.
Built with CadQuery. Coordinates: footprint origin, X = footprint x, Y = -footprint y (KiCad model convention),
Z up from the board's top surface. Dimensions follow the manufacturers' drawings (rounded to 0.05 mm).
usage: python3 make_models3d.py   -> hardware/lib/3dmodels/*.step (+ preview STLs in /tmp/models_preview)
"""
import os, math
import cadquery as cq

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'hardware', 'lib', '3dmodels')
PREVIEW = '/tmp/models_preview'

C = {
    'metal': cq.Color(0.80, 0.81, 0.83),
    'gold': cq.Color(0.86, 0.72, 0.36),
    'body': cq.Color(0.13, 0.13, 0.14),
    'black': cq.Color(0.05, 0.05, 0.06),
    'grey': cq.Color(0.45, 0.45, 0.47),
    'white': cq.Color(0.94, 0.94, 0.92),
    'pcb': cq.Color(0.06, 0.07, 0.08),
    'silk': cq.Color(0.95, 0.95, 0.95),
    'cap': cq.Color(0.55, 0.42, 0.30),
    'res': cq.Color(0.10, 0.10, 0.10),
    'led': cq.Color(0.97, 0.97, 0.90),
    'mark': cq.Color(0.62, 0.62, 0.62),
}


def Y(y):
    """footprint y -> model y"""
    return -y


def box(x0, y0, z0, x1, y1, z1):
    """box between footprint coords (x0,y0)-(x1,y1), z0..z1"""
    xa, xb = sorted((x0, x1)); ya, yb = sorted((Y(y0), Y(y1)))
    return cq.Workplane('XY').box(xb - xa, yb - ya, z1 - z0, centered=False).translate((xa, ya, z0))


def cyl(x, y, z0, z1, d):
    return cq.Workplane('XY').circle(d / 2).extrude(z1 - z0).translate((x, Y(y), z0))


def text(s, x, y, z, size, rot=0):
    return (cq.Workplane('XY').text(s, size, 0.02, halign='center', valign='center', font='Sans')
            .rotate((0, 0, 0), (0, 0, 1), rot).translate((x, Y(y), z)))


class Model:
    def __init__(self, name):
        self.name = name
        self.parts = {}

    def add(self, color, shape):
        self.parts[color] = self.parts[color].union(shape) if color in self.parts else shape

    def assembly(self, tf=None):
        a = cq.Assembly(name=self.name)
        for i, (col, shp) in enumerate(self.parts.items()):
            a.add(shp, name='%s_%d_%s' % (self.name, i, col), color=C[col])
        return a

    def save(self):
        os.makedirs(OUT, exist_ok=True); os.makedirs(PREVIEW, exist_ok=True)
        self.assembly().save(os.path.join(OUT, self.name + '.step'), exportType='STEP')
        for col, shp in self.parts.items():
            cq.exporters.export(shp, os.path.join(PREVIEW, '%s__%s.stl' % (self.name, col)), tolerance=0.02, angularTolerance=0.15)


def gull_lead(x_body, x_tip, y, w, t=0.2, z_exit=0.75, side=1):
    """gull-wing lead in the XZ plane, leaving the body at x_body and ending at x_tip (side = +1 right, -1 left)"""
    xb, xt = abs(x_body), abs(x_tip)
    k = (xt - xb)
    p = [(xb - 0.1, z_exit), (xb + 0.25 * k, z_exit), (xb + 0.5 * k, 0.0), (xt, 0.0),
         (xt, t), (xb + 0.5 * k + 0.15, t), (xb + 0.25 * k + 0.12, z_exit + t), (xb - 0.1, z_exit + t)]
    if side < 0:
        p = [(-a, b) for a, b in p]
    return cq.Workplane('XZ').polyline(p).close().extrude(w).translate((0, Y(y) + w / 2, 0))


# ----------------------------------------------------------------------------------------------
def usb_c_hro(m=None, dx=0.0, dy=0.0, rot=0, z0=0.0, tails=True):
    """HRO TYPE-C-31-M-12 (16 pin, top mount). Mouth at +footprint-y."""
    own = m is None
    m = m or Model('kmswitch_USB-C_HRO_TYPE-C-31-M-12')
    W, Hh, rear, front = 8.94, 3.26, -3.25, 4.10
    def place(s):
        return s.rotate((0, 0, 0), (0, 0, 1), rot).translate((dx, Y(dy), z0))
    shell = (cq.Workplane('XY').box(W, front - rear, Hh).edges('|Y').fillet(1.55)
             .translate((0, Y((rear + front) / 2), Hh / 2)))
    cavity = (cq.Workplane('XY').box(W - 0.5, front - rear - 0.35, Hh - 0.5).edges('|Y').fillet(1.3)
              .translate((0, Y((rear + front) / 2) - 0.2, Hh / 2)))
    shell = shell.cut(cavity)
    # pressed seam lines and the two latch windows on top
    for sx in (-2.2, 2.2):
        shell = shell.cut(box(sx - 0.45, 0.2, Hh - 0.3, sx + 0.45, 1.4, Hh + 0.1))
    # rear tabs and through-hole legs
    for sx in (-4.32, 4.32):
        m.add('metal', place(box(sx - 0.15, -3.55, -1.6, sx + 0.15, -2.7, 0.6)))
        m.add('metal', place(box(sx - 0.15, 0.55, -1.6, sx + 0.15, 1.55, 0.6)))
    m.add('metal', place(shell))
    # insulator + tongue
    m.add('body', place(box(-W / 2 + 0.3, -3.1, 0.3, W / 2 - 0.3, -2.4, Hh - 0.3)))
    m.add('body', place(box(-2.9, -2.4, Hh / 2 - 0.35, 2.9, 2.9, Hh / 2 + 0.35)))
    for i in range(12):
        x = -2.75 + i * 0.5
        for z in (Hh / 2 + 0.35, Hh / 2 - 0.37):
            m.add('gold', place(box(x - 0.12, 0.4, z, x + 0.12, 2.7, z + 0.02)))
    if tails:
        for x in (-3.25, -2.45, -1.75, -1.25, -0.75, -0.25, 0.25, 0.75, 1.25, 1.75, 2.45, 3.25):
            w = 0.5 if abs(x) > 2 else 0.25
            m.add('gold', place(box(x - w / 2, -4.55, 0.0, x + w / 2, -3.25, 0.12)))
        for sx in (-2.89, 2.89):
            m.add('body', place(cyl(sx, -2.6, -0.8, 0.3, 0.5)))
    if own:
        m.save()
    return m


def microsd_molex():
    """Molex 104031-0811 microSD socket, push-pull, 1.42 mm high. Card enters from +footprint-y."""
    m = Model('kmswitch_microSD_Molex_104031-0811')
    x0, x1, yr, yf, H = -5.6, 5.6, -5.55, 5.45, 1.42
    # black plastic base: floor, rear wall, side rails
    m.add('body', box(x0 + 0.2, yr + 0.3, 0.0, x1 - 0.2, yf - 0.6, 0.25))
    m.add('body', box(x0 + 0.2, yr + 0.2, 0.0, x1 - 0.2, yr + 1.1, 1.3))
    m.add('body', box(x0 + 0.2, yr + 0.2, 0.0, x0 + 0.85, yf - 0.4, 1.3))
    m.add('body', box(x1 - 0.85, yr + 0.2, 0.0, x1 - 0.2, yf - 0.4, 1.3))
    # spring contacts on the floor
    for i in range(8):
        x = -3.105 + i * 1.1
        m.add('gold', box(x - 0.2, yr + 1.1, 0.25, x + 0.2, yr + 4.2, 0.32))
        m.add('gold', box(x - 0.2, yr + 3.6, 0.32, x + 0.2, yr + 4.2, 0.55))
        m.add('gold', box(x - 0.25 if i < 7 else x - 0.2, yr - 0.35, 0.0, x + 0.25 if i < 7 else x + 0.2, yr + 0.3, 0.1))
    # stamped steel cover with side skirts and a bent front lip
    cover = box(x0, yr, H - 0.1, x1, yf, H)
    for (cx, cy, w, l) in ((-2.6, -1.2, 1.4, 4.2), (0.0, -1.2, 1.4, 4.2), (2.6, -1.2, 1.4, 4.2)):
        cover = cover.cut(box(cx - w / 2 + 0.25, cy - l / 2, H - 0.2, cx + w / 2 - 0.25, cy + l / 2, H + 0.1))
    m.add('metal', cover)
    for (cx, cy) in ((-2.6, -1.2), (0.0, -1.2), (2.6, -1.2)):
        m.add('metal', box(cx - 0.45, cy - 2.1, H - 0.18, cx + 0.45, cy + 2.1, H - 0.1))
    m.add('metal', box(x0, yr, 0.25, x0 + 0.1, yf - 0.4, H))
    m.add('metal', box(x1 - 0.1, yr, 0.25, x1, yf - 0.4, H))
    m.add('metal', box(x0 + 0.6, yf - 0.1, 0.85, x1 - 0.6, yf, H))
    # shield and card-detect solder tabs
    for (tx, ty, w, l) in ((-5.565, -5.325, 1.4, 1.2), (5.755, -5.1, 1.0, 1.6), (3.73, 5.375, 1.7, 1.2), (-2.24, 5.375, 1.7, 1.2)):
        m.add('metal', box(tx - w / 2, ty - l / 2, 0.0, tx + w / 2, ty + l / 2, 0.12))
    for ty in (0.7, 4.4):
        m.add('metal', box(-6.25, ty - 0.4, 0.0, -5.5, ty + 0.4, 0.12))
        m.add('metal', box(-5.65, ty - 0.4, 0.0, -5.5, ty + 0.4, 0.9))
    m.save()


def pts810():
    """C&K PTS810 SJM 250 SMTR: 4.2 x 3.2 mm, 2.5 mm high."""
    m = Model('kmswitch_SW_PTS810')
    m.add('body', box(-2.1, -1.6, 0.0, 2.1, 1.6, 1.3).edges('|Z').fillet(0.2))
    plate = box(-2.1, -1.6, 1.3, 2.1, 1.6, 1.45).cut(cyl(0, 0, 1.2, 1.6, 2.0))
    m.add('metal', plate)
    m.add('metal', box(-2.15, -1.65, 0.6, -2.05, 1.65, 1.45))
    m.add('metal', box(2.05, -1.65, 0.6, 2.15, 1.65, 1.45))
    act = cyl(0, 0, 1.0, 2.5, 1.7).faces('>Z').edges().fillet(0.25)
    m.add('black', act)
    for sx in (-1, 1):
        for y in (-1.075, 1.075):
            m.add('metal', gull_lead(2.0, 2.55, y, 0.45, t=0.15, z_exit=0.35, side=sx))
    m.save()


def soic8_208(m=None, marking=('W25Q16JV', 'SSIQ')):
    """SOIC-8, 5.23 x 5.23 mm (208 mil) body, 1.27 mm pitch, 7.9 mm lead span."""
    own = m is None
    m = m or Model('kmswitch_SOIC-8_5.23x5.23mm')
    body = (cq.Workplane('XY').box(5.23, 5.23, 1.75).edges('>Z').chamfer(0.15).translate((0, 0, 0.1 + 1.75 / 2)))
    body = body.cut(cyl(-1.85, -1.85, 1.8, 1.95, 0.7))
    m.add('body', body)
    m.add('mark', text(marking[0], 0, -0.3, 1.85, 0.75))
    m.add('mark', text(marking[1], 0, 0.8, 1.85, 0.7))
    for i in range(4):
        y = -1.905 + i * 1.27
        m.add('metal', gull_lead(2.615, 3.95, y, 0.42, side=1))
        m.add('metal', gull_lead(2.615, 3.95, y, 0.42, side=-1))
    if own:
        m.save()
    return m


def rp2040_zero():
    """Waveshare RP2040-Zero (18 x 23.5 x 1 mm) on two 1x9 pin headers, 15.24 mm row spacing."""
    m = Model('kmswitch_RP2040-Zero_on_headers')
    hz, t = 2.54, 1.0
    ztop = hz + t
    # headers
    for x in (-7.62, 7.62):
        for i in range(9):
            y = -10.16 + i * 2.54
            m.add('black', box(x - 1.25, y - 1.25, 0.0, x + 1.25, y + 1.25, hz).edges('|Z').chamfer(0.25))
            m.add('gold', box(x - 0.32, y - 0.32, -3.0, x + 0.32, y + 0.32, ztop + 1.4))
    # module PCB with plated holes and bottom castellations
    pcb = box(-9.0, -11.75, hz, 9.0, 11.75, ztop).edges('|Z').fillet(0.5)
    for i in range(5):
        pcb = pcb.cut(cyl(-5.08 + i * 2.54, 11.75, hz - 0.1, ztop + 0.1, 1.0))
    m.add('pcb', pcb)
    for x in (-7.62, 7.62):
        for i in range(9):
            y = -10.16 + i * 2.54
            m.add('gold', cyl(x, y, ztop, ztop + 0.03, 1.6).cut(cyl(x, y, ztop - 0.1, ztop + 0.1, 0.7)))
    for i in range(5):
        x = -5.08 + i * 2.54
        m.add('gold', cyl(x, 11.75, hz, ztop + 0.03, 1.4).cut(cyl(x, 11.75, hz - 0.1, ztop + 0.1, 1.0)).cut(box(x - 1, 11.75, hz - 0.1, x + 1, 13, ztop + 0.1)))
    # USB-C on the top edge (mouth outwards)
    usb_c_hro(m, dx=0.0, dy=-7.85, rot=180, z0=ztop, tails=False)
    # RP2040 QFN-56 7x7
    m.add('body', box(-3.5, -1.0, ztop, 3.5, 6.0, ztop + 0.85))
    m.add('mark', text('RP2-B2', 0, 2.0, ztop + 0.85, 0.9))
    m.add('mark', text('RP2040', 0, 3.4, ztop + 0.85, 0.8))
    m.add('grey', cyl(-2.8, -0.3, ztop + 0.85, ztop + 0.87, 0.5))
    # flash, LDO, crystal, LED, buttons, passives
    m.add('body', box(-6.3, 6.6, ztop, -3.6, 8.6, ztop + 0.55))
    m.add('body', box(3.2, 7.0, ztop, 6.2, 8.6, ztop + 1.1))
    m.add('metal', box(-2.0, 7.3, ztop, 1.2, 9.8, ztop + 0.8))
    m.add('led', box(3.6, -3.9, ztop, 5.6, -1.9, ztop + 0.7))
    m.add('white', box(4.0, -3.5, ztop + 0.7, 5.2, -2.3, ztop + 0.72))
    for bx in (-5.7, 5.7):
        m.add('white', box(bx - 1.0, -10.2, ztop, bx + 1.0, -7.4, ztop + 1.2))
        m.add('grey', box(bx - 0.6, -9.6, ztop + 1.2, bx + 0.6, -8.0, ztop + 1.5))
    caps = [(-5.0, 1.0), (-5.0, 3.0), (4.8, 1.0), (4.8, 3.0), (-1.0, 10.3), (1.0, 10.3), (-4.2, -5.5), (4.2, -5.5),
            (-2.5, -3.3), (0.5, -3.3), (2.0, 5.2+1.5)]
    for i, (cx, cy) in enumerate(caps):
        m.add('cap' if i % 3 else 'res', box(cx - 0.5, cy - 0.25, ztop, cx + 0.5, cy + 0.25, ztop + 0.5))
    m.add('silk', text('RP2040-Zero', 0, 9.9 - 15.6, ztop, 0.9).translate((0, 0, 0)))
    m.save()


if __name__ == '__main__':
    import shutil
    shutil.rmtree(PREVIEW, ignore_errors=True)
    usb_c_hro(); print('usb-c')
    microsd_molex(); print('microsd')
    pts810(); print('pts810')
    soic8_208(); print('soic8')
    rp2040_zero(); print('rp2040-zero')
    print('written to', OUT)
