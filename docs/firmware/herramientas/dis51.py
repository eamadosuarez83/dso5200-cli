# Desensamblador 8051 minimo con descenso recursivo y nombres de registros del Cypress FX2.
import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from ihex import load

SFR = {0x80:'IOA',0x81:'SP',0x82:'DPL',0x83:'DPH',0x84:'DPL1',0x85:'DPH1',0x86:'DPS',0x87:'PCON',
 0x88:'TCON',0x89:'TMOD',0x8A:'TL0',0x8B:'TL1',0x8C:'TH0',0x8D:'TH1',0x8E:'CKCON',0x90:'IOB',
 0x91:'EXIF',0x92:'MPAGE',0x98:'SCON0',0x99:'SBUF0',0x9A:'AUTOPTRH1',0x9B:'AUTOPTRL1',
 0x9D:'AUTOPTRH2',0x9E:'AUTOPTRL2',0xA0:'IOC',0xA1:'INT2CLR',0xA2:'INT4CLR',0xA8:'IE',
 0xAA:'EP2468STAT',0xAB:'EP24FIFOFLGS',0xAC:'EP68FIFOFLGS',0xAF:'AUTOPTRSETUP',0xB0:'IOD',
 0xB1:'IOE',0xB2:'OEA',0xB3:'OEB',0xB4:'OEC',0xB5:'OED',0xB6:'OEE',0xB8:'IP',0xBA:'EP01STAT',
 0xBB:'GPIFTRIG',0xBD:'GPIFSGLDATH',0xBE:'GPIFSGLDATLX',0xBF:'GPIFSGLDATLNOX',0xC0:'SCON1',
 0xC1:'SBUF1',0xC8:'T2CON',0xCA:'RCAP2L',0xCB:'RCAP2H',0xCC:'TL2',0xCD:'TH2',0xD0:'PSW',
 0xD8:'EICON',0xE0:'ACC',0xE8:'EIE',0xF0:'B',0xF8:'EIP'}
XREG = {0xE600:'CPUCS',0xE601:'IFCONFIG',0xE602:'PINFLAGSAB',0xE603:'PINFLAGSCD',0xE604:'FIFORESET',
 0xE609:'FIFOPINPOLAR',0xE60A:'REVID',0xE60B:'REVCTL',0xE60C:'GPIFHOLDAMOUNT',0xE610:'EP1OUTCFG',
 0xE611:'EP1INCFG',0xE612:'EP2CFG',0xE613:'EP4CFG',0xE614:'EP6CFG',0xE615:'EP8CFG',0xE618:'EP2FIFOCFG',
 0xE619:'EP4FIFOCFG',0xE61A:'EP6FIFOCFG',0xE61B:'EP8FIFOCFG',0xE620:'EP2AUTOINLENH',0xE621:'EP2AUTOINLENL',
 0xE624:'EP6AUTOINLENH',0xE625:'EP6AUTOINLENL',0xE630:'EP2FIFOPFH',0xE631:'EP2FIFOPFL',0xE634:'EP6FIFOPFH',
 0xE635:'EP6FIFOPFL',0xE648:'INPKTEND',0xE649:'OUTPKTEND',0xE650:'EP2FIFOIE',0xE651:'EP2FIFOIRQ',
 0xE658:'IBNIE',0xE659:'IBNIRQ',0xE65A:'NAKIE',0xE65B:'NAKIRQ',0xE65C:'USBIE',0xE65D:'USBIRQ',
 0xE65E:'EPIE',0xE65F:'EPIRQ',0xE660:'GPIFIE',0xE661:'GPIFIRQ',0xE662:'USBERRIE',0xE663:'USBERRIRQ',
 0xE668:'INTSETUP',0xE670:'PORTACFG',0xE671:'PORTCCFG',0xE672:'PORTECFG',0xE678:'I2CS',0xE679:'I2DAT',
 0xE67A:'I2CTL',0xE680:'USBCS',0xE681:'SUSPEND',0xE682:'WAKEUPCS',0xE683:'TOGCTL',0xE684:'USBFRAMEH',
 0xE685:'USBFRAMEL',0xE687:'FNADDR',0xE68A:'EP0BCH',0xE68B:'EP0BCL',0xE68D:'EP1OUTBC',0xE68F:'EP1INBC',
 0xE690:'EP2BCH',0xE691:'EP2BCL',0xE698:'EP6BCH',0xE699:'EP6BCL',0xE6A0:'EP0CS',0xE6A1:'EP1OUTCS',
 0xE6A2:'EP1INCS',0xE6A3:'EP2CS',0xE6A5:'EP6CS',0xE6A7:'EP2FIFOFLGS',0xE6A9:'EP6FIFOFLGS',
 0xE6AB:'EP2FIFOBCH',0xE6AC:'EP2FIFOBCL',0xE6AF:'EP6FIFOBCH',0xE6B0:'EP6FIFOBCL',0xE6B3:'SUDPTRH',
 0xE6B4:'SUDPTRL',0xE6B5:'SUDPTRCTL',0xE6B8:'SETUPDAT0_bmRequestType',0xE6B9:'SETUPDAT1_bRequest',
 0xE6BA:'SETUPDAT2_wValueL',0xE6BB:'SETUPDAT3_wValueH',0xE6BC:'SETUPDAT4_wIndexL',0xE6BD:'SETUPDAT5_wIndexH',
 0xE6BE:'SETUPDAT6_wLengthL',0xE6BF:'SETUPDAT7_wLengthH',0xE6C0:'GPIFWFSELECT',0xE6C1:'GPIFIDLECS',
 0xE6C2:'GPIFIDLECTL',0xE6C3:'GPIFCTLCFG',0xE6C4:'GPIFADRH',0xE6C5:'GPIFADRL',0xE6CE:'GPIFTCB3',
 0xE6CF:'GPIFTCB2',0xE6D0:'GPIFTCB1',0xE6D1:'GPIFTCB0',0xE6D2:'EP2GPIFFLGSEL',0xE6D3:'EP2GPIFPFSTOP',
 0xE6D4:'EP2GPIFTRIG',0xE6E2:'EP6GPIFFLGSEL',0xE6E3:'EP6GPIFPFSTOP',0xE6E4:'EP6GPIFTRIG',
 0xE6F0:'XGPIFSGLDATH',0xE6F1:'XGPIFSGLDATLX',0xE6F2:'XGPIFSGLDATLNOX',0xE6F3:'GPIFREADYCFG',
 0xE6F4:'GPIFREADYSTAT',0xE6F5:'GPIFABORT'}
def xname(a):
    if a in XREG: return XREG[a]
    if 0xE400 <= a < 0xE480: return 'GPIF_WAVEFORM+%02X' % (a - 0xE400)
    if 0xE740 <= a < 0xE780: return 'EP0BUF+%02X' % (a - 0xE740)
    if 0xF000 <= a < 0xF400: return 'EP2FIFOBUF+%03X' % (a - 0xF000)
    if 0xF800 <= a < 0xFC00: return 'EP6FIFOBUF+%03X' % (a - 0xF800)
    return None
def d(x): return SFR.get(x, '%02Xh' % x) if x >= 0x80 else '%02Xh' % x
def bit(x):
    if x < 0x80: return '%02Xh.%d' % (0x20 + x // 8, x % 8)
    base = x & 0xF8
    return '%s.%d' % (SFR.get(base, '%02Xh' % base), x & 7)
REG = ['R%d' % i for i in range(8)]

def decode(img, pc):
    """-> (texto, longitud, destinos, termina, llamada)"""
    g = lambda k: img.get(pc + k, 0)
    op = g(0); hi, lo = op >> 4, op & 0xF
    rel = lambda off, n: (pc + n + (off - 256 if off > 127 else off)) & 0xFFFF
    if lo == 1:
        tgt = ((pc + 2) & 0xF800) | ((op >> 5) << 8) | g(1)
        return (('ACALL' if hi & 1 else 'AJMP') + ' L%04X' % tgt, 2, [tgt], not (hi & 1), bool(hi & 1))
    if lo >= 8: r = REG[lo - 8]; rn = True
    else: rn = False
    ind = {6: '@R0', 7: '@R1'}
    T = {
     0x00:('NOP',1),0x02:None,0x03:('RR A',1),0x04:('INC A',1),0x05:('INC %s'%d(g(1)),2),
     0x10:None,0x12:None,0x13:('RRC A',1),0x14:('DEC A',1),0x15:('DEC %s'%d(g(1)),2),
     0x20:None,0x22:('RET',1),0x23:('RL A',1),0x24:('ADD A,#%02Xh'%g(1),2),0x25:('ADD A,%s'%d(g(1)),2),
     0x30:None,0x32:('RETI',1),0x33:('RLC A',1),0x34:('ADDC A,#%02Xh'%g(1),2),0x35:('ADDC A,%s'%d(g(1)),2),
     0x40:None,0x42:('ORL %s,A'%d(g(1)),2),0x43:('ORL %s,#%02Xh'%(d(g(1)),g(2)),3),0x44:('ORL A,#%02Xh'%g(1),2),0x45:('ORL A,%s'%d(g(1)),2),
     0x50:None,0x52:('ANL %s,A'%d(g(1)),2),0x53:('ANL %s,#%02Xh'%(d(g(1)),g(2)),3),0x54:('ANL A,#%02Xh'%g(1),2),0x55:('ANL A,%s'%d(g(1)),2),
     0x60:None,0x62:('XRL %s,A'%d(g(1)),2),0x63:('XRL %s,#%02Xh'%(d(g(1)),g(2)),3),0x64:('XRL A,#%02Xh'%g(1),2),0x65:('XRL A,%s'%d(g(1)),2),
     0x70:None,0x72:('ORL C,%s'%bit(g(1)),2),0x73:('JMP @A+DPTR',1),0x74:('MOV A,#%02Xh'%g(1),2),0x75:('MOV %s,#%02Xh'%(d(g(1)),g(2)),3),
     0x80:None,0x82:('ANL C,%s'%bit(g(1)),2),0x83:('MOVC A,@A+PC',1),0x84:('DIV AB',1),0x85:('MOV %s,%s'%(d(g(2)),d(g(1))),3),
     0x90:None,0x92:('MOV %s,C'%bit(g(1)),2),0x93:('MOVC A,@A+DPTR',1),0x94:('SUBB A,#%02Xh'%g(1),2),0x95:('SUBB A,%s'%d(g(1)),2),
     0xA0:('ORL C,/%s'%bit(g(1)),2),0xA2:('MOV C,%s'%bit(g(1)),2),0xA3:('INC DPTR',1),0xA4:('MUL AB',1),0xA5:('DB A5h',1),
     0xB0:('ANL C,/%s'%bit(g(1)),2),0xB2:('CPL %s'%bit(g(1)),2),0xB3:('CPL C',1),0xB4:None,0xB5:None,
     0xC0:('PUSH %s'%d(g(1)),2),0xC2:('CLR %s'%bit(g(1)),2),0xC3:('CLR C',1),0xC4:('SWAP A',1),0xC5:('XCH A,%s'%d(g(1)),2),
     0xD0:('POP %s'%d(g(1)),2),0xD2:('SETB %s'%bit(g(1)),2),0xD3:('SETB C',1),0xD4:('DA A',1),0xD5:None,
     0xE0:('MOVX A,@DPTR',1),0xE2:('MOVX A,@R0',1),0xE3:('MOVX A,@R1',1),0xE4:('CLR A',1),0xE5:('MOV A,%s'%d(g(1)),2),
     0xF0:('MOVX @DPTR,A',1),0xF2:('MOVX @R0,A',1),0xF3:('MOVX @R1,A',1),0xF4:('CPL A',1),0xF5:('MOV %s,A'%d(g(1)),2),
    }
    if op == 0x02: t = g(1) << 8 | g(2); return ('LJMP L%04X' % t, 3, [t], True, False)
    if op == 0x12: t = g(1) << 8 | g(2); return ('LCALL L%04X' % t, 3, [t], False, True)
    if op in (0x10, 0x20, 0x30):
        t = rel(g(2), 3); return ('%s %s,L%04X' % ({0x10:'JBC',0x20:'JB',0x30:'JNB'}[op], bit(g(1)), t), 3, [t], False, False)
    if op in (0x40, 0x50, 0x60, 0x70):
        t = rel(g(1), 2); return ('%s L%04X' % ({0x40:'JC',0x50:'JNC',0x60:'JZ',0x70:'JNZ'}[op], t), 2, [t], False, False)
    if op == 0x80: t = rel(g(1), 2); return ('SJMP L%04X' % t, 2, [t], True, False)
    if op == 0x90:
        v = g(1) << 8 | g(2); n = xname(v)
        return ('MOV DPTR,#%04Xh' % v + ('   ; %s' % n if n else ''), 3, [], False, False)
    if op in (0xB4, 0xB5):
        t = rel(g(2), 3); src = '#%02Xh' % g(1) if op == 0xB4 else d(g(1))
        return ('CJNE A,%s,L%04X' % (src, t), 3, [t], False, False)
    if op in (0xB6, 0xB7): t = rel(g(2), 3); return ('CJNE %s,#%02Xh,L%04X' % (ind[lo], g(1), t), 3, [t], False, False)
    if 0xB8 <= op <= 0xBF: t = rel(g(2), 3); return ('CJNE %s,#%02Xh,L%04X' % (r, g(1), t), 3, [t], False, False)
    if op == 0xD5: t = rel(g(2), 3); return ('DJNZ %s,L%04X' % (d(g(1)), t), 3, [t], False, False)
    if 0xD8 <= op <= 0xDF: t = rel(g(1), 2); return ('DJNZ %s,L%04X' % (r, t), 2, [t], False, False)
    if op in T and T[op]:
        txt, n = T[op]; return (txt, n, [], op in (0x22, 0x32, 0x73), False)
    # columnas regulares 6..F
    m = {0x0:'INC',0x1:'DEC',0x2:'ADD A,',0x3:'ADDC A,',0x4:'ORL A,',0x5:'ANL A,',0x6:'XRL A,',0x9:'SUBB A,',
         0xC:'XCH A,',0xE:'MOV A,'}
    opnd = r if rn else ind.get(lo)
    if hi in m and opnd: return (m[hi] + ('' if m[hi].endswith(',') else ' ') + opnd, 1, [], False, False)
    if hi == 0x7 and opnd: return ('MOV %s,#%02Xh' % (opnd, g(1)), 2, [], False, False)
    if hi == 0x8 and opnd: return ('MOV %s,%s' % (d(g(1)), opnd), 2, [], False, False)
    if hi == 0xA and opnd: return ('MOV %s,%s' % (opnd, d(g(1))), 2, [], False, False)
    if hi == 0xD and lo in (6, 7): return ('XCHD A,%s' % opnd, 1, [], False, False)
    if hi == 0xF and opnd: return ('MOV %s,A' % opnd, 1, [], False, False)
    return ('DB %02Xh' % op, 1, [], False, False)

def disassemble(img, entries):
    seen, code, calls, work = {}, set(), set(), list(entries)
    while work:
        pc = work.pop()
        while pc in img and pc not in seen:
            txt, n, tg, end, call = decode(img, pc)
            seen[pc] = (txt, n)
            for k in range(n): code.add(pc + k)
            for t in tg:
                if t in img: work.append(t)
                if call: calls.add(t)
            if end: break
            pc += n
    return seen, calls

if __name__ == '__main__':
    img = load(sys.argv[1])
    entries = [0x0000] + [v for v in (0x03,0x0B,0x13,0x1B,0x23,0x2B,0x33,0x3B,0x43,0x4B,0x53) if v in img]
    seen, calls = disassemble(img, entries)
    targets = set()
    for pc, (t, n) in seen.items():
        for tok in t.replace(',', ' ').split():
            if tok.startswith('L') and len(tok) == 5:
                try: targets.add(int(tok[1:], 16))
                except ValueError: pass
    for pc in sorted(seen):
        if pc in calls: print('\n; ---- subrutina L%04X ----' % pc)
        lab = ('L%04X:' % pc) if pc in targets else ''
        raw = ' '.join('%02X' % img.get(pc + k, 0) for k in range(seen[pc][1]))
        print('%-7s %04X  %-9s %s' % (lab, pc, raw, seen[pc][0]))
    cov = sum(n for _, n in seen.values())
    print('\n; bytes de codigo alcanzados: %d de %d' % (cov, len(img)), file=sys.stderr)
