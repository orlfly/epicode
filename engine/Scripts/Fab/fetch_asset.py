#!/usr/bin/env python3
import json, os, sys, hashlib, struct, zlib, urllib.request, concurrent.futures

MAGIC = 2986228386

def chunk_dir(version):
    if version >= 15: return 'ChunksV4'
    if version >= 6: return 'ChunksV3'
    if version >= 3: return 'ChunksV2'
    return 'Chunks'

def parse_chunk(path):
    b = open(path, 'rb').read()
    pos = 0
    def u32():
        nonlocal pos
        v = struct.unpack_from('<I', b, pos)[0]; pos += 4; return v
    if u32() != MAGIC:
        raise ValueError('bad magic')
    hv = u32(); u32(); u32()
    for _ in range(4): u32()
    struct.unpack_from('<Q', b, pos)[0]; pos += 8
    compressed = b[pos] != 0; pos += 1
    if hv >= 2: pos += 21
    if hv >= 3: u32()
    raw = b[pos:]
    return zlib.decompress(raw) if compressed else raw

def main():
    mf = json.load(open(sys.argv[1]))
    dest = sys.argv[2]
    base = sys.argv[3].rstrip('/')
    cdir = chunk_dir(mf['ManifestFileVersion'])
    chtmp = os.path.join(dest, '_chunks')
    data = os.path.join(dest, 'data')
    os.makedirs(chtmp, exist_ok=True)

    def chunk_url(g):
        h = mf['ChunkHashList'][g]
        grp = mf['DataGroupList'][g]
        return f'{base}/{cdir}/{grp:02}/{h:016X}_{g.upper()}.chunk'

    def fetch(g):
        dst = os.path.join(chtmp, g + '.chunk')
        exp = mf['ChunkFilesizeList'][g]
        if os.path.exists(dst) and os.path.getsize(dst) == exp:
            return g, True
        url = chunk_url(g)
        for attempt in range(5):
            try:
                req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
                with urllib.request.urlopen(req, timeout=60) as r, open(dst + '.part', 'wb') as f:
                    while True:
                        buf = r.read(1 << 20)
                        if not buf: break
                        f.write(buf)
                if os.path.getsize(dst + '.part') == exp:
                    os.replace(dst + '.part', dst)
                    return g, True
            except Exception as e:
                if attempt == 4:
                    print('FAIL', g, e, url, flush=True)
        return g, False

    guids = list(mf['ChunkHashList'].keys())
    ok = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        for i, (g, good) in enumerate(ex.map(fetch, guids), 1):
            ok += 1 if good else 0
            if i % 25 == 0:
                print(f'{i}/{len(guids)} chunks', flush=True)
    print(f'downloaded {ok}/{len(guids)} chunks', flush=True)

    bad = 0
    for f in mf['FileManifestList']:
        target = os.path.join(data, f['Filename'])
        h = hashlib.sha1()
        if os.path.exists(target):
            with open(target, 'rb') as fh:
                for b in iter(lambda: fh.read(1 << 20), b''):
                    h.update(b)
            if h.hexdigest() == f['FileHash']:
                continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target + '.part', 'wb') as out:
            for c in f['FileChunkParts']:
                p = os.path.join(chtmp, c['Guid'] + '.chunk')
                if not os.path.exists(p):
                    print('missing chunk for', f['Filename'], c['Guid'], flush=True)
                    break
                d = parse_chunk(p)
                out.write(d[c['Offset']:c['Offset'] + c['Size']])
        h = hashlib.sha1()
        with open(target + '.part', 'rb') as fh:
            for b in iter(lambda: fh.read(1 << 20), b''):
                h.update(b)
        if h.hexdigest() == f['FileHash']:
            os.replace(target + '.part', target)
        else:
            print('HASH MISMATCH', f['Filename'], flush=True)
            bad += 1
    print('bad', bad, flush=True)

if __name__ == '__main__':
    main()
