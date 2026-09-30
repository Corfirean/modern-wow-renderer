"""Build a validated UTF-8 area database; never guess custom MPQ precedence."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def parse_dbc(payload, table):
    if len(payload) < 20 or payload[:4] != b'WDBC':
        raise ValueError(f'{table}: expected WDBC')
    count, fields, size, strings = struct.unpack_from('<4I', payload, 4)
    minimum = 36 if table == 'AreaTable' else 66
    if fields != minimum or size != fields * 4 or count > 100000:
        raise ValueError(f'{table}: unsupported layout')
    end = 20 + count * size
    if end + strings != len(payload):
        raise ValueError(f'{table}: invalid length')
    block = payload[end:]

    def name(offset):
        if offset >= len(block):
            raise ValueError('String offset outside block')
        stop = block.find(b'\0', offset)
        if stop < 0:
            raise ValueError('Unterminated DBC string')
        return block[offset:stop].decode('utf-8')

    result = []
    seen = set()
    for n in range(count):
        row = struct.unpack_from(f'<{fields}I', payload, 20 + n * size)
        if row[0] in seen:
            raise ValueError('Duplicate DBC ID')
        seen.add(row[0])
        # WotLK localized names: AreaTable field 11, Map field 5.
        start = 11 if table == 'AreaTable' else 5
        title = next((name(row[i]) for i in range(start, start + 16) if row[i]), '')
        if table == 'AreaTable':
            result.append(dict(areaId=row[0], mapId=row[1], parentAreaId=row[2], flags=row[4], name=title))
        else:
            result.append(dict(mapId=row[0], name=title or name(row[1])))
    return result


def gather(client):
    import mpyq
    variants = {'AreaTable': [], 'Map': []}
    failures = []
    for archive in sorted((client / 'Data').rglob('*'), key=lambda p: str(p).lower()):
        if archive.suffix.lower() != '.mpq':
            continue
        try:
            mpq = mpyq.MPQArchive(str(archive), listfile=False)
            try:
                for table in variants:
                    internal = f'DBFilesClient\\{table}.dbc'
                    if mpq.get_hash_table_entry(internal) is None:
                        continue
                    try:
                        payload = mpq.read_file(internal) or b''
                    except Exception as exc:
                        payload = b''
                        failures.append(dict(archive=str(archive.relative_to(client)), table=table, error=str(exc)))
                    # Keep even unsupported variants in the priority set. A
                    # valid lower patch must never hide an unreadable winner.
                    variants[table].append((str(archive.relative_to(client)), payload))
                    try:
                        parse_dbc(payload, table)
                    except Exception as exc:
                        failures.append(dict(archive=str(archive.relative_to(client)), table=table, error=str(exc)))
            finally:
                mpq.file.close()
        except Exception as exc:
            failures.append(dict(archive=str(archive.relative_to(client)), error=str(exc)))
    return variants, failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('client', type=Path)
    parser.add_argument('--output', type=Path, default=Path('data/areas.txt'))
    parser.add_argument('--dbc-dir', type=Path, help='DBC exported through actual client virtual filesystem')
    parser.add_argument('--archive-order', type=Path, help='JSON list of active MPQ paths, low to high priority, verified against client loader')
    parser.add_argument('--diagnostic-archive', type=Path, help='Export ONE candidate archive; does not authorize effects')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    sources = {}
    if sum(bool(v) for v in (args.dbc_dir,args.archive_order,args.diagnostic_archive)) > 1:
        raise ValueError('Choose one explicit source mode')
    if args.diagnostic_archive:
        import mpyq
        archive = mpyq.MPQArchive(str(args.diagnostic_archive), listfile=False)
        try:
            selected = {t: archive.read_file(f'DBFilesClient\\{t}.dbc') for t in ('AreaTable', 'Map')}
        finally:
            archive.file.close()
        sources = {t: str(args.diagnostic_archive) for t in selected}
        mode = 'diagnostic-only'
    elif args.dbc_dir:
        selected = {t: (args.dbc_dir / f'{t}.dbc').read_bytes() for t in ('AreaTable', 'Map')}
        sources = {t: str(args.dbc_dir / f'{t}.dbc') for t in selected}
        mode = 'explicit-client-vfs-export'
    else:
        variants, failures = gather(args.client)
        report = {t: [dict(archive=p, sha256=hashlib.sha256(b).hexdigest()) for p, b in entries] for t, entries in variants.items()}
        report['failures'] = failures
        args.output.with_suffix('.inventory.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        selected = {}
        order = json.loads(args.archive_order.read_text(encoding='utf-8')) if args.archive_order else None
        if order and (len(order) != len(set(order)) or not all(isinstance(p, str) for p in order)):
            raise ValueError('Invalid archive order')
        for table, entries in variants.items():
            if order:
                entries = sorted((e for e in entries if e[0] in order), key=lambda e: order.index(e[0]))
                active_errors = [e for e in failures if e['archive'] in order and 'table' not in e]
                if active_errors:
                    raise ValueError(f'Active archives unreadable: {active_errors}')
            elif failures or len({hashlib.sha256(b).digest() for _, b in entries}) != 1:
                raise ValueError('Custom MPQ precedence/encryption unresolved. Inspect inventory; provide verified --archive-order or --dbc-dir. No authoritative database written.')
            if not entries:
                raise ValueError(f'{table} not found')
            sources[table], selected[table] = entries[-1]
        mode = 'verified-archive-order' if order else 'identical-all-readable-variants'
    areas = parse_dbc(selected['AreaTable'], 'AreaTable')
    maps = parse_dbc(selected['Map'], 'Map')
    area_by_id = {a['areaId']: a for a in areas}
    map_ids = {m['mapId'] for m in maps}
    cross_map_parents = []
    for area in areas:
        if area['mapId'] not in map_ids:
            raise ValueError('Area references missing map')
        visited = set()
        current = area
        while current['parentAreaId']:
            if current['areaId'] in visited:
                raise ValueError('Cyclic area hierarchy')
            visited.add(current['areaId'])
            current = area_by_id[current['parentAreaId']]
            if current['mapId'] != area['mapId']:
                cross_map_parents.append(dict(areaId=area['areaId'], parentAreaId=current['areaId']))
    def quote(name):
        if any(c in name for c in '\r\n\0'):
            raise ValueError('Control character in name')
        return '"' + name.replace('\\', '\\\\').replace('"', '\\"') + '"'
    lines = ['EnvironmentDatabase 1', f'# provenance={mode}']
    for m in maps:
        lines.append(f'M {m["mapId"]} {quote(m["name"])}')
    for a in areas:
        if a['areaId']:
            lines.append(f'A {a["areaId"]} {a["mapId"]} {a["parentAreaId"]} {a["flags"]} {quote(a["name"])}')
    metadata = dict(schemaVersion=1, generatedFrom=str(args.client.resolve()), selectionMode=mode, sources=sources,
                    sha256={t: hashlib.sha256(b).hexdigest() for t, b in selected.items()}, areas=len(areas), maps=len(maps), crossMapParents=cross_map_parents)
    args.output.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    args.output.with_suffix('.metadata.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    # Demo mappings come from THIS export, not a vanilla ID list. Names are
    # tooling-only; runtime always uses the emitted numeric IDs.
    demo = {'Elwynn Forest': 'ForestSunny', 'Duskwood': 'DarkForest', 'Tanaris': 'Desert', 'Winterspring': 'Snow'}
    sections = []
    for a in areas:
        if not a['parentAreaId'] and a['name'] in demo:
            sections.extend([f'[Zone.{a["areaId"]}]', f'Name={a["name"]}', f'Profile={demo[a["name"]]}', ''])
    args.output.with_suffix('.demo.ini').write_text('\n'.join(sections), encoding='utf-8')
    print(f'Parsed {len(areas)} areas, {len(maps)} maps. Wrote {args.output}')


if __name__ == '__main__':
    main()
