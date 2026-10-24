# Décode un fichier PacketLogger (hci_dump.pklg) de BTstack : messages de log + infos de version de la puce.
import struct, sys
data = open(sys.argv[1], "rb").read(); i = 0
types = {0: "CMD", 1: "EVT", 2: "ACL>", 3: "ACL<", 0xfc: "LOG"}
while i + 4 <= len(data):
    (n,) = struct.unpack_from(">I", data, i); rec = data[i + 4:i + 4 + n]; i += 4 + n
    t, p = rec[8], rec[9:]
    if t == 0xfc:
        print("LOG ", p.decode(errors="replace").rstrip("\x00"))
    elif t == 1 and p[0] == 0x0e and p[3:5] == b"\x01\x10":   # Command Complete: Read Local Version Information
        st, hv, hr, lv, man, ls = struct.unpack_from("<BBHBHH", p, 5)
        print(f"VERSION status={st} HCI={hv} rev=0x{hr:04x} LMP={lv} fabricant=0x{man:04x} LMP_subver=0x{ls:04x}")
    elif t == 1 and p[0] == 0x0e and p[3:5] == b"\x09\x10":   # Read BD_ADDR
        print("BD_ADDR", ":".join(f"{b:02X}" for b in reversed(p[6:12])))
    elif t in (0, 1) and len(sys.argv) > 2:
        print(types[t], p[:16].hex())
