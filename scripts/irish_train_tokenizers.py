"""Train Unigram, BPE and WordPiece with and without Scalpel in front.

Stage 3 asks whether the hybrid's gain is a Unigram artefact, and whether
pre-tokenizing at *training* time differs from doing it only at inference.
Jane's repo ships trained models but no training script, so the matched set
is trained here. Her shipped models stay the reference for reproducing her
published rows; these are for the three-way comparison, where what matters is
that the only difference between two cells is the one under test.

Held identical across every cell: the corpus, the sample, the seed, the
special tokens, the alphabet and the trainer library. The two conditions
differ only in whether layer 1 ran over the corpus first.

``data/GA/all_ga_input.txt`` is one word per line, 4,682,103 lines, so
"pre-tokenizing the corpus" means rewriting a line as ``marker base`` wherever
layer 1 fires. That is also why the mutation markers end up in the vocabulary
in the scalpel condition and not in the plain one: a marker only becomes a
candidate piece if it appears as its own whitespace-delimited unit. Eight of
the sixteen markers layer 1 emits are absent from every one of Jane's
vocabularies (`d'`, `D'`, `b'`, `B'`, `m'`, `h-`, `n-`, `t-`), which is the
defect this condition is expected to fix.
"""
import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(REPO / "src"))
sys.path.insert(0, str(ROOT / "scripts"))

from ga_clitic import CLITIC_TAG, add_clitic_readings            # noqa: E402
from ga_mutation_split import (                                  # noqa: E402
    SPLIT_TAGS, demutate_all, mutation_splits,
)

CORPUS = REPO / "data" / "GA" / "all_ga_input.txt"
OUT = Path.home() / "data" / "tok" / "results" / "irish" / "stage3" / "models"

MODELS = ["unigram", "bpe", "wordpiece"]
SIZES = [8000, 16000, 32000, 64000]
CONDITIONS = ["plain", "scalpel"]

SPECIAL_TOKENS = ["<unk>", "<s>", "</s>", "<pad>"]
SEED = 20261118


def load_words(path):
    with open(path, encoding="utf-8") as f:
        return [line.strip() for line in f if line.strip()]


def pretokenized(lines, splits):
    """Rewrite each line, putting a space at every layer-1 boundary."""
    out = []
    for w in lines:
        if w in splits:
            _, marker, base = splits[w]
            out.append(f"{marker} {base}")
        else:
            out.append(w)
    return out


def train(model, size, lines, dest):
    """One cell. Returns the path written and the wall clock."""
    from tokenizers import Tokenizer, models, pre_tokenizers, trainers

    t0 = time.monotonic()
    if model == "unigram":
        tok = Tokenizer(models.Unigram())
        trainer = trainers.UnigramTrainer(
            vocab_size=size, special_tokens=SPECIAL_TOKENS,
            unk_token="<unk>", show_progress=False)
    elif model == "bpe":
        tok = Tokenizer(models.BPE(unk_token="<unk>"))
        trainer = trainers.BpeTrainer(
            vocab_size=size, special_tokens=SPECIAL_TOKENS,
            show_progress=False)
    elif model == "wordpiece":
        tok = Tokenizer(models.WordPiece(unk_token="<unk>"))
        trainer = trainers.WordPieceTrainer(
            vocab_size=size, special_tokens=SPECIAL_TOKENS,
            show_progress=False)
    else:
        raise SystemExit(f"unknown model {model}")

    # Whitespace first, so no piece can span a boundary layer 1 inserted.
    # This is what makes the training-time condition different from the
    # inference-time one rather than a no-op.
    tok.pre_tokenizer = pre_tokenizers.WhitespaceSplit()
    tok.train_from_iterator(lines, trainer=trainer, length=len(lines))

    dest.parent.mkdir(parents=True, exist_ok=True)
    tok.save(str(dest))
    return time.monotonic() - t0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--corpus", default=str(CORPUS))
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--models", nargs="+", default=MODELS)
    ap.add_argument("--sizes", nargs="+", type=int, default=SIZES)
    ap.add_argument("--conditions", nargs="+", default=CONDITIONS)
    ap.add_argument("--limit", type=int, default=None,
                    help="Use only the first N lines. For smoke tests.")
    args = ap.parse_args()

    lines = load_words(args.corpus)
    if args.limit:
        lines = lines[:args.limit]
    print(f"corpus: {len(lines)} word-lines from {args.corpus}")

    vocab = sorted(set(lines))
    print(f"distinct forms: {len(vocab)}")

    skipped = []
    demut = demutate_all(vocab, skipped=skipped)
    if skipped:
        print(f"analyser aborts on {len(skipped)} form(s), set aside: "
              f"{skipped}")
    demut, n_clitic = add_clitic_readings(vocab, demut)
    splits = mutation_splits(vocab, demut, (CLITIC_TAG,) + SPLIT_TAGS)
    print(f"layer 1 fires on {len(splits)} / {len(vocab)} distinct forms "
          f"({len(splits) / len(vocab) * 100:.1f}%), {n_clitic} clitic")

    streams = {"plain": lines}
    if "scalpel" in args.conditions:
        streams["scalpel"] = pretokenized(lines, splits)
        moved = sum(1 for a, b in zip(lines, streams["scalpel"]) if a != b)
        print(f"pre-tokenized corpus: {moved} lines gained a boundary "
              f"({moved / len(lines) * 100:.1f}%)")

    out = Path(args.out)
    manifest = {}
    print(f"\n{'condition':<10}{'model':<11}{'size':>7}{'seconds':>9}  path")
    for cond in args.conditions:
        for model in args.models:
            for size in args.sizes:
                name = f"{cond}_{model}_{size}"
                dest = out / f"{name}.json"
                secs = train(model, size, streams[cond], dest)
                manifest[name] = {
                    "condition": cond, "model": model, "vocab_size": size,
                    "path": str(dest), "train_seconds": round(secs, 1),
                    "corpus": args.corpus, "corpus_lines": len(lines),
                }
                print(f"{cond:<10}{model:<11}{size:>7}{secs:>9.1f}  {dest.name}")

    mf = out / "manifest.json"
    mf.write_text(json.dumps(manifest, indent=2, ensure_ascii=False))
    print(f"\nwrote {mf}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
