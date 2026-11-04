"""Variant A: carrier board for two Waveshare RP2040-Zero modules."""
from common import *

ZERO = 'kmswitch:RP2040-Zero_Module'

def zero(ref, value, nets, pcb):
    """nets by function name -> pad number."""
    pad = {'5V': '1', 'GND': '2', '3V3': '3', 'GP29': '4', 'GP28': '5', 'GP27': '6', 'GP26': '7', 'GP15': '8',
           'GP14': '9', 'GP8': '15', 'GP7': '16', 'GP6': '17', 'GP5': '18', 'GP4': '19', 'GP3': '20', 'GP2': '21',
           'GP1': '22', 'GP0': '23'}
    pins = {num: None for num in pad.values()}
    for k, v in nets.items():
        pins[pad[k]] = v
    return C(ref, 'kmswitch:RP2040-Zero', value, ZERO, pins, pcb)

comps = [
    # ---- power input (5 V adapter via USB-C, power only)
    usb_c('J1', 'USB-C 5V IN', '+5V_IN', 'GND_A', 'CC1_IN', 'CC2_IN', pcb=(36, 4.3, 180)),
    R('R1', '5.1k', 'CC1_IN', 'GND_A', (37.6, 12, 90)),
    R('R2', '5.1k', 'CC2_IN', 'GND_A', (34.2, 12, 90)),
    C('F1', 'Device:Polyfuse', '2A hold (1812)', 'Fuse:Fuse_1812_4532Metric', {'1': '+5V_IN', '2': '+5V_HOST'}, (41, 16, 90)),
    C('D1', 'Device:D_Schottky', 'B5819W', 'Diode_SMD:D_SOD-123', {'1': '+5V_A', '2': '+5V_IN'}, (29, 16, 0)),
    C('C1', 'Device:C_Polarized', '100uF 10V', 'Capacitor_SMD:CP_Elec_6.3x7.7', {'1': '+5V_HOST', '2': 'GND_A'}, (34, 21, 0)),
    Cap('C2', '100nF', '+5V_HOST', 'GND_A', (38.6, 27.6, 0)),
    # ---- MCU-A (USB host, router, SD) -> PC 1 via its own USB-C
    zero('U1', 'RP2040-Zero (MCU-A)', {'5V': '+5V_A', 'GND': 'GND_A', '3V3': '+3V3_A',
          'GP2': 'USBH_DP', 'GP3': 'USBH_DM', 'GP4': 'UART_TX_A', 'GP5': 'UART_RX_A',
          'GP14': 'SD_SCK', 'GP15': 'SD_MOSI', 'GP28': 'SD_MISO', 'GP29': 'SD_CS', 'GP26': 'SD_DET',
          'GP6': 'RGB_DATA'},
         (17, 11.75, 0)),
    # ---- USB host port
    usb_a('J2', '+5V_HOST', 'USBH_DM_C', 'USBH_DP_C', 'GND_A', (30.5, 34.5, 0)),
    R('R3', '22R', 'USBH_DP', 'USBH_DP_C', (35, 29, 90)),
    R('R4', '22R', 'USBH_DM', 'USBH_DM_C', (32, 29, 90)),
    esd('U4', 'USBH_DP_C', 'USBH_DM_C', '+5V_HOST', 'GND_A', (28.0, 29.9, 0)),
    # ---- microSD
    microsd('J3', 'SD_CS', 'SD_MOSI', 'SD_SCK', 'SD_MISO', 'SD_DAT1', 'SD_DAT2', 'SD_DET', '+3V3_A', 'GND_A', (17, 41.5, 0)),
    R('R5', '10k', '+3V3_A', 'SD_CS', (11, 31, 90)),
    R('R6', '10k', '+3V3_A', 'SD_MISO', (14, 31, 90)),
    R('R7', '10k', '+3V3_A', 'SD_DAT1', (17, 31, 90)),
    R('R8', '10k', '+3V3_A', 'SD_DAT2', (20, 31, 90)),
    Cap('C5', '10uF', '+3V3_A', 'GND_A', (6.5, 37, 90)),
    Cap('C6', '100nF', '+3V3_A', 'GND_A', (6.5, 32.5, 90)),
    # ---- isolation
    isolator('U3', '+3V3_A', 'GND_A', '+3V3_B', 'GND_B', 'UART_TX_A', 'UART_RX_A', 'UART_TX_B', 'UART_RX_B', (44, 27, 0)),
    Cap('C3', '100nF', '+3V3_A', 'GND_A', (40.4, 22.6, 90)),
    Cap('C4', '100nF', '+3V3_B', 'GND_B', (47.6, 22.6, 90)),
    # ---- MCU-B -> PC 2 (powered by PC 2 through its own USB-C)
    zero('U2', 'RP2040-Zero (MCU-B)', {'GND': 'GND_B', '3V3': '+3V3_B', 'GP4': 'UART_TX_B', 'GP5': 'UART_RX_B'},
         (63, 11.75, 0)),
    # ---- optional external RGB LED (WS2812 / SK6812 chain): 5V, DIN, GND
    C('J6', 'Connector_Generic:Conn_01x03', 'RGB LED (WS2812): 5V DIN GND', 'Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical',
      {'1': '+5V_IN', '2': 'RGB_DIN', '3': 'GND_A'}, (24.0, 26.9, 270)),
    R('R9', '330R', 'RGB_DATA', 'RGB_DIN', (23.5, 30.5, 0)),
    hole('H1', (3.5, 3.5, 0)), hole('H2', (76.5, 3.5, 0)), hole('H3', (3.5, 44.5, 0)), hole('H4', (76.5, 44.5, 0)),
]

blocks = [
    ('Power input - 5 V adapter on USB-C (power only)', [c for c in comps if c['ref'] in ('J1', 'R1', 'R2', 'F1', 'D1', 'C1', 'C2')],
     'Adapter 5 V, 2-3 A. F1 protects the USB-A port. D1 feeds MCU-A so the keyboard works while PC 1 is off.\n'
     'Note: with PC 1 switched off and its cable plugged in, ~4.7 V reaches PC 1 VBUS through the module. Variant B avoids this.'),
    ('MCU-A - USB host, router, microSD (connects to PC 1 through its own USB-C)', [c for c in comps if c['ref'] == 'U1'],
     'GP2/GP3 = PIO-USB host D+/D-, GP4/GP5 = UART1 TX/RX, GP14/15/28/29 = SPI1 SCK/MOSI/MISO + CS, GP26 = card detect, GP16 = on-board WS2812.'),
    ('USB host port (keyboard, mouse, dongle or hub)', [c for c in comps if c['ref'] in ('J2', 'U4', 'R3', 'R4')], None),
    ('microSD - shared drive', [c for c in comps if c['ref'] in ('J3', 'R5', 'R6', 'R7', 'R8', 'C5', 'C6')], None),
    ('Galvanic isolation between PC 1 and PC 2 (GND_A and GND_B never meet)', [c for c in comps if c['ref'] in ('U3', 'C3', 'C4')],
     'CA-IS3721HS (pin compatible with ADuM1201). UART runs up to ~3 Mbaud; check the grade you buy.'),
    ('MCU-B - connects to PC 2 through its own USB-C', [c for c in comps if c['ref'] == 'U2'], None),
    ('Optional RGB LED header (WS2812/SK6812): 5V, DIN, GND', [c for c in comps if c['ref'] in ('J6', 'R9')],
     'Data from GP6 through R9. Most WS2812B/SK6812 accept 3.3 V data at 5 V supply; keep the wire short.'),
    ('Mechanical', [c for c in comps if c['ref'].startswith('H')], None),
]

pwr_flags = ['+5V_IN', '+5V_A', 'GND_A', 'GND_B']

W, H = 80, 48
design = dict(
    size=(W, H), comps=comps,
    keepouts=[(43.3, 0.5, 44.7, 47.5)],
    pours=[('GND_A', [(0.5, 0.5), (42.9, 0.5), (42.9, 47.5), (0.5, 47.5)]),
           ('GND_B', [(45.1, 0.5), (79.5, 0.5), (79.5, 47.5), (45.1, 47.5)])],
    power_nets={'+5V_IN': 0.6, '+5V_HOST': 0.8, '+5V_A': 0.5, '+3V3_A': 0.4, '+3V3_B': 0.4},
    stubs=[('J1', 'A9', 0, 1.7, 0.5, True), ('J1', 'A4', 0, 1.7, 0.5, True), ('U4', '5', 1.3, 0, 0.4, True)],
    texts=[('kmswitch  A  rev 1.0', 62, 34, 1.2), ('RP2040-Zero carrier', 62, 36.5, 0.9),
           ('PC 1', 12.5, 26.8, 1.0), ('PC 2', 63, 26.8, 1.0), ('5V IN', 29, 11, 0.8), ('RGB', 26.5, 24.6, 0.8),
('ISOLATION', 44, 40, 0.8), ('A', 41.5, 45, 1.0), ('B', 46.5, 45, 1.0)],
)
COLS = 2
