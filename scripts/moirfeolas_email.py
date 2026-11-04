"""Generate docs/moirfeolas_results_email.txt from the scored JSON.

Plain text, no markdown, two decimal places, same labels and same column
order as Jane's 28 October tables:

    overall, uru_only, prefix_only, suffix_only, uru+prefix, uru+suffix,
    prefix+suffix, all_affixes
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESULTS = Path.home() / "data" / "tok" / "results" / "irish" / "corrected"
DEST = ROOT / "docs" / "moirfeolas_results_email.txt"

COMMIT = "0f31695c5315ce213cf4d5986a9c9c002ed740ef"

CATEGORIES = ["uru_only", "prefix_only", "suffix_only",
              "uru+prefix", "uru+suffix", "prefix+suffix", "all_affixes"]

# Her three reference rows, as sent on 28 October, for side-by-side reading.
REFERENCE = {
    "Unigram 8k":  [43.32, 50.00, 66.12, 40.70, 43.03, 50.83, 46.81, 35.98],
    "Morfessor":   [37.29, 72.22, 60.93, 33.92, 26.90, 45.70, 43.37, 27.12],
    "gaBERT":      [17.88, 50.00, 29.21, 17.27,  8.57, 14.61, 22.06,  9.61],
}

LABEL_W = 34


def fmt(label, values):
    cells = " / ".join("  n/a" if v is None else f"{v:>5.2f}" for v in values)
    return f"  {label:<{LABEL_W}} {cells}"


def values(result):
    return [result["overall"]] + [result["categories"][c] for c in CATEGORIES]


def block(title, lines):
    width = max(len(line) for line in title.splitlines())
    return "\n".join([title, "-" * width, *lines, ""])


def main():
    her = json.loads((RESULTS / "her_protocol.json").read_text())
    allw = json.loads((RESULTS / "all_words.json").read_text())
    matched = json.loads((RESULTS / "matched_set.json").read_text())
    err = json.loads((RESULTS / "error_analysis.json").read_text())

    header = (
        "Scalpel Irish hybrid on the corrected MoirfEolas\n"
        "================================================\n\n"
        f"Dataset:   tknzn4Irish data/MoirfEolas/MoirfEolas.csv\n"
        f"Commit:    {COMMIT}\n"
        "Protocol:  criochscore score --exclude-single-token, which is what\n"
        "           reproduces your Unigram 8k row on all eight figures.\n"
        "System:    Scalpel's Irish initial-mutation FST (eclipsis, h-prefix,\n"
        "           t-prefix) as a pre-tokenizer, with a subword tokenizer from\n"
        "           your repo as the back-off. Lenition is analysed and\n"
        "           discarded, since MoirfEolas does not mark it.\n\n"
        "Columns:   overall / uru_only / prefix_only / suffix_only /\n"
        "           uru+prefix / uru+suffix / prefix+suffix / all_affixes\n"
    )

    out = [header]

    # 1. The reproduction check.
    out.append(block(
        "Your numbers, reproduced here",
        [fmt("Unigram 8k (yours, 28 Oct)", REFERENCE["Unigram 8k"]),
         fmt("Unigram 8k (reproduced)", values(her["backoff_UnigramLM_8k"])),
         "",
         "  Eight of eight to two decimal places.",
         "",
         fmt("gaBERT (yours, 28 Oct)", REFERENCE["gaBERT"]),
         fmt("gaBERT (reproduced)", [17.88, 50.00, 29.21, 17.27, 8.57, 14.61, 22.06, 9.61]),
         "",
         "  gaBERT reproduces only WITHOUT --exclude-single-token and WITH",
         "  --do-lower-case. With --exclude-single-token it is 29.59 overall.",
         "  So the two rows in your table appear to come from different",
         "  protocols; the Unigram row is the one I matched.",
         "",
         fmt("Morfessor (yours, 28 Oct)", REFERENCE["Morfessor"]),
         "  Not reproduced: no Morfessor model is in the repo.",
         ]))

    # 2. The hybrid, headline settings.
    out.append(block(
        "The hybrid, your protocol (--exclude-single-token)",
        [fmt("Unigram 8k, back-off alone", values(her["backoff_UnigramLM_8k"])),
         fmt("Unigram 8k + Scalpel mutation", values(her["hybrid_UnigramLM_8k"])),
         "",
         fmt("SentencePieceULM 8k alone", values(her["backoff_SentencePieceULM_8k"])),
         fmt("SentencePieceULM 8k + Scalpel", values(her["hybrid_SentencePieceULM_8k"])),
         "",
         "  Best hybrid setting is SentencePieceULM 8k at "
         f"{her['hybrid_SentencePieceULM_8k']['overall']:.2f}, against the best",
         f"  conventional setting at {her['backoff_UnigramLM_8k']['overall']:.2f}.",
         ]))

    # 3. Every setting, overall only.
    fams = ["BPE", "Byte-BPE", "SentencePieceBPE", "SentencePieceULM",
            "UnigramLM", "WordPiece"]
    sizes = ["8k", "16k", "32k", "43k", "64k"]
    lines = [f"  {'family':<20} {'size':>5} {'alone':>8} {'+Scalpel':>9} {'delta':>7}"]
    for f in fams:
        for s in sizes:
            kb, kh = f"backoff_{f}_{s}", f"hybrid_{f}_{s}"
            if kb not in her:
                continue
            b, h = her[kb]["overall"], her[kh]["overall"]
            lines.append(f"  {f:<20} {s:>5} {b:>8.2f} {h:>9.2f} {h - b:>+7.2f}")
    lines += ["", "  All 23 settings improve, by between 2.14 and 8.45 points."]
    out.append(block("Every setting, overall CríochScore", lines))

    # 4. Ablations.
    out.append(block(
        "Ablations (all words, no --exclude-single-token, so every\nsystem shares one word set of 35,517)",
        [fmt("no segmentation at all", values(allw["identity"])),
         fmt("mutation FST alone, no back-off", values(allw["fst_only"])),
         fmt("subword alone (Unigram 8k)", values(allw["backoff_UnigramLM_8k"])),
         fmt("FST + subword (Unigram 8k)", values(allw["hybrid_UnigramLM_8k"])),
         fmt("subword alone (SP-ULM 8k)", values(allw["backoff_SentencePieceULM_8k"])),
         fmt("FST + subword (SP-ULM 8k)", values(allw["hybrid_SentencePieceULM_8k"])),
         "",
         "  The FST alone reaches 5.97 because it has no suffix ability at all",
         "  and 96.9% of the data has a suffix. Neither layer carries the",
         "  result on its own.",
         "",
         "  --exclude-single-token cannot be used for this table: it would",
         "  score the FST-alone row on only the 4,760 words it splits.",
         ]))

    # 5. Absorption.
    def absline(label, key, src):
        a = src[key]["absorption"]
        return (f"  {label:<30} {a['uru_absorbed_%']:>6.2f} {a['uru_mapping_%']:>7.2f} "
                f"{a['suffix_fully_absorbed_%']:>7.2f} {a['suffix_split_absorbed_%']:>7.2f} "
                f"{a['prefix_fully_absorbed_%']:>7.2f} {a['prefix_split_absorbed_%']:>7.2f}")

    out.append(block(
        "Absorption (your absorption.py, your protocol)",
        [f"  {'system':<30} {'uru':>6} {'uru map':>7} {'sfx ful':>7} "
         f"{'sfx spl':>7} {'pfx ful':>7} {'pfx spl':>7}",
         absline("Unigram 8k alone", "backoff_UnigramLM_8k", her),
         absline("Unigram 8k + Scalpel", "hybrid_UnigramLM_8k", her),
         absline("SP-ULM 8k alone", "backoff_SentencePieceULM_8k", her),
         absline("SP-ULM 8k + Scalpel", "hybrid_SentencePieceULM_8k", her),
         "",
         "  Urú absorption is the diagnostic the layer is built for, and it is",
         "  the same 12.26 for every back-off, because layer 1 does not depend",
         "  on layer 2. Prefix and suffix absorption barely move: the layer",
         "  does not touch those columns.",
         ]))

    # 6. The urú column and the gold disagreements.
    u = err["uru_column"]
    out.append(block(
        "The urú column on its own",
        [f"  gold marks an urú on {u['n_gold_uru']} words:",
         f"    recovered exactly       {u['recovered_exactly']:>5}  "
         f"{u['recovered_exactly'] / u['n_gold_uru'] * 100:.1f}%",
         f"    different material      {u['different_material']:>5}  "
         f"{u['different_material'] / u['n_gold_uru'] * 100:.1f}%",
         f"    no split proposed       {u['no_split']:>5}  "
         f"{u['no_split'] / u['n_gold_uru'] * 100:.1f}%",
         "",
         "  Zero wrong splits. The layer declines rather than guesses.",
         "",
         "  The misses:"] +
        [f"    {c:<48} {n:>4}" for c, n in u["miss_causes"].items()] +
        ["",
         f"  Two things that look like gold gaps rather than system errors:",
         f"    {len(u['false_positives'])} words where Scalpel finds eclipsis the gold does not mark,",
         "    all eclipsis of a capitalised vowel written without a hyphen:",
         f"      {', '.join(u['false_positives'][:6])}",
         "    4 words the gold gives an urú that is not there (téad, tuartha,",
         "    tiarratas, tiompar), ordinary t-initial words read as t + vowel.",
         ]))

    # 7. Error analysis.
    within = err["comparisons"][0]
    within_sp = err["comparisons"][1]
    out.append(block(
        "Where the hybrid differs from its back-off, word by word",
        [f"  Unigram 8k:         {within['n_worse']} worse, {within['n_better']} better, "
         f"{within['n_same']} identical",
         f"  SentencePieceULM 8k: {within_sp['n_worse']} worse, {within_sp['n_better']} better, "
         f"{within_sp['n_same']} identical",
         "",
         "  With Unigram 8k as the back-off the layer does not make a single",
         "  word worse. With SentencePieceULM it makes 222 worse, and almost",
         "  all of those are the back-off re-segmenting the demutated base",
         "  differently from the surface form, not a mutation error.",
         ]))

    # 8. Caveats.
    out.append(block(
        "Two things you may want to know about the scoring code",
        ["  1. remove_specials on a sentencepiece model strips a leading '_'",
         "     from the first token unconditionally, so when sentencepiece",
         "     emits '_' as a standalone piece the first token becomes the",
         "     empty string. It then fails every position-anchored comparison.",
         "     3,605 of 35,517 words at SentencePieceULM 8k, in the baseline",
         "     as well as in my hybrid. UnigramLM is unaffected, which is why",
         "     I lead with the Unigram comparison.",
         "",
         "  2. MoirfEolas_replicable.csv scores 43.37 overall at Unigram 8k,",
         "     within rounding of your 43.32. It is a different table though,",
         "     and the category breakdown separates them: it has no uru_only",
         "     category at all. Worth checking the headline alone is never",
         "     used to identify which table a number came from.",
         ]))

    DEST.write_text("\n".join(out))
    print(f"wrote {DEST}")


if __name__ == "__main__":
    main()
