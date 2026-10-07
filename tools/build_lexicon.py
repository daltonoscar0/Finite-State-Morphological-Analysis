#!/usr/bin/env python3
"""Build an analyzer lexicon from a Wiktionary dump and a frequency table.

Reads `wikt_en.jsonl`, one JSON object per line with `word`, `pos` and
`forms`, plus the `word<TAB>zipf` table that tools/make_freq.py writes, and
emits the TSV format LEXICON/lexicon_loader.h reads:

    stem  POS  StemClass  [FEATURE=form ...]  WEIGHT=N.NN

Standard library only, and it never writes inside the repository: pass an
output path under ~/data/morph.

    python3 tools/build_lexicon.py \\
        --wiktionary ~/data/morph/wiktionary/wikt_en.jsonl \\
        --freq ~/data/morph/freq_en.tsv \\
        --out ~/data/morph/lexicons/en_100k.tsv --limit 100000

    python3 tools/build_lexicon.py \\
        --wiktionary ~/data/morph/wiktionary/wikt_en.jsonl \\
        --freq ~/data/morph/freq_en.tsv \\
        --out-dir ~/data/morph/lexicons --sizes 5000,20000,50000,100000,all

How a stem class is chosen
--------------------------
Every candidate class is a claim about how suffixes attach, and the dump lists
the forms, so the claim is testable: generate the forms each permitted class
predicts, and keep the class that reproduces the most of the listed ones.
Whatever it still fails to reproduce is written out as an explicit
`FEATURE=form`, which the rules treat as blocking the productive rule for that
feature.

So `break` comes out as `REG` with `PAST=broke PAST_PART=broken`: regular in
its present forms, which is why "breaks" and "breaking" decompose, and stored
in its past, which is why "breaked" does not analyze. `rid` comes out as
`DOUBLE_CONS` with `PAST=rid PAST_PART=rid`, so "ridding" decomposes and
"riding" stays the participle of `ride`.

Generated entries therefore never use the `IRR_*` stem classes. Those say
which forms are stored without saying how the rest are spelled, and losing the
orthography that way is what made the analyzer unable to read back its own
output before this work. See docs/LEXICON.md.

A `pos` with no usable forms listed gets stem class `UNKNOWN`, which no
inflectional rule matches: an uncountable noun, a non-gradable adjective, an
adverb.
"""

import argparse
import json
import os
import re
import sys
from collections import Counter, defaultdict

WORD_RE = re.compile(r"^[a-z]+$")

# Wiktionary's part of speech to the loader's POS column
POS_COLUMN = {
    "noun": "NOUN",
    "verb": "VERB",
    "adj": "ADJECTIVE",
    "adv": "ADVERB",
}

# The canonical tag set for each inflection, as the dump writes it.
#
# A form counts as that inflection only when its tag set is exactly this, so
# every variant is dropped automatically: an alternative, obsolete, dialectal,
# archaic or misspelled form always carries an extra tag, and so does a
# regional or rare one. That is stricter than dropping a fixed list of tags,
# and it is stricter in the safe direction, since the cost of missing a rare
# plural is an uninflected entry rather than a wrong rule.
CANONICAL_TAGS = {
    "noun": {
        "PLURAL": ("plural",),
    },
    "verb": {
        "THIRD_SG": ("present", "singular", "third-person"),
        "PRES_PART": ("participle", "present"),
        "PAST": ("past",),
        "PAST_PART": ("participle", "past"),
    },
    "adj": {
        "COMP": ("comparative",),
        "SUP": ("superlative",),
    },
    # Adverbs are uninflected here. The dump does list a comparative for a few
    # ("sooner"), and the rules have no adverb paradigm to put them in.
    "adv": {},
}

VOWELS = frozenset("aeiou")


# ── The orthography, mirroring RULES/orthography.h and form_generator.h ──────
#
# These five predicates and the prediction tables below have to agree with the
# C++ rules, or the lexicon will claim a class whose forms the analyzer cannot
# reproduce. The regeneration self-check at the end of this file is what keeps
# them honest here; `./analyzer --selfcheck --lexicon FILE` is the check
# against the real rules.

def takes_es(stem):
    """Does stem + -s surface as "es"? Sibilants, and verb stems in -o."""
    if not stem:
        return False
    return stem[-1] in "szxo" or stem[-2:] in ("ch", "sh")


def has_silent_e(stem):
    """Does the stem end in a consonant plus a silent e, as in "choose"?"""
    return len(stem) >= 3 and stem[-1] == "e" and stem[-2] not in VOWELS


def has_consonant_y(stem):
    """Does the stem end in a consonant plus y, as in "carry"?"""
    return len(stem) >= 2 and stem[-1] == "y" and stem[-2] not in VOWELS


def may_double_final_consonant(stem):
    """Is the stem shaped consonant, vowel, consonant, as in "put"?"""
    if len(stem) < 3:
        return False
    last, vowel, before = stem[-1], stem[-2], stem[-3]
    if last in VOWELS or last in "ywx":
        return False
    return vowel in VOWELS and before not in VOWELS


def predictions(stem, pos, stem_class):
    """The forms `stem_class` predicts for `stem`, as feature to form."""
    double = stem + stem[-1] if stem else stem
    cut = stem[:-1]

    if pos == "noun":
        if stem_class == "SIBILANT":
            return {"PLURAL": stem + "es"}
        if stem_class == "Y_FINAL":
            return {"PLURAL": cut + "ies"}
        return {"PLURAL": stem + "s"}

    if pos == "verb":
        if stem_class == "SIBILANT":
            return {"THIRD_SG": stem + "es", "PRES_PART": stem + "ing",
                    "PAST": stem + "ed", "PAST_PART": stem + "ed"}
        if stem_class == "Y_FINAL":
            return {"THIRD_SG": cut + "ies", "PRES_PART": stem + "ing",
                    "PAST": cut + "ied", "PAST_PART": cut + "ied"}
        if stem_class == "DOUBLE_CONS":
            return {"THIRD_SG": stem + "s", "PRES_PART": double + "ing",
                    "PAST": double + "ed", "PAST_PART": double + "ed"}
        if stem_class == "SILENT_E":
            # stem + "d" and cut + "ed" are the same string for a stem in -e
            return {"THIRD_SG": stem + "s", "PRES_PART": cut + "ing",
                    "PAST": stem + "d", "PAST_PART": stem + "d"}
        return {"THIRD_SG": stem + "s", "PRES_PART": stem + "ing",
                "PAST": stem + "ed", "PAST_PART": stem + "ed"}

    if pos == "adj":
        if stem_class == "Y_FINAL":
            return {"COMP": cut + "ier", "SUP": cut + "iest"}
        if stem_class == "DOUBLE_CONS":
            return {"COMP": double + "er", "SUP": double + "est"}
        if stem_class == "SILENT_E":
            return {"COMP": cut + "er", "SUP": cut + "est"}
        return {"COMP": stem + "er", "SUP": stem + "est"}

    return {}


def candidate_classes(stem, pos):
    """The classes whose shape this spelling permits, best candidate first.

    A class is only offered when the spelling could support it: Y_FINAL needs a
    consonant plus y, SILENT_E needs a consonant plus e. REG is always last and
    always offered, so there is always an answer.
    """
    if pos == "adv":
        return []

    classes = []
    if pos in ("noun", "verb") and takes_es(stem):
        classes.append("SIBILANT")
    if has_consonant_y(stem):
        classes.append("Y_FINAL")
    # A noun plural never deletes a silent e or doubles a consonant: "house"
    # gives "houses" and "bus" gives "buses", both of which REG and SIBILANT
    # already predict. Offering SILENT_E to a noun would label a third of them
    # with a class that says nothing about them.
    if pos in ("verb", "adj") and has_silent_e(stem):
        classes.append("SILENT_E")
    if pos in ("verb", "adj") and may_double_final_consonant(stem):
        classes.append("DOUBLE_CONS")
    classes.append("REG")
    return classes


# ── Reading the dump ────────────────────────────────────────────────────────

def read_freq(path):
    """Read word<TAB>zipf into a dict, ignoring comment lines."""
    freq = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line or line[0] == "#":
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 2:
                continue
            try:
                freq[parts[0]] = float(parts[1])
            except ValueError:
                continue
    return freq


def read_wiktionary(path, freq):
    """Group the dump by (word, pos), keeping only canonical inflections.

    The same (word, pos) appears on one line per etymology, so the lines are
    merged. A feature listed more than once across those lines keeps every
    candidate; choose_forms picks between them.

    Returns (groups, stats) where groups maps (word, pos) to a dict of feature
    to a list of candidate surface forms.
    """
    groups = defaultdict(lambda: defaultdict(list))
    stats = Counter()

    with open(path, encoding="utf-8") as handle:
        for line in handle:
            stats["lines"] += 1
            try:
                entry = json.loads(line)
            except json.JSONDecodeError:
                stats["unparsable_lines"] += 1
                continue

            word = entry.get("word", "")
            pos = entry.get("pos", "")
            if pos not in POS_COLUMN:
                stats["skipped_pos"] += 1
                continue
            if not WORD_RE.match(word):
                stats["skipped_word_not_lowercase_az"] += 1
                continue

            wanted = CANONICAL_TAGS[pos]
            by_tags = {tags: feature for feature, tags in wanted.items()}

            bucket = groups[(word, pos)]
            for form in entry.get("forms", []):
                surface = form.get("form", "")
                tags = tuple(sorted(form.get("tags", []) or ()))
                feature = by_tags.get(tags)
                if feature is None:
                    stats["forms_not_canonical"] += 1
                    continue
                if not WORD_RE.match(surface):
                    stats["forms_not_lowercase_az"] += 1
                    continue
                stats["forms_kept"] += 1
                if surface not in bucket[feature]:
                    bucket[feature].append(surface)

    stats["groups"] = len(groups)
    return groups, stats


def find_form_of_headwords(groups):
    """Headwords that are only an inflection of some other lemma.

    The dump gives "years", "things", "men" and "comes" their own noun entries,
    because Wiktionary documents inflected forms as headwords. Loading those as
    lexemes teaches the analyzer that "years" is a word in its own right, and a
    stored whole word outranks a decomposition, so exactly the words a
    tokenizer most needs to split would stop splitting.

    A headword is treated as one of these when two things hold: some other
    lemma lists it as an inflection, and it lists no inflection of its own. The
    second condition is what makes this safe. "saw" is the past of "see" and
    also a noun with its own plural "saws", so it stays; "arms" is the plural
    of "arm" and has no plural of its own, so it goes, and "arms" still
    analyzes, as arm plus -s.

    Matching is across parts of speech, so the noun "comes" is recognised as
    the third person singular of the verb "come". A form that is its own
    inflection does not count, or every zero-plural noun ("sheep", "series")
    would be dropped.

    @param groups  (word, pos) to feature to candidate forms
    @return        The set of (word, pos) keys to drop
    """
    listed_by = defaultdict(set)
    for (word, _pos), candidates in groups.items():
        for forms in candidates.values():
            for form in forms:
                if form != word:
                    listed_by[form].add(word)

    drop = set()
    for key, candidates in groups.items():
        word, _pos = key
        if any(candidates.values()):
            continue
        if listed_by.get(word):
            drop.add(key)
    return drop


def choose_forms(candidates, stem, pos, freq):
    """Pick one surface form per feature, the most frequent candidate.

    A feature often has more than one candidate, because the same (word, pos)
    appears once per etymology and the lines are merged. The dump carries two
    verbs spelled "break": the ordinary one, with "broke" and "broken", and a
    second one with the regularized "breaked". Merging gives PAST two
    candidates and the lexicon has one slot.

    Frequency decides, which is how ties are broken everywhere else in this
    build. It is also the signal that separates the two cases cleanly: a
    regularized variant of an irregular verb is vanishingly rare, so "broke"
    wins over "breaked" by four orders of magnitude. Choosing the candidate
    some class predicts instead, to keep the entry regular, gets this exactly
    backwards and quietly discards every irregular past the dump also
    regularizes somewhere.

    Being predicted by a permitted class only breaks a tie between candidates
    of equal frequency, where keeping the entry regular is the safer guess.

    The rejected candidates are returned so the report can count the forms the
    format cannot hold: "index" lists both "indexes" and "indices", and only
    one of them fits.
    """
    predicted = set()
    for stem_class in candidate_classes(stem, pos):
        predicted.update(predictions(stem, pos, stem_class).values())

    chosen, dropped = {}, []
    for feature, forms in candidates.items():
        if not forms:
            continue
        best = max(forms, key=lambda f: (freq.get(f, 0.0), f in predicted,
                                         -len(f), f))
        chosen[feature] = best
        dropped.extend(f for f in forms if f != best)
    return chosen, dropped


def build_entry(word, pos, forms, freq):
    """Decide the stem class and the explicit forms for one (word, pos).

    Returns (stem_class, explicit) where explicit maps feature to surface form
    for every listed form the class does not predict.
    """
    if not forms:
        # Nothing to go on. UNKNOWN matches no inflectional rule, which is the
        # honest reading of an uncountable noun or a non-gradable adjective.
        return ("REG" if pos == "adv" else "UNKNOWN"), {}

    best_class, best_predicted = None, None
    best_hits = -1
    for stem_class in candidate_classes(word, pos):
        predicted = predictions(word, pos, stem_class)
        hits = sum(1 for feature, form in forms.items()
                   if predicted.get(feature) == form)
        if hits > best_hits:
            best_class, best_predicted, best_hits = stem_class, predicted, hits

    explicit = {feature: form for feature, form in forms.items()
                if best_predicted.get(feature) != form}
    return best_class, explicit


def weight_for(zipf):
    """Map a Zipf frequency onto the analyzer's weight scale.

    An entry with no WEIGHT has weight 1.0 and a derivational reading is
    multiplied by 0.9, so 1.0 means an ordinary word. Zipf 4 maps to exactly
    1.0 and the range is clamped to [0.70, 1.30], deliberately narrow:
    frequency breaks ties, it does not override the morphology. See
    docs/LEXICON.md.
    """
    return round(min(1.30, max(0.70, 0.70 + 0.075 * zipf)), 2)


# ── Self-check ──────────────────────────────────────────────────────────────

def regenerate(word, pos, stem_class, explicit):
    """Every surface form the entry claims, as the analyzer would derive them.

    The productive rule for a feature the entry stores explicitly is blocked,
    exactly as in RULES/form_generator.h, so a stored form replaces rather than
    joins the predicted one.
    """
    produced = {}
    if stem_class != "UNKNOWN":
        for feature, form in predictions(word, pos, stem_class).items():
            produced[feature] = form
    produced.update(explicit)
    return produced


def self_check(entries, stats):
    """Regenerate every entry and count the listed forms it fails to produce.

    By construction the class was chosen to maximise matches and the remainder
    was stored, so a non-zero mismatch here is a bug in this file rather than a
    property of the data. It is checked anyway: the prediction tables above
    duplicate the C++ orthography, and a duplicate that drifts is exactly the
    kind of thing a build should notice.
    """
    for word, pos, stem_class, explicit, _weight, listed in entries:
        produced = regenerate(word, pos, stem_class, explicit)
        for feature, form in listed.items():
            stats["check_forms"] += 1
            if produced.get(feature) == form:
                stats["check_reproduced"] += 1
            else:
                stats["check_mismatch"] += 1
                if stats["check_mismatch"] <= 20:
                    stats.setdefault("examples", [])
                    print(f"  mismatch: {word} {pos} {stem_class} {feature} "
                          f"listed={form} got={produced.get(feature)}",
                          file=sys.stderr)


# ── Output ──────────────────────────────────────────────────────────────────

FEATURE_ORDER = ["PLURAL", "THIRD_SG", "PRES_PART", "PAST", "PAST_PART",
                 "COMP", "SUP"]


def write_lexicon(path, entries, provenance):
    """Write the TSV, one line per entry, in the order given."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as out:
        for line in provenance:
            out.write(f"# {line}\n")
        out.write("# Columns: stem  POS  StemClass  [FEATURE=form ...]  WEIGHT\n")
        for word, pos, stem_class, explicit, weight, _listed in entries:
            fields = [word, POS_COLUMN[pos], stem_class]
            for feature in FEATURE_ORDER:
                if feature in explicit:
                    fields.append(f"{feature}={explicit[feature]}")
            fields.append(f"WEIGHT={weight}")
            out.write("\t".join(fields) + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Build an analyzer lexicon from a Wiktionary dump")
    parser.add_argument("--wiktionary", required=True, metavar="FILE",
                        help="wikt_en.jsonl dump")
    parser.add_argument("--freq", required=True, metavar="FILE",
                        help="frequency table from tools/make_freq.py")
    parser.add_argument("--out", metavar="PATH",
                        help="write one lexicon here")
    parser.add_argument("--limit", type=int, metavar="N",
                        help="keep only the N most frequent words, for --out")
    parser.add_argument("--out-dir", metavar="DIR",
                        help="write one lexicon per --sizes entry here")
    parser.add_argument("--sizes", default="5000,20000,50000,100000,all",
                        help="word counts for --out-dir, comma separated; "
                             "'all' means every entry")
    parser.add_argument("--keep-form-of", action="store_true",
                        help="keep headwords that are only an inflection of "
                             "another lemma (\"years\", \"men\"); they are "
                             "dropped by default, see "
                             "find_form_of_headwords")
    args = parser.parse_args()

    if not args.out and not args.out_dir:
        parser.error("give --out or --out-dir")
    for path in (args.out, args.out_dir):
        if path and ".git" in os.path.abspath(path).split(os.sep):
            parser.error("refusing to write inside a repository")

    freq = read_freq(args.freq)
    print(f"frequency rows: {len(freq)}", file=sys.stderr)

    groups, stats = read_wiktionary(args.wiktionary, freq)
    print(f"lines read: {stats['lines']}, (word, pos) groups: {stats['groups']}",
          file=sys.stderr)

    # Build every entry, then rank by the frequency of its headword. Ranking is
    # by word, not by entry, so a word that is both a noun and a verb brings
    # both entries in together and the size of a lexicon is a vocabulary size.
    form_of = set() if args.keep_form_of else find_form_of_headwords(groups)
    stats["headwords_that_are_only_an_inflection"] = len(form_of)

    entries = []
    classes = Counter()
    per_pos = Counter()
    for (word, pos), candidates in groups.items():
        if (word, pos) in form_of:
            continue
        forms, dropped = choose_forms(candidates, word, pos, freq)
        stem_class, explicit = build_entry(word, pos, forms, freq)
        zipf = freq.get(word, 0.0)
        entries.append((word, pos, stem_class, explicit, weight_for(zipf),
                        forms))
        classes[f"{POS_COLUMN[pos]} {stem_class}"] += 1
        per_pos[POS_COLUMN[pos]] += 1
        if explicit:
            stats["entries_with_explicit_forms"] += 1
        if not forms:
            stats["entries_with_no_forms_listed"] += 1
        stats["forms_unexpressible"] += len(dropped)

    # Rank words by frequency, most frequent first, alphabetically within a
    # frequency so the output is deterministic.
    word_rank = {}
    for word in {word for word, _pos, _c, _e, _w, _f in entries}:
        word_rank[word] = (-freq.get(word, 0.0), word)
    entries.sort(key=lambda e: (word_rank[e[0]], e[1]))
    ranked_words = sorted(word_rank, key=lambda w: word_rank[w])

    print(f"entries: {len(entries)}, words: {len(ranked_words)}",
          file=sys.stderr)
    self_check(entries, stats)

    print("\nby part of speech:", file=sys.stderr)
    for pos, count in per_pos.most_common():
        print(f"  {pos:<10} {count}", file=sys.stderr)
    print("\nby stem class:", file=sys.stderr)
    for name, count in classes.most_common():
        share = 100.0 * count / len(entries)
        print(f"  {name:<22} {count:>7}  {share:5.1f}%", file=sys.stderr)

    checked = stats["check_forms"]
    rate = 100.0 * stats["check_mismatch"] / checked if checked else 0.0
    print("\nself-check: regenerate every entry's forms", file=sys.stderr)
    print(f"  listed forms checked:   {checked}", file=sys.stderr)
    print(f"  reproduced:             {stats['check_reproduced']}",
          file=sys.stderr)
    print(f"  mismatched:             {stats['check_mismatch']} "
          f"({rate:.4f}%)", file=sys.stderr)
    print(f"  forms the format cannot hold: {stats['forms_unexpressible']}"
          f"  (a second plural, a second past)", file=sys.stderr)
    print(f"  entries with explicit forms:  "
          f"{stats['entries_with_explicit_forms']}", file=sys.stderr)
    print(f"  entries with no forms listed: "
          f"{stats['entries_with_no_forms_listed']}", file=sys.stderr)
    print(f"  headwords dropped as inflections of another lemma: "
          f"{stats['headwords_that_are_only_an_inflection']}", file=sys.stderr)

    provenance = [
        "Generated by tools/build_lexicon.py. Do not edit.",
        f"Source: {os.path.basename(args.wiktionary)}, "
        f"frequencies from {os.path.basename(args.freq)}",
        "Stem class is the class that reproduces the most listed forms;",
        "whatever it does not reproduce is written out as FEATURE=form.",
        "UNKNOWN means no inflection was listed and none is claimed.",
    ]

    targets = []
    if args.out:
        targets.append((args.out, args.limit))
    if args.out_dir:
        for size in args.sizes.split(","):
            size = size.strip()
            if not size:
                continue
            if size == "all":
                targets.append((os.path.join(args.out_dir, "en_all.tsv"), None))
            else:
                n = int(size)
                name = f"en_{n // 1000}k.tsv" if n >= 1000 else f"en_{n}.tsv"
                targets.append((os.path.join(args.out_dir, name), n))

    print("", file=sys.stderr)
    for path, limit in targets:
        if limit is None:
            subset = entries
            words = len(ranked_words)
        else:
            keep = set(ranked_words[:limit])
            subset = [e for e in entries if e[0] in keep]
            words = min(limit, len(ranked_words))
        write_lexicon(path, subset, provenance)
        print(f"wrote {len(subset):>7} entries for {words:>7} words to {path}",
              file=sys.stderr)


if __name__ == "__main__":
    main()
