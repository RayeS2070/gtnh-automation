#!/usr/bin/env python3
"""Writes server/data/mining_catalog.json: what the server plans mining with. Not installed in
game; run it again when the modpack moves on (after smc's tools/extract_asteroids.py).

    python3 -I factoryctl/mining/tools/mining-catalog.py

Sources, joined and checked against each other (any disagreement stops it):
  * smc/data/asteroids.json: the asteroids as addRecipesToDrones gets them (GT5U source);
  * gtnh-miner/asteroid-dump/asteroid_dump.json (MEDINA's asteroiddump mod, read from a running
    game): the item id of every drop, in the same order, and what processing makes of it;
  * assets/data/data.bin (the GTNH calculator's export, GTNH 2.9.0): that every id exists, and
    the labels (without the "End " the game puts before endstone ores: ids are the key).
Drones, drill tips and rods are GT5U's (ItemMiningDrones.DroneMaterials, toolHeadDrill =
gt.metaitem.02 8000 + material id, stick = gt.metaitem.01 23000 + material id); the rods are in
the export, the tips are not, so a tip's id is the rod's material id.
"""
import gzip
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SMC = ROOT / 'smc' / 'data' / 'asteroids.json'
DUMP = ROOT / 'gtnh-miner' / 'asteroid-dump' / 'asteroid_dump.json'
DATA = ROOT / 'assets' / 'data' / 'data.bin'
OUT = ROOT / 'factoryctl' / 'server' / 'data' / 'mining_catalog.json'

# ItemMiningDrones.DroneMaterials, in drone tier order (LV..UXV); MAX has none.
DRILL_MATERIALS = ['Steel', 'Steel', 'Titanium', 'Titanium', 'Tungstensteel', 'Tungstensteel',
                   'Naquadah', 'Naquadah', 'Naquadah Alloy', 'Neutronium', 'Cosmic Neutronium',
                   'Infinity', 'Transcendent Metal']
DRONE = 'gtnhintergalactic:item.MiningDrone'
# TileEntityModuleMiner: tier, the fluid, mB per parallel.
# Stages between two machines: real items, but no target is ever about them.
INTERMEDIATE = ('Crushed ', 'Purified ', 'Centrifuged ', 'Impure Pile of ', 'Purified Pile of ',
                'Small Pile of ', 'Tiny Pile of ')
PLASMAS = [(1, 'plasma.helium', 825), (2, 'plasma.bismuth', 550), (3, 'plasma.radon', 375),
           (4, 'plasma.technetium', 250), (5, 'plasma.plutonium241', 150)]


def fail(msg):
    sys.exit('mining-catalog: ' + msg)


raw = gzip.open(DATA).read()
E = struct.unpack('<%di' % (len(raw) // 4), raw[: len(raw) // 4 * 4])


def text(p):
    return None if p == -1 else raw[p * 4 + 4 : p * 4 + 4 + E[p]].decode('utf-8', 'replace')


def elements(p):
    return E[p + 1 : p + 1 + E[p]]


item_label = {text(E[i + 4])[2:]: text(E[i + 5]) for i in elements(E[1])}  # 'mod:name:damage'
fluid_label = {text(E[f + 4]).split(':')[-1]: text(E[f + 5]) for f in elements(E[2])}
by_label = {}
for key, label in item_label.items():
    by_label.setdefault(label, key)


def item(key):
    if key not in item_label:
        fail(f'{key} is not in the calculator export')
    name, damage = key.rsplit(':', 1)
    return {'name': name, 'damage': int(damage), 'label': item_label[key]}


smc = json.load(open(SMC))
dump = {a['name']: a for a in json.load(open(DUMP))['asteroids']}

drones = []
for tier, tier_name in enumerate(smc['droneTiers']):
    if tier >= len(DRILL_MATERIALS):
        break  # MAX: no drill material, no recipe uses it
    material = DRILL_MATERIALS[tier]
    rod_key = by_label.get(material + ' Rod')
    if not rod_key or not rod_key.startswith('gregtech:gt.metaitem.01:23'):
        fail(f'no rod for {material}')
    material_id = int(rod_key.rsplit(':', 1)[1]) - 23000
    drones.append({
        'tier': tier,
        'name': tier_name,
        'drone': item(f'{DRONE}:{tier}'),
        'tip': {'name': 'gregtech:gt.metaitem.02', 'damage': 8000 + material_id,
                'label': material + ' Drill Tip'},
        'rod': item(rod_key),
    })

plasmas = []
for tier, name, usage in PLASMAS:
    if name not in fluid_label:
        fail(f'no fluid {name}')
    plasmas.append({'tier': tier, 'name': name, 'label': fluid_label[name], 'usage': usage})

asteroids = []
for a in smc['asteroids']:
    d = dump.get(a['name'])
    if d is None:
        fail(f'{a["name"]} is not in the dump')
    if [o['chance'] for o in a['outputs']] != [x['chance'] for x in d['drops']]:
        fail(f'{a["name"]}: the outputs differ from the dump')
    outputs = []
    for o, x in zip(a['outputs'], d['drops']):
        out = dict(o)
        out['item'] = item(x['id'])
        # What processing makes of it, for the targets' suggestions: the end products only.
        out['products'] = [dict(item(p['id']), hops=p['hops'])
                           for p in x.get('breaksDownInto', [])
                           if p['id'] in item_label
                           and not item_label[p['id']].startswith(INTERMEDIATE)]
        outputs.append(out)
    asteroids.append(dict(a, outputs=outputs))
extra = sorted(set(dump) - {a['name'] for a in smc['asteroids']})
if extra:
    fail('the dump has asteroids smc does not: ' + ', '.join(extra))

catalog = {
    'source': {'gt5u': smc['source']['gt5u'],
               'note': 'generated by factoryctl/mining/tools/mining-catalog.py'},
    'droneTiers': smc['droneTiers'],
    'drones': drones,
    'plasmas': plasmas,
    'asteroids': asteroids,
}
OUT.parent.mkdir(parents=True, exist_ok=True)
# One asteroid per line: diffs stay readable, the file small.
lines = [json.dumps({k: v for k, v in catalog.items() if k != 'asteroids'}, ensure_ascii=False)[:-1]
         + ', "asteroids": [']
lines += [json.dumps(a, ensure_ascii=False) + (',' if i + 1 < len(asteroids) else '')
          for i, a in enumerate(asteroids)]
lines.append(']}')
OUT.write_text('\n'.join(lines) + '\n')
print(f'{len(asteroids)} asteroids, {sum(len(a["outputs"]) for a in asteroids)} outputs, '
      f'{len(drones)} drone tiers, {len(plasmas)} plasmas -> {OUT.relative_to(ROOT)}')
