#!/usr/bin/env python3
"""Builds the web's item icons from an NEI item panel dump made in the game client.

In the game: NEI Options -> Inventory -> Collapsible Items -> Disabled (or the groups dump only
their first item), clear the search, then Tools -> Data Dumps -> Item Panel three times, changing
nothing between them: CSV, JSON, PNG at 32x32. They land in .minecraft/dumps/.

    tools/icons.py <.minecraft/dumps> [out-dir]    (out-dir: factoryctl/icons)

Writes icons.webp, a grid of 32 px icons 256 to a row, and index.bin (gzipped JSON: the static
handler of userver refuses a .gz name), which numbers them:
    {"version": 1, "size": 32, "perRow": 256,
     "items": [[name, damage, label], ...],   # the n-th is icon n
     "fluids": [[label, icon], ...]}
An item with NBT variants keeps the first one's icon (OC gives no NBT). A fluid has no name in
the dump (NEI shows it as neiFluidDisplay, its damage the world's fluid id): it goes by label.
Needs Pillow.
"""

import difflib
import gzip
import json
import os
import re
import sys

from PIL import Image

kSize = 32
kPerRow = 256
kFluid = 'NotEnoughItems:neiFluidDisplay'
# GuiItemIconDumper.illegalChars: these become '_' in a file name.
kIllegal = {34, 60, 62, 124, 58, 42, 63, 92, 47} | set(range(32))
# The top-level fields of a line of the JSON dump: they come last, after the tag.
kTopLevel = re.compile(r'Damage:(-?\d+)s,id:(\d+)s}$')


def fail(message):
    sys.exit(f'icons: {message}')


def clean_file_name(label):
    return ''.join('_' if ord(c) in kIllegal else c for c in label)


def parse_csv_line(line):
    """A line as DataDumper.concat writes it: a field is quoted only when it has a comma or a
    quote past its first character, so a label that starts with a quote may be bare."""
    head = line.split(',', 4)
    if len(head) != 5:
        return None
    label = head[4]
    if len(label) >= 2 and label[0] == '"' and label[-1] == '"':
        inner = label[1:-1].replace('""', '"')
        if inner.find(',') > 0 or inner.find('"') > 0:
            label = inner
    return head[:4] + [label]


def read_dump(dumps):
    with open(os.path.join(dumps, 'itempanel.csv'), encoding='utf-8') as f:
        lines = f.read().splitlines()[1:]
    with open(os.path.join(dumps, 'itempanel.json'), encoding='utf-8') as f:
        tags = f.read().splitlines()
    if len(lines) != len(tags):
        fail(f'the CSV has {len(lines)} items, the JSON {len(tags)}: not one dump')
    icons = os.path.join(dumps, 'itempanel_icons')
    files = set(os.listdir(icons))
    taken = set()
    out = []
    for n, (line, tag) in enumerate(zip(lines, tags), 1):
        row = parse_csv_line(line)
        top = kTopLevel.search(tag)
        if row is None or top is None:
            fail(f'item {n}: cannot read {line!r}')
        name, item_id, meta, _, label = row
        if int(top.group(2)) != int(item_id) or int(top.group(1)) != int(meta):
            fail(f'item {n}: the CSV and the JSON disagree ({line!r}): not one dump')
        # GuiItemIconDumper.exportImage: a taken name gets _2, _3, ...
        base = clean_file_name(label)
        file = base + '.png'
        k = 2
        while file in taken:
            file = f'{base}_{k}.png'
            k += 1
        taken.add(file)
        out.append([name, int(meta), label, file])
    # A few items change their name from call to call (QED, Pizza or Cake Glove): such a file
    # goes to the item missing its own whose name is closest. Many: not one dump.
    missing = [entry for entry in out if entry[3] not in files]
    left = sorted(files - taken)
    if len(missing) != len(left) or len(missing) > 10:
        fail(f'{len(missing)} items have no icon, {len(left)} icons no item: not one dump')
    for entry in missing:
        best = max(left, key=lambda f: difflib.SequenceMatcher(None, f, entry[3]).ratio())
        print(f'icons: {entry[0]}:{entry[1]}: no {entry[3]!r}, took {best!r}', file=sys.stderr)
        entry[3] = best
        left.remove(best)
    for entry in out:
        entry[3] = os.path.join(icons, entry[3])
    return out


def main():
    if len(sys.argv) not in (2, 3):
        fail('usage: icons.py <.minecraft/dumps> [out-dir]')
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = sys.argv[2] if len(sys.argv) == 3 else os.path.join(here, 'icons')

    items, item_pngs, fluids, fluid_pngs, seen = [], [], [], [], set()
    for name, damage, label, png in read_dump(sys.argv[1]):
        if name == kFluid:
            fluids.append(label)
            fluid_pngs.append(png)
        elif (name, damage) not in seen:  # else an NBT variant
            seen.add((name, damage))
            items.append([name, damage, label])
            item_pngs.append(png)
    # The items first: an item's icon is its place in "items"; the fluids follow.
    pngs = item_pngs + fluid_pngs
    fluids = [[label, len(items) + n] for n, label in enumerate(fluids)]

    atlas = Image.new('RGBA', (kSize * kPerRow, kSize * ((len(pngs) + kPerRow - 1) // kPerRow)))
    for n, png in enumerate(pngs):
        with Image.open(png) as icon:
            if icon.size != (kSize, kSize):
                fail(f'{png}: {icon.size[0]}x{icon.size[1]}, not {kSize}x{kSize}')
            atlas.paste(icon.convert('RGBA'), ((n % kPerRow) * kSize, (n // kPerRow) * kSize))

    os.makedirs(out_dir, exist_ok=True)
    atlas.save(os.path.join(out_dir, 'icons.webp'), lossless=True, method=4)
    index = {'version': 1, 'size': kSize, 'perRow': kPerRow, 'items': items, 'fluids': fluids}
    with gzip.open(os.path.join(out_dir, 'index.bin'), 'wt', encoding='utf-8') as f:
        json.dump(index, f, ensure_ascii=False, separators=(',', ':'))
    print(f'icons: {len(items)} items, {len(fluids)} fluids, {len(pngs)} icons -> {out_dir}')


if __name__ == '__main__':
    main()
