"""Schematic layout for variant B (A2 sheet). Coordinates in units of 2.54 mm."""
from schpretty import Sheet
import design_b

PAPER = ('A2', 594, 420)


def u(*v):
    return tuple(x * 2.54 for x in v)


NOTES = """GPIO map (same as variant A, one firmware for both)
  GPIO2 / GPIO3: USB host D+ / D- (PIO-USB, 22 ohm series).   GPIO4 / GPIO5: UART1 TX / RX through the isolator.
  GPIO14 / GPIO15 / GPIO28 / GPIO29: SPI1 SCK / MOSI / MISO and card CS.   GPIO26: card detect.   GPIO6: external RGB LED (J6).
  GPIO24: PC 1 VBUS sense (10k / 20k divider).   GPIO25: green status LED.

Power domains
  side A (GND_A): adapter (J1) and PC 1 VBUS (J4) are OR-ed by D2 / D3 into +5V_SYS_A, which feeds the USB host port (through F1),
                  the RGB header and LDO U12 (+3V3_A, MCU-A, microSD). Neither source can back-feed the other.
  side B (GND_B): PC 2 VBUS (J5) feeds LDO U52 (+3V3_B, MCU-B). The only link between the sides is the isolator U3.

First flash: hold BOOTSEL, tap RESET, copy the .uf2 to the drive that appears. SWD pads (SWCLK, SWDIO, GND) for a debugger."""


def usb_dev(sh, J, ra, rb, rdm, rdp, x, y):
    """USB-C device port with CC pull-downs and the 27 ohm series resistors of the MCU's USB pins."""
    p, wire = sh.put, sh.wire
    p(J, *u(x, y), fields_at='top')
    X = x + 6
    wire(u(X, y - 1), u(X + 3, y - 1), u(X + 3, y)); wire(u(X, y), u(X + 3, y))       # D- pair
    wire(u(X, y + 1), u(X + 2, y + 1), u(X + 2, y + 2)); wire(u(X, y + 2), u(X + 2, y + 2))  # D+ pair
    p(rdm, *u(X + 9.5, y - 1), rot=270); p(rdp, *u(X + 9.5, y + 2), rot=270)
    wire(u(X + 3, y - 1), u(X + 8, y - 1)); wire(u(X + 2, y + 2), u(X + 8, y + 2))
    sh.lab(rdm, '1', 2.54); sh.lab(rdp, '1', 2.54)
    sh.quiet |= {sh.net(J, 'A6'), sh.net(J, 'A7')}
    sh.lab(J, 'A5', 2.54); sh.lab(J, 'B5', 2.54)
    p(ra, *u(X + 3, y + 8.5)); p(rb, *u(X + 9, y + 8.5))
    sh.text('CC pull-downs: 5.1k = USB device', *u(X + 1, y + 12.5), 1.27, bold=False)
    wire(u(x - 3, y + 9), u(x - 3, y + 10), u(x, y + 10)); wire(u(x, y + 9), u(x, y + 10))
    sh.power(sh.net(J, 'A1'), *u(x, y + 10), (0, 1))
    return X


def core(sh, s, cx, cy):
    """One RP2040 with flash, crystal, LDO, decoupling, BOOTSEL / RESET, SWD pads and LED."""
    c = [k['ref'] for k in (design_b.coreA if s == 'A' else design_b.coreB)]
    U, FL, LDO, Y = c[0], c[1], c[2], c[3]
    p, wire = sh.put, sh.wire
    X = lambda dx: cx + dx
    Yy = lambda dy: cy + dy
    p(U, *u(cx, cy))
    # top supply pins: +1V1 pair and +3V3 group
    wire(u(X(-2), Yy(-18)), u(X(-2), Yy(-21)), u(X(-1), Yy(-21)), u(X(-1), Yy(-18)))
    sh.power('+1V1_' + s, *u(X(-2), Yy(-21)), (0, -1))
    wire(u(X(1), Yy(-18)), u(X(1), Yy(-20)), u(X(4), Yy(-20)), u(X(4), Yy(-18)))
    wire(u(X(2), Yy(-18)), u(X(2), Yy(-20))); wire(u(X(3), Yy(-18)), u(X(3), Yy(-20)))
    sh.power('+3V3_' + s, *u(X(4), Yy(-20)), (0, -1))
    # crystal network
    p(Y, *u(X(-22), Yy(4.5)), rot=270, fields_at='left')
    p(c[4], *u(X(-31), Yy(4.5))); p(c[5], *u(X(-27), Yy(8.5)))
    p(c[6], *u(X(-14.5), Yy(7)), rot=270)
    wire(u(X(-10), Yy(3)), u(X(-31), Yy(3)))
    wire(u(X(-10), Yy(7)), u(X(-13), Yy(7)))
    wire(u(X(-16), Yy(7)), u(X(-27), Yy(7))); wire(u(X(-22), Yy(6)), u(X(-22), Yy(7)))
    sh.pwr(Y, '2', 0, (0, 1)); sh.pwr(Y, '4', 0, (0, 1))
    sh.quiet |= {'XIN_' + s, 'XOUT_' + s, 'XOUT_R_' + s, 'BOOT_' + s, 'LED_K_' + s}
    # flash
    p(FL, *u(X(-32), Yy(-6)))
    p(c[18], *u(X(-25), Yy(-9.5)))
    sh.text('2 MB QSPI flash', *u(X(-42), Yy(-15)), 1.27, bold=False)
    # LDO
    p(LDO, *u(X(-34), Yy(-25)))
    wire(u(X(-37), Yy(-26)), u(X(-40), Yy(-26)), u(X(-40), Yy(-24)))
    wire(u(X(-37), Yy(-25)), u(X(-38), Yy(-25)), u(X(-38), Yy(-26)))
    sh.power(sh.net(LDO, '1'), *u(X(-40), Yy(-26)), (0, -1))
    p(c[19], *u(X(-40), Yy(-22.5)))
    wire(u(X(-31), Yy(-26)), u(X(-27), Yy(-26)), u(X(-27), Yy(-24)))
    sh.power('+3V3_' + s, *u(X(-27), Yy(-26)), (0, -1))
    p(c[20], *u(X(-27), Yy(-22.5)))
    sh.text('3.3 V LDO', *u(X(-41), Yy(-31)), 1.27, bold=False)
    # decoupling bank (one capacitor per supply pin)
    bank = [c[i] for i in (12, 13, 16, 7, 8, 9, 10, 11, 14, 15, 17)]
    for i, ref in enumerate(bank):
        p(ref, *u(X(17 + 5 * (i % 6)), Yy(-27 + 8 * (i // 6))))
    sh.text('decoupling, one per supply pin:\nDVDD x2, VREG_VOUT, IOVDD x6, USB_VDD, VREG_IN, ADC_AVDD',
            *u(X(16), Yy(-34)), 1.27, bold=False)
    # BOOTSEL
    p(c[23], *u(X(-36.5), Yy(16)), rot=90); p(c[24], *u(X(-31), Yy(16)))
    wire(u(X(-35), Yy(16)), u(X(-33), Yy(16)))
    sh.lab(c[23], '1', 2.54); sh.pwr(c[24], '2', 2.54, (0, 1))
    sh.text('BOOTSEL', *u(X(-36), Yy(12.5)), 1.27, bold=False)
    # RESET
    p(c[25], *u(X(-25), Yy(19.5))); p(c[26], *u(X(-19), Yy(22)))
    wire(u(X(-25), Yy(21)), u(X(-25), Yy(22)), u(X(-21), Yy(22)))
    wire(u(X(-25), Yy(22)), u(X(-28), Yy(22))); sh._label('RUN_' + s, *u(X(-28), Yy(22)), -1, 0)
    sh.pwr(c[26], '2', 2.54, (0, 1))
    sh.text('RESET', *u(X(-21), Yy(18.5)), 1.27, bold=False)
    # SWD pads
    for i, ref in enumerate(c[27:30]):
        p(ref, *u(X(-44 + 3 * i), Yy(22)))
    sh.text('SWD pads', *u(X(-45), Yy(18.5)), 1.27, bold=False)
    # status LED
    p(c[31], *u(X(16.5), Yy(21)), rot=90); p(c[30], *u(X(22.5), Yy(21)), rot=180)
    wire(u(X(18), Yy(21)), u(X(21), Yy(21)))
    sh.lab(c[31], '1', 2.54); sh.pwr(c[30], '1', 2.54, (0, 1))
    sh.text('status LED', *u(X(15), Yy(18)), 1.27, bold=False)


def draw(sh: Sheet):
    p, link, wire = sh.put, sh.link, sh.wire

    # ------------------------------------------------------------ power input
    sh.frame('Power input: adapter (J1) OR-ed with PC 1 VBUS', *u(4, 5, 60, 45),
             'D2 / D3 let the adapter and PC 1 share the load without back-feeding each other. F1 protects the USB-A port.')
    p('J1', *u(12, 21), fields_at='top')
    p('R1', *u(25, 19.5)); p('R2', *u(22, 20.5))
    wire(u(18, 17), u(25, 17), u(25, 18)); wire(u(18, 18), u(22, 18), u(22, 19))
    wire(u(9, 30), u(9, 31), u(12, 31)); wire(u(12, 30), u(12, 31))
    sh.power('GND_A', *u(12, 31), (0, 1)); sh.flag(*u(9, 31), (0, 1))
    p('D2', *u(30.5, 15), rot=180)
    wire(u(18, 15), u(29, 15))
    sh.power('+5V_IN', *u(21, 15), (0, -1)); sh.flag(*u(25, 15))
    p('D3', *u(30.5, 27), rot=180)
    wire(u(26, 27), u(29, 27))
    sh.power('VBUS_PC1', *u(26, 27), (-1, 0))
    wire(u(32, 15), u(38.5, 15)); wire(u(32, 27), u(35, 27), u(35, 15))
    sh.power('+5V_SYS_A', *u(35, 15), (0, -1)); sh.flag(*u(35, 21), (1, 0))
    p('F1', *u(40, 15), rot=90)
    p('C1', *u(46, 18.5)); p('C2', *u(53, 18.5))
    wire(u(41.5, 15), u(56, 15)); wire(u(46, 15), u(46, 17)); wire(u(53, 15), u(53, 17))
    sh.power('+5V_HOST', *u(56, 15), (0, -1))

    # ------------------------------------------------------------ PC 1 USB-C
    sh.frame('PC 1: USB-C device port + VBUS sense', *u(62, 5, 106, 45),
             'MCU-A enumerates on PC 1 (27 ohm series R28 / R29).\nGPIO24 sees PC 1 VBUS through R42 / R43.')
    X = usb_dev(sh, 'J4', 'R40', 'R41', 'R29', 'R28', 68, 21)
    wire(u(74, 15), u(96, 15))
    sh.power('VBUS_PC1', *u(77, 15), (0, -1)); sh.flag(*u(81, 15))
    p('R42', *u(96, 16.5)); p('R43', *u(96, 21.5))
    wire(u(96, 18), u(96, 20)); wire(u(96, 19), u(98, 19))
    sh._label('VBUS_SENSE_A', *u(98, 19), 1, 0)

    # ------------------------------------------------------------ USB host port
    sh.frame('USB host port: keyboard, mouse, dongle or hub', *u(108, 5, 144, 45),
             'USBH_DP / USBH_DM come from MCU-A GPIO2 / GPIO3 (PIO-USB host).')
    p('J2', *u(114, 21), fields_at='top')
    p('R3', *u(125.5, 21), rot=270); p('R4', *u(125.5, 24), rot=270)
    link(('J2', '3'), ('R3', '2'))
    link(('J2', '2'), ('R4', '2'), via=[u(120, 22), u(120, 24)])
    sh.pwr('J2', '1', 2.54 * 5, (0, -1))
    wire(u(113, 25), u(113, 26), u(114, 26)); wire(u(114, 25), u(114, 26))
    sh.power('GND_A', *u(114, 26), (0, 1))
    sh.lab('R3', '1', 2.54 * 2); sh.lab('R4', '1', 2.54 * 2)

    # ------------------------------------------------------------ RGB header
    sh.frame('Optional RGB LED header J6', *u(146, 5, 177, 45),
             'WS2812 / SK6812: 5V, DIN, GND. Data from GPIO6 through R9.')
    p('J6', *u(171, 21), fields_at='top')
    p('R9', *u(160.5, 21), rot=90)
    link(('R9', '2'), ('J6', '2'))
    sh.lab('R9', '1', 2.54 * 2)
    sh.pwr('J6', '1', 2.54 * 2, (0, -1)); sh.pwr('J6', '3', 2.54 * 2, (0, 1))

    # ------------------------------------------------------------ PC 2 USB-C
    sh.frame('PC 2: USB-C device port (powers side B)', *u(179, 5, 213, 45),
             'MCU-B enumerates on PC 2 (27 ohm series R68 / R69)\nand runs from its VBUS.')
    usb_dev(sh, 'J5', 'R44', 'R45', 'R69', 'R68', 185, 21)
    wire(u(191, 15), u(196, 15))
    sh.power('VBUS_PC2', *u(196, 15), (0, -1)); sh.flag(*u(194, 15))
    sh.flag(*u(185, 31), (1, 0))

    # ------------------------------------------------------------ mechanical
    sh.frame('Mounting holes', *u(215, 5, 229, 45))
    for i, h in enumerate(('H1', 'H2', 'H3', 'H4')):
        p(h, *u(219 + 6 * (i % 2), 18 + 8 * (i // 2)))

    # ------------------------------------------------------------ MCU-A core
    sh.frame('MCU-A: RP2040 (USB host, router, microSD; enumerates on PC 1)', *u(4, 47, 96, 122))
    core(sh, 'A', 50, 84)

    # ------------------------------------------------------------ microSD
    sh.frame('microSD: shared drive (SPI1)', *u(98, 47, 136, 92),
             'Shown as a USB drive on the active PC only.\n10k pull-ups on CS, MISO and the unused DAT1 / DAT2.')
    p('J3', *u(121, 63))
    wire(u(126, 62), u(129, 62), u(129, 67)); wire(u(126, 66), u(129, 66))
    sh.power('GND_A', *u(129, 67), (0, 1))
    for i, r in enumerate(('R5', 'R6', 'R7', 'R8')):
        p(r, *u(102 + 4 * i, 81.5))
    p('C5', *u(124, 81.5)); p('C6', *u(129, 81.5))

    # ------------------------------------------------------------ isolation
    sh.frame('Isolation: GND_A and GND_B never meet', *u(98, 94, 136, 122),
             'CA-IS3721HS (pin compatible with ADuM1201).')
    p('U3', *u(117, 106))
    sh.pwr('U3', '1', 2.54 * 2, (0, -1)); sh.pwr('U3', '4', 2.54 * 2, (0, 1))
    sh.pwr('U3', '8', 2.54 * 2, (0, -1)); sh.pwr('U3', '5', 2.54 * 2, (0, 1))
    p('C3', *u(102, 117)); p('C4', *u(132, 117))

    # ------------------------------------------------------------ MCU-B core
    sh.frame('MCU-B: RP2040 (enumerates on PC 2, powered by PC 2)', *u(138, 47, 229, 122))
    core(sh, 'B', 184, 84)

    # ------------------------------------------------------------ notes
    sh.frame('Notes', *u(4, 124, 182, 160))
    sh.text(NOTES, *u(6, 128), 1.27, bold=False)
