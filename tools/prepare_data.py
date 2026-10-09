#!/usr/bin/env python3
"""Prepare the Lander game data for the rewrite from your own disc image.

usage:
    python prepare_data.py <image> <out_dir>

<image> is one of:
    LANDER.mds (Alcohol 120%: .mds + .mdf next to it)
    LANDER.cue (CUE/BIN, data track + audio tracks)
    a folder holding the CD's files (a mounted disc or a copy); music is then
    taken from TrackNN.wav files in that folder or next to it, if present.

The result in <out_dir>:
    <out_dir>/...                 the CD's files (MISSIONS, MISC, SOUND, ...)
    <out_dir>/MUSIC/TRACK02.WAV   CD-audio music tracks 2..6 (44.1 kHz stereo)

Only standard Python 3 is needed.
"""
import os
import shutil
import struct
import sys
import wave

SECTOR = 2048
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


# ---------------------------------------------------------------- raw tracks

class Track:
    def __init__(self, number, audio, path, offset, sector_size, length):
        self.number, self.audio, self.path = number, audio, path
        self.offset, self.sector_size, self.length = offset, sector_size, length


def tracks_from_mds(mds_path):
    d = open(mds_path, "rb").read()
    if d[:16] != b"MEDIA DESCRIPTOR":
        raise SystemExit("not an MDS file: " + mds_path)
    mdf = os.path.splitext(mds_path)[0] + ".mdf"
    if not os.path.exists(mdf):
        mdf = os.path.splitext(mds_path)[0] + ".MDF"
    tracks = []
    num_sessions = struct.unpack_from("<H", d, 0x14)[0]
    sess_off = struct.unpack_from("<I", d, 0x50)[0]
    for s in range(num_sessions):
        so = sess_off + s * 24
        num_blocks = d[so + 10]
        blocks_off = struct.unpack_from("<I", d, so + 20)[0]
        for b in range(num_blocks):
            bo = blocks_off + b * 80
            mode, point = d[bo], d[bo + 4]
            if not 1 <= point <= 99:
                continue
            index_off = struct.unpack_from("<I", d, bo + 12)[0]
            sector_size = struct.unpack_from("<H", d, bo + 16)[0]
            offset = struct.unpack_from("<Q", d, bo + 40)[0]
            length = struct.unpack_from("<I", d, index_off + 4)[0]
            tracks.append(Track(point, (mode & 0x0F) == 0x09, mdf, offset, sector_size, length))
    return tracks


def tracks_from_cue(cue_path):
    base = os.path.dirname(cue_path)
    tracks, cur_file, pending = [], None, None
    for line in open(cue_path, encoding="latin-1"):
        w = line.strip().split()
        if not w:
            continue
        if w[0].upper() == "FILE":
            name = line.strip()[5:].rsplit(" ", 1)[0].strip().strip('"')
            cur_file = os.path.join(base, name)
        elif w[0].upper() == "TRACK":
            mode = w[2].upper()
            ss = 2352 if mode in ("AUDIO", "MODE1/2352", "MODE2/2352") else 2048
            pending = Track(int(w[1]), mode == "AUDIO", cur_file, 0, ss, None)
            tracks.append(pending)
        elif w[0].upper() == "INDEX" and w[1] == "01" and pending:
            m, s, f = (int(x) for x in w[2].split(":"))
            pending.offset = ((m * 60 + s) * 75 + f) * pending.sector_size
    for i, t in enumerate(tracks):  # lengths: up to the next track in the same file, or EOF
        nxt = next((u for u in tracks[i + 1:] if u.path == t.path), None)
        end = nxt.offset if nxt else os.path.getsize(t.path)
        t.length = (end - t.offset) // t.sector_size
    return tracks


def write_iso(track, out_iso):
    with open(track.path, "rb") as f, open(out_iso, "wb") as o:
        f.seek(track.offset)
        for _ in range(track.length):
            sec = f.read(track.sector_size)
            if track.sector_size == 2048:
                o.write(sec)
            elif sec[:12] == SYNC and sec[15] == 2:  # mode 2 form 1
                o.write(sec[24:24 + 2048])
            else:  # mode 1
                o.write(sec[16:16 + 2048])


def write_wav(track, out_wav):
    with open(track.path, "rb") as f:
        f.seek(track.offset)
        w = wave.open(out_wav, "wb")
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        for _ in range(track.length):
            w.writeframes(f.read(track.sector_size)[:2352])
        w.close()


# ---------------------------------------------------------------- ISO9660

def extract_iso(iso, out):
    f = open(iso, "rb")

    def read(lba, n):
        f.seek(lba * SECTOR)
        return f.read(n)

    pvd = joliet = None
    lba = 16
    while True:
        vd = read(lba, SECTOR)
        if vd[1:6] != b"CD001" or vd[0] == 255:
            break
        if vd[0] == 1:
            pvd = vd
        elif vd[0] == 2 and vd[88:91] in (b"%/@", b"%/C", b"%/E"):
            joliet = vd
        lba += 1
    vd = joliet or pvd
    if vd is None:
        raise SystemExit("no ISO9660 file system in the data track")
    count = [0]

    def walk(extent, size, path):
        data = read(extent, size)
        pos = 0
        while pos < len(data):
            ln = data[pos]
            if ln == 0:
                pos = (pos // SECTOR + 1) * SECTOR
                continue
            rec = data[pos:pos + ln]
            pos += ln
            if rec[32] == 1 and rec[33] in (0, 1):
                continue
            raw = rec[33:33 + rec[32]]
            name = raw.decode("utf-16-be" if joliet else "ascii", "replace").split(";")[0]
            ext, sz = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
            p = os.path.join(path, name)
            if rec[25] & 2:
                os.makedirs(os.path.join(out, p), exist_ok=True)
                walk(ext, sz, p)
            else:
                with open(os.path.join(out, p), "wb") as o:
                    f.seek(ext * SECTOR)
                    left = sz
                    while left:
                        chunk = f.read(min(left, 1 << 20))
                        o.write(chunk)
                        left -= len(chunk)
                count[0] += 1

    root = vd[156:156 + 34]
    os.makedirs(out, exist_ok=True)
    walk(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "")
    f.close()
    return count[0]


# ---------------------------------------------------------------- main

def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(os.path.join(out, "MUSIC"), exist_ok=True)

    if os.path.isdir(src):
        print("copying CD files from", src)
        shutil.copytree(src, out, dirs_exist_ok=True)
        for d in (src, os.path.dirname(os.path.abspath(src))):
            for n in range(2, 7):
                for name in ("Track%02d.wav" % n, "TRACK%02d.WAV" % n, "track%02d.wav" % n):
                    p = os.path.join(d, name)
                    if os.path.exists(p):
                        shutil.copy(p, os.path.join(out, "MUSIC", "TRACK%02d.WAV" % n))
        return

    ext = os.path.splitext(src)[1].lower()
    tracks = tracks_from_mds(src) if ext == ".mds" else tracks_from_cue(src)
    data = next((t for t in tracks if not t.audio), None)
    if data is None:
        raise SystemExit("no data track found")
    iso = os.path.join(out, "_data_track.iso")
    print("data track %d: %d sectors" % (data.number, data.length))
    write_iso(data, iso)
    print("extracted %d files" % extract_iso(iso, out))
    os.remove(iso)
    for t in tracks:
        if t.audio:
            p = os.path.join(out, "MUSIC", "TRACK%02d.WAV" % t.number)
            write_wav(t, p)
            print("music track %d -> %s" % (t.number, p))


if __name__ == "__main__":
    main()
