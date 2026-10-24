"""Reads Aidyn's DCM music (Borg12) from the ROM into note events, as DCM::VoiceHandler plays it."""
import struct, sys
sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'hdui'))
import romassets

def borg12(rom, index):
    d = rom.data(index)
    inst_off, unk4, chans, ninst, size, loop, tab_off, dat_off = struct.unpack('>8I', d[:32])
    insts = list(struct.unpack(f'>{ninst}I', d[inst_off:inst_off + 4 * ninst]))
    table = [struct.unpack('>IIIBBBB', d[tab_off + 16 * i:tab_off + 16 * i + 16]) for i in range(ninst)]
    return {'insts': insts, 'table': table, 'chans': chans, 'size': size, 'loop': loop, 'data': d[dat_off:dat_off + size], 'unk4': unk4}

def borg11(rom, index):
    d = rom.data(index)
    flag, ln, samples, wav = struct.unpack('>4I', d[:16])
    return {'flag': flag, 'len': ln, 'samples': samples, 'pcm': d[wav:wav + ln]}

def events(b):
    """[(tick, channel, kind, values)] decoded like DCM::VoiceHandler (one tick per handler call, all channels)."""
    data, n = b['data'], b['chans']
    pos, tick = 0, 0
    timer = [0] * n
    chan = [{'pitch': 0, 'vol': 0, 'pan': 0, 'inst': 0} for _ in range(n)]
    out = []
    tempo = []
    exp = [(0, 0), (0, 0x4000), (0, 0xc000), (1, 0xc000)]
    while pos < len(data):
        for c in range(n):
            if pos >= len(data):
                break
            if timer[c]:
                timer[c] -= 1
                continue
            cmd = data[pos]; pos += 1
            if cmd & 0x80:
                timer[c] = cmd & 0x7f   # (cmd&0x7f)+1, minus this tick
                continue
            st = chan[c]
            if cmd & 0x40:
                v = data[pos] | data[pos + 1] << 8; pos += 2
                e = v >> 14
                st['pitch'] = exp[e][1] + ((v & 0x3fff) << e)
                if not cmd & 0x02: out.append((tick, c, 'pitch', st['pitch']))
            if cmd & 0x20:
                st['vol'] = data[pos]; pos += 1
                if not cmd & 0x02: out.append((tick, c, 'vol', st['vol']))
            if cmd & 0x10:
                st['pan'] = data[pos]; pos += 1
            if cmd & 0x08:
                st['inst'] = data[pos]; pos += 1
            if cmd & 0x04:
                pos += 1
            if cmd & 0x01:
                sub = data[pos]; pos += 1
                if sub & 0x80: out.append((tick, c, 'stop', 0))
                if sub & 0x40:
                    t = data[pos]; pos += 1; tempo.append((tick, t))
                if sub & 0x20: pos += 1
            if cmd & 0x02:
                out.append((tick, c, 'note', dict(st)))
        tick += 1
    return out, tempo
