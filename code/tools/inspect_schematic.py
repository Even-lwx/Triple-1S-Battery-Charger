"""Read-only Altium text-record connectivity inspection (requires olefile)."""
import collections
import struct
import sys
import olefile

data = olefile.OleFileIO(sys.argv[1]).openstream('FileHeader').read()
records = []
offset = 0
while offset < len(data):
    size = struct.unpack_from('<I', data, offset)[0]
    offset += 4
    if size > len(data) - offset:
        raise ValueError('Unsupported binary record at offset %d' % offset)
    raw = data[offset:offset + size].rstrip(b'\0')
    offset += size
    records.append(dict(item.split('=', 1) for item in raw.decode('utf-8', 'replace').split('|') if '=' in item))

# OwnerIndex excludes the initial file header record.
components = {}
for index, record in enumerate(records[1:]):
    if record.get('RECORD') == '1':
        attrs = {r.get('Name'): r.get('Text') for r in records if r.get('OwnerIndex') == str(index) and r.get('RECORD') in ('34', '41')}
        components[index] = (attrs.get('Designator', '?'), attrs.get('ManufacturerPart', record.get('LibReference')), attrs.get('Value', ''), record.get('UniqueID'))

def point(r):
    x, y = int(r.get('Location.X', 0)), int(r.get('Location.Y', 0))
    if r.get('RECORD') == '2':
        dx, dy = ((1,0),(0,1),(-1,0),(0,-1))[int(r.get('PinConglomerate',0)) & 3]
        length = int(r.get('PinLength',0))
        x += dx * length
        y += dy * length
    return x, y

parent = {}
def root(p):
    parent.setdefault(p, p)
    if parent[p] != p:
        parent[p] = root(parent[p])
    return parent[p]

def join(a, b):
    parent[root(a)] = root(b)

pins = [r for r in records if r.get('RECORD') == '2']
labels = [r for r in records if r.get('RECORD') in ('17', '25')]
wires = [r for r in records if r.get('RECORD') == '27']
points = {point(r) for r in pins + labels}
segments = []
for wire in wires:
    ps = [(int(wire.get('X%d' % i, 0)), int(wire.get('Y%d' % i, 0))) for i in range(1, int(wire['LocationCount']) + 1)]
    points.update(ps)
    segments.extend(zip(ps, ps[1:]))
# Join segment endpoints and anchors on each segment; crossing interiors alone
# are not joined without a junction/endpoint.
points.update(point(r) for r in records if r.get('RECORD') == '29')
for a, b in segments:
    join(a, b)
    for p in points:
        if (p[0]-a[0])*(b[1]-a[1]) == (p[1]-a[1])*(b[0]-a[0]) and min(a[0],b[0]) <= p[0] <= max(a[0],b[0]) and min(a[1],b[1]) <= p[1] <= max(a[1],b[1]):
            join(a, p)
named = {}
for r in labels:
    name = r['Text']
    if name in named:
        join(point(r), named[name])
    named[name] = point(r)
names = collections.defaultdict(set)
members = collections.defaultdict(list)
for r in labels:
    names[root(point(r))].add(r['Text'])
for r in pins:
    owner = int(r['OwnerIndex'])
    if owner in components:
        c = components[owner]
        members[root(point(r))].append('%s.%s(%s)' % (c[0], r.get('Designator'), r.get('Name')))
print('RECORDS', len(records), 'COMPONENTS', len(components), 'WIRES', len(wires))
for owner,c in components.items():
    print('COMP', owner, *c)
    if c[0].startswith('U') or 'stm32' in str(c).lower():
        for r in pins:
            if int(r['OwnerIndex']) == owner:
                net = root(point(r))
                print(' PIN', r.get('Designator'), r.get('Name'), 'NET', ','.join(sorted(names[net])) or str(net), 'MEMBERS', ', '.join(members[net]), 'UID', r.get('UniqueID'))
