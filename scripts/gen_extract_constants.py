#!/usr/bin/env python3
"""Regenerate tools/extract_gamejson_const.inc from the King's Bounty pack.

    python3 scripts/gen_extract_constants.py [game.json] [strings/en.json] [out.inc]

The extractor (tools/extract_gamejson.c) writes these port-authored sections
into the game.json it builds, unchanged, since they are not recovered from
KB.EXE. Run this after editing any of them in assets/kings-bounty/.
"""
import json
import sys

SECTIONS = ["render", "tile_codes", "spawn", "contract", "controls", "sprites",
            "ending", "colors", "credits", "audio"]


def main():
    game = sys.argv[1] if len(sys.argv) > 1 else "assets/kings-bounty/game.json"
    strings = sys.argv[2] if len(sys.argv) > 2 else "assets/kings-bounty/strings/en.json"
    out = sys.argv[3] if len(sys.argv) > 3 else "tools/extract_gamejson_const.inc"
    g = json.load(open(game, encoding="utf-8"))
    d = {k: g[k] for k in SECTIONS}
    d["strings"] = json.load(open(strings, encoding="utf-8"))
    js = json.dumps(d, ensure_ascii=False, separators=(",", ":"))
    lit = js.replace("\\", "\\\\").replace('"', '\\"')
    with open(out, "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED from assets/kings-bounty/game.json sections that are\n"
                "// port-authored constants rather than values recovered from KB.EXE.\n"
                "// The extractor writes them into its game.json output unchanged, and the\n"
                "// strings to strings/en.json. Regenerate with\n"
                "// scripts/gen_extract_constants.py after editing the pack.\n"
                "//\n"
                "// Sections: render, tile_codes, spawn, contract, controls, sprites,\n"
                "//           ending, colors, credits, audio, strings.\n"
                "static const char EX_PORT_CONSTANTS_JSON[] =\n"
                '"' + lit + '";\n')


if __name__ == "__main__":
    main()
