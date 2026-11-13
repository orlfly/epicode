#!/usr/bin/env python3
import struct, zlib, json, sys

class R:
    def __init__(self, b, pos=0):
        self.b = b; self.p = pos
    def u8(self):
        v = self.b[self.p]; self.p += 1; return v
    def u32(self):
        v = struct.unpack_from('<I', self.b, self.p)[0]; self.p += 4; return v
    def u64(self):
        v = struct.unpack_from('<Q', self.b, self.p)[0]; self.p += 8; return v
    def i64(self):
        v = struct.unpack_from('<q', self.b, self.p)[0]; self.p += 8; return v
    def raw(self, n):
        v = self.b[self.p:self.p+n]; self.p += n; return v
    def skip(self, n):
        self.p += n
    def fstring(self):
        ln = struct.unpack_from('<i', self.b, self.p)[0]; self.p += 4
        if ln == 0:
            return ''
        if ln < 0:
            n = -ln
            raw = self.raw(n * 2)
            s = raw[:-2].decode('utf-16-le', 'replace')
        else:
            raw = self.raw(ln)
            s = raw[:-1].decode('utf-8', 'replace')
        return s
    def guid(self):
        return ''.join(f'{self.u32():08x}' for _ in range(4))

def parse(path):
    b = open(path, 'rb').read()
    r = R(b)
    magic = r.u32()
    assert magic == 1153351692, hex(magic)
    header_size = r.u32()
    r.u32(); r.u32()
    sha = r.raw(20)
    compressed = r.u8() != 0
    r.u32()
    if compressed:
        data = zlib.decompress(b[r.p:])
        r = R(data)
    # meta
    meta_size = r.u32()
    data_version = r.u8()
    manifest_file_version = r.u32()
    b_is_file_data = r.u8() != 0
    app_id = r.u32()
    app_name = r.fstring()
    build_version = r.fstring()
    launch_exe = r.fstring()
    launch_command = r.fstring()
    prereq_count = r.u32()
    prereq_ids = [r.fstring() for _ in range(prereq_count)]
    prereq_name = r.fstring()
    prereq_path = r.fstring()
    prereq_args = r.fstring()
    if data_version >= 1:
        build_version = r.fstring()
    uninstall_path = uninstall_args = None
    if data_version >= 2:
        uninstall_path = r.fstring()
        uninstall_args = r.fstring()

    # chunks
    chunk_size = r.u32()
    r.u8()
    count = r.u32()
    guids = [r.guid() for _ in range(count)]
    hashes = [r.u64() for _ in range(count)]
    shas = [r.raw(20) for _ in range(count)]
    groups = [r.u8() for _ in range(count)]
    windows = [r.u32() for _ in range(count)]
    sizes = [r.i64() for _ in range(count)]

    # files
    filemanifest_size = r.u32()
    fm_version = r.u8()
    fcount = r.u32()
    names = [r.fstring() for _ in range(fcount)]
    symlinks = [r.fstring() for _ in range(fcount)]
    fhashes = [r.raw(20) for _ in range(fcount)]
    flags = [r.u8() for _ in range(fcount)]
    tags = []
    for _ in range(fcount):
        n = r.u32()
        tags.append([r.fstring() for _ in range(n)])
    parts = []
    for _ in range(fcount):
        n = r.u32()
        off = 0
        ps = []
        for _ in range(n):
            total = r.p
            csize = r.u32()
            g = r.guid()
            o = r.u32(); sz = r.u32()
            ps.append({'Guid': g, 'Link': None, 'Offset': o, 'Size': sz})
            off += sz
            diff = r.p - total - csize
            if diff > 0:
                r.skip(diff)
        parts.append(ps)
    if fm_version >= 1:
        for _ in range(fcount):
            if r.u32() != 0:
                r.skip(16)
        for _ in range(fcount):
            r.fstring()
    if fm_version >= 2:
        for _ in range(fcount):
            r.skip(32)

    # custom fields
    csize = r.u32()
    r.u8()
    ccount = r.u32()
    keys = [r.fstring() for _ in range(ccount)]
    vals = [r.fstring() for _ in range(ccount)]

    return {
        'ManifestFileVersion': manifest_file_version,
        'bIsFileData': b_is_file_data,
        'AppID': str(app_id),
        'AppNameString': app_name,
        'BuildVersionString': build_version,
        'UninstallActionPath': uninstall_path,
        'UninstallActionArgs': uninstall_args,
        'LaunchExeString': launch_exe,
        'LaunchCommand': launch_command,
        'PrereqIds': prereq_ids,
        'PrereqName': prereq_name,
        'PrereqPath': prereq_path,
        'PrereqArgs': prereq_args,
        'FileManifestList': [
            {'Filename': names[i], 'FileHash': fhashes[i].hex(), 'FileChunkParts': parts[i]}
            for i in range(fcount)
        ],
        'ChunkHashList': {guids[i]: hashes[i] for i in range(count)},
        'ChunkShaList': {guids[i]: shas[i].hex() for i in range(count)},
        'DataGroupList': {guids[i]: groups[i] for i in range(count)},
        'ChunkFilesizeList': {guids[i]: sizes[i] for i in range(count)},
        'CustomFields': {keys[i]: vals[i] for i in range(ccount)},
    }

if __name__ == '__main__':
    m = parse(sys.argv[1])
    json.dump(m, open(sys.argv[2], 'w'), indent=1)
    print('files', len(m['FileManifestList']), 'chunks', len(m['ChunkHashList']))
