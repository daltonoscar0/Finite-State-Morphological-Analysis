"""How deep into a word does Scalpel's lenition rule reach?

The question from the 13 November call: does the FST only see lenition on the
very first consonant of a word, or does it also find it when morphemes sit in
front of the lenited consonant? 'bhean' is word-initial lenition; 'seanbhean'
and 'an-mhaith' are the same séimhiú one morpheme further in.

MoirfEolas does not annotate lenition, so the sites are located here rather
than read off the gold. Two facts make that tractable:

* Orthographically a séimhiú is always a lenitable consonant followed by h,
  so the candidate sites are exactly the C+h sequences for C in bcdfgmpst.
* A séimhiú only ever lands on the *first* consonant of a morpheme. A C+h
  inside a morpheme ('athair', the 'mh' of the prefix 'comh') is ordinary
  spelling, not a mutation. So the morpheme layout decides which C+h
  sequences are real sites, and the layout is what MoirfEolas does annotate.

Layout is read as [urú material][prefix][stem], each optional. Depth is the
number of those morphemes preceding the site:

  0   site at the start of the stem with nothing in front   bhean
  1   one morpheme in front: a prefix, or a proclitic        seanbhean, d'fhág
  2+  both, or a prefix on top of a clitic                   d'fhorbhfhás

The urú column carries the d'/m'/b' proclitics as well as eclipsis and the
h-/t- prefixes, so a clitic counts as a preceding morpheme. Clitic-fronted
lenition is reported as its own line rather than folded into prefix lenition,
because the two need different fixes: Stage 2 splits the clitic, Stage 1
extends the prefix inventory.

MoirfEolas has a single prefix column, so it cannot express two stacked
prefixes. Depth 2+ is therefore detectable only where one of the two
morphemes is a clitic or urú; genuine double prefixing is reported as a
known blind spot rather than counted.
"""
import argparse
import csv
import json
import random
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(REPO / "src"))
sys.path.insert(0, str(ROOT / "scripts"))

from ga_mutation_split import demutate_all                     # noqa: E402
from ga_lenition_prefix import (                               # noqa: E402
    LENITING_PREFIXES, lenition_site, prefix_lenition,
    prefix_lenition_composed,
)

MOIRFEOLAS = REPO / "data" / "MoirfEolas" / "MoirfEolas.csv"
CORPUS = REPO / "data" / "GA" / "unique_ga_tokens.txt"
DEFAULT_CSV = ROOT / "results" / "irish" / "lenition_sites_v2.csv"
DEFAULT_MD = ROOT / "docs" / "irish" / "lenition_depth.md"

# The nine consonants that take a séimhiú. l, n, r, h and the vowels do not.
LENITABLE = set("bcdfgmpstBCDFGMPST")
VOWELS = set("aeiouáéíóúAEIOUÁÉÍÓÚ")

# Fossilised th-/ch- forms: single morphemes that merely begin that way.
FIXED = {
    "chun", "chuig", "chuige", "chuici", "chucu", "chugam", "chugat",
    "chugainn", "chugaibh", "cheana", "chomh", "cha", "char", "chan",
    "thar", "tharam", "tharat", "thairis", "thairste", "tharainn",
    "tharaibh", "thartu", "thart", "thall", "thíos", "thuas", "thoir",
    "thiar", "theas", "thimpeall", "thrí", "thríd",
}

SEED = 20261118
SAMPLE = 30

FIELDS = ["word", "site_index", "depth", "bucket", "radical", "hyphenated",
          "uru", "prefix", "stem", "scalpel_identified", "scalpel_reading",
          "ext_identified", "ext_prefix", "ext_stem", "stem_attested",
          "note"]


def layout(word, uru, prefix):
    """Where the stem starts, and how many morphemes precede it.

    Returns ``(stem_start, n_before, hyphenated)``. The urú material is
    stripped first because a prefix sits under it ('gcomhaontófar' is g +
    comh + aontófar). Seventeen gold rows tag the h of a Greek-derived stem
    ('hidri-', 'haema-') as an h-prefix, which makes the urú-stripped read
    fail; those fall back to matching the prefix at index 0.
    """
    n_before, start = 0, 0
    if uru and word.lower().startswith(uru.lower()):
        start = len(uru)
        n_before += 1

    if prefix:
        if word[start:].lower().startswith(prefix.lower()):
            start += len(prefix)
            n_before += 1
        elif word.lower().startswith(prefix.lower()):
            # Gold mistagged the urú; the prefix is really at index 0.
            start, n_before = len(prefix), 1
        else:
            return None

    hyphenated = False
    if prefix:
        # 'an-mhaith' hyphenates straight after the prefix; 'seana-mháthair'
        # inserts a linking vowel first. Both are one morpheme boundary.
        probe = start
        while probe < len(word) and probe - start < 2 and word[probe] in VOWELS:
            probe += 1
        if word[start:start + 1] == "-":
            start, hyphenated = start + 1, True
        elif word[probe:probe + 1] == "-":
            start, hyphenated = probe + 1, True

    return start, n_before, hyphenated


def sites(word, stem_start):
    """Morpheme-initial séimhiú sites, by the FST's own validity rule.

    Using ``lenition_site`` rather than a looser C+h test matters: 'sh'
    before c or m is not a séimhiú ('shciatháin', 'Shmutánach'), and
    counting those as sites would charge the FST with misses for forms it
    is right to reject.
    """
    rest = word[stem_start:]
    if not lenition_site(rest):
        return []
    # A doubled h ('ghhairdín') is a corpus typo, and surface and analyser
    # disagree about which h was added. Degenerate, so not counted either way.
    if rest[2:3] in ("h", "H"):
        return []
    return [stem_start]


def scalpel_len_sites(word, readings):
    """Where Scalpel's own Len readings put the inserted h.

    A Len reading gives the stem with the h taken out, so the first index at
    which surface and stem differ is the h, and the radical is one before it.
    """
    found = {}
    for tag, base in readings:
        if tag != "Len" or not base:
            continue
        i = 0
        while i < len(word) and i < len(base) and word[i] == base[i]:
            i += 1
        if i > 0 and word[i:i + 1].lower() == "h":
            found[i - 1] = f"{tag}:{base}"
    return found


def bucket_of(depth):
    return "0 (word-initial)" if depth == 0 else \
           "1 (one morpheme in front)" if depth == 1 else "2+ (two or more)"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--moirfeolas", default=str(MOIRFEOLAS))
    ap.add_argument("--corpus", default=str(CORPUS),
                    help="One word per line, used only to vet short prefixes.")
    ap.add_argument("--out-csv", default=str(DEFAULT_CSV))
    ap.add_argument("--out-md", default=str(DEFAULT_MD))
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    with open(args.moirfeolas, encoding="utf-8") as f:
        gold = list(csv.DictReader(f))
    words = [r["word"] for r in gold]
    print(f"Loaded {len(words)} words from {args.moirfeolas}")

    demut = demutate_all(words)
    print("Ran Scalpel's mutation FST over the full list.")

    # Attestation set for vetting short prefixes. Both sources, because a
    # stem can be a real word without being a MoirfEolas row.
    attested = {w.lower() for w in words}
    if Path(args.corpus).exists():
        with open(args.corpus, encoding="utf-8") as f:
            attested |= {line.strip().lower() for line in f if line.strip()}
    print(f"Attestation set: {len(attested)} forms "
          f"(MoirfEolas + {Path(args.corpus).name})")

    rows, unparsed = [], 0
    for r in gold:
        word = r["word"]
        uru, prefix = r["urú"].strip(), r["prefix"].strip()
        lay = layout(word, uru, prefix)
        if lay is None:
            unparsed += 1
            continue
        stem_start, n_before, hyphenated = lay
        identified = scalpel_len_sites(word, demut.get(word, []))

        for idx in sites(word, stem_start):
            # A clitic in front is a morpheme, but it is Stage 2's problem.
            clitic = bool(uru) and uru.endswith("'")
            note = []
            if word.lower() in FIXED:
                note.append("fossilised")
            if clitic:
                note.append("clitic-fronted")

            # Only credit the extension when it finds *this* site, not just
            # any site: firing at the wrong offset is a miss, not a hit.
            ext = prefix_lenition(word, attested)
            ext_ok = bool(ext) and len(ext[0]) == idx
            if not ext_ok:
                comp = prefix_lenition_composed(
                    word, demut.get(word, []), attested)
                if comp and len(comp[0]) + len(comp[1]) == idx:
                    ext = (comp[0] + comp[1], comp[2])
                    ext_ok = True
            rows.append({
                "word": word,
                "site_index": idx,
                "depth": n_before,
                "bucket": bucket_of(n_before),
                "radical": word[idx],
                "hyphenated": int(hyphenated),
                "uru": uru,
                "prefix": prefix,
                "stem": word[stem_start:],
                "scalpel_identified": int(idx in identified),
                "scalpel_reading": identified.get(idx, ""),
                "ext_identified": int(ext_ok),
                "ext_prefix": ext[0] if ext_ok else "",
                "ext_stem": ext[1] if ext_ok else "",
                "stem_attested": int(
                    (ext[1].lower() in attested) if ext_ok else False),
                "note": ";".join(note),
            })

    print(f"{len(rows)} morpheme-initial lenition sites; "
          f"{unparsed} rows whose layout could not be read")

    dest = Path(args.out_csv)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with open(dest, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {dest}")

    # ---------------------------------------------------------------- report
    by = defaultdict(list)
    for row in rows:
        by[row["bucket"]].append(row)

    print(f"\n{'bucket':<28}{'sites':>8}{'found':>8}{'recall':>9}")
    summary = {}
    for b in sorted(by):
        got = sum(r["scalpel_identified"] for r in by[b])
        n = len(by[b])
        summary[b] = {"sites": n, "identified": got, "recall": got / n * 100}
        print(f"{b:<28}{n:>8}{got:>8}{got / n * 100:>8.1f}%")

    # Hyphenated vs not, within depth >= 1.
    deep = [r for r in rows if r["depth"] >= 1 and not r["note"]]
    for label, sel in (("hyphenated", 1), ("unhyphenated", 0)):
        s = [r for r in deep if r["hyphenated"] == sel]
        if s:
            got = sum(r["scalpel_identified"] for r in s)
            print(f"  depth>=1 {label:<14}{len(s):>8}{got:>8}"
                  f"{got / len(s) * 100:>8.1f}%")

    clitic = [r for r in rows if "clitic-fronted" in r["note"]]
    if clitic:
        got = sum(r["scalpel_identified"] for r in clitic)
        print(f"  of which clitic-fronted{len(clitic):>13}{got:>8}"
              f"{got / len(clitic) * 100:>8.1f}%")

    # ------------------------------------------------- the flagged extension
    print("\nextension (closed prefix list), recall by bucket:")
    ext_summary = {}
    for b in sorted(by):
        n = len(by[b])
        off = sum(r["scalpel_identified"] for r in by[b])
        on = sum(1 for r in by[b]
                 if r["scalpel_identified"] or r["ext_identified"])
        ext_summary[b] = {"sites": n, "flag_off": off, "flag_on": on,
                          "recall_off": off / n * 100, "recall_on": on / n * 100}
        print(f"  {b:<28}{n:>7}{off:>8}{off / n * 100:>7.1f}%"
              f"{on:>8}{on / n * 100:>7.1f}%")

    # Over-firing: the extension fires on a word where the gold layout gives
    # no site at that offset. Without a lenition gold these are candidates,
    # not confirmed errors, so they are counted and sampled for hand-checking.
    gold_sites = {(r["word"], r["site_index"]) for r in rows}
    overfire = []
    for word in words:
        ext = prefix_lenition(word, attested)
        if not ext:
            comp = prefix_lenition_composed(word, demut.get(word, []), attested)
            ext = (comp[0] + comp[1], comp[2]) if comp else None
        if ext and (word, len(ext[0])) not in gold_sites:
            overfire.append({"word": word, "prefix": ext[0], "stem": ext[1],
                             "stem_attested": int(ext[1].lower() in attested)})
    n_fire = sum(1 for r in rows if r["ext_identified"]) + len(overfire)
    print(f"\n  extension fires on {n_fire} sites in total")
    print(f"    agreeing with a gold-layout site {n_fire - len(overfire)}")
    print(f"    at an offset the gold layout does not mark {len(overfire)}"
          f"  ({len(overfire) / n_fire * 100:.1f}%)")
    if overfire:
        att = sum(r["stem_attested"] for r in overfire)
        print(f"    of those, delenited stem attested elsewhere {att}"
              f"  ({att / len(overfire) * 100:.1f}%)")

    random.seed(SEED)
    out = {"summary": summary, "extension": ext_summary,
           "n_sites": len(rows), "unparsed": unparsed,
           "n_ext_fires": n_fire, "n_overfire": len(overfire),
           "overfire_sample": random.sample(overfire, min(40, len(overfire))),
           "prefixes": sorted(LENITING_PREFIXES),
           "samples": {}}
    for b in sorted(by):
        miss = [r for r in by[b] if not r["scalpel_identified"]]
        hit = [r for r in by[b] if r["scalpel_identified"]]
        out["samples"][b] = {
            "misses": [r["word"] for r in random.sample(miss, min(SAMPLE, len(miss)))],
            "hits": [r["word"] for r in random.sample(hit, min(SAMPLE, len(hit)))],
            "n_misses": len(miss), "n_hits": len(hit),
        }

    if args.json:
        d = Path(args.json)
        d.parent.mkdir(parents=True, exist_ok=True)
        d.write_text(json.dumps(out, ensure_ascii=False, indent=2))
        print(f"wrote {d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
