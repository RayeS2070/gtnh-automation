#!/usr/bin/env python3
"""Writes qft/catalog.lua: every Quantum Force Transformer recipe of the GTNH calculator's export
(assets/data/data.bin, ShadowTheAge/gtnh, GTNH 2.9.0), so recipes need no typing in. Not
installed in game; run it again when the modpack (and the assets submodule) moves on.

The calculator's export cannot be fully trusted: it lags the modpack and leaves things out (the
catalysts; on 2026-11-04 Rubber Polymer had Carbon 64 where GT5-Unofficial 5.09.54.264 has
Sulfur 32 + Carbon 32). The inputs and outputs are not checked against the mods' sources yet:
check a recipe there (tools/mod-source.py) and in game (NEI) before trusting it.

    python3 -I factoryctl/qft/tools/qft-recipes.py [assets/data/data.bin] [factoryctl/qft/qft/catalog.lua]

data.bin (see assets/src/repository.ts): gzip, int32 little-endian; elements[4] / [5] point at
the recipe types / recipes. A recipe: 4 index ints, 4 id, 5 its inputs and outputs (type,
pointer, slot, amount, probability per 100 %), 6 type, 7 the GT recipe (0 voltage, 1 ticks,
4 metadata: key, double). Goods: 4 id ("i:mod:name:damage", "f:mod:name"), 5 label.
"""
import gzip
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parents[2] / 'assets' / 'data' / 'data.bin'
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE.parent / 'qft' / 'catalog.lua'

ITEM_IN, OREDICT_IN, FLUID_IN, ITEM_OUT, FLUID_OUT = range(5)

raw = gzip.open(DATA).read()
E = struct.unpack('<%di' % (len(raw) // 4), raw[: len(raw) // 4 * 4])


def text(p):
    if p == -1:
        return None
    return raw[p * 4 + 4 : p * 4 + 4 + E[p]].decode('utf-8', 'replace')


def items(p):
    return E[p + 1 : p + 1 + E[p]]


def double(p):
    return struct.unpack('<d', raw[4 * p : 4 * p + 8])[0]


def goods(ptr):
    """(kind, name, damage, label) of an item or a fluid, as OC and AE name them."""
    gid, label = text(E[ptr + 4]), text(E[ptr + 5])
    parts = gid.split(':')
    if parts[0] == 'f':  # f:mod:name; a fluid's name in game is the last part
        return 'fluid', parts[-1], None, label
    return 'item', parts[1] + ':' + parts[2], int(parts[3]), label


# Fluids by label, for Fluid Mode: a dust output comes as the molten material (144 mB a piece)
# or, without one, as the material's fluid (1000 mB). MTEQuantumForceTransformer goes by the
# item's GT material; here it is guessed from the label ("X Dust" -> "Molten X", else "X").
fluid_by_label = {}
for f in items(E[2]):
    kind, name, _, label = goods(f)
    fluid_by_label.setdefault(label, name)


def molten(label):
    base = re.sub(r' Dust$', '', label)
    if 'Molten ' + base in fluid_by_label:
        return 'Molten ' + base, fluid_by_label['Molten ' + base], 144
    if base != label and base in fluid_by_label:
        return base, fluid_by_label[base], 1000
    return None


def slug(label):
    return re.sub(r'[^a-z0-9]+', '_', label.lower()).strip('_')


def lua(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


qft_types = {t for t in items(E[4]) if text(E[t]) == 'Quantum Force Transformer'}
recipes, skipped = [], []
for r in items(E[5]):
    if E[r + 6] not in qft_types:
        continue
    gt = E[r + 7]
    meta = {text(E[m]): double(m + 1) for m in items(E[gt + 4])}
    io = items(E[r + 5])
    ins, outs = [], []
    oredict = False
    for i in range(0, len(io), 5):
        typ, ptr, slot, amount, _ = io[i : i + 5]
        if typ == OREDICT_IN:
            oredict = True
            continue
        kind, name, damage, label = goods(ptr)
        entry = dict(kind=kind, name=name, damage=damage, label=label, amount=amount, slot=slot)
        (ins if typ < ITEM_OUT else outs).append((typ, entry))
    # NEI order = circuit number: item outputs first, then fluids (QFT's mOutputs, mFluidOutputs).
    outs.sort(key=lambda t: (t[0] != ITEM_OUT, t[1]['slot']))
    ins.sort(key=lambda t: (t[0] != ITEM_IN, t[1]['slot']))
    first_out = outs[0][1]['label']
    rec = dict(name=first_out, tier=int(meta.get('qft_focus_tier', 1)), ticks=E[gt + 1],
               inputs=[e for _, e in ins], outputs=[e for _, e in outs])
    if oredict:
        skipped.append(rec)
    else:
        recipes.append(rec)

# Ids: the first output's label; when two recipes share it, the first input tells them apart.
by_slug = {}
for rec in recipes:
    by_slug.setdefault(slug(rec['name']), []).append(rec)
for s, group in by_slug.items():
    for rec in group:
        if len(group) == 1:
            rec['id'] = s
        else:
            rec['id'] = s + '_from_' + slug(rec['inputs'][0]['label'])
            rec['name'] = rec['name'] + ' from ' + rec['inputs'][0]['label']
recipes.sort(key=lambda rec: rec['id'])

# The catalyst of each recipe (the NC item in NEI): the calculator's export leaves it out, it is
# the recipe's QFT_CATALYST metadata in the mods' sources (GTNH 2.9.0): GT5-Unofficial
# RecipeLoaderChemicalSkips and goodgenerator NaquadahRecipeLoader, NewHorizonsCoreMod
# ScriptGregtechPlusPlus (shirabon), CropsNH CropRecipes (molten_platinum). A new recipe needs a
# line here: the script stops without it.
CATALYSTS = {
    'biocells': 'Biological Intelligence Catalyst',
    'cerium_dust': 'Rare-Earth Group Catalyst',
    'holmium_dust_from_bastnasite_dust': 'Rare-Earth Group Catalyst',
    'holmium_dust_from_cerium_rich_mixture_dust': 'Rare-Earth Group Catalyst',
    'hyper_stable_self_healing_adhesive': 'Adhesion Promoter Catalyst',
    'inert_enriched_naquadah_dust': 'Simple Naquadah Catalyst',
    'inert_naquadah_dust': 'Simple Naquadah Catalyst',
    'inert_naquadria_dust': 'Advanced Naquadah Catalyst',
    'iridium_dust_from_iridium_metal_residue_dust': 'Platinum Group Catalyst',
    'iridium_dust_from_leach_residue_dust': 'Platinum Group Catalyst',
    'molten_osmium': 'Platinum Group Catalyst',
    'molten_platinum': 'Platinum Group Catalyst',
    'molten_polyethylene_pe': 'Plastic Polymer Catalyst',
    'molten_silicone_rubber': 'Rubber Polymer Catalyst',
    'netherite_scrap': 'Hellish Force Catalyst',
    'osmium_dust': 'Platinum Group Catalyst',
    'palladium_dust': 'Platinum Group Catalyst',
    'platinum_dust': 'Platinum Group Catalyst',
    'polyurethane_resin': 'Ultimate Plasticizer Catalyst',
    'prismatic_naquadah_dust': 'Crystal Colorization Catalyst',
    'rhodium_dust': 'Platinum Group Catalyst',
    'shirabon_dust': 'Temporal Harmonizer Catalyst',
    'stemcells': 'Raw Intelligence Catalyst',
    't_ceti_e_seaweed': 'Algagenic Growth Promoter Catalyst',
    'thorium_232_dust': 'Radioactivity Catalyst',
    'titanium_dust': 'Tita-Tungsten Indium Catalyst',
    'xenoxene': 'Ultimate Plasticizer Catalyst',
}
labels = {goods(g)[3] for g in items(E[1])}
ids = {rec['id'] for rec in recipes}
problems = [f'no catalyst for {i}: add it to CATALYSTS' for i in sorted(ids - CATALYSTS.keys())]
problems += [f'CATALYSTS: no recipe {i}' for i in sorted(CATALYSTS.keys() - ids)]
problems += [f'CATALYSTS: no item {c!r} in the data' for c in sorted(set(CATALYSTS.values()) - labels)]
if problems:
    sys.exit('\n'.join(problems))


def resource(e):
    fields = [f"{e['kind']} = {lua(e['label'])}", f"name = {lua(e['name'])}"]
    if e['damage'] is not None:
        fields.append(f"damage = {e['damage']}")
    fields.append(f"amount = {e['amount']}")
    return ', '.join(fields)


lines = [
    '---Every Quantum Force Transformer recipe, from the GTNH calculator\'s export (assets/data,',
    '---GTNH 2.9.0). GENERATED by tools/qft-recipes.py: do not edit, run it again instead.',
    '---NOT CHECKED against the mods\' sources: the calculator lags the modpack (see the script).',
    '---config/catalysts.lua says which catalysts the QFT has: every recipe of those runs on it.',
    '---  inputs  every input with its internal name: the stocking bus and hatch slots are set from',
    '---          them; amount per parallel per cycle (pieces, mB)',
    '---  outputs in NEI order: the index is the circuit that focuses it with Neptunium; amount per',
    '---          parallel per cycle; `molten`: what a dust output comes as in Fluid Mode, guessed',
    '---          by label ("X Dust" -> "Molten X", 144 mB a piece; else the fluid "X", 1000 mB)',
    '---  tier    the recipe\'s focus tier (qft_focus_tier): plasma needs shielding >= it',
    '---  catalyst its catalyst (NC in NEI): the label of the item in the bulk catalyst housing',
    'return {',
]
for rec in recipes:
    lines.append(f"    [{lua(rec['id'])}] = {{")
    lines.append(f"        name = {lua(rec['name'])},")
    lines.append(f"        tier = {rec['tier']},")
    lines.append(f"        catalyst = {lua(CATALYSTS[rec['id']])},")
    lines.append(f"        seconds = {rec['ticks'] / 20:g},")
    lines.append('        inputs = {')
    for e in rec['inputs']:
        lines.append(f'            {{ {resource(e)} }},')
    lines.append('        },')
    lines.append('        outputs = {')
    for e in rec['outputs']:
        m = molten(e['label']) if e['kind'] == 'item' else None
        extra = ''
        if m:
            extra = f", molten = {{ fluid = {lua(m[0])}, name = {lua(m[1])}, amount = {m[2] * e['amount']} }}"
        lines.append(f'            {{ {resource(e)}{extra} }},')
    lines.append('        },')
    lines.append('    },')
lines.append('}')
for rec in skipped:
    lines.insert(11, f"---  skipped: {rec['name']} (an ore dictionary input: a bus slot takes one item)")
OUT.write_text('\n'.join(lines) + '\n')
print(f'{len(recipes)} recipes -> {OUT}' + (f', {len(skipped)} skipped (ore dictionary input)' if skipped else ''))
