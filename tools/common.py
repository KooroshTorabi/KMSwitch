"""Shared building blocks for both board variants."""

R0805 = 'Resistor_SMD:R_0805_2012Metric'
C0805 = 'Capacitor_SMD:C_0805_2012Metric'
R0603 = 'Resistor_SMD:R_0603_1608Metric'
C0603 = 'Capacitor_SMD:C_0603_1608Metric'
USBC = 'Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12'
USBA = 'Connector_USB:USB_A_Molex_67643_Horizontal'
SOIC8 = 'Package_SO:SOIC-8_3.9x4.9mm_P1.27mm'
HOLE = 'MountingHole:MountingHole_2.7mm_M2.5'


def C(ref, lib_id, value, footprint, pins, pcb=None, **kw):
    d = dict(ref=ref, lib_id=lib_id, value=value, footprint=footprint, pins=pins)
    if pcb is not None:
        d['pcb'] = pcb
    d.update(kw)
    return d


def R(ref, value, a, b, pcb, fp=R0805, **kw):
    return C(ref, 'Device:R', value, fp, {'1': a, '2': b}, pcb, **kw)


def Cap(ref, value, a, b, pcb, fp=C0805, **kw):
    return C(ref, 'Device:C', value, fp, {'1': a, '2': b}, pcb, **kw)


def usb_c(ref, value, vbus, gnd, cc1, cc2, dp=None, dm=None, pcb=None):
    pins = {}
    for p in ('A4', 'A9', 'B4', 'B9'):
        pins[p] = vbus
    for p in ('A1', 'A12', 'B1', 'B12', 'S1'):
        pins[p] = gnd
    pins['A5'] = cc1
    pins['B5'] = cc2
    pins['A6'] = pins['B6'] = dp
    pins['A7'] = pins['B7'] = dm
    pins['A8'] = pins['B8'] = None
    return C(ref, 'Connector:USB_C_Receptacle_USB2.0_16P', value, USBC, pins, pcb,
             fields={'LCSC': 'C165948'})


def usb_a(ref, vbus, dm, dp, gnd, pcb):
    return C(ref, 'Connector:USB_A', 'USB-A host (hub/keyboard)', USBA,
             {'1': vbus, '2': dm, '3': dp, '4': gnd, '5': gnd}, pcb)


def isolator(ref, vdd1, gnd1, vdd2, gnd2, tx1, rx1, tx2, rx2, pcb):
    # ADuM1201 pinout (CA-IS3721HS is pin compatible): ch A side2->side1, ch B side1->side2
    return C(ref, 'Isolator:ADuM1201AR', 'CA-IS3721HS', SOIC8,
             {'1': vdd1, '2': rx1, '3': tx1, '4': gnd1, '5': gnd2, '6': rx2, '7': tx2, '8': vdd2}, pcb,
             fields={'Alt': 'ADuM1201BRZ (>=10 Mbps grade)', 'LCSC': 'C428884'})


def microsd(ref, cs, mosi, sck, miso, dat1, dat2, det, vdd, gnd, pcb):
    return C(ref, 'kmswitch:MicroSD_Molex_104031', '104031-0811', 'Connector_Card:microSD_HC_Molex_104031-0811',
             {'1': dat2, '2': cs, '3': mosi, '4': vdd, '5': sck, '6': gnd, '7': miso, '8': dat1,
              '9': det, '10': gnd, '11': gnd}, pcb)


def esd(ref, dp, dm, vbus, gnd, pcb):
    """USBLC6-2SC6 TVS array: D+ through pins 1/6, D- through 3/4."""
    return C(ref, 'Power_Protection:USBLC6-2SC6', 'USBLC6-2SC6', 'Package_TO_SOT_SMD:SOT-23-6',
             {'1': dp, '6': dp, '3': dm, '4': dm, '5': vbus, '2': gnd}, pcb, fields={'LCSC': 'C7519'})


def hole(ref, pcb):
    return C(ref, 'Mechanical:MountingHole', 'M2.5', HOLE, {}, pcb)
