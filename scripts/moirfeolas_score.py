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

# Tags whose material MoirfEolas records in the urú column. "Len" is excluded:
# the gold does not mark lenition, so emitting it can only cost.
SPLIT_TAGS = ("Ecl", "HPref", "TPref")

# Lenition last, so eclipsis still outranks it on a word that reads as both.
SPLIT_TAGS_WITH_LENITION = SPLIT_TAGS + ("Len",)

# The d' / D' verbal particle. Scalpel has no rule for it (decision 4) and
# --simulate-dprime injects it as a synthetic reading to price that decision.
DPRIME_TAG = "Dpart"


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


def add_dprime(words, demut):
    """Inject a synthetic d'/D' reading, to price decision 4.

    d' is the past-tense verbal particle, not an initial mutation, so
    mutation_fst has no rule for it. The gold records it in the urú column
    anyway, and it is 639 of the 661 words layer 1 misses.
    """
    out = dict(demut)
    for w in words:
        if w[:2] in ("d'", "D'") and len(w) > 2:
            out[w] = [(DPRIME_TAG, w[2:])] + list(out.get(w, []))
    return out


def pick(word, candidates, tags=SPLIT_TAGS, concatenative_only=True):
    """Choose one mutation reading, or None.

    Eclipsis outranks lenition: the over-generating analyser reads 'bhfear'
    as both Len:bfear and Ecl:fear, and only the second is real. Tags are
    tried in the order given, so whichever list is passed sets the ranking.
    A candidate that fails the concatenative test is skipped rather than
    accepted, so a later candidate can still win its tag.
    """
    for tag in tags:
        for cand_tag, base in candidates:
            if cand_tag != tag or not base or base == word:
                continue
            marker, _, concat = marker_and_base(word, base)
            if not marker or (concatenative_only and not concat):
                continue
            return cand_tag, base
    return None


def marker_and_base(word, base):
    """Split ``word`` into the material a mutation added and the stem.

    Eclipsis, h-prefix, t-prefix and d' all prepend, so the marker is a
    genuine prefix and marker + base reconstructs the surface exactly.
    Lenition does not: it inserts an h after the first consonant, so
    'bhean' reads as 'bean' and the added 'h' sits at index 1. Trimming the
    shared prefix and suffix finds the added material in both cases.

    Returns ``(marker, base, concatenative)``. ``concatenative`` is True when
    marker + base == word, which is what the plain hybrid requires.
    """
    i = 0
    while i < len(word) and i < len(base) and word[i] == base[i]:
        i += 1
    j = 0
    while (j < len(word) - i and j < len(base) - i
           and word[len(word) - 1 - j] == base[len(base) - 1 - j]):
        j += 1
    marker = word[i:len(word) - j]
    return marker, base, marker + base == word


def mutation_splits(words, demut, tags=SPLIT_TAGS, concatenative_only=True):
    """word -> (tag, marker, base) for every word layer 1 fires on.

    With ``concatenative_only`` the marker must be a literal prefix of the
    surface, so the token stream still concatenates back to the word. That
    is the plain hybrid, and it excludes lenition by construction. Dropping
    the constraint is the rearranging variant.
    """
    split = {}
    for w in words:
        chosen = pick(w, demut.get(w, []), tags, concatenative_only)
        if not chosen:
            continue
        tag, base = chosen
        marker, base, _ = marker_and_base(w, base)
        split[w] = (tag, marker, base)
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


def system_backoff(words, encode, splits):
    return tl(encode(words))


def _layered(words, encode, split):
    # Encode every string the back-off actually sees: unsplit words whole,
    # split words by their base only. Decision 2 in docs/MOIRFEOLAS.md.
    to_encode = sorted({split[w][2] if w in split else w for w in words})
    enc = encode(to_encode)
    out = {}
    for w in words:
        if w in split:
            _, marker, base = split[w]
            out[w] = [marker] + enc[base]
        else:
            out[w] = enc[w]
    return tl(out)


def system_hybrid(words, encode, splits):
    return _layered(words, encode, splits["concat"])


def system_hybrid_splinter(words, encode, splits):
    """The rearranging variant: tokens no longer concatenate to the surface.

    Lenition is an infix, so no boundary split can isolate it. Emitting the
    added material as its own token and leaving the stem contiguous is the
    only way to reach it, at the cost of the token stream no longer
    reconstructing the word. MoirfEolas does not mark lenition yet, so
    against the current gold this is expected to lose: see the write-up.
    """
    return _layered(words, encode, splits["splinter"])


def system_fst_only(words, encode, splits):
    split = splits["concat"]
    out = {}
    for w in words:
        if w in split:
            _, marker, base = split[w]
            out[w] = [marker, base]
        else:
            out[w] = [w]
    return tl(out)


def system_identity(words, encode, splits):
    return tl({w: [w] for w in words})


SYSTEMS = {
    "backoff": system_backoff,
    "hybrid": system_hybrid,
    "hybrid_splinter": system_hybrid_splinter,
    "fst_only": system_fst_only,
    "identity": system_identity,
}

# Systems that do not depend on the back-off, so they are scored once rather
# than once per family.
BACKOFF_FREE = ("fst_only", "identity")


# --------------------------------------------------------------------------
# Scoring. The exclude-single-token filter is hers, copied from cli.py:
#   token_lens = {word: v for word, v in token_lens.items() if v[0] > 1}
# --------------------------------------------------------------------------

def exclude_single(token_lens):
    return {w: v for w, v in token_lens.items() if v[0] > 1}


def score(token_lens, affix, label):
    score_info = find_morphological_info(token_lens, affix)
    per_word, final, _, averages = calculate_morphological_score(score_info)

    # How many words sit behind each category average. The averages alone
    # hide that uru_only is 27 words and suffix_only is twenty thousand.
    counts = {name: 0 for name in CATEGORIES}
    for word in score_info:
        uru, prefix, suffix = affix[word]
        key = {(1, 0, 0): "uru_only", (0, 1, 0): "prefix_only",
               (0, 0, 1): "suffix_only", (1, 1, 0): "uru+prefix",
               (1, 0, 1): "uru+suffix", (0, 1, 1): "prefix+suffix",
               (1, 1, 1): "all_affixes"}.get(
                   (int(bool(uru)), int(bool(prefix)), int(bool(suffix))))
        if key:
            counts[key] += 1

    # remove_specials turns a standalone sentencepiece '_' piece into an
    # empty first token, which then fails every position-anchored test.
    # Her behaviour, not corrected here: see docs/MOIRFEOLAS.md.
    n_empty = sum(1 for _, toks in token_lens.values() if any(t == "" for t in toks))

    return {
        "label": label,
        "n_evaluated": len(score_info),
        "overall": final * 100,
        "categories": {k: (v * 100 if v is not None else None) for k, v in averages.items()},
        "category_counts": counts,
        "n_empty_token_words": n_empty,
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
    ap.add_argument("--simulate-dprime", action="store_true",
                    help="Inject a synthetic d'/D' reading. Scalpel has no rule "
                         "for the verbal particle (decision 4) and this prices "
                         "that decision without shipping it.")
    args = ap.parse_args()

    affix = load_moirfeolas(args.moirfeolas)
    words = list(affix.keys())
    print(f"Loaded {len(words)} words from {args.moirfeolas}")

    demut = demutate_all(words)
    tags = SPLIT_TAGS
    if args.simulate_dprime:
        demut = add_dprime(words, demut)
        tags = (DPRIME_TAG,) + tags
        print("Simulating the d' rule: synthetic readings injected.")

    splits = {
        "concat": mutation_splits(words, demut, tags),
        "splinter": mutation_splits(
            words, demut,
            tags + ("Len",) if "Len" not in tags else tags,
            concatenative_only=False),
    }
    for name, s in splits.items():
        print(f"Layer 1 ({name}) fires on {len(s)} / {len(words)} words "
              f"({len(s) / len(words) * 100:.1f}%)")

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
            backoff_tl = system_backoff(words, encode, splits)
            keep = {w for w, v in backoff_tl.items() if v[0] > 1}

            for name in args.systems:
                key = (f"{name}" if name in BACKOFF_FREE
                       else f"{name}_{family}_{size}")
                if key in out:
                    continue

                token_lens = SYSTEMS[name](words, encode, splits)
                if args.all_words:
                    pass
                elif args.matched_set:
                    token_lens = {w: v for w, v in token_lens.items() if w in keep}
                else:
                    token_lens = exclude_single(token_lens)

                label = (name if name in BACKOFF_FREE
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
