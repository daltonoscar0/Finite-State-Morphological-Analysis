"""Score the Scalpel Irish hybrid on MoirfEolas with Jane Adkins' CríochScore.

Wrapper only. Every part of the metric comes from her package in
``external/tknzn4Irish`` and nothing there is modified: the MoirfEolas loader,
the tokenizer loading and special-token handling, the boundary
classification, the aggregation and the absorption analysis are all hers.
This file builds the token streams for the systems under test and applies the
same ``--exclude-single-token`` filter her CLI applies.

Four systems:

``backoff``       a conventional subword tokenizer alone. Reproduces her
                  published numbers, and is also the "subword alone, no FST"
                  ablation.
``hybrid``        Scalpel's Irish initial-mutation FST in front of that
                  tokenizer. Layer 1 strips an eclipsis / h-prefix / t-prefix
                  and emits the mutation material as the first token; layer 2
                  encodes what is left.
``fst_only``      layer 1 with no back-off at all. A split word becomes
                  ``[material, base]`` and an unsplit word stays whole.
``identity``      no segmentation whatsoever, one token per word. The floor.

Lenition is analysed by the FST and deliberately discarded: MoirfEolas does
not mark it, so emitting it would only insert boundaries the gold cannot
credit. See docs/MOIRFEOLAS.md.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(REPO / "src"))

from criochscore.absorption import compute_absorption          # noqa: E402
from criochscore.moirfeolas import load_moirfeolas             # noqa: E402
from criochscore.scoring import (                              # noqa: E402
    calculate_morphological_score,
    find_morphological_info,
)
from criochscore.tokenizers_io import (                        # noqa: E402
    get_token_lens,
    load_tokenizer,
    remove_specials,
)

MOIRFEOLAS = REPO / "data" / "MoirfEolas" / "MoirfEolas.csv"
BRIDGE = ROOT / "ga_demutate"
RESULTS = Path.home() / "data" / "tok" / "results" / "irish" / "corrected"

# The six families she ships, with the model type and special-token marker
# each one needs. These are the values that reproduce her published numbers.
FAMILIES = {
    "BPE":              ("bpe_tokenizer_{size}.json",            "standard",      None),
    "Byte-BPE":         ("byte_level_bpe_tokenizer_{size}.json", "byte-level",    None),
    "SentencePieceBPE": ("sentencepiece_bpe_{size}.model",       "sentencepiece", "▁"),
    "SentencePieceULM": ("sentencepiece_unigram_{size}.model",   "sentencepiece", "▁"),
    "UnigramLM":        ("unigram_tokenizer_{size}.json",        "standard",      None),
    "WordPiece":        ("wordpiece_tokenizer_{size}.json",      "standard",      "##"),
}

SIZES = ["8k", "16k", "32k", "43k", "64k"]

# Tags whose material MoirfEolas records in the urú column. "Len" is excluded.
SPLIT_TAGS = ("Ecl", "HPref", "TPref")


# --------------------------------------------------------------------------
# Layer 1: Scalpel's mutation FST, via the bridge binary.
# --------------------------------------------------------------------------

def demutate_all(words):
    """word -> [(tag, base), ...], in the analyser's own candidate order."""
    proc = subprocess.run(
        [str(BRIDGE)], input="\n".join(words),
        capture_output=True, text=True, check=True,
    )
    out = {}
    for line in proc.stdout.splitlines():
        parts = line.split("\t")
        out[parts[0]] = [tuple(c.split(":", 1)) for c in parts[1:]]
    return out


def pick(word, candidates):
    """Choose one mutation reading, or None.

    Eclipsis outranks lenition: the over-generating analyser reads 'bhfear'
    as both Len:bfear and Ecl:fear, and only the second is real. The base
    must be a literal suffix of the surface, so the emitted material plus
    the base reconstruct the word exactly.
    """
    for tag in SPLIT_TAGS:
        for cand_tag, base in candidates:
            if cand_tag == tag and base and word.endswith(base) and len(base) < len(word):
                return cand_tag, base
    return None


def mutation_splits(words, demut):
    """word -> (tag, material, base) for every word layer 1 fires on."""
    split = {}
    for w in words:
        chosen = pick(w, demut.get(w, []))
        if chosen:
            tag, base = chosen
            split[w] = (tag, w[: len(w) - len(base)], base)
    return split


# --------------------------------------------------------------------------
# Layer 2: a conventional tokenizer, loaded and encoded by her code.
# --------------------------------------------------------------------------

def make_backoff(family, size):
    fname, model_type, special = FAMILIES[family]
    path = REPO / "tokenizers" / family / fname.format(size=size)
    if not path.exists():
        return None
    tok = load_tokenizer(path, model_type)

    def encode(ws):
        token_lens = remove_specials(get_token_lens(tok, list(ws), model_type), special)
        return {w: list(v[1]) for w, v in token_lens.items()}

    return encode


# --------------------------------------------------------------------------
# The four systems, each returning {word: (n_tokens, [tokens])}.
# --------------------------------------------------------------------------

def tl(mapping):
    return {w: (len(t), t) for w, t in mapping.items()}


def system_backoff(words, encode, split):
    return tl(encode(words))


def system_hybrid(words, encode, split):
    # Encode every string the back-off actually sees: unsplit words whole,
    # split words by their base only. Decision 2 in docs/MOIRFEOLAS.md.
    to_encode = sorted({split[w][2] if w in split else w for w in words})
    enc = encode(to_encode)
    out = {}
    for w in words:
        if w in split:
            _, material, base = split[w]
            out[w] = [material] + enc[base]
        else:
            out[w] = enc[w]
    return tl(out)


def system_fst_only(words, encode, split):
    out = {}
    for w in words:
        if w in split:
            _, material, base = split[w]
            out[w] = [material, base]
        else:
            out[w] = [w]
    return tl(out)


def system_identity(words, encode, split):
    return tl({w: [w] for w in words})


SYSTEMS = {
    "backoff": system_backoff,
    "hybrid": system_hybrid,
    "fst_only": system_fst_only,
    "identity": system_identity,
}


# --------------------------------------------------------------------------
# Scoring. The exclude-single-token filter is hers, copied from cli.py:
#   token_lens = {word: v for word, v in token_lens.items() if v[0] > 1}
# --------------------------------------------------------------------------

def exclude_single(token_lens):
    return {w: v for w, v in token_lens.items() if v[0] > 1}


def score(token_lens, affix, label):
    score_info = find_morphological_info(token_lens, affix)
    per_word, final, _, averages = calculate_morphological_score(score_info)
    return {
        "label": label,
        "n_evaluated": len(score_info),
        "overall": final * 100,
        "categories": {k: (v * 100 if v is not None else None) for k, v in averages.items()},
        "absorption": compute_absorption(score_info, affix),
        "_per_word": per_word,
        "_score_info": score_info,
    }


CATEGORIES = ["uru_only", "prefix_only", "suffix_only",
              "uru+prefix", "uru+suffix", "prefix+suffix", "all_affixes"]


def row(result):
    cells = [f"{result['overall']:.2f}"]
    for name in CATEGORIES:
        v = result["categories"][name]
        cells.append(f"{v:.2f}" if v is not None else "n/a")
    return cells


def report(result):
    print(f"{result['label']:<44} " + "  ".join(f"{c:>6}" for c in row(result))
          + f"   n={result['n_evaluated']}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--families", nargs="+", default=list(FAMILIES))
    ap.add_argument("--sizes", nargs="+", default=SIZES)
    ap.add_argument("--systems", nargs="+", default=list(SYSTEMS))
    ap.add_argument("--moirfeolas", default=str(MOIRFEOLAS))
    ap.add_argument("--all-words", action="store_true",
                    help="Skip her --exclude-single-token filter. Off by default: "
                         "her reference numbers are computed with it on.")
    ap.add_argument("--matched-set", action="store_true",
                    help="Filter every system by the BACK-OFF's token count rather "
                         "than its own, so all systems are scored on one identical "
                         "word set. A control for the hybrid delta, not her protocol.")
    ap.add_argument("--out", default=None, help="Write results JSON here.")
    ap.add_argument("--dump-per-word", default=None,
                    help="Write {system: {word: [statuses, score, tokens]}} here, "
                         "for the error analysis.")
    ap.add_argument("--summary-dir", default=None,
                    help="Write one <system>_morphological_summary.json per system, "
                         "in the format her 'criochscore absorption' subcommand "
                         "reads, so her absorption script can be run on it directly.")
    args = ap.parse_args()

    affix = load_moirfeolas(args.moirfeolas)
    words = list(affix.keys())
    print(f"Loaded {len(words)} words from {args.moirfeolas}")

    demut = demutate_all(words)
    split = mutation_splits(words, demut)
    print(f"Layer 1 fires on {len(split)} / {len(words)} words "
          f"({len(split) / len(words) * 100:.1f}%)")

    mode = ("all words" if args.all_words else
            "matched set (back-off token count)" if args.matched_set else
            "her protocol (--exclude-single-token, per system)")
    print(f"Filter: {mode}\n")
    print(f"{'system':<44} " + "  ".join(f"{c:>6}" for c in
          ["overall"] + [c[:6] for c in CATEGORIES]))

    out, per_word_dump = {}, {}
    for family in args.families:
        for size in args.sizes:
            encode = make_backoff(family, size)
            if encode is None:
                continue

            # The back-off's own token counts, needed for the matched set.
            backoff_tl = system_backoff(words, encode, split)
            keep = {w for w, v in backoff_tl.items() if v[0] > 1}

            for name in args.systems:
                # fst_only and identity do not depend on the back-off, so
                # they are scored once rather than once per family.
                key = (f"{name}" if name in ("fst_only", "identity")
                       else f"{name}_{family}_{size}")
                if key in out:
                    continue

                token_lens = SYSTEMS[name](words, encode, split)
                if args.all_words:
                    pass
                elif args.matched_set:
                    token_lens = {w: v for w, v in token_lens.items() if w in keep}
                else:
                    token_lens = exclude_single(token_lens)

                label = (name if name in ("fst_only", "identity")
                         else f"{name} {family} {size}")
                r = score(token_lens, affix, label)
                report(r)
                per_word_dump[key] = {
                    w: [list(statuses), s, r["_score_info"][w][3]]
                    for w, (statuses, s) in r["_per_word"].items()
                }
                if args.summary_dir:
                    d = Path(args.summary_dir)
                    d.mkdir(parents=True, exist_ok=True)
                    (d / f"{key}_morphological_summary.json").write_text(
                        json.dumps(r["_score_info"], ensure_ascii=False, indent=2))
                del r["_per_word"], r["_score_info"]
                out[key] = r

    if args.out:
        dest = Path(args.out)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(json.dumps(out, indent=2, ensure_ascii=False))
        print(f"\nwrote {dest}")
    if args.dump_per_word:
        dest = Path(args.dump_per_word)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(json.dumps(per_word_dump, ensure_ascii=False))
        print(f"wrote {dest}")


if __name__ == "__main__":
    main()
