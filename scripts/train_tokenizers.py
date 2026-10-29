"""Stage 2 of the matched-vocabulary experiment: train the twelve tokenizers.

    pre-tokenizer   {none, analyzer}
    subword model   {unigram, bpe}
    vocabulary      {8k, 16k, 32k}

Everything except the input stream is held identical across all twelve, so the
only thing that varies within a (model, size) pair is whether the analyzer ran
in front. That means one library for both models rather than the two
`tools/train_baselines.py` uses: `tokenizers` has a BPE trainer and a Unigram
trainer that take the same pre-tokenizer, the same alphabet and the same
special tokens, and mixing in `sentencepiece` would have put a second set of
defaults on one side of the comparison.

The pre-tokenizer is `WhitespaceSplit` then `ByteLevel`. Splitting on
whitespace first is what makes a morph boundary hard: stage 1 put a space at
every one, and no piece can span a space. Byte level after it means the
alphabet is the 256 bytes rather than the several hundred characters of the
corpus, so any input at all can be spelled and round trip is exact without a
byte-fallback option that the Unigram trainer does not have.

Each cell is trained in its own subprocess, one at a time, so the wall clock
and peak memory reported for it are its own. Results go to
`~/data/tok/matched/models/<condition>_<model>_<size>/`.
"""

import argparse
import json
import os
import random
import resource
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, "tools"))

from pretokenize import open_text                           # noqa: E402

CONDITIONS = ["none", "analyzer"]
MODELS = ["unigram", "bpe"]
SIZES = [8000, 16000, 32000]

SPECIAL_TOKENS = ["<unk>", "<s>", "</s>", "<pad>"]

# All twelve train on the same fixed sample of the training split rather than
# all 11.6M lines. The Unigram trainer holds its candidate set in memory and
# the full stream does not fit in 16 GB beside everything else; sampling one
# side only would have put a second difference inside a comparison that is
# supposed to have one. The sample is drawn by line number, so the two
# conditions see the same lines of the same corpus, differing only in how
# those lines were segmented.
SAMPLE_LINES = 5_000_000
SAMPLE_SEED = 20251013


def expand(path):
    return os.path.expanduser(path) if path else path


def peak_rss_bytes():
    peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return peak if sys.platform == "darwin" else peak * 1024


def sample_lines(source, target, count, seed):
    """Write `count` lines chosen uniformly at random from `source`.

    Knuth's selection sampling, the same routine `tools/train_baselines.py`
    uses. The draw sequence depends only on how many lines have been seen, so
    calling this on the two streams with one seed takes the same line numbers
    from each.
    """
    total = 0
    with open_text(source) as handle:
        for _ in handle:
            total += 1
    if count >= total:
        raise ValueError("sample of %d from only %d lines" % (count, total))

    rng = random.Random(seed)
    wanted = count
    remaining = total
    with open_text(source) as handle, open_text(target, "w") as out:
        for line in handle:
            if rng.random() * remaining < wanted:
                out.write(line)
                wanted -= 1
            remaining -= 1
    if wanted:
        raise RuntimeError("sampling ended %d lines short" % wanted)
    return {"sampled": count, "of": total, "seed": seed}


def ensure_sample(out_dir, condition):
    """Return the training input for one condition, sampling if needed."""
    stem = "base" if condition == "none" else "morph"
    target = os.path.join(out_dir, "%s_train_sample.txt.gz" % stem)
    meta_path = target + ".json"
    if os.path.exists(target) and os.path.exists(meta_path):
        return target
    source = os.path.join(out_dir, "%s_train.txt.gz" % stem)
    print("sampling %d lines from %s" % (SAMPLE_LINES, os.path.basename(source)),
          file=sys.stderr)
    meta = sample_lines(source, target, SAMPLE_LINES, SAMPLE_SEED)
    with open(meta_path, "w", encoding="utf-8") as out:
        json.dump(meta, out, indent=2)
        out.write("\n")
    return target


def lines_of(path):
    with open_text(path) as handle:
        for line in handle:
            yield line.rstrip("\n")


def build(model, vocab_size, train_file, out_dir):
    """Train one tokenizer and write it. Returns a detail dict."""
    from tokenizers import Tokenizer, decoders, models, pre_tokenizers, trainers

    if model == "bpe":
        backend = Tokenizer(models.BPE(unk_token=None))
        trainer = trainers.BpeTrainer(
            vocab_size=vocab_size,
            special_tokens=list(SPECIAL_TOKENS),
            initial_alphabet=pre_tokenizers.ByteLevel.alphabet(),
            show_progress=False,
        )
    elif model == "unigram":
        backend = Tokenizer(models.Unigram())
        trainer = trainers.UnigramTrainer(
            vocab_size=vocab_size,
            special_tokens=list(SPECIAL_TOKENS),
            initial_alphabet=pre_tokenizers.ByteLevel.alphabet(),
            unk_token="<unk>",
            show_progress=False,
        )
    else:
        raise ValueError("unknown model %r" % model)

    backend.pre_tokenizer = pre_tokenizers.Sequence([
        pre_tokenizers.WhitespaceSplit(),
        pre_tokenizers.ByteLevel(add_prefix_space=False, use_regex=False),
    ])
    backend.decoder = decoders.ByteLevel()

    backend.train_from_iterator(lines_of(train_file), trainer=trainer)

    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "tokenizer.json")
    backend.save(path)
    return {
        "path": path,
        "actual_vocab_size": backend.get_vocab_size(),
        "train_file": train_file,
    }


def run_one(condition, model, size, out_dir):
    models_dir = os.path.join(out_dir, "models")
    cell_dir = os.path.join(models_dir, "%s_%s_%d" % (condition, model, size))
    train_file = ensure_sample(out_dir, condition)

    start = time.time()
    detail = build(model, size, train_file, cell_dir)
    elapsed = time.time() - start

    import tokenizers

    record = {
        "condition": condition,
        "model": model,
        "nominal_vocab_size": size,
        "seconds": round(elapsed, 1),
        "peak_rss_bytes": peak_rss_bytes(),
        "tokenizers_version": tokenizers.__version__,
        "python_version": sys.version.split()[0],
        "sample_lines": SAMPLE_LINES,
        "sample_seed": SAMPLE_SEED,
    }
    record.update(detail)
    with open(os.path.join(cell_dir, "metrics.json"), "w",
              encoding="utf-8") as out:
        json.dump(record, out, indent=2, sort_keys=True)
        out.write("\n")
    return record


def human_bytes(count):
    for unit in ("B", "KB", "MB", "GB"):
        if count < 1024 or unit == "GB":
            return "%.1f %s" % (count, unit)
        count /= 1024.0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out-dir", default="~/data/tok/matched")
    parser.add_argument("--one", nargs=3, metavar=("CONDITION", "MODEL", "SIZE"),
                        help="train a single cell in this process; the driver "
                             "uses this to give each cell its own subprocess")
    parser.add_argument("--condition", choices=CONDITIONS, action="append")
    parser.add_argument("--model", choices=MODELS, action="append")
    parser.add_argument("--size", type=int, action="append")
    args = parser.parse_args(argv)

    out_dir = expand(args.out_dir)

    if args.one:
        condition, model, size = args.one[0], args.one[1], int(args.one[2])
        record = run_one(condition, model, size, out_dir)
        print(json.dumps(record, sort_keys=True))
        return

    conditions = args.condition or CONDITIONS
    want_models = args.model or MODELS
    sizes = args.size or SIZES

    # Both samples are drawn before any training, so a cell's reported wall
    # clock never includes the one-off cost of the sample it reads.
    for condition in conditions:
        ensure_sample(out_dir, condition)

    records = []
    for condition in conditions:
        for model in want_models:
            for size in sizes:
                print("training %s %s %d" % (condition, model, size),
                      file=sys.stderr)
                done = subprocess.run(
                    [sys.executable, os.path.abspath(__file__),
                     "--out-dir", args.out_dir,
                     "--one", condition, model, str(size)],
                    capture_output=True, text=True,
                )
                if done.returncode:
                    sys.stderr.write(done.stderr)
                    sys.exit("training %s %s %d failed"
                             % (condition, model, size))
                record = json.loads(done.stdout.strip().splitlines()[-1])
                records.append(record)
                print("  %d pieces, %.1f s, %s"
                      % (record["actual_vocab_size"], record["seconds"],
                         human_bytes(record["peak_rss_bytes"])),
                      file=sys.stderr)

    path = os.path.join(out_dir, "models", "index.json")
    with open(path, "w", encoding="utf-8") as out:
        json.dump(records, out, indent=2, sort_keys=True)
        out.write("\n")

    print("\n%-10s %-8s %8s %8s %9s %10s"
          % ("condition", "model", "nominal", "actual", "seconds", "peak RSS"))
    for record in records:
        print("%-10s %-8s %8d %8d %9.1f %10s"
              % (record["condition"], record["model"],
                 record["nominal_vocab_size"], record["actual_vocab_size"],
                 record["seconds"], human_bytes(record["peak_rss_bytes"])))
    print("\nwrote %s" % path, file=sys.stderr)


if __name__ == "__main__":
    main()
