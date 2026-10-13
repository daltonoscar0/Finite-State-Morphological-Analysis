#!/usr/bin/env python3
"""Train the standard subword baselines: BPE, WordPiece and Unigram.

Three algorithms at three vocabulary sizes, nine models, all trained on the
same pretokenized text so that the only difference between them is the
algorithm and the size.

    ~/venvs/tok/bin/python tools/train_baselines.py

BPE and WordPiece come from Hugging Face `tokenizers`, Unigram from
`sentencepiece`. Each model is trained in its own subprocess, one at a time,
so the wall time and peak resident memory reported for it are its own and
nothing runs in parallel. A model whose directory already holds a finished
run is skipped unless `--force` is given.

Settings shared by all three
----------------------------
* Input is `~/data/tok/splits/train_pretok.txt`, written by
  `tools/make_splits.py`: one line per training line, units of the shared
  pretokenizer joined by single spaces. Each library's own splitting is set to
  plain whitespace, so all three see exactly those units.
* No normalization. Nothing is lowercased, stripped or NFKC folded.
* Special tokens `<unk>`, `<s>`, `</s>`, `<pad>`, in that order, ids 0 to 3.
* Full byte coverage, so that any input at all can be encoded and decoded
  back. BPE and Unigram use the byte fallback their libraries provide.
  `tokenizers` has no byte fallback for WordPiece, so WordPiece is given a
  byte level pre-tokenizer instead, which has the same effect: see the note in
  `build_wordpiece`.

Everything not listed above is the library default. The defaults that were
left alone are recorded in `docs/BASELINES.md` next to the results.
"""

import argparse
import json
import os
import random
import resource
import subprocess
import sys
import time

SPECIAL_TOKENS = ["<unk>", "<s>", "</s>", "<pad>"]
UNK, BOS, EOS, PAD = SPECIAL_TOKENS

ALGOS = ["bpe", "wordpiece", "unigram"]
SIZES = [8000, 16000, 32000]

# sentencepiece holds the whole training set in memory, so it gets a sample
# rather than all 11.6M lines. The sample is drawn once, deterministically,
# and reused by all three Unigram runs.
SP_SENTENCES = 5000000
SP_SEED = 20251013

# No training line is to be dropped for length. The longest in
# train_pretok.txt is 20,857 bytes, against a sentencepiece default limit of
# 4,192 that would have silently discarded 382 lines.
SP_MAX_SENTENCE_LENGTH = 24000


def expand(path):
    return os.path.expanduser(path)


# ── The three builders ────────────────────────────────────────────────────

def build_bpe(train_file, vocab_size, out_dir):
    """Byte-pair encoding, with byte fallback for anything unseen."""
    from tokenizers import (Tokenizer, decoders, models, pre_tokenizers,
                            trainers)

    byte_tokens = ["<0x%02X>" % i for i in range(256)]

    tokenizer = Tokenizer(models.BPE(unk_token=UNK, byte_fallback=True))
    tokenizer.pre_tokenizer = pre_tokenizers.WhitespaceSplit()
    tokenizer.decoder = decoders.Sequence([decoders.ByteFallback(),
                                           decoders.Fuse()])

    # The trainer does not add the <0xXX> pieces that byte fallback needs, and
    # the forced-token list is the only way to put them in the vocabulary.
    trainer = trainers.BpeTrainer(
        vocab_size=vocab_size,
        special_tokens=SPECIAL_TOKENS + byte_tokens,
        show_progress=False,
    )
    tokenizer.train([train_file], trainer)
    tokenizer.save(os.path.join(out_dir, "tokenizer.json"))
    return {"vocab_size": tokenizer.get_vocab_size()}


def build_wordpiece(train_file, vocab_size, out_dir):
    """WordPiece over bytes rather than characters.

    `tokenizers` offers no byte fallback for WordPiece: a word holding one
    character the model does not know becomes a single `<unk>` and cannot be
    decoded back. The test text has 36 characters the training text never
    shows, so that is not hypothetical.

    Running WordPiece over a byte level pre-tokenizer fixes it at the root.
    The model's alphabet is then the 256 byte symbols instead of the 702
    characters of the training text, every possible input is spelled out of
    those 256, and the byte level decoder puts the characters back. For
    ASCII, which is every word this comparison scores against a gold
    standard, the pieces are identical to what plain WordPiece would produce.

    One gap is left by the trainer: it adds an alphabet symbol in word initial
    form and only adds the `##` continuation form for symbols it actually saw
    inside a word. A byte symbol missing its continuation form still forces an
    `<unk>`, so both forms of all 256 are filled in afterwards. That is a
    handful of entries past the nominal size, and the exact final size is
    reported.
    """
    from tokenizers import (Tokenizer, decoders, models, pre_tokenizers,
                            trainers)

    alphabet = pre_tokenizers.ByteLevel.alphabet()
    pre_tokenizer = pre_tokenizers.Sequence([
        pre_tokenizers.WhitespaceSplit(),
        pre_tokenizers.ByteLevel(add_prefix_space=False, use_regex=False),
    ])

    tokenizer = Tokenizer(models.WordPiece(unk_token=UNK,
                                           max_input_chars_per_word=1000))
    tokenizer.pre_tokenizer = pre_tokenizer

    trainer = trainers.WordPieceTrainer(
        vocab_size=vocab_size,
        special_tokens=SPECIAL_TOKENS,
        continuing_subword_prefix="##",
        initial_alphabet=alphabet,
        limit_alphabet=len(alphabet),
        show_progress=False,
    )
    tokenizer.train([train_file], trainer)

    vocab = tokenizer.get_vocab()
    trained_size = len(vocab)
    added = 0
    for symbol in alphabet:
        for form in (symbol, "##" + symbol):
            if form not in vocab:
                vocab[form] = len(vocab)
                added += 1

    tokenizer = Tokenizer(models.WordPiece(vocab=vocab, unk_token=UNK,
                                           max_input_chars_per_word=1000))
    tokenizer.pre_tokenizer = pre_tokenizer
    tokenizer.decoder = decoders.Sequence([
        decoders.WordPiece(prefix="##", cleanup=False),
        decoders.ByteLevel(),
    ])
    tokenizer.save(os.path.join(out_dir, "tokenizer.json"))
    return {"vocab_size": tokenizer.get_vocab_size(),
            "trained_vocab_size": trained_size,
            "byte_forms_added": added}


def build_unigram(train_file, vocab_size, out_dir):
    """The sentencepiece unigram language model."""
    import sentencepiece as spm

    prefix = os.path.join(out_dir, "spm")
    spm.SentencePieceTrainer.train(
        input=train_file,
        model_prefix=prefix,
        model_type="unigram",
        vocab_size=vocab_size,
        byte_fallback=True,
        normalization_rule_name="identity",
        remove_extra_whitespaces=False,
        split_by_whitespace=True,
        max_sentence_length=SP_MAX_SENTENCE_LENGTH,
        input_sentence_size=0,
        unk_id=0, bos_id=1, eos_id=2, pad_id=3,
        unk_piece=UNK, bos_piece=BOS, eos_piece=EOS, pad_piece=PAD,
        minloglevel=1,
    )
    processor = spm.SentencePieceProcessor(model_file=prefix + ".model")
    return {"vocab_size": processor.get_piece_size()}


BUILDERS = {"bpe": build_bpe, "wordpiece": build_wordpiece,
            "unigram": build_unigram}


# ── The sentencepiece sample ──────────────────────────────────────────────

def sample_lines(source, target, count, seed):
    """Write `count` lines chosen uniformly at random from `source`.

    Knuth's selection sampling: walk the file once and take the next line
    with probability (lines still wanted) / (lines still to come). That gives
    exactly `count` lines, uniformly, holding one line in memory at a time,
    and the same lines on every run for a given seed.
    """
    total = 0
    with open(source, encoding="utf-8") as handle:
        for _ in handle:
            total += 1
    if count >= total:
        raise ValueError("sample of %d from only %d lines" % (count, total))

    rng = random.Random(seed)
    wanted = count
    remaining = total
    with open(source, encoding="utf-8") as handle, \
            open(target, "w", encoding="utf-8") as out:
        for line in handle:
            if rng.random() * remaining < wanted:
                out.write(line)
                wanted -= 1
            remaining -= 1
    if wanted:
        raise RuntimeError("sampling ended %d lines short" % wanted)
    return {"sampled": count, "of": total, "seed": seed}


def ensure_sample(splits_dir, sentences=SP_SENTENCES):
    """Return the sentencepiece input path, drawing the sample if needed."""
    target = os.path.join(splits_dir, "train_pretok_sample.txt")
    meta_path = target + ".json"
    if os.path.exists(target) and os.path.exists(meta_path):
        return target
    source = os.path.join(splits_dir, "train_pretok.txt")
    print("sampling %d lines for sentencepiece" % sentences, file=sys.stderr)
    meta = sample_lines(source, target, sentences, SP_SEED)
    with open(meta_path, "w", encoding="utf-8") as out:
        json.dump(meta, out, indent=2)
        out.write("\n")
    return target


# ── One run ───────────────────────────────────────────────────────────────

def peak_rss_bytes():
    """Peak resident set size of this process.

    getrusage reports kilobytes on Linux and bytes on macOS, which is the
    one platform difference that matters here.
    """
    peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return peak if sys.platform == "darwin" else peak * 1024


def run_one(algo, size, train_file, out_dir):
    """Train one model in this process and write its metrics file."""
    os.makedirs(out_dir, exist_ok=True)
    start = time.time()
    detail = BUILDERS[algo](train_file, size, out_dir)
    elapsed = time.time() - start

    record = {
        "algo": algo,
        "nominal_vocab_size": size,
        "train_file": train_file,
        "seconds": round(elapsed, 1),
        "peak_rss_bytes": peak_rss_bytes(),
    }
    record.update(detail)
    with open(os.path.join(out_dir, "metrics.json"), "w",
              encoding="utf-8") as out:
        json.dump(record, out, indent=2)
        out.write("\n")
    return record


def human_bytes(count):
    for unit in ("B", "KB", "MB", "GB"):
        if count < 1024 or unit == "GB":
            return "%.1f %s" % (count, unit)
        count /= 1024.0


# ── Driver ────────────────────────────────────────────────────────────────

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--splits", default="~/data/tok/splits")
    parser.add_argument("--models", default="~/data/tok/models")
    parser.add_argument("--algos", nargs="*", default=ALGOS, choices=ALGOS)
    parser.add_argument("--sizes", nargs="*", type=int, default=SIZES)
    parser.add_argument("--force", action="store_true",
                        help="retrain models that are already finished")
    parser.add_argument("--sp-sentences", type=int, default=SP_SENTENCES,
                        help="cap on sentencepiece training sentences")
    parser.add_argument("--one", nargs=2, metavar=("ALGO", "SIZE"),
                        help="train exactly one model in this process; "
                             "this is how the driver runs each model")
    args = parser.parse_args(argv)

    splits_dir = expand(args.splits)
    models_dir = expand(args.models)

    if args.one:
        algo, size = args.one[0], int(args.one[1])
        train_file = (ensure_sample(splits_dir, args.sp_sentences)
                      if algo == "unigram"
                      else os.path.join(splits_dir, "train_pretok.txt"))
        out_dir = os.path.join(models_dir, "%s_%d" % (algo, size))
        record = run_one(algo, size, train_file, out_dir)
        print("  %s %d: %.1f s, peak %s, vocab %d"
              % (algo, size, record["seconds"],
                 human_bytes(record["peak_rss_bytes"]),
                 record["vocab_size"]), file=sys.stderr)
        return 0

    if "unigram" in args.algos:
        ensure_sample(splits_dir, args.sp_sentences)

    results = []
    for algo in args.algos:
        for size in args.sizes:
            out_dir = os.path.join(models_dir, "%s_%d" % (algo, size))
            metrics = os.path.join(out_dir, "metrics.json")
            if os.path.exists(metrics) and not args.force:
                print("%s %d: already trained, skipping" % (algo, size),
                      file=sys.stderr)
                with open(metrics, encoding="utf-8") as handle:
                    results.append(json.load(handle))
                continue

            print("%s %d: training" % (algo, size), file=sys.stderr)
            command = [sys.executable, os.path.abspath(__file__),
                       "--splits", splits_dir, "--models", models_dir,
                       "--sp-sentences", str(args.sp_sentences),
                       "--one", algo, str(size)]
            code = subprocess.call(command)
            if code != 0:
                print("%s %d: failed with status %d" % (algo, size, code),
                      file=sys.stderr)
                return 1
            with open(metrics, encoding="utf-8") as handle:
                results.append(json.load(handle))

    print("\n%-12s%8s%10s%12s%12s" % ("algorithm", "nominal", "vocab",
                                      "seconds", "peak RSS"))
    for record in results:
        print("%-12s%8d%10d%12.1f%12s"
              % (record["algo"], record["nominal_vocab_size"],
                 record["vocab_size"], record["seconds"],
                 human_bytes(record["peak_rss_bytes"])))

    summary = os.path.join(models_dir, "training.json")
    with open(summary, "w", encoding="utf-8") as out:
        json.dump(results, out, indent=2)
        out.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
