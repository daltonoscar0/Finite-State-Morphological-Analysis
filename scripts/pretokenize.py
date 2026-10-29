"""Stage 1 of the matched-vocabulary experiment: build the two training streams.

Both conditions read the same corpus and apply the same word segmentation,
`tools/pretokenize.py`'s three character classes. They differ in one thing: in
the `analyzer` condition an alphabetic word the analyzer decomposes is replaced
by its morphs.

    none      The cats walked.  ->  _The _cats _walked _.
    analyzer  The cats walked.  ->  _The _cat s _walk ed _.

`_` above stands for the word-initial marker U+2581. Every unit that begins a
word carries it; a morph that continues a word does not. Two things follow, and
they are the whole point of the convention:

  * Morph boundaries are hard. The subword trainers are given a whitespace
    pre-tokenizer, so no piece they learn can span a space, and every morph
    boundary is a space. A piece can never straddle one.
  * The original text is recoverable. Drop the spaces, turn each marker back
    into a word boundary, and the stream reconstructs the pretokenized text
    exactly, which is what makes bits per character comparable across all
    twelve tokenizers in stage 4.

The marker must not occur in the corpus or the convention is ambiguous. That is
checked here over train and dev, and recorded in the manifest.

Outputs, all under `~/data/tok/matched/`:

    base_train.txt.gz    the `none` stream, training text
    base_dev.txt.gz      the `none` stream, dev text
    morph_train.txt.gz   the `analyzer` stream, training text
    morph_dev.txt.gz     the `analyzer` stream, dev text
    morphs.tsv.gz        word <TAB> morph morph ..., the decompositions used
    manifest.json        counts, the marker check, the analyzer settings

Nothing is written inside the repository.
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, "tools"))

from fst_tokenizer import FstTokenizer                      # noqa: E402
from pretokenize import UNIT_RE, WORD_RE, existing, open_text  # noqa: E402

MARKER = "▁"

# The analyzer setting this experiment runs. It is the one docs/BASELINES.md
# calls the strongest: the mined 100k lexicon, the closed-class list so that
# function words have a stored reading, and the ranking policy that actually
# attempts a decomposition.
DEFAULT_LEXICONS = ["data/closed_class.tsv", "~/data/morph/lexicons/en_100k.tsv"]

# A word is replaced by its morphs only when the analyzer reports this gate.
# `lexicon_stem` means the whole word was found as a stored lexeme and no
# decomposition was attempted; `irregular`, `no_analysis` and `passthrough`
# likewise leave nothing to split on. `decomposed` is the analyzer saying it
# took the word apart, which is the confidence condition the design asks for.
CONFIDENT_GATE = "decomposed"

REPORT_EVERY = 1_000_000


def expand(path):
    return os.path.expanduser(path) if path else path


def collect_types(path, alphabet):
    """One pass over a corpus: every alphabetic word type, and the alphabet."""
    find = UNIT_RE.findall
    match = WORD_RE.match
    types = set()
    lines = 0
    with open_text(existing(path)) as source:
        for line in source:
            lines += 1
            alphabet.update(line)
            for unit in find(line):
                if match(unit):
                    types.add(unit)
            if lines % REPORT_EVERY == 0:
                print("  %s: %d lines" % (os.path.basename(path), lines),
                      file=sys.stderr)
    return types, lines


def analyze(types, analyzer, lexicons, derived_first, batch=20000):
    """Return {word: [morph, ...]} for the words the analyzer decomposes.

    Only decompositions that reconstruct the word exactly are kept. The
    analyzer's pieces are substrings of its input, so this never fires, but a
    stream that does not round-trip would corrupt every metric downstream and
    it costs one comparison per word to be sure.
    """
    extra = []
    for lexicon in lexicons:
        extra += ["--lexicon", expand(lexicon)]
    if derived_first:
        extra.append("--derived-first")

    backend = FstTokenizer(analyzer, extra_args=extra)
    morphs = {}
    gates = {}
    rejected = 0
    ordered = sorted(types)
    started = time.time()
    try:
        for start in range(0, len(ordered), batch):
            chunk = ordered[start:start + batch]
            for word, record in zip(chunk, backend.segment_many(chunk)):
                gate = record["gate"]
                gates[gate] = gates.get(gate, 0) + 1
                if gate != CONFIDENT_GATE:
                    continue
                pieces = record["pieces"]
                if len(pieces) < 2:
                    continue
                if "".join(pieces) != word:
                    rejected += 1
                    continue
                morphs[word] = pieces
            done = min(start + batch, len(ordered))
            print("  analyzed %d/%d types (%.0f/s)"
                  % (done, len(ordered), done / max(time.time() - started, 1e-9)),
                  file=sys.stderr)
    finally:
        backend.close()
    return morphs, gates, rejected


def write_streams(source, base_path, morph_path, morphs):
    """Second pass: write the two streams side by side from one read."""
    find = UNIT_RE.findall
    match = WORD_RE.match
    lines = 0
    base_units = 0
    morph_units = 0
    split_words = 0
    word_tokens = 0

    with open_text(existing(source)) as handle, \
            open_text(base_path, "w") as base_out, \
            open_text(morph_path, "w") as morph_out:
        for line in handle:
            lines += 1
            units = find(line)
            base_units += len(units)

            base_out.write(" ".join(MARKER + unit for unit in units))
            base_out.write("\n")

            pieces = []
            for unit in units:
                if match(unit):
                    word_tokens += 1
                    parts = morphs.get(unit)
                    if parts:
                        split_words += 1
                        pieces.append(MARKER + parts[0])
                        pieces.extend(parts[1:])
                        continue
                pieces.append(MARKER + unit)
            morph_units += len(pieces)
            morph_out.write(" ".join(pieces))
            morph_out.write("\n")

            if lines % REPORT_EVERY == 0:
                print("  %s: %d lines" % (os.path.basename(source), lines),
                      file=sys.stderr)

    return {
        "lines": lines,
        "units": base_units,
        "morph_units": morph_units,
        "word_tokens": word_tokens,
        "word_tokens_split": split_words,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--train", default="~/data/corpora/babylm/train_100M.txt")
    parser.add_argument("--dev", default="~/data/corpora/babylm/dev.txt")
    parser.add_argument("--out-dir", default="~/data/tok/matched")
    parser.add_argument("--analyzer", default="./analyzer")
    parser.add_argument("--lexicon", action="append", default=None,
                        help="repeatable; defaults to the closed-class list "
                             "plus en_100k")
    parser.add_argument("--no-derived-first", action="store_true")
    args = parser.parse_args(argv)

    lexicons = args.lexicon if args.lexicon is not None else list(DEFAULT_LEXICONS)
    derived_first = not args.no_derived_first

    out_dir = expand(args.out_dir)
    os.makedirs(out_dir, exist_ok=True)
    train = expand(args.train)
    dev = expand(args.dev)

    alphabet = set()
    print("pass 1: collecting word types", file=sys.stderr)
    train_types, train_lines = collect_types(train, alphabet)
    dev_types, dev_lines = collect_types(dev, alphabet)
    alphabet.discard("\n")
    types = train_types | dev_types
    print("  %d train types, %d dev types, %d together"
          % (len(train_types), len(dev_types), len(types)), file=sys.stderr)

    if MARKER in alphabet:
        sys.exit("the marker %r occurs in the corpus; pick another one"
                 % MARKER)
    print("  marker %r absent from the %d-character alphabet"
          % (MARKER, len(alphabet)), file=sys.stderr)

    print("analyzing %d types" % len(types), file=sys.stderr)
    morphs, gates, rejected = analyze(types, args.analyzer, lexicons,
                                      derived_first)
    print("  %d types decomposed, %d rejected for not reconstructing"
          % (len(morphs), rejected), file=sys.stderr)

    table = os.path.join(out_dir, "morphs.tsv.gz")
    with open_text(table, "w") as out:
        for word in sorted(morphs):
            out.write("%s\t%s\n" % (word, " ".join(morphs[word])))

    print("pass 2: writing the streams", file=sys.stderr)
    train_stats = write_streams(train,
                                os.path.join(out_dir, "base_train.txt.gz"),
                                os.path.join(out_dir, "morph_train.txt.gz"),
                                morphs)
    dev_stats = write_streams(dev,
                              os.path.join(out_dir, "base_dev.txt.gz"),
                              os.path.join(out_dir, "morph_dev.txt.gz"),
                              morphs)

    manifest = {
        "marker": MARKER,
        "marker_codepoint": "U+%04X" % ord(MARKER),
        "marker_in_corpus": False,
        "alphabet_size": len(alphabet),
        "analyzer": {
            "binary": args.analyzer,
            "lexicons": lexicons,
            "derived_first": derived_first,
            "confident_gate": CONFIDENT_GATE,
        },
        "types": {
            "train": len(train_types),
            "dev": len(dev_types),
            "together": len(types),
            "decomposed": len(morphs),
            "rejected_no_roundtrip": rejected,
            "gates": gates,
        },
        "train": train_stats,
        "dev": dev_stats,
    }
    path = os.path.join(out_dir, "manifest.json")
    with open(path, "w", encoding="utf-8") as out:
        json.dump(manifest, out, indent=2, sort_keys=True)
        out.write("\n")
    print(json.dumps(manifest["types"], indent=2, sort_keys=True))
    print("wrote %s" % path, file=sys.stderr)


if __name__ == "__main__":
    main()
