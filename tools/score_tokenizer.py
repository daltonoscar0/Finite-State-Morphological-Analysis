#!/usr/bin/env python3
"""Score every tokenizer in the comparison with one scorer, on one word list.

Thirteen systems: BPE, WordPiece and Unigram at 8k, 16k and 32k, and the
analyzer under four lexicon settings. They are reached through one interface,
`encode(word) -> list[str]`, so no metric can be computed a different way for
one system than for another.

    ~/venvs/tok/bin/python tools/score_tokenizer.py

What is reported, on the alphabetic word tokens of `test.txt`:

  compression   pieces per word token weighted and type weighted, characters
                and bytes per piece, and pieces per 1000 bytes of raw
                `test.txt` when the whole pretokenized stream is encoded
  round trip    the share of words that decode back exactly
  vocabulary    the share of the vocabulary that occurs on test, and the
                share of word tokens emitted whole
  alignment     boundary precision, recall and F1 and exact match against
                MorphoLex (independent) and MorphyNet inflection and
                derivation (in distribution), strict and alternation tolerant
  analyzer      coverage and the share of tokens in each gate

Pieces and surfaces
-------------------
A piece is a vocabulary entry. Its *surface* is the text it contributes to the
word: `##iness` contributes `iness`, sentencepiece's `_cit` contributes `cit`,
and a byte fallback piece contributes part of a character. Surfaces are what
every metric is computed on, so a marker never counts as a character and never
moves a boundary. Surfaces always concatenate back to the word, which is what
the round trip check verifies.

Standard library, plus `tokenizers` and `sentencepiece` for the baselines. The
gold standards are read by `tools/eval_lexicon.py`, which already converts all
three to surface boundaries, and the boundary arithmetic is
`tools/score_segmentation.py`.
"""

import argparse
import collections
import contextlib
import json
import os
import re
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from eval_lexicon import (read_derivation_gold, read_inflection_gold,  # noqa: E402
                          read_morpholex)
from fst_tokenizer import FstTokenizer  # noqa: E402
from pretokenize import existing, open_text  # noqa: E402
from score_segmentation import (internal_boundaries, match_boundaries,  # noqa: E402
                                prf)

BYTE_PIECE_RE = re.compile(r"\A<0x([0-9A-Fa-f]{2})>\Z")
SPM_MARKER = "▁"

DEFAULT_SPLITS = "~/data/tok/splits"
DEFAULT_MODELS = "~/data/tok/models"
DEFAULT_RESULTS = "~/data/tok/results"
DEFAULT_LEXICON_DIR = "~/data/morph/lexicons"
DEFAULT_MORPHOLEX = "~/data/morph/morpholex/MorphoLex-en/MorphoLEX_en.xlsx"
DEFAULT_INFLECTION = "~/data/morph/morphynet/eng.inflectional.v1.tsv"
DEFAULT_DERIVATION = "~/data/morph/morphynet/eng.derivational.v1.tsv"

BASELINE_ALGOS = ["bpe", "wordpiece", "unigram"]
BASELINE_SIZES = [8000, 16000, 32000]

# The four analyzer settings, as (name, lexicon file or None, derived_first).
# "built-in lexicon only" means no --lexicon at all: the 1,825 lexemes the
# analyzer compiles in. data/closed_class.tsv is deliberately not loaded in
# any of the four, see the decisions in docs/BASELINES.md.
ANALYZER_SETTINGS = [
    ("analyzer-builtin", None, False),
    ("analyzer-en_100k", "en_100k.tsv", False),
    ("analyzer-en_100k-derived", "en_100k.tsv", True),
    ("analyzer-en_all-derived", "en_all.tsv", True),
]

GATES = ["decomposed", "lexicon_stem", "irregular", "no_analysis",
         "passthrough"]


def expand(path):
    return os.path.expanduser(path) if path else path


# ── The byte level alphabet ───────────────────────────────────────────────

def bytes_to_unicode():
    """The GPT-2 byte to printable character map that `tokenizers` uses.

    A byte level pre-tokenizer rewrites each input byte as one printable
    character so that the model never sees whitespace or a control code. The
    library exposes the resulting alphabet but not the mapping, and the
    mapping is what turns a WordPiece piece back into bytes, so it is rebuilt
    here and checked against the library's alphabet at load time.
    """
    printable = (list(range(ord("!"), ord("~") + 1)) +
                 list(range(ord("¡"), ord("¬") + 1)) +
                 list(range(ord("®"), ord("ÿ") + 1)))
    mapped = list(printable)
    spare = 0
    for byte in range(256):
        if byte not in printable:
            printable.append(byte)
            mapped.append(256 + spare)
            spare += 1
    return {byte: chr(code) for byte, code in zip(printable, mapped)}


# ── The common interface ──────────────────────────────────────────────────

class Tokenizer:
    """One system under test.

    Subclasses provide `encode_many` and `piece_bytes`. Everything else,
    including surfaces and decoding, follows from those two.
    """

    kind = "baseline"

    def __init__(self, name):
        self.name = name
        self.vocab_size = None

    def encode(self, word):
        """The pieces of one word. This is the interface the brief names."""
        return self.encode_many([word])[0]

    def encode_many(self, words):
        raise NotImplementedError

    def piece_bytes(self, piece, index):
        """The bytes this piece contributes to the word."""
        raise NotImplementedError

    def surfaces(self, pieces):
        """The text each piece contributes, in order.

        A piece that carries only part of a character, which is what byte
        fallback produces, contributes nothing until the character is
        complete, and the completed character is credited to the piece that
        finished it. So the surfaces always concatenate to the decoded word
        and there is never a boundary inside a character.
        """
        out = []
        pending = b""
        for index, piece in enumerate(pieces):
            pending += self.piece_bytes(piece, index)
            try:
                text = pending.decode("utf-8")
            except UnicodeDecodeError:
                out.append("")
                continue
            out.append(text)
            pending = b""
        if pending:
            # Not valid UTF-8 even at the end of the word. Hand back what is
            # there so the round trip check can report the word rather than
            # raise.
            out[-1] = out[-1] + pending.decode("utf-8", "replace")
        return out

    def decode(self, pieces):
        return "".join(self.surfaces(pieces))

    def library_decode(self, pieces):
        """What the library's own decoder makes of the same pieces.

        `decode` above is this file's arithmetic, and every metric rests on
        it. This is the independent second opinion it is checked against on a
        sample of words, so a mistake in the surface rules cannot quietly
        flatter a system.
        """
        return "".join(pieces)

    def close(self):
        pass


class HuggingFaceTokenizer(Tokenizer):
    """BPE or WordPiece from the `tokenizers` library."""

    def __init__(self, name, path, byte_level):
        Tokenizer.__init__(self, name)
        from tokenizers import Tokenizer as Backend
        from tokenizers import pre_tokenizers

        self.backend = Backend.from_file(path)
        self.vocab_size = self.backend.get_vocab_size()
        self.vocabulary = set(self.backend.get_vocab())
        self.byte_level = byte_level
        if byte_level:
            table = bytes_to_unicode()
            alphabet = set(pre_tokenizers.ByteLevel.alphabet())
            if set(table.values()) != alphabet:
                raise RuntimeError("the byte level alphabet does not match "
                                   "the rebuilt mapping")
            self.from_symbol = {symbol: byte for byte, symbol in table.items()}

    def encode_many(self, words):
        return [encoding.tokens for encoding
                in self.backend.encode_batch(words, add_special_tokens=False)]

    def library_decode(self, pieces):
        return self.backend.decoder.decode(pieces)

    def piece_bytes(self, piece, index):
        if self.byte_level:
            # Only a piece after the first can be a continuation, so only
            # there does a leading ## mean the marker. At the start of a word
            # it is the text itself: the unit `##` occurs in the test corpus,
            # its vocabulary entry is spelled exactly like the continuation
            # form of `#`, and stripping it there would lose the word. The
            # library's own decoder does strip it, which is why the two
            # disagree on that one string and nowhere else.
            text = piece[2:] if index and piece.startswith("##") else piece
            try:
                return bytes(self.from_symbol[ch] for ch in text)
            except KeyError:
                # An unknown token, or anything else outside the byte level
                # alphabet, has no byte reading.
                return piece.encode("utf-8")
        match = BYTE_PIECE_RE.match(piece)
        if match:
            return bytes([int(match.group(1), 16)])
        return piece.encode("utf-8")


class SentencePieceTokenizer(Tokenizer):
    """Unigram from `sentencepiece`."""

    def __init__(self, name, path):
        Tokenizer.__init__(self, name)
        import sentencepiece as spm

        self.backend = spm.SentencePieceProcessor(model_file=path)
        self.vocab_size = self.backend.get_piece_size()
        self.vocabulary = {self.backend.id_to_piece(i)
                           for i in range(self.vocab_size)}

    def encode_many(self, words):
        return self.backend.encode(words, out_type=str)

    def library_decode(self, pieces):
        return self.backend.decode(pieces)

    def piece_bytes(self, piece, index):
        match = BYTE_PIECE_RE.match(piece)
        if match:
            return bytes([int(match.group(1), 16)])
        # add_dummy_prefix puts a marker in front of the first piece that
        # stands for a space the word never had. A marker anywhere else would
        # be a real space, which a unit of the shared pretokenizer cannot
        # contain.
        if index == 0 and piece.startswith(SPM_MARKER):
            piece = piece[len(SPM_MARKER):]
        return piece.replace(SPM_MARKER, " ").encode("utf-8")


class AnalyzerTokenizer(Tokenizer):
    """The analyzer, through its surface segmentation mode."""

    kind = "analyzer"

    def __init__(self, name, analyzer, lexicon, derived_first):
        Tokenizer.__init__(self, name)
        extra = []
        if lexicon:
            extra += ["--lexicon", lexicon]
        if derived_first:
            extra.append("--derived-first")
        self.backend = FstTokenizer(analyzer, extra_args=extra)
        self.lexicon = lexicon or "built-in"
        self.derived_first = derived_first
        self.records = {}

    def encode_many(self, words):
        records = self.backend.segment_many(words)
        for word, record in zip(words, records):
            self.records[word] = (record["source"], record["gate"],
                                  record["alt"])
        return [record["pieces"] for record in records]

    def piece_bytes(self, piece, index):
        return piece.encode("utf-8")

    def close(self):
        self.backend.close()


# ── Building the systems ──────────────────────────────────────────────────

def build_systems(args):
    """Every system to score, in table order, as (name, factory) pairs.

    Nothing is loaded until it is scored. Holding all thirteen open at once
    would mean nine models in memory and four analyzer processes, one of them
    with the 440k-entry lexicon resident.
    """
    systems = []
    models = expand(args.models)
    for algo in BASELINE_ALGOS:
        for size in BASELINE_SIZES:
            directory = os.path.join(models, "%s_%d" % (algo, size))
            name = "%s-%dk" % (algo, size // 1000)
            if algo == "unigram":
                path = os.path.join(directory, "spm.model")
                systems.append((name, lambda n=name, p=path:
                                SentencePieceTokenizer(n, p)))
            else:
                path = os.path.join(directory, "tokenizer.json")
                byte_level = algo == "wordpiece"
                systems.append((name, lambda n=name, p=path, b=byte_level:
                                HuggingFaceTokenizer(n, p, b)))

    lexicon_dir = expand(args.lexicon_dir)
    for name, lexicon, derived_first in ANALYZER_SETTINGS:
        path = os.path.join(lexicon_dir, lexicon) if lexicon else None
        systems.append((name, lambda n=name, p=path, d=derived_first:
                        AnalyzerTokenizer(n, args.analyzer, p, d)))
    return systems


# ── Inputs ────────────────────────────────────────────────────────────────

def read_counts(path):
    """Read a `item<TAB>count` table into a list of pairs, order preserved."""
    rows = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            item, _, count = line.rstrip("\n").partition("\t")
            rows.append((item, int(count)))
    return rows


@contextlib.contextmanager
def readable(path):
    """Yield a path the gold readers can open with a plain `open`.

    tools/eval_lexicon.py reads its gold files by path, and one of them has
    since been compressed on disk by the machine's housekeeping. Rather than
    reimplement a reader that already works, the compressed file is unpacked
    to a temporary copy for the length of the read.
    """
    found = existing(path)
    if not found.endswith(".gz"):
        yield found
        return
    handle = tempfile.NamedTemporaryFile(suffix=".tsv", delete=False)
    try:
        with open_text(found) as source:
            shutil.copyfileobj(source, open(handle.name, "w",
                                            encoding="utf-8"))
        yield handle.name
    finally:
        handle.close()
        os.unlink(handle.name)


def load_gold(args):
    """Read the three gold standards as surface pieces.

    All three conversions come from tools/eval_lexicon.py unchanged.
    """
    gold = {}
    morpholex = expand(args.morpholex)
    if os.path.exists(morpholex):
        forms, stats = read_morpholex(morpholex)
        gold["morpholex"] = {"forms": forms, "stats": dict(stats),
                             "independent": True}
    for key, path, reader in (
            ("morphynet-inflection", args.inflection, read_inflection_gold),
            ("morphynet-derivation", args.derivation, read_derivation_gold)):
        full = expand(path)
        if not os.path.exists(existing(full)):
            print("gold %s: not found at %s" % (key, full), file=sys.stderr)
            continue
        with readable(full) as usable:
            forms, stats = reader(usable)
        gold[key] = {"forms": dict(forms), "stats": dict(stats),
                     "independent": False}
    return gold


# ── Metrics ───────────────────────────────────────────────────────────────

def score_system(system, word_rows, unit_rows, gold, alt_mask, raw_bytes,
                 batch=20000, examples_for=None):
    """Every metric for one system."""
    started = time.time()

    unit_words = [unit for unit, _ in unit_rows]
    word_set = {word for word, _ in word_rows}
    gold_words = set()
    for entry in gold.values():
        gold_words.update(entry["forms"])

    # One encoding pass over every string any metric needs.
    todo = sorted(set(unit_words) | gold_words)
    pieces_by_word = {}
    for start in range(0, len(todo), batch):
        chunk = todo[start:start + batch]
        for word, pieces in zip(chunk, system.encode_many(chunk)):
            pieces_by_word[word] = pieces

    result = {"name": system.name, "kind": system.kind,
              "vocab_size": system.vocab_size}

    # ── Round trip ──
    #
    # Over every string encoded, and again over the test word tokens alone,
    # weighted by how often each word occurs. The table reports the second,
    # which is the share of words in the running text that come back exactly.
    offenders = []
    strings_ok = 0
    for word in todo:
        decoded = system.decode(pieces_by_word[word])
        if decoded == word:
            strings_ok += 1
        elif len(offenders) < 20:
            offenders.append({"word": word,
                              "pieces": pieces_by_word[word],
                              "decoded": decoded})
    token_ok = 0
    type_ok = 0
    for word, count in word_rows:
        if system.decode(pieces_by_word[word]) == word:
            token_ok += count
            type_ok += 1
    word_token_total = sum(count for _, count in word_rows)
    result["round_trip"] = {
        "strings": len(todo),
        "strings_exact": strings_ok,
        "strings_rate": strings_ok / len(todo) if todo else 0.0,
        "word_tokens": word_token_total,
        "word_tokens_exact": token_ok,
        "word_types_exact": type_ok,
        "rate": token_ok / word_token_total if word_token_total else 0.0,
        "offenders": offenders,
    }

    # The library's own decoder over the same pieces, as a check on the
    # surface arithmetic above rather than as a metric of its own. A
    # disagreement is reported, not treated as a failure: see the note in
    # HuggingFaceTokenizer.piece_bytes for the one case where this scorer is
    # deliberately the more careful of the two.
    disagreements = []
    for word in todo:
        if system.library_decode(pieces_by_word[word]) != word:
            if len(disagreements) < 20:
                disagreements.append(word)
    result["round_trip"]["library_decode_checked"] = len(todo)
    result["round_trip"]["library_decode_disagreed"] = len(disagreements)
    result["round_trip"]["library_decode_examples"] = disagreements

    # ── Compression on the alphabetic word tokens ──
    tokens = 0
    token_pieces = 0
    token_chars = 0
    token_bytes = 0
    type_pieces = 0
    single_piece_tokens = 0
    for word, count in word_rows:
        pieces = pieces_by_word[word]
        n = len(pieces)
        tokens += count
        token_pieces += count * n
        token_chars += count * len(word)
        token_bytes += count * len(word.encode("utf-8"))
        type_pieces += n
        if n == 1:
            single_piece_tokens += count
    types = len(word_rows)
    result["compression"] = {
        "word_tokens": tokens,
        "word_types": types,
        "pieces_per_word_token_weighted": token_pieces / tokens,
        "pieces_per_word_type_weighted": type_pieces / types,
        "chars_per_piece": token_chars / token_pieces,
        "bytes_per_piece": token_bytes / token_pieces,
        "single_piece_token_share": single_piece_tokens / tokens,
    }

    # ── The whole pretokenized stream, for pieces per 1000 bytes ──
    stream_pieces = 0
    emitted = set()
    for unit, count in unit_rows:
        pieces = pieces_by_word[unit]
        stream_pieces += count * len(pieces)
        emitted.update(pieces)
    result["stream"] = {
        "raw_bytes": raw_bytes,
        "unit_tokens": sum(count for _, count in unit_rows),
        "pieces": stream_pieces,
        "pieces_per_1000_bytes": 1000.0 * stream_pieces / raw_bytes,
        "distinct_pieces_emitted": len(emitted),
    }

    # ── Vocabulary use ──
    if system.kind == "baseline":
        used = len(emitted & system.vocabulary)
        result["vocabulary"] = {
            "size": system.vocab_size,
            "used_on_test": used,
            "used_share": used / system.vocab_size,
            "emitted_outside_vocab": len(emitted - system.vocabulary),
        }
    else:
        result["vocabulary"] = {
            "size": None,
            "used_on_test": len(emitted),
            "used_share": None,
            "emitted_outside_vocab": 0,
        }

    # ── Distinct pieces emitted on the word tokens alone ──
    word_emitted = set()
    for word in word_set:
        word_emitted.update(pieces_by_word[word])
    result["distinct_pieces_on_words"] = len(word_emitted)

    # ── Morphological alignment ──
    result["gold"] = {}
    for key, entry in gold.items():
        result["gold"][key] = score_gold_set(system, entry, pieces_by_word,
                                             alt_mask)

    # ── Analyzer only ──
    if system.kind == "analyzer":
        fst_tokens = 0
        gate_tokens = collections.Counter()
        fst_types = 0
        gate_types = collections.Counter()
        for word, count in word_rows:
            source, gate, _alt = system.records[word]
            if source == "fst":
                fst_tokens += count
                fst_types += 1
            gate_tokens[gate] += count
            gate_types[gate] += 1
        result["analyzer"] = {
            "lexicon": system.lexicon,
            "derived_first": system.derived_first,
            "coverage_token_weighted": fst_tokens / tokens,
            "coverage_type_weighted": fst_types / types,
            "gate_token_share": {gate: gate_tokens.get(gate, 0) / tokens
                                 for gate in GATES},
            "gate_type_share": {gate: gate_types.get(gate, 0) / types
                                for gate in GATES},
        }

    if examples_for is not None:
        examples_for[system.name] = {
            word: pieces_by_word[word] for word in examples_for["__words__"]
            if word in pieces_by_word
        }

    result["seconds"] = round(time.time() - started, 1)
    return result


def score_gold_set(system, entry, pieces_by_word, alt_mask):
    """Boundary scores for one gold standard, strict and alternation tolerant.

    Every system is scored on the same forms and against the same alternation
    mask, so the only thing that varies between the rows of the table is the
    segmentation.
    """
    forms = entry["forms"]
    counts = collections.Counter()
    mismatches = []

    for form in sorted(forms):
        pieces = pieces_by_word.get(form)
        if pieces is None:
            continue
        surfaces = [s for s in system.surfaces(pieces) if s]
        predicted = internal_boundaries(surfaces)
        tolerance = 1 if alt_mask.get(form) else 0

        counts["forms"] += 1
        if any(tuple(surfaces) == gold_pieces for gold_pieces in forms[form]):
            counts["exact"] += 1
        elif len(mismatches) < 200:
            mismatches.append({
                "word": form,
                "predicted": "|".join(surfaces),
                "gold": " or ".join("|".join(p) for p in sorted(forms[form])),
            })

        for label, limit in (("strict", 0), ("tolerant", tolerance)):
            best = None
            for gold_pieces in forms[form]:
                scored = match_boundaries(predicted,
                                          internal_boundaries(gold_pieces),
                                          limit)
                if best is None or scored[0] > best[0]:
                    best = scored
            counts[label + "_tp"] += best[0]
            counts[label + "_fp"] += best[1]
            counts[label + "_fn"] += best[2]

    out = {"forms": counts["forms"],
           "exact": counts["exact"],
           "exact_rate": counts["exact"] / counts["forms"] if counts["forms"]
           else 0.0,
           "independent": entry["independent"],
           "mismatches": mismatches[:40]}
    for label in ("strict", "tolerant"):
        precision, recall, f1 = prf(counts[label + "_tp"],
                                    counts[label + "_fp"],
                                    counts[label + "_fn"])
        out[label] = {"precision": precision, "recall": recall, "f1": f1,
                      "tp": counts[label + "_tp"], "fp": counts[label + "_fp"],
                      "fn": counts[label + "_fn"]}
    return out


def build_alt_mask(args, words):
    """Which words involve an orthographic alternation.

    Whether a word's stem changes spelling before its suffix is a fact about
    the word, not about the tokenizer that reads it, so the alternation
    tolerant variant has to use one mask for every system. The analyzer is the
    only thing here that reports alternations, so the mask is taken once from
    its richest setting, the largest lexicon with --derived-first, and then
    applied unchanged to all thirteen systems.
    """
    lexicon = os.path.join(expand(args.lexicon_dir), "en_all.tsv")
    tokenizer = FstTokenizer(args.analyzer,
                             extra_args=["--lexicon", lexicon,
                                         "--derived-first"])
    mask = {}
    ordered = sorted(words)
    try:
        for start in range(0, len(ordered), 20000):
            chunk = ordered[start:start + 20000]
            for word, record in zip(chunk, tokenizer.segment_many(chunk)):
                if record["alt"]:
                    mask[word] = record["alt"]
    finally:
        tokenizer.close()
    return mask


# ── Reporting ─────────────────────────────────────────────────────────────

def print_table(results, gold_keys):
    tokens = results[0]["compression"]["word_tokens"]
    print("\n== Compression and round trip "
          "(%s alphabetic word tokens of test.txt) ==\n" % "{:,}".format(tokens))
    header = ("%-26s%9s%11s%11s%9s%9s%11s%9s"
              % ("system", "vocab", "pcs/token", "pcs/type", "chr/pc",
                 "byte/pc", "pcs/1kB", "rt"))
    print(header)
    print("-" * len(header))
    for row in results:
        print("%-26s%9s%11.3f%11.3f%9.3f%9.3f%11.2f%9s"
              % (row["name"],
                 row["vocab_size"] if row["vocab_size"] else "n/a",
                 row["compression"]["pieces_per_word_token_weighted"],
                 row["compression"]["pieces_per_word_type_weighted"],
                 row["compression"]["chars_per_piece"],
                 row["compression"]["bytes_per_piece"],
                 row["stream"]["pieces_per_1000_bytes"],
                 "100%" if row["round_trip"]["rate"] == 1.0
                 else "%.4f%%" % (100 * row["round_trip"]["rate"])))

    print("\n== Vocabulary use ==\n")
    header = ("%-26s%9s%10s%9s%12s%19s"
              % ("system", "vocab", "used", "used %", "1-piece %",
                 "distinct emitted"))
    print(header)
    print("-" * len(header))
    for row in results:
        vocabulary = row["vocabulary"]
        print("%-26s%9s%10d%9s%11.1f%%%19d"
              % (row["name"],
                 vocabulary["size"] if vocabulary["size"] else "n/a",
                 vocabulary["used_on_test"],
                 "n/a" if vocabulary["used_share"] is None
                 else "%.1f%%" % (100 * vocabulary["used_share"]),
                 100 * row["compression"]["single_piece_token_share"],
                 row["distinct_pieces_on_words"]))

    for key in gold_keys:
        if not any(key in row["gold"] for row in results):
            continue
        sample = next(row for row in results if key in row["gold"])
        label = ("independent" if sample["gold"][key]["independent"]
                 else "IN DISTRIBUTION")
        print("\n== Boundary agreement with %s (%s), %d forms ==\n"
              % (key, label, sample["gold"][key]["forms"]))
        header = ("%-26s%8s%8s%8s%8s%9s%9s%9s"
                  % ("system", "P", "R", "F1", "exact",
                     "P alt", "R alt", "F1 alt"))
        print(header)
        print("-" * len(header))
        for row in results:
            scored = row["gold"].get(key)
            if not scored:
                continue
            print("%-26s%8.3f%8.3f%8.3f%8.3f%9.3f%9.3f%9.3f"
                  % (row["name"], scored["strict"]["precision"],
                     scored["strict"]["recall"], scored["strict"]["f1"],
                     scored["exact_rate"], scored["tolerant"]["precision"],
                     scored["tolerant"]["recall"], scored["tolerant"]["f1"]))

    analyzers = [row for row in results if row["kind"] == "analyzer"]
    if analyzers:
        print("\n== Analyzer coverage and gates "
              "(token weighted, test word tokens) ==\n")
        header = ("%-26s%10s" % ("system", "coverage")
                  + "".join("%14s" % g for g in GATES))
        print(header)
        print("-" * len(header))
        for row in analyzers:
            info = row["analyzer"]
            print("%-26s%9.1f%%" % (row["name"],
                                    100 * info["coverage_token_weighted"])
                  + "".join("%13.1f%%" % (100 * info["gate_token_share"][g])
                            for g in GATES))


def print_rankings(results, gold_keys):
    """Best to worst on each headline metric."""
    print("\n== Rankings ==\n")

    def ranking(label, key, reverse, limit=5):
        rows = [(key(row), row["name"]) for row in results
                if key(row) is not None]
        rows.sort(reverse=reverse)
        print("%-34s %s" % (label + ":",
                            ", ".join("%s %.3f" % (name, value)
                                      for value, name in rows[:limit])))

    ranking("fewest pieces per word token",
            lambda r: r["compression"]["pieces_per_word_token_weighted"],
            False)
    ranking("fewest pieces per word type",
            lambda r: r["compression"]["pieces_per_word_type_weighted"],
            False)
    ranking("fewest pieces per 1000 bytes",
            lambda r: r["stream"]["pieces_per_1000_bytes"], False)
    ranking("most words emitted whole",
            lambda r: r["compression"]["single_piece_token_share"], True)
    ranking("largest share of vocabulary used",
            lambda r: r["vocabulary"]["used_share"], True)
    for key in gold_keys:
        if not any(key in row["gold"] for row in results):
            continue
        ranking("best boundary F1, " + key,
                lambda r, k=key: (r["gold"][k]["strict"]["f1"]
                                  if k in r["gold"] else None), True)
        ranking("best exact match, " + key,
                lambda r, k=key: (r["gold"][k]["exact_rate"]
                                  if k in r["gold"] else None), True)


def write_examples(path, pieces_by_system, left, right, gold, word_counts):
    """Dump the words two systems segment differently, for inspection."""
    if left not in pieces_by_system or right not in pieces_by_system:
        return 0
    left_pieces = pieces_by_system[left]
    right_pieces = pieces_by_system[right]
    gold_by_word = {}
    for key, entry in gold.items():
        for word, options in entry["forms"].items():
            gold_by_word.setdefault(word, []).append(
                "%s=%s" % (key, " or ".join("|".join(p)
                                            for p in sorted(options))))

    rows = []
    for word in left_pieces:
        if word not in right_pieces:
            continue
        a = left_pieces[word]
        b = right_pieces[word]
        if a == b:
            continue
        rows.append((word_counts.get(word, 0), word, "|".join(a), "|".join(b),
                     "; ".join(gold_by_word.get(word, []))))
    rows.sort(key=lambda row: (-row[0], row[1]))

    with open(path, "w", encoding="utf-8") as out:
        out.write("count\tword\t%s\t%s\tgold\n" % (left, right))
        for row in rows:
            out.write("%d\t%s\t%s\t%s\t%s\n" % row)
    return len(rows)


# ── Driver ────────────────────────────────────────────────────────────────

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--splits", default=DEFAULT_SPLITS)
    parser.add_argument("--models", default=DEFAULT_MODELS)
    parser.add_argument("--results", default=DEFAULT_RESULTS)
    parser.add_argument("--analyzer", default="./analyzer")
    parser.add_argument("--lexicon-dir", default=DEFAULT_LEXICON_DIR)
    parser.add_argument("--morpholex", default=DEFAULT_MORPHOLEX)
    parser.add_argument("--inflection", default=DEFAULT_INFLECTION)
    parser.add_argument("--derivation", default=DEFAULT_DERIVATION)
    parser.add_argument("--json", default=None,
                        help="defaults to <results>/baselines.json")
    parser.add_argument("--only", nargs="*", default=None,
                        help="score only these system names")
    parser.add_argument("--examples", nargs=2,
                        default=["analyzer-en_100k-derived", "unigram-32k"],
                        metavar=("LEFT", "RIGHT"),
                        help="dump the words these two segment differently")
    args = parser.parse_args(argv)

    splits = expand(args.splits)
    results_dir = expand(args.results)
    os.makedirs(results_dir, exist_ok=True)

    with open(os.path.join(splits, "test_stats.json"), encoding="utf-8") as fh:
        test_stats = json.load(fh)
    word_rows = read_counts(os.path.join(splits, "test_types.tsv"))
    unit_rows = read_counts(os.path.join(splits, "test_units.tsv"))
    word_counts = dict(word_rows)
    print("test split: %d word tokens, %d types, %d unit types"
          % (test_stats["word_tokens"], len(word_rows), len(unit_rows)),
          file=sys.stderr)

    gold = load_gold(args)
    for key, entry in gold.items():
        print("gold %s: %d forms" % (key, len(entry["forms"])),
              file=sys.stderr)

    gold_words = set()
    for entry in gold.values():
        gold_words.update(entry["forms"])
    print("building the alternation mask", file=sys.stderr)
    alt_mask = build_alt_mask(args, gold_words)
    print("  %d of %d gold forms involve an alternation"
          % (len(alt_mask), len(gold_words)), file=sys.stderr)

    example_words = set()
    for entry in gold.values():
        example_words.update(entry["forms"])
    example_words &= set(word_counts)
    example_store = {"__words__": sorted(example_words)}

    results = []
    for name, factory in build_systems(args):
        if args.only and name not in args.only:
            continue
        print("scoring %s" % name, file=sys.stderr)
        system = factory()
        try:
            row = score_system(system, word_rows, unit_rows, gold, alt_mask,
                               test_stats["raw_bytes"],
                               examples_for=example_store)
        finally:
            system.close()
        results.append(row)
        rt = row["round_trip"]
        print("  %.1f s, %.3f pieces per word token, round trip %.4f%% of "
              "tokens, %d of %d strings, library decoder differed on %d"
              % (row["seconds"],
                 row["compression"]["pieces_per_word_token_weighted"],
                 100 * rt["rate"], rt["strings_exact"], rt["strings"],
                 rt["library_decode_disagreed"]), file=sys.stderr)

    gold_keys = ["morpholex", "morphynet-inflection", "morphynet-derivation"]
    print_table(results, gold_keys)
    print_rankings(results, gold_keys)

    out_path = args.json or os.path.join(results_dir, "baselines.json")
    payload = {
        "test": test_stats,
        "gold_stats": {key: entry["stats"] for key, entry in gold.items()},
        "alternation_mask_forms": len(alt_mask),
        "systems": results,
    }
    with open(out_path, "w", encoding="utf-8") as out:
        json.dump(payload, out, indent=2, ensure_ascii=False)
        out.write("\n")
    print("\nwrote %s" % out_path)

    del example_store["__words__"]
    examples_path = os.path.join(results_dir, "disagreements.tsv")
    count = write_examples(examples_path, example_store, args.examples[0],
                           args.examples[1], gold, word_counts)
    if count:
        print("wrote %s (%d words)" % (examples_path, count))

    failures = [row for row in results
                if row["round_trip"]["strings_rate"] != 1.0]
    if failures:
        print("\n== Round trip offenders ==\n")
        for row in failures:
            rt = row["round_trip"]
            print("%s: %d of %d strings, %d of %d word tokens"
                  % (row["name"], rt["strings_exact"], rt["strings"],
                     rt["word_tokens_exact"], rt["word_tokens"]))
            for bad in rt["offenders"]:
                print("    %r -> %s -> %r"
                      % (bad["word"], "|".join(bad["pieces"]),
                         bad["decoded"]))
        return 1

    mismatched = [row for row in results
                  if row["round_trip"]["library_decode_disagreed"]]
    print("\nround trip is exact for all %d systems, on every one of the "
          "%d strings encoded" % (len(results), results[0]["round_trip"]["strings"]))
    if mismatched:
        print("\nthe library decoder read some pieces differently from this "
              "scorer, which is reported rather than fixed up:")
        for row in mismatched:
            rt = row["round_trip"]
            print("  %s: %d strings, for example %s"
                  % (row["name"], rt["library_decode_disagreed"],
                     ", ".join(repr(w) for w in rt["library_decode_examples"])))
    else:
        print("the library decoders agree with it on every string")
    return 0


if __name__ == "__main__":
    sys.exit(main())
