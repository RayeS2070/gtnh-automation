#!/usr/bin/env python3
"""Local tool, not for the game: the source of a modpack mod at the version
the modpack ships.

The version comes from the mod list in gtnh-modpack/README.md, as checked out
(a release tag, e.g. 2.8.4). Mod versions are tags of the mods' repositories.
The source goes to sources/<repository>, which git ignores; run again after
moving the modpack to another tag and it follows.

  tools/mod-source.py OpenComputers     clone or update sources/OpenComputers
  tools/mod-source.py --list [TEXT]     the mods and versions, filtered by TEXT
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODPACK = ROOT / "gtnh-modpack"
SOURCES = ROOT / "sources"

# "| [OpenComputers](https://github.com/GTNewHorizons/OpenComputers) | 1.11.20-GTNH |"
ROW = re.compile(r"^\| \[(?P<name>[^\]]+)\]\((?P<url>[^)]+)\) \| (?P<version>[^|]+?) \|$")


def git(*args, cwd=None, check=True):
    return subprocess.run(["git", *args], cwd=cwd, check=check, text=True,
                          capture_output=True)


def fail(message):
    print(f"mod-source: {message}", file=sys.stderr)
    sys.exit(1)


def modpack_tag():
    tag = git("describe", "--tags", "--exact-match", cwd=MODPACK, check=False)
    return tag.stdout.strip() if tag.returncode == 0 else None


def mods():
    readme = MODPACK / "README.md"
    if not readme.is_file():
        fail(f"{readme} not found: is the gtnh-modpack submodule checked out?")
    return [m.groupdict() for line in readme.read_text().splitlines()
            if (m := ROW.match(line.strip()))]


def find(name, all_mods):
    exact = [m for m in all_mods if m["name"].lower() == name.lower()]
    if exact:
        return exact[0]
    partial = [m for m in all_mods if name.lower() in m["name"].lower()]
    if len(partial) == 1:
        return partial[0]
    if not partial:
        fail(f'no mod "{name}" in the modpack; see --list')
    names = "\n  ".join(m["name"] for m in partial)
    fail(f'"{name}" matches several mods, name one:\n  {names}')


def fetch(mod):
    url, version = mod["url"], mod["version"]
    if not url.startswith("https://github.com/"):
        fail(f"{mod['name']} {version} is not on GitHub: {url}")
    dest = SOURCES / url.rstrip("/").rsplit("/", 1)[1]

    if not dest.exists():
        clone = git("clone", "--quiet", "--depth", "1", "--branch", version,
                    url, str(dest), check=False)
        if clone.returncode != 0:
            fail(f"cannot clone {url} at {version}:\n{clone.stderr.strip()}")
        return dest, "cloned"

    current = git("describe", "--tags", "--exact-match", cwd=dest, check=False)
    if current.returncode == 0 and current.stdout.strip() == version:
        return dest, "already there"
    if git("status", "--porcelain", cwd=dest).stdout.strip():
        fail(f"{dest} has local changes: commit, stash or remove them first")
    for args in (("fetch", "--quiet", "--depth", "1", "origin", "tag", version),
                 ("checkout", "--quiet", version)):
        step = git(*args, cwd=dest, check=False)
        if step.returncode != 0:
            fail(f"cannot move {dest} to {version}:\n{step.stderr.strip()}")
    return dest, "moved"


def main():
    parser = argparse.ArgumentParser(
        description="The source of a modpack mod at the modpack's version.")
    parser.add_argument("name", nargs="?", help="mod name, or a unique part of it")
    parser.add_argument("--list", nargs="?", const="", metavar="TEXT",
                        help="list the mods and versions, filtered by TEXT")
    args = parser.parse_args()
    if (args.name is None) == (args.list is None):
        parser.error("give a mod name or --list")

    tag = modpack_tag()
    if tag is None:
        print("mod-source: gtnh-modpack is not on a release tag; versions "
              "are as its README has them", file=sys.stderr)
    all_mods = mods()

    if args.list is not None:
        for m in all_mods:
            if args.list.lower() in m["name"].lower():
                print(f"{m['name']}  {m['version']}")
        return

    mod = find(args.name, all_mods)
    dest, what = fetch(mod)
    print(f"{mod['name']} {mod['version']} (modpack {tag or '?'}): {what}, "
          f"{dest.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
