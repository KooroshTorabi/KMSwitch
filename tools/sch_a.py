"""Schematic layout for variant A (A3 sheet). Coordinates in units of 2.54 mm."""
from schpretty import Sheet

PAPER = ('A3', 420, 297)


def u(*v):
    return tuple(x * 2.54 for x in v)


NOTES = """GPIO map (same firmware for both variants)
  GP2 / GP3      USB host D+ / D- (PIO-USB, 22 ohm series)        GP4 / GP5   UART1 TX / RX through the isolator
  GP14 / GP15    SPI1 SCK / MOSI to microSD                        GP28 / GP29 SPI1 MISO / card CS
  GP26           card detect (switch to GND, internal pull-up)     GP6         external RGB LED data (header J6)
  GP16           on-board WS2812 of the RP2040-Zero (status)

Power domains
  side A  (+5V_IN, +5V_HOST, +5V_A, +3V3_A, GND_A): adapter on J1, MCU-A, USB host port, microSD.  PC 1 connects to U1's USB-C.
  side B  (+3V3_B, GND_B): MCU-B, powered by PC 2 through U2's USB-C.  The only link between the sides is U3."""


def draw(sh: Sheet):
    p, link, wire = sh.put, sh.link, sh.wire

    # ------------------------------------------------------------ power input
    sh.frame('Power input: 5 V adapter on USB-C (power only)', *u(4, 5, 54, 37),
             'Adapter 5 V, 2-3 A. F1 protects the USB-A port; D1 feeds MCU-A so the keyboard works while PC 1 is off.')
    p('J1', *u(12, 21), fields_at='top')
    p('R1', *u(25, 19.5)); p('R2', *u(22, 20.5))
    wire(u(18, 17), u(25, 17), u(25, 18))           # CC1 -> R1
    wire(u(18, 18), u(22, 18), u(22, 19))           # CC2 -> R2
    wire(u(9, 30), u(9, 31), u(12, 31)); wire(u(12, 30), u(12, 31))   # shield + GND
    sh.power('GND_A', *u(12, 31), (0, 1)); sh.flag(*u(9, 31), (0, 1))
    p('F1', *u(36, 15), rot=90)
    link(('J1', 'A4'), ('F1', '1'))
    sh.power('+5V_IN', *u(21, 15), (0, -1))
    sh.flag(*u(25, 15))
    p('D1', *u(34.5, 25), rot=180)
    wire(u(29, 15), u(29, 25), u(33, 25))
    wire(u(36, 25), u(42, 25))
    sh.flag(*u(38, 25), (0, 1))
    sh.power('+5V_A', *u(42, 25), (0, -1))
    p('C1', *u(42, 18.5)); p('C2', *u(48, 18.5))
    wire(u(37.5, 15), u(52, 15))
    wire(u(42, 15), u(42, 17)); wire(u(48, 15), u(48, 17))
    sh.power('+5V_HOST', *u(52, 15), (0, -1))

    # ------------------------------------------------------------ USB host port
    sh.frame('USB host port: keyboard, mouse, dongle or hub', *u(56, 5, 96, 37),
             'USBH_DP / USBH_DM come from MCU-A GP2 / GP3 (PIO-USB host).\nU4 (USBLC6-2SC6) clamps ESD on the port.')
    p('J2', *u(62, 21), fields_at='top')
    p('U4', *u(72, 21), fields_at='corners')
    link(('J2', '3'), ('U4', '1'))
    link(('J2', '2'), ('U4', '3'))
    vb = sh.P('J2', '1')
    link(('J2', '1'), ('U4', '5'), first='h'); sh.power('+5V_HOST', sh.P('U4', '5')['x'], vb['y'], (0, -1))
    sh.pwr('U4', '2', 0, (0, 1))
    p('R3', *u(83.5, 21), rot=270)
    p('R4', *u(83.5, 24), rot=270)
    link(('U4', '6'), ('R3', '2'))
    link(('U4', '4'), ('R4', '2'), via=[u(80, 22), u(80, 24)])
    wire(u(61, 25), u(61, 26), u(62, 26)); wire(u(62, 25), u(62, 26))
    sh.power('GND_A', *u(62, 26), (0, 1))
    sh.lab('R3', '1', 2.54 * 2); sh.lab('R4', '1', 2.54 * 2)

    # ------------------------------------------------------------ RGB header
    sh.frame('Optional RGB LED header J6 (WS2812 / SK6812)', *u(98, 5, 138, 37),
             'Data from GP6 through R9. Most WS2812B accept 3.3 V data at 5 V; keep the wire short.')
    p('J6', *u(128, 21), fields_at='top')
    p('R9', *u(117.5, 21), rot=90)
    link(('R9', '2'), ('J6', '2'))
    sh.lab('R9', '1', 2.54 * 2)
    sh.pwr('J6', '1', 2.54 * 2, (0, -1))
    sh.pwr('J6', '3', 2.54 * 2, (0, 1))

    # ------------------------------------------------------------ mechanical
    sh.frame('Mounting holes (M2.5)', *u(140, 5, 159, 37))
    for i, h in enumerate(('H1', 'H2', 'H3', 'H4')):
        p(h, *u(143 + 4.5 * i, 22))

    # ------------------------------------------------------------ microSD
    sh.frame('microSD: shared drive (SPI1)', *u(4, 39, 46, 81),
             'Shown as a USB drive on the active PC only.\n10k pull-ups on CS, MISO and the unused DAT1 / DAT2.')
    p('J3', *u(28, 57))
    wire(u(33, 56), u(36, 56), u(36, 61)); wire(u(33, 60), u(36, 60))
    sh.power('GND_A', *u(36, 61), (0, 1))
    for i, r in enumerate(('R5', 'R6', 'R7', 'R8')):
        p(r, *u(9 + 4 * i, 72.5))
    p('C5', *u(31, 72.5)); p('C6', *u(36, 72.5))
    sh.text('pull-ups', *u(8, 67.5), 1.27, bold=False)
    sh.text('decoupling', *u(30, 67.5), 1.27, bold=False)

    # ------------------------------------------------------------ MCU-A
    sh.frame('MCU-A: RP2040-Zero  (to PC 1 by its own USB-C)', *u(48, 39, 89, 81),
             'USB host, router and microSD.\nOn-board WS2812 on GP16 shows the active PC.')
    p('U1', *u(68, 60))

    # ------------------------------------------------------------ isolation
    sh.frame('Isolation: GND_A and GND_B never meet', *u(91, 39, 124, 81),
             'CA-IS3721HS, pin compatible with ADuM1201.\nUse a >= 10 Mbps grade for a 3 Mbaud UART.')
    p('U3', *u(107.5, 58))
    sh.pwr('U3', '1', 2.54 * 2, (0, -1)); sh.pwr('U3', '4', 2.54 * 2, (0, 1))
    sh.pwr('U3', '8', 2.54 * 2, (0, -1)); sh.pwr('U3', '5', 2.54 * 2, (0, 1))
    sh.flag(*u(114.5, 60), (1, 0))
    p('C3', *u(100.5, 71)); p('C4', *u(114.5, 71))

    # ------------------------------------------------------------ MCU-B
    sh.frame('MCU-B: RP2040-Zero  (to PC 2, powered by PC 2)', *u(126, 39, 159, 81))
    p('U2', *u(143, 60))

    # ------------------------------------------------------------ notes
    sh.frame('Notes', *u(4, 83, 110, 104))
    sh.text(NOTES, *u(6, 86.5), 1.27, bold=False)
