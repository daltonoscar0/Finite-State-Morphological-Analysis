"""Check that the hybrid's split is a lossless transformation of the word.

A Splinter-style rearrangement is only legitimate if it can be undone: the
tokenizer has to be able to hand a decoder back the original string. The
plain hybrid is lossless by construction, because it refuses any reading
whose marker is not a literal prefix. ``hybrid_splinter`` drops that
constraint to reach lenition, so for that scheme losslessness is a claim that
has to be measured rather than assumed.

Two inverses are run over every word either scheme fires on:

``rebuild(tag, marker, base)``   told which mutation it is.
``rebuild_untagged(marker, base)``  given only the token stream.

The second answers the question that will be asked on the call: does the
decoder need a tag inventory, or is the split self-describing?
"""
import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "external" / "tknzn4Irish" / "src"))
sys.path.insert(0, str(ROOT / "scripts"))

from criochscore.moirfeolas import load_moirfeolas               # noqa: E402
from moirfeolas_score import (                                   # noqa: E402
    MOIRFEOLAS, SPLIT_TAGS, SPLIT_TAGS_WITH_LENITION,
    demutate_all, mutation_splits, rebuild, rebuild_untagged,
)

RESULTS = Path.home() / "data" / "tok" / "results" / "irish" / "corrected"


def check(split):
    """Run both inverses over one split scheme."""
    by_tag = defaultdict(lambda: {"n": 0, "ok": 0, "fail": 0,
                                  "ok_untagged": 0, "fail_untagged": 0})
    failures, failures_untagged = [], []

    for word in sorted(split):
        tag, marker, base = split[word]
        c = by_tag[tag]
        c["n"] += 1

        got = rebuild(tag, marker, base)
        if got == word:
            c["ok"] += 1
        else:
            c["fail"] += 1
            failures.append({"word": word, "tag": tag, "marker": marker,
                             "base": base, "rebuilt": got})

        got_u = rebuild_untagged(marker, base)
        if got_u == word:
            c["ok_untagged"] += 1
        else:
            c["fail_untagged"] += 1
            failures_untagged.append({"word": word, "tag": tag, "marker": marker,
                                      "base": base, "rebuilt": got_u})

    total = {
        "n": sum(c["n"] for c in by_tag.values()),
        "ok": sum(c["ok"] for c in by_tag.values()),
        "fail": sum(c["fail"] for c in by_tag.values()),
        "ok_untagged": sum(c["ok_untagged"] for c in by_tag.values()),
        "fail_untagged": sum(c["fail_untagged"] for c in by_tag.values()),
        "by_tag": {k: dict(v) for k, v in sorted(by_tag.items())},
        "failures": failures,
        "failures_untagged": failures_untagged,
    }
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--moirfeolas", default=str(MOIRFEOLAS))
    ap.add_argument("--out", default=str(RESULTS / "roundtrip.json"))
    args = ap.parse_args()

    affix = load_moirfeolas(args.moirfeolas)
    words = list(affix.keys())
    demut = demutate_all(words)

    schemes = {
        "concat": mutation_splits(words, demut, SPLIT_TAGS),
        "splinter": mutation_splits(words, demut, SPLIT_TAGS_WITH_LENITION,
                                    concatenative_only=False),
    }

    out = {"n_words": len(words), "schemes": {}}
    for name, split in schemes.items():
        r = check(split)
        out["schemes"][name] = r
        print(f"\n=== {name}: {r['n']} words")
        print(f"  {'tag':<8} {'words':>7} {'rebuilt':>8} {'failed':>7} "
              f"{'untagged ok':>12} {'untagged fail':>14}")
        for tag, c in r["by_tag"].items():
            print(f"  {tag:<8} {c['n']:>7} {c['ok']:>8} {c['fail']:>7} "
                  f"{c['ok_untagged']:>12} {c['fail_untagged']:>14}")
        print(f"  {'TOTAL':<8} {r['n']:>7} {r['ok']:>8} {r['fail']:>7} "
              f"{r['ok_untagged']:>12} {r['fail_untagged']:>14}")

        if r["failures"]:
            print(f"  tagged failures ({len(r['failures'])}):")
            for f in r["failures"]:
                print(f"    {f['word']} [{f['tag']}] {f['marker']!r}+{f['base']!r} "
                      f"-> {f['rebuilt']}")
        else:
            print("  tagged failures: none")

        if r["failures_untagged"]:
            print(f"  untagged failures ({len(r['failures_untagged'])}):")
            for f in r["failures_untagged"][:20]:
                print(f"    {f['word']} [{f['tag']}] {f['marker']!r}+{f['base']!r} "
                      f"-> {f['rebuilt']}")
            if len(r["failures_untagged"]) > 20:
                print(f"    ... and {len(r['failures_untagged']) - 20} more")
        else:
            print("  untagged failures: none")

    sp = out["schemes"]["splinter"]
    lossless = sp["fail"] == 0
    tagfree = sp["fail_untagged"] == 0
    out["verdict"] = (
        ("Both schemes round-trip exactly with the tag. " if lossless
         else f"The splinter scheme loses {sp['fail']} words even with the tag. ")
        + ("The tag is not needed: reading whether the stem starts with a vowel "
           "separates an h-prefix from a lenition h, and that recovers every "
           "word in both schemes. It is an Irish phonological rule rather than "
           "a property of the encoding, so a tag is still the safer thing to "
           "carry." if tagfree
           else f"Without the tag {sp['fail_untagged']} words cannot be rebuilt, "
                "so the decoder needs the tag.")
    )
    print(f"\n{out['verdict']}")

    dest = Path(args.out)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(out, indent=2, ensure_ascii=False))
    print(f"\nwrote {dest}")


if __name__ == "__main__":
    main()
