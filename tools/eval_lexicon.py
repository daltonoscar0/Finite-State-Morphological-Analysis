#!/usr/bin/env python3
"""Evaluate the analyzer as a tokenizer layer, at several lexicon sizes.

Five stages, each of which can be run on its own:

  corpus   Coverage on running text. What share of alphabetic tokens the
           analyzer reaches at all, what share it decomposes, and which words
           it most often fails on, token weighted and type weighted.

  gold     Boundary precision, recall and F1 against the MorphyNet gold,
           inflection and derivation separately. IN DISTRIBUTION: see the note
           below.

  curve    Both of the above for each lexicon size, as a table and as JSON.

  policy   The default ranking against --derived-first: where they disagree,
           which one the gold prefers, and the worst cases in each direction
           ordered by corpus frequency.

  morpholex  An independent check, if the spreadsheet can be read. Optional
           and reported separately.

Standard library only. It drives the analyzer binary through
tools/fst_tokenizer.py, which keeps one process alive per configuration,
because loading a 440k-entry lexicon costs about three seconds and analyzing
a word costs about eighty microseconds.

    python3 tools/eval_lexicon.py --all \\
        --lexicon-dir ~/data/morph/lexicons \\
        --json ~/data/morph/eval.json

On independence
---------------
MorphyNet is mined from Wiktionary, which is also what tools/build_lexicon.py
builds the lexicon from. Scores against the MorphyNet gold for words that are
in the lexicon are therefore optimistic: the gold and the lexicon are two
views of the same source, and agreeing with yourself is not evidence. Every
MorphyNet number here is labelled in-distribution for that reason, and should
be read as an upper bound and as a regression check rather than as accuracy on
unseen text.

MorphoLex is independent, built by psycholinguists from a different pipeline,
and is the honest held-out number. It is licensed for non-commercial use, so
it is used for evaluation only and never to build a lexicon.
"""

import argparse
import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from fst_tokenizer import AnalyzerError, FstTokenizer  # noqa: E402
from score_segmentation import (internal_boundaries, match_boundaries,  # noqa: E402
                                prf)

WORD_RE = re.compile(r"^[a-z]+$")

# An alphabetic token is a run of letters, which may carry an internal
# apostrophe. Requiring a letter first is what keeps a clitic out of the
# counts: WikiText writes possessives and negations with a space, as
# "Federer 's" and "do n't", so a bare "[A-Za-z]+" pattern turns every one of
# them into a one-letter token. "s" alone came to 7,607 tokens, one percent of
# the corpus, all of it counted as the analyzer failing on a word that was
# never a word.
TOKEN_RE = re.compile(r"[A-Za-z]+(?:['’][A-Za-z]+)*")

DEFAULT_CORPUS = "~/data/morph/corpus/sample.txt"
DEFAULT_LEXICON_DIR = "~/data/morph/lexicons"
DEFAULT_INFLECTION = "~/data/morph/morphynet/eng.inflectional.v1.tsv"
DEFAULT_DERIVATION = "~/data/morph/morphynet/eng.derivational.v1.tsv"
DEFAULT_MORPHOLEX = "~/data/morph/morpholex/MorphoLex-en/MorphoLEX_en.xlsx"

# The lexicons tools/build_lexicon.py writes, smallest first. "" is the
# built-in lists on their own, which is the baseline the whole exercise is
# measured against.
CURVE_LEXICONS = ["", "en_5k.tsv", "en_20k.tsv", "en_50k.tsv", "en_100k.tsv",
                  "en_all.tsv"]


def expand(path):
    return os.path.expanduser(path) if path else path


def longest_common_prefix(a, b):
    i = 0
    limit = min(len(a), len(b))
    while i < limit and a[i] == b[i]:
        i += 1
    return i


# ── Gold conversion ─────────────────────────────────────────────────────────
#
# MorphyNet segments against the LEMMA, not the surface form: "microtome|ing"
# for "microtoming". The surface boundary is the length of the longest common
# prefix of the first morph and the surface form, which puts the boundary at 8
# and gives "microtom|ing".
#
# That is the same convention the analyzer uses, by construction: its stem
# piece is the longest prefix of the surface word shared with the lemma. So
# the two agree on where an alternation puts the boundary, and no tolerance
# for being one character out is needed. See docs/SEGMENTATION.md.

def read_inflection_gold(path):
    """Read eng.inflectional.v1.tsv as surface pieces.

    Rows are `lemma form POS|FEATURES segmentation`. A row is a boundary only
    when its segmentation has two morphs; the rest are the lemma's own row,
    which has nothing to say about a boundary.

    Returns:
        (gold, stats) where gold maps a surface form to the set of piece
        tuples attested for it, and stats counts what was skipped and why.
    """
    gold = collections.defaultdict(set)
    stats = collections.Counter()

    with open(path, encoding="utf-8") as handle:
        for line in handle:
            parts = line.rstrip("\n").split("\t")
            stats["rows"] += 1
            if len(parts) != 4:
                stats["skipped_field_count"] += 1
                continue
            lemma, form, feature, segmentation = parts
            morphs = segmentation.split("|")
            if len(morphs) != 2:
                stats["skipped_not_two_morphs"] += 1
                continue
            if not (WORD_RE.match(form) and WORD_RE.match(lemma)):
                stats["skipped_not_lowercase_az"] += 1
                continue
            cut = longest_common_prefix(morphs[0], form)
            if cut == 0:
                stats["skipped_no_shared_prefix"] += 1
                continue
            if cut >= len(form):
                stats["skipped_boundary_at_edge"] += 1
                continue
            gold[form].add((form[:cut], form[cut:]))
            stats["kept"] += 1
            stats["kept_" + feature.split("|")[0]] += 1

    stats["forms"] = len(gold)
    return gold, stats


def read_derivation_gold(path):
    """Read eng.derivational.v1.tsv as surface pieces.

    Rows are `source target srcPOS tgtPOS affix type`. For a suffix the
    boundary is the longest common prefix of source and target; for a prefix
    it is the length of the affix.

    The gold contains etymological pairs as well as synchronic ones: `moon`
    to `month` gives `mo|nth`, and `merchant` to `merchandise` gives
    `merchan|dise`. Nothing filters those out, because any filter would be a
    guess about which derivations are still productive, and the point of this
    number is to compare policies against a fixed reference rather than to
    measure truth.
    """
    gold = collections.defaultdict(set)
    stats = collections.Counter()

    with open(path, encoding="utf-8") as handle:
        for line in handle:
            parts = line.rstrip("\n").split("\t")
            stats["rows"] += 1
            if len(parts) != 6:
                stats["skipped_field_count"] += 1
                continue
            source, target, _src_pos, _tgt_pos, affix, kind = parts
            if not (WORD_RE.match(source) and WORD_RE.match(target)):
                stats["skipped_not_lowercase_az"] += 1
                continue
            if kind == "suffix":
                cut = longest_common_prefix(source, target)
            elif kind == "prefix":
                if not target.startswith(affix):
                    stats["skipped_prefix_not_present"] += 1
                    continue
                cut = len(affix)
            else:
                stats["skipped_unknown_type"] += 1
                continue
            if cut == 0:
                stats["skipped_no_shared_prefix"] += 1
                continue
            if cut >= len(target):
                stats["skipped_boundary_at_edge"] += 1
                continue
            gold[target].add((target[:cut], target[cut:]))
            stats["kept"] += 1
            stats["kept_" + kind] += 1

    stats["forms"] = len(gold)
    return gold, stats


# ── Scoring ─────────────────────────────────────────────────────────────────

def score_gold(tokenizer, gold, limit=None):
    """Score the analyzer's primary segmentation against a gold mapping.

    A form with more than one attested gold segmentation is credited for
    matching any of them, which is the usual treatment of ambiguous gold.

    Two sets of boundary numbers are reported, because they answer different
    questions. `analyzed` covers only the forms the analyzer reached at all,
    and says how good the segmentations are when there is one. `all` counts a
    form the analyzer could not analyze as predicting no boundaries, so its
    gold boundary is a false negative, and says how good the layer is as a
    whole. Precision is identical in both, since a fallback record predicts
    nothing; recall and F1 are not.
    """
    forms = sorted(gold)
    if limit:
        forms = forms[:limit]

    counts = collections.Counter()
    worst = []

    records = tokenizer.segment_many(forms)
    for form, record in zip(forms, records):
        counts["forms"] += 1
        analyzed = record["source"] == "fst"
        if analyzed:
            counts["analyzed"] += 1
        counts["gate_" + record["gate"]] += 1

        predicted_pieces = record["pieces"]
        predicted = internal_boundaries(predicted_pieces)
        exact = any(tuple(predicted_pieces) == pieces for pieces in gold[form])
        if exact:
            counts["exact"] += 1
            if analyzed:
                counts["exact_analyzed"] += 1

        # Score against whichever attested segmentation this prediction does
        # best on, so an ambiguous gold entry is not an automatic loss.
        best = None
        for pieces in gold[form]:
            tp, fp, fn = match_boundaries(predicted,
                                          internal_boundaries(pieces), 0)
            if best is None or tp > best[0]:
                best = (tp, fp, fn)
        tp, fp, fn = best

        counts["all_tp"] += tp
        counts["all_fp"] += fp
        counts["all_fn"] += fn
        if analyzed:
            counts["an_tp"] += tp
            counts["an_fp"] += fp
            counts["an_fn"] += fn
            if not exact and len(worst) < 400:
                worst.append((form, "|".join(predicted_pieces),
                              " or ".join("|".join(p) for p in sorted(gold[form]))))

    result = {
        "forms": counts["forms"],
        "analyzed": counts["analyzed"],
        "coverage": ratio(counts["analyzed"], counts["forms"]),
        "exact_over_all": ratio(counts["exact"], counts["forms"]),
        "exact_over_analyzed": ratio(counts["exact_analyzed"],
                                     counts["analyzed"]),
        "gates": {k[len("gate_"):]: v for k, v in counts.items()
                  if k.startswith("gate_")},
        "mismatches": worst[:40],
    }
    for label, prefix in (("analyzed_only", "an_"), ("misses_as_errors", "all_")):
        p, r, f = prf(counts[prefix + "tp"], counts[prefix + "fp"],
                      counts[prefix + "fn"])
        result[label] = {"precision": p, "recall": r, "f1": f,
                         "tp": counts[prefix + "tp"],
                         "fp": counts[prefix + "fp"],
                         "fn": counts[prefix + "fn"]}
    return result


def ratio(part, whole):
    return (part / whole) if whole else 0.0


def score_corpus(tokenizer, path, top_fallback=100):
    """Coverage on running text, token weighted and type weighted.

    Only alphabetic tokens are counted. Numbers and punctuation are
    passthrough by construction and would dilute every share with a constant.

    WikiText writes a hyphen as "@-@" and a decimal point as "@.@", so those
    markers are stripped before tokenizing; otherwise every hyphenated word
    would arrive as two tokens plus a stray "@".
    """
    counts = collections.Counter()
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.replace("@-@", "-").replace("@.@", ".").replace("@,@", ",")
            for token in TOKEN_RE.findall(line):
                counts[token.lower()] += 1

    types = sorted(counts)
    tokens_total = sum(counts.values())

    by_gate_tokens = collections.Counter()
    by_gate_types = collections.Counter()
    by_source_tokens = collections.Counter()
    by_source_types = collections.Counter()
    fallback = collections.Counter()
    decomposed_tokens = decomposed_types = 0

    records = tokenizer.segment_many(types)
    for word, record in zip(types, records):
        n = counts[word]
        by_gate_tokens[record["gate"]] += n
        by_gate_types[record["gate"]] += 1
        by_source_tokens[record["source"]] += n
        by_source_types[record["source"]] += 1
        if len(record["pieces"]) > 1:
            decomposed_tokens += n
            decomposed_types += 1
        if record["source"] == "fallback":
            fallback[word] = n

    return {
        "corpus": path,
        "tokens": tokens_total,
        "types": len(types),
        "token_weighted": {
            "source": {k: ratio(v, tokens_total)
                       for k, v in by_source_tokens.items()},
            "gate": {k: ratio(v, tokens_total)
                     for k, v in by_gate_tokens.items()},
            "decomposed": ratio(decomposed_tokens, tokens_total),
        },
        "type_weighted": {
            "source": {k: ratio(v, len(types))
                       for k, v in by_source_types.items()},
            "gate": {k: ratio(v, len(types)) for k, v in by_gate_types.items()},
            "decomposed": ratio(decomposed_types, len(types)),
        },
        "fallback_tokens": sum(fallback.values()),
        "top_fallback": fallback.most_common(top_fallback),
    }


# ── Comparing the two ranking policies ──────────────────────────────────────

def corpus_counts(path):
    """Token counts per lowercase type, for weighting the policy comparison."""
    counts = collections.Counter()
    if not os.path.isfile(path):
        return counts
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.replace("@-@", "-").replace("@.@", ".").replace("@,@", ",")
            for token in TOKEN_RE.findall(line):
                counts[token.lower()] += 1
    return counts


def compare_policies(standard, derived, gold, counts, limit=25):
    """Where the two rankings disagree, and which one the gold prefers.

    Both policies rank the same analyses, so a disagreement is always one
    reading winning over another, never a reading appearing or vanishing.
    Each disagreement falls into one of three buckets: --derived-first agrees
    with the gold where the default did not, the default agreed and
    --derived-first does not, or neither agrees.

    The lists are ordered by corpus frequency, because a wrong split on a word
    nobody writes costs a tokenizer nothing and a wrong split on "government"
    costs it on every page.
    """
    forms = sorted(gold)
    standard_records = standard.segment_many(forms)
    derived_records = derived.segment_many(forms)

    counts_by_bucket = collections.Counter()
    gains, losses, both_wrong = [], [], []

    for form, a, b in zip(forms, standard_records, derived_records):
        pieces_a, pieces_b = tuple(a["pieces"]), tuple(b["pieces"])
        if pieces_a == pieces_b:
            counts_by_bucket["same"] += 1
            continue
        counts_by_bucket["differ"] += 1
        right_a = pieces_a in gold[form]
        right_b = pieces_b in gold[form]
        row = (counts.get(form, 0), form, "|".join(pieces_a),
               "|".join(pieces_b),
               " or ".join("|".join(p) for p in sorted(gold[form])))
        if right_b and not right_a:
            counts_by_bucket["gain"] += 1
            gains.append(row)
        elif right_a and not right_b:
            counts_by_bucket["loss"] += 1
            losses.append(row)
        else:
            counts_by_bucket["neither"] += 1
            both_wrong.append(row)

    gains.sort(reverse=True)
    losses.sort(reverse=True)
    both_wrong.sort(reverse=True)
    return {
        "same": counts_by_bucket["same"],
        "differ": counts_by_bucket["differ"],
        "gain": counts_by_bucket["gain"],
        "loss": counts_by_bucket["loss"],
        "neither": counts_by_bucket["neither"],
        "top_gains": gains[:limit],
        "top_losses": losses[:limit],
        "top_neither": both_wrong[:limit],
    }


def print_policy(name, result):
    print(f"\n{name}")
    print(f"  forms where the two policies agree   {result['same']}")
    print(f"  forms where they differ              {result['differ']}")
    print(f"    --derived-first right, default wrong   {result['gain']}")
    print(f"    default right, --derived-first wrong   {result['loss']}")
    print(f"    neither matches the gold               {result['neither']}")
    for label, key in (("gains", "top_gains"), ("losses", "top_losses"),
                       ("neither", "top_neither")):
        rows = result[key]
        if not rows:
            continue
        print(f"\n  worst {label}, by corpus frequency:")
        print(f"    {'freq':>6}  {'word':<18}{'default':<22}"
              f"{'derived-first':<22}gold")
        for freq, form, a, b, g in rows:
            print(f"    {freq:>6}  {form:<18}{a:<22}{b:<22}{g}")


# ── MorphoLex, the independent check ────────────────────────────────────────

def read_morpholex(path):
    """Read the MorphoLex spreadsheet, if it can be read reliably.

    An .xlsx file is a zip of XML, so the standard library can open it:
    zipfile plus xml.etree. What it cannot do is guess the format of the
    segmentation column, and MorphoLex writes a hierarchical parse rather than
    surface pieces, in the notation its data dictionary calls the "MorphoLexSegm"
    column:

        {(dis)>agree>(ment)}        prefix dis, root agree, suffix ment
        {((in)>flect)>(ion)}        nested, innermost root first

    Braces hold the whole parse, angle brackets separate a morpheme from what
    follows it, parentheses mark an affix, and a bare token is a root. Nesting
    records the order of attachment, which surface pieces do not carry, so the
    conversion here flattens it: strip the braces and the nesting, take the
    morphemes left to right, and keep the result only when the morphemes
    concatenate to the word exactly.

    That last condition is what makes this safe to report. MorphoLex gives
    morphemes in their canonical spelling, not as slices of the surface form,
    so "agreement" comes out as dis + agree + ment and concatenates, while
    "cities" comes out as city + s and does not. Rows that do not concatenate
    are counted and excluded rather than repaired by guesswork, because
    guessing is exactly what would make an independent gold standard stop
    being independent.

    Returns:
        (gold, stats). gold maps a word to a set of one piece tuple. stats
        records how many rows were read, kept and dropped, and why.
    """
    import xml.etree.ElementTree as ElementTree
    import zipfile

    stats = collections.Counter()
    gold = {}

    namespace = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"

    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        shared = []
        if "xl/sharedStrings.xml" in names:
            root = ElementTree.fromstring(archive.read("xl/sharedStrings.xml"))
            for si in root.findall(f"{namespace}si"):
                shared.append("".join(t.text or "" for t in
                                      si.iter(f"{namespace}t")))

        sheets = sorted(n for n in names
                        if n.startswith("xl/worksheets/sheet"))
        if not sheets:
            raise ValueError("no worksheets in the workbook")

        for sheet in sheets:
            root = ElementTree.fromstring(archive.read(sheet))
            rows = root.iter(f"{namespace}row")
            header = None
            for row in rows:
                values = []
                for cell in row.findall(f"{namespace}c"):
                    value = cell.find(f"{namespace}v")
                    text = value.text if value is not None else None
                    if cell.get("t") == "s" and text is not None:
                        index = int(text)
                        text = shared[index] if index < len(shared) else ""
                    elif cell.get("t") == "inlineStr":
                        node = cell.find(f"{namespace}is")
                        text = ("".join(t.text or "" for t in
                                        node.iter(f"{namespace}t"))
                                if node is not None else "")
                    values.append(text or "")

                if header is None:
                    header = [v.strip() for v in values]
                    stats["sheets_with_header"] += 1
                    continue

                record = dict(zip(header, values))
                word = (record.get("Word") or "").strip().lower()
                parse = (record.get("MorphoLexSegm") or "").strip()
                if not word or not parse:
                    continue
                stats["rows"] += 1
                if not WORD_RE.match(word):
                    stats["skipped_not_lowercase_az"] += 1
                    continue
                pieces = flatten_morpholex(parse)
                if not pieces:
                    stats["skipped_unparsable_parse"] += 1
                    continue
                if len(pieces) < 2:
                    stats["skipped_single_morpheme"] += 1
                    continue
                if "".join(pieces) != word:
                    stats["skipped_does_not_concatenate"] += 1
                    continue
                gold[word] = {tuple(pieces)}
                stats["kept"] += 1

    stats["forms"] = len(gold)
    return gold, stats


def flatten_morpholex(parse):
    """Flatten a MorphoLex parse to a left-to-right morpheme list.

    The notation marks roots with braces, morphemes with parentheses and
    suffix attachment with angle brackets, and it nests to record the order of
    attachment:

        {(work)}(man){(like)}            work, man, like
        {(typo)}(graph)>ic>>al>>ly>      typo, graph, ic, al, ly
        {(theo)(log)>ic>>al>}>ly>        theo, log, ic, al, ly

    All of that is structure surface pieces do not carry, so it is dropped and
    the morphemes are taken left to right. Nothing is required of the brackets
    themselves: a parse can end in `}` or in `>` depending on whether the last
    morpheme is a root or a suffix, and demanding a closing brace threw away
    27,261 of the 68,623 rows. What makes the result trustworthy is the check
    in the caller, that the morphemes concatenate to the word.

    Returns an empty list if any token is not a plain lowercase word.
    """
    morphemes = []
    for token in re.split(r"[>{}()]+", parse):
        token = token.strip().lower()
        if not token:
            continue
        if not WORD_RE.match(token):
            return []
        morphemes.append(token)
    return morphemes


# ── Reporting ───────────────────────────────────────────────────────────────

def print_corpus(result):
    print(f"\nCorpus: {result['corpus']}")
    print(f"  alphabetic tokens {result['tokens']}, types {result['types']}")
    print(f"  {'':<16}{'tokens':>10}{'types':>10}")
    for key in ("fst", "fallback", "passthrough"):
        t = result["token_weighted"]["source"].get(key, 0.0)
        y = result["type_weighted"]["source"].get(key, 0.0)
        if t or y:
            print(f"  source {key:<9}{t:>10.1%}{y:>10.1%}")
    for key in ("decomposed", "lexicon_stem", "irregular", "no_analysis",
                "passthrough"):
        t = result["token_weighted"]["gate"].get(key, 0.0)
        y = result["type_weighted"]["gate"].get(key, 0.0)
        if t or y:
            print(f"  gate {key:<11}{t:>10.1%}{y:>10.1%}")
    print(f"  pieces > 1      "
          f"{result['token_weighted']['decomposed']:>10.1%}"
          f"{result['type_weighted']['decomposed']:>10.1%}")


def print_top_fallback(result, limit=100):
    top = result["top_fallback"][:limit]
    if not top:
        print("\n  no fallback words")
        return
    print(f"\n  {len(top)} most frequent fallback words "
          f"({result['fallback_tokens']} fallback tokens in all):")
    for i in range(0, len(top), 5):
        row = top[i:i + 5]
        print("    " + "  ".join(f"{w} {n}" for w, n in row))


def print_gold(name, result, stats=None):
    print(f"\n{name}")
    if stats:
        print(f"  gold rows {stats['rows']}, usable {stats['kept']}, "
              f"distinct forms {stats['forms']}")
    print(f"  forms scored          {result['forms']}")
    print(f"  coverage (source fst) {result['coverage']:.1%}"
          f"  ({result['analyzed']} forms)")
    print(f"  exact match, analyzed only    "
          f"{result['exact_over_analyzed']:.1%}")
    print(f"  exact match, misses as errors {result['exact_over_all']:.1%}")
    print(f"  {'':<22}{'prec':>8}{'rec':>8}{'F1':>8}")
    for label in ("analyzed_only", "misses_as_errors"):
        s = result[label]
        print(f"  {label:<22}{s['precision']:>8.3f}{s['recall']:>8.3f}"
              f"{s['f1']:>8.3f}")


def print_mismatches(result, limit=15):
    if not result["mismatches"]:
        return
    print(f"  worst cases (predicted, then gold):")
    for form, predicted, gold in result["mismatches"][:limit]:
        print(f"    {form:<22} {predicted:<26} {gold}")


def print_curve(rows):
    print("\nLexicon size curve")
    print(f"  {'lexicon':<12}{'lexemes':>9}{'tok fst':>9}{'typ fst':>9}"
          f"{'tok dec':>9}{'infl F1':>9}{'deriv F1':>9}")
    for row in rows:
        print(f"  {row['name']:<12}{row['lexemes']:>9}"
              f"{row['token_fst']:>8.1%}{row['type_fst']:>9.1%}"
              f"{row['token_decomposed']:>9.1%}"
              f"{row['inflection_f1']:>9.3f}{row['derivation_f1']:>9.3f}")


# ── Driving the analyzer ────────────────────────────────────────────────────

def analyzer_args(lexicons, derived_first):
    args = []
    for path in lexicons:
        args += ["--lexicon", path]
    if derived_first:
        args.append("--derived-first")
    return args


def lexeme_count(tokenizer):
    """The lexeme count the analyzer printed on its stderr at startup."""
    match = re.search(r"Loaded (\d+) lexemes", tokenizer.stderr_tail(8000))
    return int(match.group(1)) if match else 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Evaluate the analyzer as a tokenizer layer")
    parser.add_argument("--analyzer", default="./analyzer")
    parser.add_argument("--corpus", default=DEFAULT_CORPUS)
    parser.add_argument("--lexicon-dir", default=DEFAULT_LEXICON_DIR)
    parser.add_argument("--lexicon", action="append", default=[],
                        metavar="FILE",
                        help="lexicon for the corpus and gold stages; "
                             "repeatable. The curve stage uses --lexicon-dir.")
    parser.add_argument("--closed-class", default="data/closed_class.tsv",
                        help="loaded alongside every lexicon, since no built "
                             "lexicon can supply a determiner; "
                             "'' to leave it out")
    parser.add_argument("--inflection", default=DEFAULT_INFLECTION)
    parser.add_argument("--derivation", default=DEFAULT_DERIVATION)
    parser.add_argument("--morpholex", default=DEFAULT_MORPHOLEX)
    parser.add_argument("--gold-limit", type=int, default=0,
                        help="score only the first N gold forms, for a quick "
                             "run (0 means all)")
    parser.add_argument("--derived-first", action="store_true",
                        help="pass --derived-first to the analyzer")
    parser.add_argument("--json", metavar="PATH",
                        help="write every number to this path as JSON")
    parser.add_argument("--stages", default="",
                        help="comma separated: corpus, gold, curve, policy, "
                             "morpholex")
    parser.add_argument("--all", action="store_true",
                        help="run corpus, gold and curve")
    args = parser.parse_args(argv)

    stages = {s.strip() for s in args.stages.split(",") if s.strip()}
    if args.all:
        stages |= {"corpus", "gold", "curve"}
    if not stages:
        stages = {"corpus"}

    corpus = expand(args.corpus)
    report = {"stages": sorted(stages), "derived_first": args.derived_first}

    # Loaded alongside every lexicon, including every point of the curve,
    # because no built lexicon can supply a determiner.
    closed_class = [args.closed_class] if args.closed_class else []

    # The corpus, gold, policy and morpholex stages use whatever --lexicon
    # asked for. The curve supplies its own lexicon at each point and must not
    # also load this one, or every point would measure the same thing.
    base = closed_class + [expand(p) for p in args.lexicon]

    gold_limit = args.gold_limit or None

    # Gold is read once and shared by the gold and curve stages.
    inflection = derivation = None
    if stages & {"gold", "curve", "policy"}:
        inflection = read_inflection_gold(expand(args.inflection))
        derivation = read_derivation_gold(expand(args.derivation))
        report["gold_inflection_stats"] = dict(inflection[1])
        report["gold_derivation_stats"] = dict(derivation[1])

    if "corpus" in stages:
        if not os.path.isfile(corpus):
            sys.exit(f"error: no corpus at {corpus}. See docs/LEXICON.md for "
                     f"how to make one.")
        with FstTokenizer(args.analyzer,
                          extra_args=analyzer_args(base, args.derived_first)) as tok:
            print(f"\n=== Corpus coverage, lexicons: "
                  f"{base or ['built-in only']} ===")
            result = score_corpus(tok, corpus)
            result["lexemes"] = lexeme_count(tok)
            print_corpus(result)
            print_top_fallback(result)
            report["corpus"] = result

    if "gold" in stages:
        with FstTokenizer(args.analyzer,
                          extra_args=analyzer_args(base, args.derived_first)) as tok:
            print(f"\n=== MorphyNet gold, IN DISTRIBUTION, lexicons: "
                  f"{base or ['built-in only']} ===")
            print("    MorphyNet is mined from Wiktionary, which is also what")
            print("    the lexicon is built from, so these are an upper bound.")
            for name, (gold, stats) in (("Inflection", inflection),
                                        ("Derivation", derivation)):
                result = score_gold(tok, gold, gold_limit)
                print_gold(name + " (in distribution)", result, stats)
                print_mismatches(result)
                report["gold_" + name.lower()] = result

    if "curve" in stages:
        lexicon_dir = expand(args.lexicon_dir)
        rows = []
        for name in CURVE_LEXICONS:
            path = os.path.join(lexicon_dir, name) if name else ""
            if name and not os.path.isfile(path):
                print(f"\nnote: skipping {name}, not found in {lexicon_dir}")
                continue
            lexicons = list(closed_class)
            if path:
                lexicons.append(path)
            with FstTokenizer(args.analyzer,
                              extra_args=analyzer_args(lexicons,
                                                       args.derived_first)) as tok:
                label = name or "built-in"
                print(f"\n=== curve point: {label} ===")
                corpus_result = score_corpus(tok, corpus) if os.path.isfile(corpus) else None
                infl = score_gold(tok, inflection[0], gold_limit)
                deriv = score_gold(tok, derivation[0], gold_limit)
                row = {
                    "name": label,
                    "lexemes": lexeme_count(tok),
                    "token_fst": (corpus_result["token_weighted"]["source"]
                                  .get("fst", 0.0) if corpus_result else 0.0),
                    "type_fst": (corpus_result["type_weighted"]["source"]
                                 .get("fst", 0.0) if corpus_result else 0.0),
                    "token_decomposed": (corpus_result["token_weighted"]
                                         ["decomposed"] if corpus_result else 0.0),
                    "inflection_f1": infl["analyzed_only"]["f1"],
                    "inflection_f1_misses_as_errors":
                        infl["misses_as_errors"]["f1"],
                    "inflection_coverage": infl["coverage"],
                    "derivation_f1": deriv["analyzed_only"]["f1"],
                    "derivation_f1_misses_as_errors":
                        deriv["misses_as_errors"]["f1"],
                    "derivation_coverage": deriv["coverage"],
                }
                rows.append(row)
                print(f"  lexemes {row['lexemes']}, "
                      f"token coverage {row['token_fst']:.1%}, "
                      f"inflection F1 {row['inflection_f1']:.3f}, "
                      f"derivation F1 {row['derivation_f1']:.3f}")
        print_curve(rows)
        report["curve"] = rows

    if "policy" in stages:
        print("\n=== Ranking policies: default against --derived-first ===")
        print("    Both rank the same analyses, so every disagreement is one")
        print("    reading winning over another, never a reading appearing.")
        counts = corpus_counts(corpus)
        with FstTokenizer(args.analyzer,
                          extra_args=analyzer_args(base, False)) as standard, \
             FstTokenizer(args.analyzer,
                          extra_args=analyzer_args(base, True)) as derived:
            for name, (gold, _stats) in (("Derivation", derivation),
                                         ("Inflection", inflection)):
                result = compare_policies(standard, derived, gold, counts)
                print_policy(name + " gold (in distribution)", result)
                report["policy_" + name.lower()] = result

    if "morpholex" in stages:
        path = expand(args.morpholex)
        print("\n=== MorphoLex, HELD OUT and INDEPENDENT ===")
        print("    Non-commercial licence: evaluation only, never used to")
        print("    build a lexicon.")
        if not os.path.isfile(path):
            print(f"    skipped: no file at {path}")
        else:
            try:
                gold, stats = read_morpholex(path)
            except Exception as exc:                 # noqa: BLE001
                print(f"    skipped: could not read the workbook: {exc}")
                gold, stats = None, None
            if gold:
                with FstTokenizer(
                        args.analyzer,
                        extra_args=analyzer_args(base, args.derived_first)) as tok:
                    result = score_gold(tok, gold, gold_limit)
                    print_gold("MorphoLex (held out)", result, stats)
                    print_mismatches(result)
                    report["morpholex"] = result
                    report["morpholex_stats"] = dict(stats)
            elif stats is not None:
                print(f"    skipped: no usable rows ({dict(stats)})")

    if args.json:
        out = expand(args.json)
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        with open(out, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2, sort_keys=True)
        print(f"\nwrote {out}")

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AnalyzerError as exc:
        sys.exit(f"error: {exc}")
    except KeyboardInterrupt:
        sys.exit(130)
