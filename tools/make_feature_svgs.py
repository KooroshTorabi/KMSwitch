#!/usr/bin/env python3
"""Animated feature illustrations for the README (docs/features/*.svg).

Plain SVG + CSS animation, no script, so GitHub shows them animated in <img>.
Run: python3 tools/make_feature_svgs.py
"""
import os

OUT = os.path.join(os.path.dirname(__file__), '..', 'docs', 'features')
W, H = 480, 270
T = 6  # loop length, seconds
C1, C2 = '#2563eb', '#ea580c'          # PC 1 blue, PC 2 orange
INK, MUTE, LINE, BG = '#0f172a', '#64748b', '#cbd5e1', '#f8fafc'

# monitor screens (inner rects)
S1 = (40, 34, 170, 100)
S2 = (270, 34, 170, 100)
BOX = (200, 188, 80, 36)


def svg(title, css, body):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" aria-label="{title}">
<title>{title}</title>
<style>
text{{font-family:-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;fill:{INK}}}
.m{{fill:{MUTE};font-size:11px}} .b{{font-weight:600}}
.loop{{animation-duration:{T}s;animation-iteration-count:infinite}}
{css}
</style>
<rect x="1" y="1" width="{W-2}" height="{H-2}" rx="16" fill="{BG}" stroke="#e2e8f0" stroke-width="2"/>
{body}
</svg>
'''


def onoff(name, on_ranges):
    """keyframes `name`: opacity 1 inside the (start%, end%) ranges, 0 elsewhere (hard steps)."""
    stops = []
    for a, b in on_ranges:
        if a > 0:
            stops += [(a - 0.01, 0)]
        stops += [(a, 1), (b, 1)]
        if b < 100:
            stops += [(b + 0.01, 0)]
    if stops[0][0] > 0:
        stops.insert(0, (0, 0))
    if stops[-1][0] < 100:
        stops.append((100, 0))
    return '@keyframes %s{%s}' % (name, ' '.join('%.2f%%{opacity:%d}' % st for st in stops))


def monitor(s, label, cls_glow='', color=C1):
    x, y, w, h = s
    cx = x + w / 2
    lx_left = x < W / 2
    g = f'''<g>
<rect x="{x-6}" y="{y-6}" width="{w+12}" height="{h+12}" rx="8" fill="#1e293b"/>
<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="#e2e8f0"/>
<rect x="{cx-6}" y="{y+h+6}" width="12" height="14" fill="#334155"/>
<rect x="{cx-30}" y="{y+h+19}" width="60" height="5" rx="2.5" fill="#334155"/>
<text x="{cx-58 if lx_left else cx+58}" y="{y+h+24}" text-anchor="middle" class="b" font-size="13">{label}</text>
'''
    if cls_glow:
        g += f'<rect class="{cls_glow}" x="{x-8}" y="{y-8}" width="{w+16}" height="{h+16}" rx="10" fill="none" stroke="{color}" stroke-width="4"/>\n'
    return g + '</g>\n'


def box(label='kmswitch', x=None, y=None, w=None, h=None):
    bx, by, bw, bh = BOX if x is None else (x, y, w, h)
    return f'''<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="7" fill="#ffffff" stroke="{INK}" stroke-width="2"/>
<text x="{bx+bw/2}" y="{by+bh/2+4}" text-anchor="middle" class="b" font-size="12">{label}</text>
'''


def cables(cls1='', cls2=''):
    bx, by, bw, bh = BOX
    p1 = f'M{bx+18},{by} C{bx+18},{by-20} 125,170 125,{S1[1]+S1[3]+26}'
    p2 = f'M{bx+bw-18},{by} C{bx+bw-18},{by-20} 355,170 355,{S2[1]+S2[3]+26}'
    s = f'<path d="{p1}" fill="none" stroke="{LINE}" stroke-width="3"/>\n<path d="{p2}" fill="none" stroke="{LINE}" stroke-width="3"/>\n'
    if cls1:
        s += f'<path class="{cls1}" d="{p1}" fill="none" stroke="{C1}" stroke-width="3.5"/>\n'
    if cls2:
        s += f'<path class="{cls2}" d="{p2}" fill="none" stroke="{C2}" stroke-width="3.5"/>\n'
    return s


def keyboard(x=40, y=200, keys_hi=()):
    """small keyboard; keys_hi: list of (col,row,label,cls) highlighted keys."""
    s = f'<rect x="{x}" y="{y}" width="130" height="40" rx="6" fill="#ffffff" stroke="{INK}" stroke-width="2"/>\n'
    for r in range(3):
        for c in range(10):
            s += f'<rect x="{x+6+c*12}" y="{y+6+r*10}" width="10" height="8" rx="1.5" fill="{LINE}"/>\n'
    bx = BOX[0]
    s += f'<path d="M{x+130},{y+20} C{x+150},{y+20} {bx-20},{BOX[1]+18} {bx},{BOX[1]+18}" fill="none" stroke="{LINE}" stroke-width="3"/>\n'
    return s


def mouse(x=420, y=204):
    bx = BOX[0] + BOX[2]
    return f'''<path d="M{bx},{BOX[1]+18} C{bx+40},{BOX[1]+18} {x-40},{y+12} {x-14},{y+12}" fill="none" stroke="{LINE}" stroke-width="3"/>
<rect x="{x-14}" y="{y}" width="28" height="38" rx="14" fill="#ffffff" stroke="{INK}" stroke-width="2"/>
<line x1="{x}" y1="{y+2}" x2="{x}" y2="{y+14}" stroke="{INK}" stroke-width="1.5"/>
'''


POINTER = 'M0,0 L0,16 L4.5,12 L7.5,19 L10,18 L7,11 L12,11 Z'


def caption(t):
    return f'<text x="{W/2}" y="{H-8}" text-anchor="middle" class="m">{t}</text>\n'


# 1. shared drive --------------------------------------------------------------
def f_shared_drive():
    css = (onoff('on1', [(0, 50)]) + onoff('on2', [(50, 100)]) +
           '.a1{animation-name:on1}.a2{animation-name:on2}'
           '@keyframes file{0%{transform:translate(70px,50px);opacity:0}6%{opacity:1}'
           '18%{transform:translate(70px,50px)}36%{transform:translate(150px,70px)}'
           '44%{transform:translate(150px,70px);opacity:1}48%{opacity:0}100%{opacity:0}}'
           '@keyframes file2{0%,56%{transform:translate(285px,70px);opacity:0}60%{opacity:1}'
           '78%{transform:translate(370px,48px)}94%{opacity:1}100%{transform:translate(370px,48px);opacity:0}}'
           '.f1{animation-name:file}.f2{animation-name:file2}'
           '@keyframes sw{0%,46%{opacity:0}50%{opacity:1}60%,100%{opacity:0}}.sw{animation-name:sw}')

    def drive(x, y, cls, color):
        return f'''<g class="{cls} loop"><rect x="{x-6}" y="{y}" width="58" height="34" rx="5" fill="#ffffff" stroke="{color}" stroke-width="2"/>
<rect x="{x+5}" y="{y+6}" width="36" height="12" rx="2" fill="{color}" opacity=".2"/>
<text x="{x+23}" y="{y+29}" text-anchor="middle" font-size="8" class="b" style="fill:{color}">KMSWITCH</text></g>
'''

    def file(cls, color):
        return f'''<g class="{cls} loop"><path d="M0,0 h16 l7,7 v21 h-23 Z" fill="#ffffff" stroke="{color}" stroke-width="2"/>
<path d="M16,0 v7 h7" fill="none" stroke="{color}" stroke-width="2"/>
<line x1="4" y1="14" x2="17" y2="14" stroke="{color}" stroke-width="2"/><line x1="4" y1="20" x2="14" y2="20" stroke="{color}" stroke-width="2"/></g>
'''
    body = (cables('a1 loop', 'a2 loop') + monitor(S1, 'PC 1', 'a1 loop', C1) + monitor(S2, 'PC 2', 'a2 loop', C2) +
            drive(150, 66, 'a1', C1) + drive(284, 66, 'a2', C2) +
            file('f1', C1) + file('f2', C2) + box('microSD') +
            f'<text class="sw loop b" x="240" y="104" text-anchor="middle" font-size="12" style="fill:{INK}">switch</text>\n'
            f'<path class="sw loop" d="M222,112 h36 m-6,-5 l6,5 l-6,5" fill="none" stroke="{INK}" stroke-width="2"/>\n' +
            caption('Save a file on the box\'s drive, switch, open it on the other PC'))
    return svg('Shared drive: move files between the two PCs', css, body)


# 2. edge switching ------------------------------------------------------------
def f_edge():
    css = (onoff('on1', [(0, 40), (85, 100)]) + onoff('on2', [(40.01, 85)]) +
           '.a1{animation-name:on1}.a2{animation-name:on2}'
           '@keyframes ptr{0%{transform:translate(80px,95px)}'
           '20%{transform:translate(150px,60px)}40%{transform:translate(206px,72px)}'
           '40.01%{transform:translate(272px,72px)}60%{transform:translate(380px,105px)}'
           '72%{transform:translate(330px,60px)}85%{transform:translate(272px,80px)}'
           '85.01%{transform:translate(206px,80px)}100%{transform:translate(80px,95px)}}'
           '.ptr{animation-name:ptr;animation-timing-function:ease-in-out}'
           '@keyframes flash{0%,36%{opacity:0}40%{opacity:1}48%,80%{opacity:0}85%{opacity:1}93%,100%{opacity:0}}'
           '.edge{animation-name:flash}')
    body = (cables('a1 loop', 'a2 loop') + monitor(S1, 'PC 1', 'a1 loop', C1) + monitor(S2, 'PC 2', 'a2 loop', C2) +
            f'<rect class="edge loop" x="205" y="34" width="6" height="100" fill="{C1}" opacity="0"/>\n'
            f'<rect class="edge loop" x="269" y="34" width="6" height="100" fill="{C2}" opacity="0"/>\n'
            f'<path class="ptr loop" d="{POINTER}" fill="#ffffff" stroke="{INK}" stroke-width="1.6" stroke-linejoin="round"/>\n' +
            box() + keyboard() + mouse() +
            caption('Push the pointer past the screen edge: keyboard and mouse follow it'))
    return svg('Edge switching: the mouse moves across to the other PC', css, body)


# 3. hotkey --------------------------------------------------------------------
def f_hotkey():
    css = (onoff('on1', [(0, 25), (75.01, 100)]) + onoff('on2', [(25.01, 75)]) +
           '.a1{animation-name:on1}.a2{animation-name:on2}'
           '@keyframes press{0%,15%{transform:translateY(0)}18%,26%{transform:translateY(3px)}29%,65%{transform:translateY(0)}'
           '68%,76%{transform:translateY(3px)}79%,100%{transform:translateY(0)}}.k{animation-name:press}'
           '@keyframes kfill{0%,15%{fill:#ffffff}18%,26%{fill:#fde68a}29%,65%{fill:#ffffff}68%,76%{fill:#fde68a}79%,100%{fill:#ffffff}}'
           '.kf{animation-name:kfill}'
           '@keyframes caret{0%,49%{opacity:1}50%,100%{opacity:0}}.caret{animation:caret 1s infinite step-end}'
           '@keyframes type{0%{width:0}100%{width:110px}}'
           '.t1{animation:type 1.5s steps(14) infinite alternate}')

    def screen_text(s, cls):
        x, y = s[0] + 18, s[1] + 26
        return (f'<g class="{cls} loop"><rect class="t1" x="{x}" y="{y}" width="110" height="7" rx="2" fill="#94a3b8"/>'
                f'<rect x="{x}" y="{y+16}" width="80" height="7" rx="2" fill="#94a3b8" opacity=".6"/>'
                f'<rect x="{x}" y="{y+32}" width="96" height="7" rx="2" fill="#94a3b8" opacity=".6"/>'
                f'<rect class="caret" x="{x+2}" y="{y+46}" width="2" height="12" fill="{INK}"/></g>\n')

    # big keyboard with Win and F12 caps
    kx, ky = 120, 180
    kb = f'<rect x="{kx}" y="{ky}" width="240" height="64" rx="8" fill="#ffffff" stroke="{INK}" stroke-width="2"/>\n'
    for r in range(3):
        for c in range(14):
            if (r, c) in ((0, 12), (2, 1)):
                continue
            kb += f'<rect x="{kx+8+c*16.5}" y="{ky+8+r*17}" width="14" height="14" rx="2.5" fill="{LINE}"/>\n'

    def cap(x, y, w, t):
        return (f'<g class="k loop"><rect class="kf loop" x="{x}" y="{y}" width="{w}" height="14" rx="2.5" fill="#ffffff" stroke="{INK}" stroke-width="1.5"/>'
                f'<text x="{x+w/2}" y="{y+10.5}" text-anchor="middle" font-size="8.5" class="b">{t}</text></g>\n')
    kb += cap(kx + 8 + 12 * 16.5 - 2, ky + 8, 20, 'F12') + cap(kx + 8 + 1 * 16.5 - 2, ky + 8 + 34, 20, 'Win')
    p1 = f'M200,{ky} C200,160 125,170 125,{S1[1]+S1[3]+26}'
    p2 = f'M280,{ky} C280,160 355,170 355,{S2[1]+S2[3]+26}'
    cab = (f'<path d="{p1}" fill="none" stroke="{LINE}" stroke-width="3"/><path d="{p2}" fill="none" stroke="{LINE}" stroke-width="3"/>'
           f'<path class="a1 loop" d="{p1}" fill="none" stroke="{C1}" stroke-width="3.5"/><path class="a2 loop" d="{p2}" fill="none" stroke="{C2}" stroke-width="3.5"/>\n')
    body = (cab + monitor(S1, 'PC 1', 'a1 loop', C1) + monitor(S2, 'PC 2', 'a2 loop', C2) +
            screen_text(S1, 'a1') + screen_text(S2, 'a2') + kb +
            caption('Win + F12 switches keyboard and mouse; works in BIOS too, no software'))
    return svg('Hotkey switching with Win+F12', css, body)


# 4. game mode -----------------------------------------------------------------
def f_game():
    css = ('@keyframes aim{0%{transform:translate(110px,80px)}22%{transform:translate(198px,62px)}'
           '30%{transform:translate(204px,70px)}40%{transform:translate(185px,74px)}62%{transform:translate(78px,58px)}'
           '80%{transform:translate(150px,108px)}100%{transform:translate(110px,80px)}}'
           '.aim{animation-name:aim;animation-timing-function:ease-in-out}'
           '@keyframes wall{0%,20%{opacity:0}26%,36%{opacity:1}44%,100%{opacity:0}}.wall{animation-name:wall}'
           '@keyframes hit{0%,24%{opacity:0;transform:scale(.6)}30%{opacity:1;transform:scale(1)}40%,100%{opacity:0}}'
           '.hit{animation-name:hit;transform-origin:212px 84px;transform-box:view-box}')
    x, y, w, h = S1
    scene = (f'<rect x="{x}" y="{y+62}" width="{w}" height="{h-62}" fill="#86efac" opacity=".55"/>'
             f'<path d="M{x},{y+62} L{x+40},{y+38} L{x+70},{y+58} L{x+110},{y+30} L{x+150},{y+56} L{x+w},{y+44} L{x+w},{y+62} Z" fill="#94a3b8" opacity=".55"/>\n')
    cross = (f'<g class="aim loop"><circle r="10" fill="none" stroke="#dc2626" stroke-width="2"/>'
             f'<line x1="-15" x2="-5" stroke="#dc2626" stroke-width="2"/><line x1="5" x2="15" stroke="#dc2626" stroke-width="2"/>'
             f'<line y1="-15" y2="-5" stroke="#dc2626" stroke-width="2"/><line y1="5" y2="15" stroke="#dc2626" stroke-width="2"/>'
             f'<circle r="1.8" fill="#dc2626"/></g>\n')
    wall = f'<rect class="wall loop" x="207" y="34" width="5" height="100" fill="#dc2626" opacity="0"/>\n'
    lock = (f'<g class="hit loop"><rect x="226" y="74" width="28" height="22" rx="4" fill="#dc2626"/>'
            f'<path d="M232,74 v-6 a8,8 0 0 1 16,0 v6" fill="none" stroke="#dc2626" stroke-width="3"/>'
            f'<circle cx="240" cy="85" r="2.6" fill="#ffffff"/></g>\n')
    badge = (f'<rect x="{x+6}" y="{y+6}" width="100" height="20" rx="10" fill="{INK}"/>'
             f'<text x="{x+56}" y="{y+20}" text-anchor="middle" font-size="10" class="b" style="fill:#ffffff">Win + F11 · game</text>\n')
    body = (cables('', '') + f'<path d="M{BOX[0]+18},{BOX[1]} C{BOX[0]+18},{BOX[1]-20} 125,170 125,{S1[1]+S1[3]+26}" fill="none" stroke="{C1}" stroke-width="3.5"/>' +
            monitor(S1, 'PC 1') + monitor(S2, 'PC 2') +
            f'<rect x="{x-8}" y="{y-8}" width="{w+16}" height="{h+16}" rx="10" fill="none" stroke="{C1}" stroke-width="4"/>' +
            scene + badge + wall + cross + lock + box() + keyboard() + mouse() +
            caption('Relative mouse for games: the pointer stays on this PC, edges are locked'))
    return svg('Game mode: relative mouse, edge switching paused', css, body)


# 5. RGB LED -------------------------------------------------------------------
def f_rgb():
    css = ('@keyframes led{0%,45%{fill:' + C1 + '}50%,95%{fill:' + C2 + '}100%{fill:' + C1 + '}}'
           '.led{animation-name:led}'
           '@keyframes glow{0%,45%{fill:' + C1 + ';opacity:.35}50%,95%{fill:' + C2 + ';opacity:.35}100%{fill:' + C1 + ';opacity:.35}}'
           '.glow{animation-name:glow}' +
           onoff('on1', [(0, 47)]) + onoff('on2', [(47.01, 97)]) + onoff('on1b', [(97.01, 100)]) +
           '.a1{animation-name:on1}.a2{animation-name:on2}.a1b{animation-name:on1b}'
           '@keyframes run{0%{transform:translateX(0)}100%{transform:translateX(-20px)}}'
           '.data{animation:run .6s linear infinite}')
    sm1, sm2 = (40, 40, 110, 66), (330, 40, 110, 66)
    mons = ''
    for s, lab, cls, col in ((sm1, 'PC 1', 'a1', C1), (sm2, 'PC 2', 'a2', C2)):
        mx, my, mw, mh = s
        mons += (f'<rect x="{mx-5}" y="{my-5}" width="{mw+10}" height="{mh+10}" rx="7" fill="#1e293b"/>'
                 f'<rect x="{mx}" y="{my}" width="{mw}" height="{mh}" rx="3" fill="#e2e8f0"/>'
                 f'<rect x="{mx+mw/2-5}" y="{my+mh+5}" width="10" height="10" fill="#334155"/>'
                 f'<text x="{mx+mw/2}" y="{my+mh+32}" text-anchor="middle" class="b" font-size="12">{lab}</text>'
                 f'<rect class="{cls} loop" x="{mx-7}" y="{my-7}" width="{mw+14}" height="{mh+14}" rx="9" fill="none" stroke="{col}" stroke-width="4"/>\n')
        if cls == 'a1':
            mons += f'<rect class="a1b loop" x="{mx-7}" y="{my-7}" width="{mw+14}" height="{mh+14}" rx="9" fill="none" stroke="{col}" stroke-width="4"/>\n'
    # case with glowing strip
    cx, cy, cw, ch = 175, 60, 130, 70
    case = (f'<rect class="glow loop" x="{cx-14}" y="{cy+ch-4}" width="{cw+28}" height="34" rx="17" fill="{C1}" opacity=".35" filter="url(#bl)"/>'
            f'<rect x="{cx}" y="{cy}" width="{cw}" height="{ch}" rx="10" fill="#ffffff" stroke="{INK}" stroke-width="2"/>'
            f'<text x="{cx+cw/2}" y="{cy+30}" text-anchor="middle" class="b" font-size="13">kmswitch</text>'
            f'<text x="{cx+cw/2}" y="{cy+48}" text-anchor="middle" class="m">active PC</text>\n')
    leds = ''
    for i in range(6):
        lx = cx + 12 + i * 21
        leds += (f'<rect x="{lx}" y="{cy+ch+6}" width="14" height="14" rx="3" fill="#ffffff" stroke="{INK}" stroke-width="1.2"/>'
                 f'<circle class="led loop" cx="{lx+7}" cy="{cy+ch+13}" r="4.5" fill="{C1}"/>')
    strip = f'<rect x="{cx+4}" y="{cy+ch+3}" width="{cw-8}" height="20" rx="4" fill="#1e293b"/>' + leds + '\n'
    # J6 header detail
    hx, hy = 150, 192
    hdr = (f'<rect x="{hx}" y="{hy}" width="180" height="46" rx="8" fill="#ffffff" stroke="{LINE}" stroke-width="2"/>'
           f'<text x="{hx+12}" y="{hy+18}" class="b" font-size="11">J6 · RGB</text>'
           f'<text x="{hx+12}" y="{hy+34}" class="m">WS2812 / SK6812</text>')
    for i, (lab, col) in enumerate((('5V', '#dc2626'), ('DIN', '#16a34a'), ('GND', INK))):
        px = hx + 116 + i * 22
        hdr += (f'<rect x="{px-6}" y="{hy+9}" width="12" height="12" fill="#facc15" stroke="{INK}" stroke-width="1"/>'
                f'<text x="{px}" y="{hy+36}" text-anchor="middle" font-size="9" class="b" style="fill:{col}">{lab}</text>')
    hdr += f'<path d="M{hx+138},{hy} C{hx+138},{hy-30} {cx+cw-20},{cy+ch+40} {cx+cw-20},{cy+ch+24}" fill="none" stroke="#16a34a" stroke-width="2" stroke-dasharray="4 4"/>\n'
    defs = '<defs><filter id="bl" x="-30%" y="-80%" width="160%" height="260%"><feGaussianBlur stdDeviation="8"/></filter></defs>\n'
    body = defs + mons + case + strip + hdr + caption('Optional LED strip on header J6, lit in the colour of the active PC')
    return svg('RGB LED header: light shows which PC is active', css, body)


# 6. power + isolation ---------------------------------------------------------
def f_power():
    css = ('@keyframes off{0%,25%{opacity:0}32%,88%{opacity:1}95%,100%{opacity:0}}.off{animation-name:off}'
           '@keyframes flow{0%{stroke-dashoffset:24}100%{stroke-dashoffset:0}}'
           '.flow{animation:flow .8s linear infinite}'
           '@keyframes type{0%{width:0}100%{width:120px}}.ty{animation:type 2s steps(16) infinite alternate}')
    x1, y1, w1, h1 = S1
    body = (f'<path d="M{BOX[0]+18},{BOX[1]} C{BOX[0]+18},{BOX[1]-20} 125,170 125,{S1[1]+S1[3]+26}" fill="none" stroke="{LINE}" stroke-width="3"/>'
            f'<path d="M{BOX[0]+BOX[2]-18},{BOX[1]} C{BOX[0]+BOX[2]-18},{BOX[1]-20} 355,170 355,{S2[1]+S2[3]+26}" fill="none" stroke="{C2}" stroke-width="3.5"/>\n' +
            monitor(S1, 'PC 1') + monitor(S2, 'PC 2') +
            f'<rect x="{S2[0]-8}" y="{S2[1]-8}" width="{S2[2]+16}" height="{S2[3]+16}" rx="10" fill="none" stroke="{C2}" stroke-width="4"/>'
            f'<rect class="ty" x="{S2[0]+18}" y="{S2[1]+26}" width="120" height="7" rx="2" fill="#94a3b8"/>'
            f'<rect x="{S2[0]+18}" y="{S2[1]+42}" width="90" height="7" rx="2" fill="#94a3b8" opacity=".6"/>\n'
            f'<g class="off loop" opacity="0"><rect x="{x1}" y="{y1}" width="{w1}" height="{h1}" rx="3" fill="#0f172a"/>'
            f'<circle cx="{x1+w1/2}" cy="{y1+h1/2-4}" r="13" fill="none" stroke="#64748b" stroke-width="3"/>'
            f'<line x1="{x1+w1/2}" y1="{y1+h1/2-22}" x2="{x1+w1/2}" y2="{y1+h1/2-6}" stroke="#64748b" stroke-width="3"/>'
            f'<text x="{x1+w1/2}" y="{y1+h1-12}" text-anchor="middle" font-size="11" class="b" style="fill:#94a3b8">PC 1 off</text></g>\n' +
            # box split by isolation barrier
            box('') +
            f'<line x1="240" y1="{BOX[1]-6}" x2="240" y2="{BOX[1]+BOX[3]+6}" stroke="#dc2626" stroke-width="2" stroke-dasharray="4 3"/>'
            f'<text x="220" y="{BOX[1]+22}" text-anchor="middle" font-size="10" class="b" style="fill:{C1}">A</text>'
            f'<text x="260" y="{BOX[1]+22}" text-anchor="middle" font-size="10" class="b" style="fill:{C2}">B</text>'
            f'<text x="240" y="{BOX[1]-10}" text-anchor="middle" font-size="10" class="b" style="fill:#dc2626">isolated</text>\n' +
            keyboard() +
            # adapter
            f'<rect x="392" y="198" width="44" height="34" rx="6" fill="#ffffff" stroke="{INK}" stroke-width="2"/>'
            f'<text x="414" y="219" text-anchor="middle" font-size="11" class="b">5 V</text>'
            f'<rect x="436" y="206" width="10" height="5" fill="{INK}"/><rect x="436" y="219" width="10" height="5" fill="{INK}"/>'
            f'<path d="M392,215 H{BOX[0]+BOX[2]}" fill="none" stroke="{LINE}" stroke-width="3"/>'
            f'<path class="flow" d="M392,215 H{BOX[0]+BOX[2]}" fill="none" stroke="#16a34a" stroke-width="3" stroke-dasharray="6 6"/>\n' +
            caption('Own 5 V supply: keyboard keeps working when PC 1 is off; PC grounds stay apart'))
    return svg('Separate power supply and galvanic isolation', css, body)


FILES = {
    '1-shared-drive.svg': f_shared_drive,
    '2-edge-switch.svg': f_edge,
    '3-hotkey.svg': f_hotkey,
    '4-game-mode.svg': f_game,
    '5-rgb-led.svg': f_rgb,
    '6-power.svg': f_power,
}

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for name, fn in FILES.items():
        with open(os.path.join(OUT, name), 'w') as f:
            f.write(fn())
        print('wrote', name)
