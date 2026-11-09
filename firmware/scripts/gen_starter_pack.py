#!/usr/bin/env python3
"""Builds the starter vocabulary embedded in the firmware from scripts/starter_words.tsv.

Writes main/offline/starter/starter_cards.bin and starter_cards.idx, in the same format as the packs the
server builds (see backend/app/packbuild.py): records "word\\0ipa\\0meaning\\0example\\0" and uint32 offsets.
The starter pack has no audio, so the device creates an empty audio.bin / all-zero audio.idx when it installs it.
Run: python3 scripts/gen_starter_pack.py
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "starter_words.tsv")
OUT = os.path.join(HERE, "..", "main", "offline", "starter")


def load_rows():
    """The starter words in card order: (word, ipa, meaning, example), duplicates dropped."""
    rows, seen = [], set()
    with open(SRC, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) != 4:
                sys.exit(f"{SRC}:{n}: expected 4 tab separated columns, got {len(cols)}")
            word, ipa, meaning, example = (c.strip() for c in cols)
            if word != word.lower() or not word.replace("-", "").replace("'", "").isalpha():
                sys.exit(f"{SRC}:{n}: word must be lowercase letters: {word!r}")
            if word in seen:
                continue
            seen.add(word)
            rows.append((word, ipa, meaning, example))
    return rows


def main():
    rows = load_rows()
    cards, idx = bytearray(), bytearray()
    for row in rows:
        idx += struct.pack("<I", len(cards))
        cards += b"\0".join(s.encode("utf-8") for s in row) + b"\0"
    idx += struct.pack("<I", len(cards))
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "starter_cards.bin"), "wb") as f:
        f.write(cards)
    with open(os.path.join(OUT, "starter_cards.idx"), "wb") as f:
        f.write(idx)
    print(f"{len(rows)} words, cards.bin {len(cards)} bytes, cards.idx {len(idx)} bytes")


if __name__ == "__main__":
    main()
