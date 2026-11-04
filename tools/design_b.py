"""Variant B: two RP2040 chips directly on the board (factory assembly, e.g. JLCPCB)."""
from common import *
from schgen import SymbolLibs

QFN = 'Package_DFN_QFN:QFN-56-1EP_7x7mm_P0.4mm_EP3.2x3.2mm'
_libs = SymbolLibs()
_rp = {p['name'] + ('#' + p['number'] if p['name'] in ('IOVDD', 'DVDD') else ''): p['number']
       for p in _libs.pins('MCU_RaspberryPi:RP2040')}


def rp2040(ref, s, gpio, pcb):
    """s = side suffix (A/B); gpio = {'GPIO2': net, ...}"""
    pins = {num: None for num in _rp.values()}
    def setn(name, net):
        pins[_rp[name]] = net
    V3, V1, G = '+3V3_' + s, '+1V1_' + s, 'GND_' + s
    for k, num in _rp.items():
        if k.startswith('IOVDD'):
            pins[num] = V3
        if k.startswith('DVDD'):
            pins[num] = V1
    setn('USB_VDD', V3); setn('ADC_AVDD', V3); setn('VREG_IN', V3); setn('VREG_VOUT', V1)
    setn('GND', G); setn('TESTEN', G)
    setn('XIN', 'XIN_' + s); setn('XOUT', 'XOUT_' + s); setn('RUN', 'RUN_' + s)
    setn('SWCLK', 'SWCLK_' + s); setn('SWD', 'SWDIO_' + s)
    setn('USB_DP', 'USB_DP_' + s); setn('USB_DM', 'USB_DM_' + s)
    for q in ('SS', 'SCLK', 'SD0', 'SD1', 'SD2', 'SD3'):
        setn('QSPI_' + q, 'QSPI_%s_%s' % (q, s))
    for k, v in gpio.items():
        name = [n for n in _rp if n == k or n.startswith(k + '_')][0]
        setn(name, v)
    return C(ref, 'MCU_RaspberryPi:RP2040', 'RP2040', QFN, pins, pcb, fields={'LCSC': 'C2040'})


def core(s, cx, cy, n0, vin, gpio, fdx=0.0):
    """RP2040 + flash + crystal + LDO + buttons + SWD pads + LED. Reference numbers start at n0."""
    V3, V1, G = '+3V3_' + s, '+1V1_' + s, 'GND_' + s
    def at(dx, dy, rot=0):
        return (cx + dx, cy + dy, rot)
    n = [n0]
    def nx(prefix):
        n[0] += 1
        return '%s%d' % (prefix, n[0])
    c = []
    c.append(rp2040('U%d' % n0, s, gpio, at(0, 0)))
    c.append(C('U%d' % (n0 + 1), 'Memory_Flash:W25Q32JVSS', 'W25Q16JVSSIQ (2 MB)', 'Package_SO:SOIC-8_5.23x5.23mm_P1.27mm',
               {'1': 'QSPI_SS_' + s, '2': 'QSPI_SD1_' + s, '3': 'QSPI_SD2_' + s, '4': G, '5': 'QSPI_SD0_' + s,
                '6': 'QSPI_SCLK_' + s, '7': 'QSPI_SD3_' + s, '8': V3}, at(-11.0 + fdx, -7.4), fields={'LCSC': 'C131024'}))
    c.append(C('U%d' % (n0 + 2), 'Regulator_Linear:AP2112K-3.3', 'AP2112K-3.3', 'Package_TO_SOT_SMD:SOT-23-5',
               {'1': vin, '2': G, '3': vin, '4': None, '5': V3}, at(8.5, -1.0), fields={'LCSC': 'C51118'}))
    c.append(C('Y%d' % n0, 'Device:Crystal_GND24', '12MHz', 'Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm',
               {'1': 'XIN_' + s, '2': G, '3': 'XOUT_R_' + s, '4': G}, at(-1.5, 8.0), fields={'LCSC': 'C9002'}))
    cap = lambda ref, v, a, b, pos: Cap(ref, v, a, b, pos, fp=C0603)
    res = lambda ref, v, a, b, pos: R(ref, v, a, b, pos, fp=R0603)
    c += [
        cap(nx('C'), '15pF', 'XIN_' + s, G, at(-5.0, 8.0, 90)),
        cap(nx('C'), '15pF', 'XOUT_R_' + s, G, at(2.4, 10.0, 0)),
        res(nx('R'), '1k', 'XOUT_' + s, 'XOUT_R_' + s, at(5.4, 6.5, 90)),
        # decoupling
        cap(nx('C'), '100nF', V3, G, at(-5.6, -2.6, 90)),   # IOVDD 1
        cap(nx('C'), '100nF', V3, G, at(-5.6, 1.0, 90)),    # IOVDD 10
        cap(nx('C'), '100nF', V3, G, at(5.6, -2.6, 90)),    # IOVDD 42
        cap(nx('C'), '100nF', V3, G, at(5.6, 1.0, 90)),     # IOVDD 33
        cap(nx('C'), '100nF', V3, G, at(2.2, 6.5, 90)),     # IOVDD 22
        cap(nx('C'), '100nF', V1, G, at(3.8, 6.5, 90)),     # DVDD 23
        cap(nx('C'), '100nF', V1, G, at(0.2, -5.9, 90)),   # DVDD 50
        cap(nx('C'), '100nF', V3, G, at(2.6, -5.9, 90)),    # USB_VDD / IOVDD 49
        cap(nx('C'), '1uF', V3, G, at(4.1, -5.9, 90)),      # VREG_IN
        cap(nx('C'), '1uF', V1, G, at(5.6, -5.9, 90)),      # VREG_VOUT
        cap(nx('C'), '100nF', V3, G, at(7.1, -5.9, 90)),    # ADC_AVDD
        cap(nx('C'), '100nF', V3, G, at(-11.0, -11.9, 0)),  # flash
        Cap(nx('C'), '10uF', vin, G, at(9.0, -5.0, 90)),     # LDO in (0805)
        Cap(nx('C'), '10uF', V3, G, at(8.5, 2.5, 0)),       # LDO out (0805)
        # USB device series resistors
        res(nx('R'), '27R', 'USB_DP_' + s, 'USB_DP_C_' + s, at(0.6, -9.0, 90)),
        res(nx('R'), '27R', 'USB_DM_' + s, 'USB_DM_C_' + s, at(2.2, -9.0, 90)),
        # boot / reset
        res(nx('R'), '1k', 'QSPI_SS_' + s, 'BOOT_' + s, at(-8.0, 1.5, 90)),
        C(nx('SW'), 'Switch:SW_Push', 'BOOTSEL', 'Button_Switch_SMD:SW_SPST_PTS810', {'1': 'BOOT_' + s, '2': G}, at(-12.5, 2.0)),
        res(nx('R'), '10k', V3, 'RUN_' + s, at(-8.0, 5.5, 90)),
        C(nx('SW'), 'Switch:SW_Push', 'RESET', 'Button_Switch_SMD:SW_SPST_PTS810', {'1': 'RUN_' + s, '2': G}, at(-12.5, 6.5)),
        # SWD test pads
        C(nx('TP'), 'Connector:TestPoint', 'SWCLK', 'TestPoint:TestPoint_Pad_D1.0mm', {'1': 'SWCLK_' + s}, at(-14.5, 10.0)),
        C(nx('TP'), 'Connector:TestPoint', 'SWDIO', 'TestPoint:TestPoint_Pad_D1.0mm', {'1': 'SWDIO_' + s}, at(-12.5, 10.0)),
        C(nx('TP'), 'Connector:TestPoint', 'GND', 'TestPoint:TestPoint_Pad_D1.0mm', {'1': G}, at(-10.5, 10.0)),
        # status LED: three 0603 LEDs (red / green / blue) on GPIO16 / 17 / 18, under the case window
        C('D%d' % (n0 + 27), 'Device:LED', 'red', 'LED_SMD:LED_0603_1608Metric', {'1': G, '2': 'LED_RA_' + s}, at(8.5, 4.65)),
        C('D%d' % (n0 + 28), 'Device:LED', 'green', 'LED_SMD:LED_0603_1608Metric', {'1': G, '2': 'LED_GA_' + s}, at(8.5, 6.3)),
        C('D%d' % (n0 + 29), 'Device:LED', 'blue', 'LED_SMD:LED_0603_1608Metric', {'1': G, '2': 'LED_BA_' + s}, at(8.5, 7.95)),
        res('R%d' % (n0 + 27), '1k', 'LED_R_' + s, 'LED_RA_' + s, at(7.0, 10.2, 90)),
        res('R%d' % (n0 + 28), '330R', 'LED_G_' + s, 'LED_GA_' + s, at(8.5, 10.2, 90)),
        res('R%d' % (n0 + 29), '100R', 'LED_B_' + s, 'LED_BA_' + s, at(10.0, 10.2, 90)),
    ]
    return c


gA = {'GPIO2': 'USBH_DP', 'GPIO3': 'USBH_DM', 'GPIO4': 'UART_TX_A', 'GPIO5': 'UART_RX_A',
      'GPIO14': 'SD_SCK', 'GPIO15': 'SD_MOSI', 'GPIO28': 'SD_MISO', 'GPIO29': 'SD_CS', 'GPIO26': 'SD_DET',
      'GPIO24': 'VBUS_SENSE_A', 'GPIO6': 'RGB_DATA', 'GPIO16': 'LED_R_A', 'GPIO17': 'LED_G_A', 'GPIO18': 'LED_B_A'}
gB = {'GPIO4': 'UART_TX_B', 'GPIO5': 'UART_RX_B', 'GPIO16': 'LED_R_B', 'GPIO17': 'LED_G_B', 'GPIO18': 'LED_B_B'}

coreA = core('A', 20, 22, 10, '+5V_SYS_A', gA)
coreB = core('B', 66, 22, 50, 'VBUS_PC2', gB)

comps = [
    # ---- power: adapter (J1) OR'd with PC 1 VBUS, never back-feeds PC 1
    usb_c('J1', 'USB-C 5V IN', '+5V_IN', 'GND_A', 'CC1_IN', 'CC2_IN', pcb=(36, 4.3, 180)),
    R('R1', '5.1k', 'CC1_IN', 'GND_A', (37.6, 12, 90)),
    R('R2', '5.1k', 'CC2_IN', 'GND_A', (34.2, 12, 90)),
    C('D2', 'Device:D_Schottky', 'SS34', 'Diode_SMD:D_SMA', {'1': '+5V_SYS_A', '2': '+5V_IN'}, (32.6, 18.0, 90), fields={'LCSC': 'C8678'}),
    C('D3', 'Device:D_Schottky', 'SS34', 'Diode_SMD:D_SMA', {'1': '+5V_SYS_A', '2': 'VBUS_PC1'}, (26.5, 7.5, 0), fields={'LCSC': 'C8678'}),
    C('F1', 'Device:Polyfuse', '2A hold (1812)', 'Fuse:Fuse_1812_4532Metric', {'1': '+5V_SYS_A', '2': '+5V_HOST'}, (41, 16, 90)),
    C('C1', 'Device:C_Polarized', '100uF 10V', 'Capacitor_SMD:CP_Elec_6.3x7.7', {'1': '+5V_HOST', '2': 'GND_A'}, (35.2, 27.0, 0)),
    Cap('C2', '100nF', '+5V_HOST', 'GND_A', (39.5, 35.2, 0)),
    # ---- PC 1 USB-C (data + VBUS sense only)
    usb_c('J4', 'USB-C to PC 1', 'VBUS_PC1', 'GND_A', 'CC1_PC1', 'CC2_PC1', 'USB_DP_C_A', 'USB_DM_C_A', pcb=(17, 4.3, 180)),
    R('R40', '5.1k', 'CC1_PC1', 'GND_A', (24.0, 10.8, 90), fp=R0603),
    R('R41', '5.1k', 'CC2_PC1', 'GND_A', (25.6, 10.8, 90), fp=R0603),
    R('R42', '10k', 'VBUS_PC1', 'VBUS_SENSE_A', (5.0, 37.0, 90), fp=R0603),
    esd('U5', 'USB_DM_C_A', 'USB_DP_C_A', 'VBUS_PC1', 'GND_A', (17.0, 11.45, 0)),
    R('R43', '20k', 'VBUS_SENSE_A', 'GND_A', (6.6, 37.0, 90), fp=R0603),
    # ---- USB host port
    usb_a('J2', '+5V_HOST', 'USBH_DM_C', 'USBH_DP_C', 'GND_A', (30.5, 40.5, 0)),
    R('R3', '22R', 'USBH_DP', 'USBH_DP_C', (35, 35, 90), fp=R0603),
    R('R4', '22R', 'USBH_DM', 'USBH_DM_C', (32, 35, 90), fp=R0603),
    esd('U4', 'USBH_DP_C', 'USBH_DM_C', '+5V_HOST', 'GND_A', (38.0, 32.4, 0)),
    # ---- microSD
    microsd('J3', 'SD_CS', 'SD_MOSI', 'SD_SCK', 'SD_MISO', 'SD_DAT1', 'SD_DAT2', 'SD_DET', '+3V3_A', 'GND_A', (17, 47.5, 0)),
    R('R5', '10k', '+3V3_A', 'SD_CS', (12, 36, 90), fp=R0603),
    R('R6', '10k', '+3V3_A', 'SD_MISO', (14, 36, 90), fp=R0603),
    R('R7', '10k', '+3V3_A', 'SD_DAT1', (16, 36, 90), fp=R0603),
    R('R8', '10k', '+3V3_A', 'SD_DAT2', (18, 36, 90), fp=R0603),
    Cap('C5', '10uF', '+3V3_A', 'GND_A', (6.5, 44.0, 90)),
    Cap('C6', '100nF', '+3V3_A', 'GND_A', (8.4, 41.5, 90), fp=C0603),
    # ---- isolation
    isolator('U3', '+3V3_A', 'GND_A', '+3V3_B', 'GND_B', 'UART_TX_A', 'UART_RX_A', 'UART_TX_B', 'UART_RX_B', (44, 29, 0)),
    Cap('C3', '100nF', '+3V3_A', 'GND_A', (41.4, 24.4, 90), fp=C0603),
    Cap('C4', '100nF', '+3V3_B', 'GND_B', (46.6, 24.4, 90), fp=C0603),
    # ---- PC 2 USB-C (powers side B)
    usb_c('J5', 'USB-C to PC 2', 'VBUS_PC2', 'GND_B', 'CC1_PC2', 'CC2_PC2', 'USB_DP_C_B', 'USB_DM_C_B', pcb=(63, 4.3, 180)),
    R('R44', '5.1k', 'CC1_PC2', 'GND_B', (70.0, 10.8, 90), fp=R0603),
    R('R45', '5.1k', 'CC2_PC2', 'GND_B', (71.6, 10.8, 90), fp=R0603),
    esd('U6', 'USB_DM_C_B', 'USB_DP_C_B', 'VBUS_PC2', 'GND_B', (63.0, 11.45, 0)),
    # ---- optional external RGB LED (WS2812 / SK6812 chain): 5V, DIN, GND
    C('J6', 'Connector_Generic:Conn_01x03', 'RGB LED (WS2812): 5V DIN GND', 'Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical',
      {'1': '+5V_SYS_A', '2': 'RGB_DIN', '3': 'GND_A'}, (22.5, 35.5, 90)),
    R('R9', '330R', 'RGB_DATA', 'RGB_DIN', (21.0, 39.2, 0), fp=R0603),
    hole('H1', (3.5, 3.5, 0)), hole('H2', (76.5, 3.5, 0)), hole('H3', (3.5, 50.5, 0)), hole('H4', (76.5, 50.5, 0)),
] + coreA + coreB

def _pick(refs):
    return [c for c in comps if c['ref'] in refs]

blocks = [
    ('Power input - 5 V adapter (J1) OR-ed with PC 1 VBUS (D2/D3)', _pick(('J1', 'R1', 'R2', 'D2', 'D3', 'F1', 'C1', 'C2')),
     'D2/D3 make the adapter and PC 1 share the load without ever back-feeding each other.\n'
     'Works without the adapter as long as PC 1 is on; with the adapter the keyboard works while PC 1 is off.'),
    ('PC 1 USB-C (device) + VBUS sense on GPIO24', _pick(('J4', 'U5', 'R40', 'R41', 'R42', 'R43')), None),
    ('MCU-A - RP2040 (USB host, router, microSD)', coreA,
     'GPIO2/3 = PIO-USB host, GPIO4/5 = UART1, GPIO14/15/28/29 = SPI1 + CS, GPIO26 = card detect, GPIO16/17/18 = status LED red/green/blue.'),
    ('USB host port (keyboard, mouse, dongle or hub)', _pick(('J2', 'U4', 'R3', 'R4')), None),
    ('microSD - shared drive', _pick(('J3', 'R5', 'R6', 'R7', 'R8', 'C5', 'C6')), None),
    ('Galvanic isolation (GND_A and GND_B never meet)', _pick(('U3', 'C3', 'C4')),
     'CA-IS3721HS (pin compatible with ADuM1201).'),
    ('PC 2 USB-C (device, powers side B)', _pick(('J5', 'U6', 'R44', 'R45')), None),
    ('MCU-B - RP2040', coreB, None),
    ('Optional RGB LED header (WS2812/SK6812): 5V, DIN, GND', _pick(('J6', 'R9')),
     'Data from GPIO6 through R9 (same pin as variant A).'),
    ('Mechanical', _pick(('H1', 'H2', 'H3', 'H4')), None),
]

pwr_flags = ['+5V_IN', 'VBUS_PC1', 'VBUS_PC2', '+5V_SYS_A', 'GND_A', 'GND_B']

W, H = 80, 54
design = dict(
    size=(W, H), comps=comps, fab_refs=True, fab_ref_list=('J4', 'J5'), default_track=0.16, default_clearance=0.15,
    keepouts=[(43.3, 0.5, 44.7, 53.5)],
    pours=[('GND_A', [(0.5, 0.5), (42.9, 0.5), (42.9, 53.5), (0.5, 53.5)]),
           ('GND_B', [(45.1, 0.5), (79.5, 0.5), (79.5, 53.5), (45.1, 53.5)])],
    power_nets={'+5V_IN': 0.6, '+5V_HOST': 0.8, '+5V_SYS_A': 0.6, 'VBUS_PC1': 0.5, 'VBUS_PC2': 0.5},
    stubs=[(j, p, 0, 1.7, 0.5, True) for j in ('J1', 'J4', 'J5') for p in ('A9', 'A4')] + [('U4', '5', 1.3, 0, 0.4, True)],
    texts=[('kmswitch  B  rev 1.0', 64, 42, 1.2), ('2x RP2040', 64, 44.5, 0.9),
           ('PC 1', 24.8, 14.0, 0.8), ('PC 2', 70.8, 14.0, 0.8), ('5V IN', 29, 11, 0.8),
           ('BOOT', 3.9, 24, 0.8, 'F', 90), ('RST', 3.9, 28.5, 0.8, 'F', 90),
           ('BOOT', 49.9, 24, 0.8, 'F', 90), ('RST', 49.9, 28.5, 0.8, 'F', 90),
           ('SWD', 3.4, 32, 0.8, 'F', 90), ('SWD', 49.4, 32, 0.8, 'F', 90),
           ('RGB', 25.0, 33.4, 0.8), ('ISOLATION', 44, 46, 0.8), ('A', 41.5, 51, 1.0), ('B', 46.5, 51, 1.0)],
)
PASSES = 120
PAPER = ('A1', 841, 594)
COLS = 2
