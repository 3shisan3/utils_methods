# -*- coding: utf-8 -*-
"""Axure RP 9 (.rp) file parser — reverse-engineered format.

Container: [15-byte header][index JSON (custom encoding, skipped)]
           [entry stream: (u32 flag)(u32 len)(payload)]*
payload = gzip member (blob) or raw bytes (JPEG thumbnail etc.)

Blob (decompressed):
  (u32 27)(u32 9)(u32 0)(u32 1)(u32 0)(u32 0xC351)   fixed header
  (u32 31)(u32 stringCount)(stringCount x (u32 len + utf8 bytes))
  (u32 30)(u32 poolCount)(poolCount x 16-byte GUID)   GUID pool
  (u32 26)(u32 15)(u32 1)(u32 18)(u32 2)(u32 9)(u32 1)(u32 1)(u32 1)  constants
  (u32 rootPropCount)(rootPropCount x (key, value))
  ... possibly more sections ... (u32 28) end marker

Values:
  1  = dict:  (u32 count)(count x (key, value))
  2  = array: (u32 count)(count x value)
  3  = ambiguous: object-ref (u32) | inline object (count + props)  -> backtracking
  5  = int32 (4 bytes)
  6  = double (8 bytes)
  8  = string-ref (u32, 1-based into string table)
  17 = GUID-pool-ref (u32, 0-based into pool)
  25 = bool (1 byte)
  28 = end marker
  30 = GUID array: (u32 count)(count x 16 bytes)
"""
import struct, zlib, io, re

MAGIC_CONSTS = (26, 15, 1, 18, 2, 9, 1, 1, 1)


class ParseError(Exception):
    pass


# ---------------------------------------------------------------- container

def find_stream_start(data):
    i = data.find(b'\x1f\x8b\x08', 16)
    while i > 8:
        cand = i - 8
        flag, ln = struct.unpack_from('<II', data, cand)
        if flag == 0 and ln > 0 and cand + 8 + ln <= len(data):
            try:
                zlib.decompress(data[i:i + ln], 31)
                return cand
            except Exception:
                pass
        i = data.find(b'\x1f\x8b\x08', i + 1)
    raise ParseError('entry stream not found')


def walk_entries(data):
    """Yield (index, flag, length, payload_offset, kind)."""
    start = find_stream_start(data)
    n = len(data)
    pos = start
    out = []
    while pos + 8 <= n:
        flag, ln = struct.unpack_from('<II', data, pos)
        payload = pos + 8
        if ln == 0 or payload + ln > n:
            break
        head = data[payload:payload + 8]
        if head[:3] == b'\x1f\x8b\x08':
            kind = 'gzip'
        elif head[:3] == b'\xff\xd8\xff':
            kind = 'jpeg'
        elif head[:8] == b'\x89PNG\r\n\x1a\n':
            kind = 'png'
        else:
            kind = 'raw'
        out.append((len(out), flag, ln, payload, kind))
        pos = payload + ln
    return out


def blob_type_name(dec):
    m = re.search(rb'Axure:[A-Za-z0-9]+', dec[:200])
    return m.group(0).decode() if m else 'Unknown'


# ---------------------------------------------------------------- graph reader

class Reader:
    __slots__ = ('b', 'i', 'n')

    def __init__(self, b, i=0):
        self.b = b
        self.i = i
        self.n = len(b)

    def u32(self):
        if self.i + 4 > self.n:
            raise ParseError('EOF(u32) @%d' % self.i)
        v = struct.unpack_from('<I', self.b, self.i)[0]
        self.i += 4
        return v

    def byte(self):
        if self.i + 1 > self.n:
            raise ParseError('EOF(byte) @%d' % self.i)
        v = self.b[self.i]
        self.i += 1
        return v

    def bytes(self, k):
        if k < 0 or self.i + k > self.n:
            raise ParseError('EOF(bytes) @%d' % self.i)
        v = self.b[self.i:self.i + k]
        self.i += k
        return v

    def clone(self):
        return Reader(self.b, self.i)


def decode_blob(data, name='<blob>'):
    """Decode one decompressed blob into a dict-tree.

    Returns dict with: type, strings, pool, root (dict), consumed, size,
    warnings (list of strings).
    """
    r = Reader(data)
    warn = []
    h = [r.u32() for _ in range(6)]
    a, cnt = r.u32(), r.u32()
    strings = []
    for _ in range(cnt):
        ln = r.u32()
        strings.append(r.bytes(ln).decode('utf-8', 'replace'))
    # GUID pool
    tag = r.u32()
    pool = []
    if tag == 30:
        pc = r.u32()
        for _ in range(pc):
            pool.append(r.bytes(16))
    else:
        raise ParseError('%s: expected GUID pool tag 30, got %d' % (name, tag))
    # constants
    consts = [r.u32() for _ in range(9)]
    if tuple(consts) != MAGIC_CONSTS:
        warn.append('%s: unexpected constants %s' % (name, consts))
    nroot = r.u32()
    if nroot > 100000 or nroot * 12 > len(data) - r.i:
        raise ParseError('%s: bad root prop count %d' % (name, nroot))

    ctx = Ctx(strings, pool, warn, name)
    root, unknown = parse_props(r, nroot, ctx, top=True)

    # ---- tail object table: [9][objId][typeRef][dict(props)]* until [28] / EOF
    objects = {}
    try:
        while True:
            if r.i + 4 > r.n:
                break
            tag = struct.unpack_from('<I', r.b, r.i)[0]
            if tag == 28:
                r.i += 4
                break
            if tag != 9:
                warn.append('%s: tail: unexpected tag %d @%d (stop)' % (name, tag, r.i))
                break
            r.i += 4
            obj_id = r.u32()
            otype = ctx.s(r.u32())
            dtag = r.u32()
            if dtag != 1:
                warn.append('%s: obj %d: expected dict tag 1, got %d @%d' % (name, obj_id, dtag, r.i - 4))
                break
            n = r.u32()
            props, unk = parse_props(r, n, ctx)
            unknown |= unk
            props['__type__'] = otype
            props['__id__'] = obj_id
            objects[obj_id] = props
    except ParseError as e:
        warn.append('%s: tail table stopped: %s' % (name, e))

    consumed = r.i
    tail = len(data) - consumed
    return {
        'type': blob_type_name(data) if isinstance(data, (bytes, bytearray)) else '?',
        'strings': strings,
        'pool': pool,
        'consts': consts,
        'nroot': nroot,
        'root': root,
        'objects': objects,
        'unknown': unknown,
        'consumed': consumed,
        'size': len(data),
        'tail': tail,
        'tail_bytes': data[consumed:consumed + 64] if tail else b'',
        'trace': ctx.trace[-400:],
        'warnings': warn,
    }


class Ctx:
    __slots__ = ('strings', 'pool', 'warn', 'name', 'trace')

    def __init__(self, strings, pool, warn, name):
        self.strings = strings
        self.pool = pool
        self.warn = warn
        self.name = name
        self.trace = []

    def s(self, ref):
        if 1 <= ref <= len(self.strings):
            return self.strings[ref - 1]
        return '<ref%d>' % ref


def parse_props(r, count, ctx, top=False):
    """Parse `count` (key, value) pairs; returns (dict, unknown_tags_set)."""
    d = {}
    unknown = set()
    if count > 100000 or count * 12 > r.n - r.i:
        raise ParseError('%s: bad prop count %d @%d' % (ctx.name, count, r.i - 4))
    for _ in range(count):
        tag = r.u32()
        ctx.trace.append((r.i - 4, tag, 'key'))
        if tag == 8:
            key = ctx.s(r.u32())
        elif tag == 3:                 # property dictionary keyed by object id
            key = '#obj%d' % r.u32()
        elif tag == 5:                 # property dictionary keyed by int (adaptive view id)
            key = '#int%d' % struct.unpack('<i', r.bytes(4))[0]
        else:
            raise ParseError('%s: expected key tag 8 @%d, got %d' % (ctx.name, r.i - 4, tag))
        v, unk = parse_value(r, ctx)
        unknown |= unk
        d[key] = v
    return d, unknown


def parse_value(r, ctx, depth=0):
    """Parse one value; returns (value, unknown_tags_set).

    Raises ParseError on structural impossibility (caller may backtrack).
    """
    if depth > 200:
        raise ParseError('depth limit')
    unknown = set()
    tag = r.u32()
    ctx.trace.append((r.i - 4, tag, 'val'))
    if tag == 1:                       # dict
        n = r.u32()
        if n > 500000 or n * 12 > r.n - r.i:
            raise ParseError('bad dict count %d @%d' % (n, r.i - 4))
        d = {}
        for _ in range(n):
            ktag = r.u32()
            if ktag == 8:
                key = ctx.s(r.u32())
            elif ktag == 3:            # object-id key
                key = '#obj%d' % r.u32()
            elif ktag == 5:            # int key
                key = '#int%d' % struct.unpack('<i', r.bytes(4))[0]
            else:
                raise ParseError('dict key tag %d @%d' % (ktag, r.i - 4))
            v, unk = parse_value(r, ctx, depth + 1)
            unknown |= unk
            d[key] = v
        return d, unknown
    if tag == 2:                       # array
        n = r.u32()
        if n > 500000 or n * 4 > r.n - r.i:
            raise ParseError('bad array count %d @%d' % (n, r.i - 4))
        arr = []
        for _ in range(n):
            v, unk = parse_value(r, ctx, depth + 1)
            unknown |= unk
            arr.append(v)
        return arr, unknown
    if tag == 5:                       # int32
        return struct.unpack('<i', r.bytes(4))[0], unknown
    if tag == 6:                       # double
        return struct.unpack('<d', r.bytes(8))[0], unknown
    if tag == 8:                       # string-ref
        return ctx.s(r.u32()), unknown
    if tag == 17:                      # GUID pool ref
        idx = r.u32()
        if idx < len(ctx.pool):
            g = ctx.pool[idx]
            return 'guid:%s' % format_guid(g), unknown
        return 'guid:<pool%d>' % idx, unknown
    if tag == 25:                      # bool
        return bool(r.byte()), unknown
    if tag == 30:                      # GUID array
        n = r.u32()
        if n > 100000:
            raise ParseError('guid array count %d' % n)
        return [format_guid(r.bytes(16)) for _ in range(n)], unknown
    if tag == 28:                      # end marker (should not appear mid-value)
        r.i -= 4
        raise ParseError('end marker mid-value @%d' % r.i)
    if tag == 4:                       # inline binary blob: (u32 len)(bytes)
        ln = r.u32()
        if ln > r.n - r.i:
            raise ParseError('bad binary length %d @%d' % (ln, r.i - 4))
        return r.bytes(ln), unknown
    if tag == 3:                       # object reference -> tail object id
        return {'__ref__': r.u32()}, unknown
    if tag == 22:                      # object reference (alias of 3, seen as 'object-id')
        return {'__ref__': r.u32()}, unknown
    if tag in (16, 29):                # fixed 2-u32 payload (semantics unresolved)
        a = r.u32()
        b = r.u32()
        return {'__tagpair__': [tag, a, b]}, unknown
    if tag == 19:                      # named object ref: (strRef, objId), 2 payload u32s
        a = r.u32()
        b = r.u32()
        return {'__named_ref__': [ctx.s(a), b]}, unknown
    raise ParseError('%s: unknown tag %d @%d' % (ctx.name, tag, r.i - 4))


def format_guid(b):
    h = b.hex()
    return '%s-%s-%s-%s-%s' % (h[0:8], h[8:12], h[12:16], h[16:20], h[20:32])


def resolve_refs(node, objects, depth=0, _cache=None, _active=None):
    """Deep-copy a value tree, replacing {'__ref__': id} with the tail object.

    Resolved objects are memoized (shared subtrees are reused); reference
    cycles are broken with a {'__cycle__': id} marker.
    """
    if depth > 64:
        return {'__cycle__': True}
    if _cache is None:
        _cache = {}
    if _active is None:
        _active = set()

    def resolve_obj(oid):
        if oid in _cache:
            return _cache[oid]
        if oid in _active:
            return {'__cycle__': oid}
        obj = objects.get(oid)
        if obj is None:
            return {'__ref__': oid}
        _active.add(oid)
        try:
            res = {k: resolve_refs(v, objects, depth + 1, _cache, _active)
                   for k, v in obj.items()}
        finally:
            _active.discard(oid)
        _cache[oid] = res
        return res

    if isinstance(node, dict):
        if len(node) == 1 and '__ref__' in node:
            return resolve_obj(node['__ref__'])
        if len(node) == 1 and '__named_ref__' in node:
            return resolve_obj(node['__named_ref__'][1])
        return {k: resolve_refs(v, objects, depth + 1, _cache, _active)
                for k, v in node.items()}
    if isinstance(node, list):
        return [resolve_refs(v, objects, depth + 1, _cache, _active) for v in node]
    return node
