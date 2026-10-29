"""Stage 3 of the matched-vocabulary experiment: the intrinsic metrics.

One row per tokenizer, twelve rows, every number computed the same way:

  * boundary precision, recall and F1 against MorphoLex, MorphyNet inflection
    and MorphyNet derivation, through `tools/score_tokenizer.py`'s own gold
    loader and scorer, so these numbers sit in the same table as every other
    round in docs/BASELINES.md;
  * fertility, pieces per alphabetic word token, on the dev split;
  * compression, bytes per piece, over the whole dev unit stream;
  * boundary recall restricted to compound boundaries, reported separately
    because compounds are the majority of what the analyzer currently misses
    and an aggregate hides them.

A tokenizer here is a pair: the subword model trained in stage 2, and the
morph table from stage 1 if its condition is `analyzer`. Encoding a word means
splitting it into morphs, marking the first, and running the subword model
over each morph on its own, which is exactly what the training stream did.
"""

import argparse
import collections
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, "tools"))

from eval_lexicon import (MORPHOLEX_READER_DEFAULT,            # noqa: E402
                          MORPHOLEX_READERS)
from pretokenize import (UNIT_RE, WORD_RE, existing,           # noqa: E402
                         is_word, open_text)
from score_segmentation import internal_boundaries             # noqa: E402
from score_tokenizer import (                                  # noqa: E402
    CLOSED_CLASS,
    DEFAULT_DERIVATION,
    DEFAULT_INFLECTION,
    DEFAULT_LEXICON_DIR,
    DEFAULT_MORPHOLEX,
    DEFAULT_PREFIX_RULES,
    Tokenizer,
    build_alt_mask,
    bytes_to_unicode,
    expand,
    load_gold,
    score_gold_set,
)
from suffix_report import INFLECTIONAL, PREFIXES, SUFFIXES     # noqa: E402

MARKER = "▁"


class MatchedTokenizer(Tokenizer):
    """A stage-2 subword model, with the stage-1 morph table in front of it.

    `morphs` is None for the `none` condition, in which case a word is one
    unit and this is a plain byte-level subword tokenizer.
    """

    def __init__(self, name, path, morphs, condition):
        Tokenizer.__init__(self, name)
        from tokenizers import Tokenizer as Backend

        self.backend = Backend.from_file(path)
        self.vocab_size = self.backend.get_vocab_size()
        self.morphs = morphs
        self.condition = condition
        self.from_symbol = {symbol: byte
                            for byte, symbol in bytes_to_unicode().items()}

    def units_for(self, word):
        """The whitespace-separated units this word becomes in the stream."""
        parts = self.morphs.get(word) if self.morphs else None
        if not parts:
            return [MARKER + word]
        return [MARKER + parts[0]] + list(parts[1:])

    def encode_many(self, words):
        # Every unit of every word is encoded in one batch, then the pieces
        # are handed back out word by word. A unit is encoded on its own
        # because that is how the trainer saw it: the whitespace
        # pre-tokenizer means no piece can span two units, so encoding them
        # separately and concatenating is the same thing the library would
        # do with the whole line, and it keeps morph boundaries hard here
        # too rather than trusting them to survive.
        spans = []
        flat = []
        for word in words:
            units = self.units_for(word)
            spans.append((len(flat), len(units)))
            flat.extend(units)
        encoded = self.backend.encode_batch(flat, add_special_tokens=False)
        out = []
        for start, count in spans:
            pieces = []
            for encoding in encoded[start:start + count]:
                pieces.extend(encoding.tokens)
            out.append(pieces)
        return out

    def piece_bytes(self, piece, index):
        try:
            return bytes(self.from_symbol[ch] for ch in piece)
        except KeyError:
            # A special token has no byte reading. Nothing here should ever
            # produce one, and returning empty rather than its spelling keeps
            # a stray <unk> from inventing surface text.
            return b""

    def surfaces(self, pieces):
        """As the base class, with the word-initial marker removed.

        The marker is in the stream, so it is in the pieces, but it is not
        text: it stands for the word boundary. It is stripped from the first
        surface that carries it, which leaves the surfaces concatenating to
        the word exactly.
        """
        out = Tokenizer.surfaces(self, pieces)
        for i, text in enumerate(out):
            if not text:
                continue
            if text.startswith(MARKER):
                out[i] = text[len(MARKER):]
            break
        return out

    def library_decode(self, pieces):
        ids = [self.backend.token_to_id(p) for p in pieces]
        if any(i is None for i in ids):
            return None
        text = self.backend.decode(ids)
        return text[len(MARKER):] if text.startswith(MARKER) else text


# ── Compounds ─────────────────────────────────────────────────────────────

def compound_offsets(parses):
    """Boundary offsets of a gold entry that look like a compound seam.

    The test is the one `tools/suffix_report.py` uses to bucket missed
    boundaries: both sides free-standing words of three characters or more,
    the right side not a suffix the rules know, the left side not a prefix
    they know. It reads only the gold, so the same offsets are used for every
    tokenizer. Where a form has several gold parses the offsets are unioned,
    which is what the aggregate scorer effectively allows by taking the best
    parse per form.
    """
    offsets = set()
    for pieces in parses:
        at = 0
        for i in range(len(pieces) - 1):
            at += len(pieces[i])
            before, after = pieces[i], pieces[i + 1]
            if (is_word(before) and is_word(after)
                    and len(before) >= 3 and len(after) >= 3
                    and after not in SUFFIXES + INFLECTIONAL
                    and before not in PREFIXES):
                offsets.add(at)
    return offsets


def compound_recall(system, entry, pieces_by_word):
    """Boundary recall over compound seams only."""
    total = 0
    found = 0
    forms = 0
    for form, parses in entry["forms"].items():
        offsets = compound_offsets(parses)
        if not offsets:
            continue
        pieces = pieces_by_word.get(form)
        if pieces is None:
            continue
        forms += 1
        predicted = set(internal_boundaries(
            [s for s in system.surfaces(pieces) if s]))
        total += len(offsets)
        found += len(offsets & predicted)
    return {"forms": forms, "boundaries": total, "found": found,
            "recall": found / total if total else 0.0}


# ── Dev split counts ──────────────────────────────────────────────────────

def dev_counts(path):
    """Unit and word type counts of the dev split, plus its size in bytes.

    Read from the raw corpus with the shared pretokenizer rather than from a
    stage-1 stream, so the byte count is the text's own and does not include
    the markers and extra spaces one of the two conditions adds.
    """
    find = UNIT_RE.findall
    match = WORD_RE.match
    units = collections.Counter()
    words = collections.Counter()
    raw_bytes = 0
    text_chars = 0
    with open_text(existing(path)) as handle:
        for line in handle:
            raw_bytes += len(line.encode("utf-8"))
            found = find(line)
            units.update(found)
            text_chars += sum(len(u) for u in found)
            for unit in found:
                if match(unit):
                    words[unit] += 1
    return units, words, raw_bytes, text_chars


def compression(system, units, words):
    """Fertility and bytes per piece, weighted by frequency.

    Every unit is encoded once and multiplied by its count. That is
    numerically identical to encoding the whole stream, because each unit is
    encoded independently of its neighbours, and it is two orders of
    magnitude cheaper.
    """
    types = sorted(units)
    pieces_by_unit = {}
    for start in range(0, len(types), 20000):
        chunk = types[start:start + 20000]
        for unit, pieces in zip(chunk, system.encode_many(chunk)):
            pieces_by_unit[unit] = pieces

    word_pieces = 0
    word_tokens = 0
    for word, count in words.items():
        word_pieces += len(pieces_by_unit[word]) * count
        word_tokens += count

    unit_pieces = 0
    unit_bytes = 0
    for unit, count in units.items():
        unit_pieces += len(pieces_by_unit[unit]) * count
        unit_bytes += len(unit.encode("utf-8")) * count

    bad = [u for u in types
           if "".join(system.surfaces(pieces_by_unit[u])) != u]
    return {
        "word_tokens": word_tokens,
        "fertility": word_pieces / word_tokens if word_tokens else 0.0,
        "unit_pieces": unit_pieces,
        "unit_bytes": unit_bytes,
        "bytes_per_piece": unit_bytes / unit_pieces if unit_pieces else 0.0,
        "round_trip_failures": len(bad),
        "round_trip_examples": bad[:10],
    }


# ── Driver ────────────────────────────────────────────────────────────────

def load_morphs(path):
    table = {}
    with open_text(path) as handle:
        for line in handle:
            word, _, rest = line.rstrip("\n").partition("\t")
            table[word] = rest.split(" ")
    return table


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--matched-dir", default="~/data/tok/matched")
    parser.add_argument("--dev", default="~/data/corpora/babylm/dev.txt")
    parser.add_argument("--out", default=None,
                        help="defaults to <matched-dir>/results/intrinsic.json")
    parser.add_argument("--analyzer", default="./analyzer")
    parser.add_argument("--lexicon-dir", default=DEFAULT_LEXICON_DIR)
    parser.add_argument("--morpholex", default=DEFAULT_MORPHOLEX)
    parser.add_argument("--morpholex-reader", default=MORPHOLEX_READER_DEFAULT,
                        choices=list(MORPHOLEX_READERS),
                        help="'prefix' is the fixed reader and gives the "
                             "12,828 forms this round is scored on")
    parser.add_argument("--prefix-rules", default=DEFAULT_PREFIX_RULES)
    parser.add_argument("--inflection", default=DEFAULT_INFLECTION)
    parser.add_argument("--derivation", default=DEFAULT_DERIVATION)
    parser.add_argument("--closed-class", default=CLOSED_CLASS)
    parser.add_argument("--unfiltered-gold", action="store_true")
    parser.add_argument("--only", nargs="*", default=None)
    args = parser.parse_args(argv)

    matched = expand(args.matched_dir)
    index_path = os.path.join(matched, "models", "index.json")
    with open(index_path, encoding="utf-8") as handle:
        index = json.load(handle)

    morphs = load_morphs(os.path.join(matched, "morphs.tsv.gz"))
    print("morph table: %d words" % len(morphs), file=sys.stderr)

    gold = load_gold(args)
    gold_keys = sorted(gold)

    units, words, raw_bytes, text_chars = dev_counts(expand(args.dev))
    print("dev: %d unit types, %d word types, %d bytes"
          % (len(units), len(words), raw_bytes), file=sys.stderr)

    gold_words = set()
    for entry in gold.values():
        gold_words.update(entry["forms"])
    alt_mask = build_alt_mask(args, gold_words)
    print("alternation mask: %d of %d gold forms"
          % (len(alt_mask), len(gold_words)), file=sys.stderr)

    results = []
    for record in index:
        name = "%s-%s-%dk" % (record["condition"], record["model"],
                              record["nominal_vocab_size"] // 1000)
        if args.only and name not in args.only:
            continue
        print("scoring %s" % name, file=sys.stderr)

        system = MatchedTokenizer(
            name, record["path"],
            morphs if record["condition"] == "analyzer" else None,
            record["condition"])

        row = {
            "name": name,
            "condition": record["condition"],
            "model": record["model"],
            "nominal_vocab_size": record["nominal_vocab_size"],
            "actual_vocab_size": record["actual_vocab_size"],
            "compression": compression(system, units, words),
        }

        ordered = sorted(gold_words)
        pieces_by_word = {}
        for start in range(0, len(ordered), 20000):
            chunk = ordered[start:start + 20000]
            for word, pieces in zip(chunk, system.encode_many(chunk)):
                pieces_by_word[word] = pieces

        row["gold"] = {}
        for key in gold_keys:
            row["gold"][key] = score_gold_set(system, gold[key],
                                              pieces_by_word, alt_mask)
            row["gold"][key].pop("mismatches", None)
        row["compounds"] = compound_recall(system, gold["morpholex"],
                                           pieces_by_word)
        results.append(row)

        morpholex = row["gold"]["morpholex"]["strict"]
        print("  F1 %.4f  fertility %.4f  bytes/piece %.3f  compound R %.4f"
              % (morpholex["f1"], row["compression"]["fertility"],
                 row["compression"]["bytes_per_piece"],
                 row["compounds"]["recall"]), file=sys.stderr)

    out_path = expand(args.out) if args.out else os.path.join(
        matched, "results", "intrinsic.json")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    payload = {
        "dev": {"raw_bytes": raw_bytes, "text_chars": text_chars,
                "unit_types": len(units), "word_types": len(words)},
        "gold_sizes": {key: len(gold[key]["forms"]) for key in gold_keys},
        "rows": results,
    }
    with open(out_path, "w", encoding="utf-8") as out:
        json.dump(payload, out, indent=2, sort_keys=True)
        out.write("\n")
    print("wrote %s" % out_path, file=sys.stderr)


if __name__ == "__main__":
    main()
