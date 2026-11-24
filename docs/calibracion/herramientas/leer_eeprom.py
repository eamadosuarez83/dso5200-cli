# Lectura (SOLO LECTURA) de la EEPROM del DSO-5200A con el comando vendor A2 en direccion IN.
# Nunca se envia bmRequestType 0x40 (escritura).
import sys, usb.core
dev = usb.core.find(idVendor=0x04b5, idProduct=0x520a) or usb.core.find(idVendor=0x04b5, idProduct=0x5200)
if dev is None: sys.exit("No hay DSO-5200 con firmware cargado (04b5:520a)")
size = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x400
data = bytearray()
for addr in range(0, size, 64):
    data += bytes(dev.ctrl_transfer(0xC0, 0xA2, addr, 0, 64, timeout=1000))
out = sys.argv[2] if len(sys.argv) > 2 else 'eeprom.bin'
open(out, 'wb').write(data)
print("leidos", len(data), "bytes ->", out)
