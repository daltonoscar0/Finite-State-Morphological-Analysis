"""Stage 4 of the matched-vocabulary experiment: one small LM per tokenizer.

Twelve decoder-only transformers, identical in every respect except the
vocabulary they read and write. The point is a downstream number that can be
compared across tokenizers, which perplexity cannot be: perplexity is per
token, and a tokenizer that cuts words into more pieces is rewarded for it.
Bits per character divides the same total likelihood by the same number of
characters of the same text, so the twelve are on one scale.

    BPC = (total negative log likelihood of the dev token stream, in bits)
          / (characters of the pretokenized dev text)

The denominator is fixed once, from the `none` stream with its markers
removed, and reused for all twelve. It is a property of the text, not of the
tokenizer reading it. It counts the text the models actually see, which is
the dev split after the shared pretokenizer has normalised its whitespace,
not the raw corpus: 60,111,059 characters against the corpus's 56,500,281
bytes, the difference being the single space the pretokenizer puts between
two units that were adjacent in the original.

What cannot be held identical is the embedding table: a 32k vocabulary needs
four times the rows of an 8k one. Everything else is, so the non-embedding
parameter count is the same for all twelve and is reported next to the total.

Each run gets the same number of optimizer steps at the same batch size in
tokens, which is equal compute rather than equal text. A tokenizer with higher
fertility therefore sees less text in the same budget. That is a real effect
of the tokenizer and the table reports the text each run saw so it is visible
rather than hidden.

Training blocks are drawn uniformly from the whole encoded training split,
never from a prefix of it. BabyLM is a concatenation of six corpora in order,
BNC then CHILDES then Gutenberg then OpenSubtitles then Wikipedia and
Switchboard, while dev is a mixture of all six. A prefix is therefore a
different domain from the evaluation set, and worse, a different domain for
each condition: the same token budget reaches a shorter prefix of the corpus
when fertility is higher, so the analyzer condition would have been scored on
domains it had seen less of. That is a difference between the rows with
nothing to do with tokenization, which is exactly what this experiment exists
to exclude. The first attempt at this stage did take a prefix, and the
train-to-dev gap it produced, 3.73 nats against 5.64, is what gave it away.
"""

import argparse
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, "tools"))

from pretokenize import open_text                           # noqa: E402

MARKER = "▁"

# Architecture. Fixed for all twelve; only the vocabulary varies.
#
# These are the sizes the stage was measured at and the ones it ships with.
# The first configuration tried was d_model 256 with 6 layers and a 1024
# feed-forward, 4.74M non-embedding parameters, which measured 11,625 tokens
# per second on this machine's MPS backend: 70 minutes a run and 14.1 hours
# for all twelve, over the budget the stage is allowed. Cutting to 192 and 4
# layers measured 2.2 times faster on the same machine for 1.78M
# non-embedding parameters, which brings the twelve to about 7 hours.
#
# Batch is held at 32 because the memory wall here is the logits tensor, not
# the model: at a 32k vocabulary, batch 32 and sequence 256 it is already
# 1.05 GB, and batch 64 was killed by the OS on a 16 GB machine.
D_MODEL = 192
N_LAYER = 4
N_HEAD = 4
D_FF = 768
SEQ_LEN = 256
BATCH = 32
DROPOUT = 0.0

LEARNING_RATE = 3e-4
WARMUP_STEPS = 200
GRAD_CLIP = 1.0
WEIGHT_DECAY = 0.1
SEED = 20251029


def expand(path):
    return os.path.expanduser(path) if path else path


# ── Data ──────────────────────────────────────────────────────────────────

def encode_stream(tokenizer_path, stream_path, limit_tokens=None,
                  batch_lines=20000):
    """Encode a stage-1 stream to a flat array of token ids."""
    import numpy as np
    from tokenizers import Tokenizer

    backend = Tokenizer.from_file(tokenizer_path)
    vocab = backend.get_vocab_size()
    dtype = np.uint16 if vocab <= 65535 else np.uint32

    chunks = []
    total = 0
    pending = []
    with open_text(stream_path) as handle:
        for line in handle:
            line = line.rstrip("\n")
            if line:
                pending.append(line)
            if len(pending) >= batch_lines:
                for encoding in backend.encode_batch(pending,
                                                     add_special_tokens=False):
                    chunks.append(np.asarray(encoding.ids, dtype=dtype))
                    total += len(encoding.ids)
                pending = []
                if limit_tokens and total >= limit_tokens:
                    break
        if pending and not (limit_tokens and total >= limit_tokens):
            for encoding in backend.encode_batch(pending,
                                                 add_special_tokens=False):
                chunks.append(np.asarray(encoding.ids, dtype=dtype))
                total += len(encoding.ids)

    flat = np.concatenate(chunks) if chunks else np.zeros(0, dtype=dtype)
    if limit_tokens and len(flat) > limit_tokens:
        flat = flat[:limit_tokens]
    return flat


def dev_characters(base_dev_path):
    """Characters of the dev text, the denominator shared by all twelve.

    Taken from the `none` stream with the markers removed, which reconstructs
    the pretokenized dev text: units separated by single spaces, one line per
    line of the corpus.
    """
    chars = 0
    with open_text(base_dev_path) as handle:
        for line in handle:
            chars += len(line.replace(MARKER, ""))
    return chars


def ensure_tokens(cache_dir, name, tokenizer_path, stream_path, limit):
    import numpy as np

    os.makedirs(cache_dir, exist_ok=True)
    # The limit is part of the name. A benchmark run asks for far fewer
    # tokens than a full run, and a cache keyed on the name alone would hand
    # the short array back to the long run.
    tag = "all" if limit is None else str(limit)
    path = os.path.join(cache_dir, "%s-%s.npy" % (name, tag))
    if os.path.exists(path):
        return np.load(path)
    print("  encoding %s" % os.path.basename(stream_path), file=sys.stderr)
    started = time.time()
    ids = encode_stream(tokenizer_path, stream_path, limit)
    np.save(path, ids)
    print("    %d tokens in %.0f s" % (len(ids), time.time() - started),
          file=sys.stderr)
    return ids


# ── Model ─────────────────────────────────────────────────────────────────

def build_model(vocab_size):
    import torch
    from torch import nn

    class Block(nn.Module):
        """Pre-norm decoder block.

        Attention is `nn.MultiheadAttention` with an additive causal mask
        rather than a hand-rolled `scaled_dot_product_attention` with
        `is_causal`. The fused call is the faster one on CUDA and was tried
        first here; on this machine's MPS backend it measured 10,224 tokens
        per second against the library's 11,625 on the same shape, so the
        library path is what ships. The two have the same parameter count.
        """

        def __init__(self):
            super().__init__()
            self.norm1 = nn.LayerNorm(D_MODEL)
            self.attn = nn.MultiheadAttention(D_MODEL, N_HEAD, dropout=DROPOUT,
                                              batch_first=True)
            self.norm2 = nn.LayerNorm(D_MODEL)
            self.ff = nn.Sequential(nn.Linear(D_MODEL, D_FF), nn.GELU(),
                                    nn.Linear(D_FF, D_MODEL))
            self.register_buffer("mask", torch.triu(
                torch.full((SEQ_LEN, SEQ_LEN), float("-inf")), diagonal=1))

        def forward(self, x):
            h = self.norm1(x)
            attended, _ = self.attn(h, h, h, attn_mask=self.mask,
                                    need_weights=False)
            x = x + attended
            return x + self.ff(self.norm2(x))

    class Decoder(nn.Module):
        def __init__(self):
            super().__init__()
            self.embed = nn.Embedding(vocab_size, D_MODEL)
            self.pos = nn.Embedding(SEQ_LEN, D_MODEL)
            self.blocks = nn.ModuleList([Block() for _ in range(N_LAYER)])
            self.norm = nn.LayerNorm(D_MODEL)
            self.head = nn.Linear(D_MODEL, vocab_size, bias=False)
            self.head.weight = self.embed.weight       # tied

        def forward(self, idx):
            positions = torch.arange(idx.shape[1], device=idx.device)
            x = self.embed(idx) + self.pos(positions)[None, :, :]
            for block in self.blocks:
                x = block(x)
            return self.head(self.norm(x))

    return Decoder()


def parameter_counts(model):
    total = sum(p.numel() for p in model.parameters())
    embedding = model.embed.weight.numel() + model.pos.weight.numel()
    return total, total - embedding


# Evaluation holds no activations for the backward pass, but the logits
# tensor is the same size as in training and that is what sets the limit, so
# the eval batch stays where training's is.
EVAL_BATCH = BATCH


def batches(ids, steps, seed, device):
    """Yield `steps` batches of contiguous blocks, in a fixed shuffled order."""
    import numpy as np
    import torch

    usable = (len(ids) - 1) // SEQ_LEN
    rng = np.random.default_rng(seed)
    order = rng.permutation(usable)
    needed = steps * BATCH
    if needed > usable:
        # Wrap rather than stop, so every run takes the same number of steps.
        repeats = -(-needed // usable)
        order = np.concatenate([rng.permutation(usable) for _ in range(repeats)])
    order = order[:needed]

    for step in range(steps):
        picks = order[step * BATCH:(step + 1) * BATCH]
        xs = np.stack([ids[p * SEQ_LEN:p * SEQ_LEN + SEQ_LEN] for p in picks])
        ys = np.stack([ids[p * SEQ_LEN + 1:p * SEQ_LEN + SEQ_LEN + 1]
                       for p in picks])
        yield (torch.from_numpy(xs.astype("int64")).to(device),
               torch.from_numpy(ys.astype("int64")).to(device))


def evaluate(model, ids, device, chars):
    """Total dev NLL in bits, and bits per character."""
    import numpy as np
    import torch
    from torch.nn import functional as F

    model.eval()
    blocks = (len(ids) - 1) // SEQ_LEN
    total_nats = 0.0
    counted = 0
    with torch.no_grad():
        for start in range(0, blocks, EVAL_BATCH):
            picks = range(start, min(start + EVAL_BATCH, blocks))
            xs = np.stack([ids[p * SEQ_LEN:p * SEQ_LEN + SEQ_LEN]
                           for p in picks])
            ys = np.stack([ids[p * SEQ_LEN + 1:p * SEQ_LEN + SEQ_LEN + 1]
                           for p in picks])
            x = torch.from_numpy(xs.astype("int64")).to(device)
            y = torch.from_numpy(ys.astype("int64")).to(device)
            logits = model(x)
            loss = F.cross_entropy(logits.reshape(-1, logits.shape[-1]),
                                   y.reshape(-1), reduction="sum")
            total_nats += float(loss)
            counted += y.numel()
    model.train()
    bits = total_nats / math.log(2)
    return {
        "dev_tokens_scored": counted,
        "dev_nll_bits": bits,
        "bits_per_character": bits / chars,
        "bits_per_token": bits / counted if counted else 0.0,
    }


def train_one(name, tokenizer_path, train_ids, dev_ids, chars, steps, device,
              log_every=200):
    import torch

    torch.manual_seed(SEED)
    from tokenizers import Tokenizer

    vocab = Tokenizer.from_file(tokenizer_path).get_vocab_size()
    model = build_model(vocab).to(device)
    total_params, non_embedding = parameter_counts(model)

    optimizer = torch.optim.AdamW(model.parameters(), lr=LEARNING_RATE,
                                  weight_decay=WEIGHT_DECAY, betas=(0.9, 0.95))

    def lr_at(step):
        if step < WARMUP_STEPS:
            return LEARNING_RATE * (step + 1) / WARMUP_STEPS
        progress = (step - WARMUP_STEPS) / max(steps - WARMUP_STEPS, 1)
        return LEARNING_RATE * 0.5 * (1 + math.cos(math.pi * progress))

    from torch.nn import functional as F

    started = time.time()
    running = 0.0
    for step, (x, y) in enumerate(batches(train_ids, steps, SEED, device)):
        for group in optimizer.param_groups:
            group["lr"] = lr_at(step)
        logits = model(x)
        loss = F.cross_entropy(logits.reshape(-1, logits.shape[-1]),
                               y.reshape(-1))
        optimizer.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), GRAD_CLIP)
        optimizer.step()
        running += float(loss.detach())
        if (step + 1) % log_every == 0:
            elapsed = time.time() - started
            seen = (step + 1) * BATCH * SEQ_LEN
            print("    step %d/%d loss %.4f  %.0f tok/s"
                  % (step + 1, steps, running / log_every, seen / elapsed),
                  file=sys.stderr)
            running = 0.0

    train_seconds = time.time() - started
    record = {
        "name": name,
        "vocab_size": vocab,
        "total_parameters": total_params,
        "non_embedding_parameters": non_embedding,
        "steps": steps,
        "batch_tokens": BATCH * SEQ_LEN,
        "train_tokens_seen": steps * BATCH * SEQ_LEN,
        "train_tokens_available": int(len(train_ids)),
        "train_seconds": round(train_seconds, 1),
        "tokens_per_second": round(steps * BATCH * SEQ_LEN / train_seconds, 1),
    }
    record.update(evaluate(model, dev_ids, device, chars))
    return record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--matched-dir", default="~/data/tok/matched")
    parser.add_argument("--steps", type=int, default=6000)
    parser.add_argument("--device", default="mps")
    parser.add_argument("--benchmark", type=int, default=0,
                        help="run this many steps on one cell and report an "
                             "extrapolation for all twelve, then stop")
    parser.add_argument("--only", nargs="*", default=None)
    parser.add_argument("--out", default=None)
    args = parser.parse_args(argv)

    import torch

    matched = expand(args.matched_dir)
    with open(os.path.join(matched, "models", "index.json"),
              encoding="utf-8") as handle:
        index = json.load(handle)

    device = torch.device(args.device)
    chars = dev_characters(os.path.join(matched, "base_dev.txt.gz"))
    print("dev denominator: %d characters" % chars, file=sys.stderr)

    cache = os.path.join(matched, "lm_tokens")
    steps = args.benchmark or args.steps
    # A full run encodes the whole training split and samples blocks from all
    # of it; see the note on corpus order at the top of this file. A benchmark
    # only measures throughput, so it takes a prefix and saves the encoding.
    limit = (steps * BATCH * SEQ_LEN + SEQ_LEN) if args.benchmark else None

    rows = []
    for record in index:
        name = "%s-%s-%dk" % (record["condition"], record["model"],
                              record["nominal_vocab_size"] // 1000)
        if args.only and name not in args.only:
            continue
        stem = "base" if record["condition"] == "none" else "morph"
        print("training %s" % name, file=sys.stderr)

        train_ids = ensure_tokens(cache, name + "-train", record["path"],
                                  os.path.join(matched, "%s_train.txt.gz" % stem),
                                  limit)
        dev_ids = ensure_tokens(cache, name + "-dev", record["path"],
                                os.path.join(matched, "%s_dev.txt.gz" % stem),
                                None)

        row = train_one(name, record["path"], train_ids, dev_ids, chars,
                        steps, device)
        row["condition"] = record["condition"]
        row["model"] = record["model"]
        row["nominal_vocab_size"] = record["nominal_vocab_size"]
        rows.append(row)
        print("  BPC %.4f  (%.0f tok/s, %.0f s)"
              % (row["bits_per_character"], row["tokens_per_second"],
                 row["train_seconds"]), file=sys.stderr)

        if args.benchmark:
            rate = row["tokens_per_second"]
            per_run = args.steps * BATCH * SEQ_LEN / rate
            print("\nbenchmark: %.0f tokens/s on %s" % (rate, args.device))
            print("one full run of %d steps: %.1f minutes"
                  % (args.steps, per_run / 60))
            print("all twelve: %.1f hours" % (per_run * 12 / 3600))
            return

    out_path = expand(args.out) if args.out else os.path.join(
        matched, "results", "lm.json")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as out:
        json.dump({"dev_characters": chars, "architecture": {
            "d_model": D_MODEL, "n_layer": N_LAYER, "n_head": N_HEAD,
            "d_ff": D_FF, "seq_len": SEQ_LEN, "batch": BATCH,
            "dropout": DROPOUT, "learning_rate": LEARNING_RATE,
            "warmup_steps": WARMUP_STEPS, "weight_decay": WEIGHT_DECAY,
            "grad_clip": GRAD_CLIP, "seed": SEED, "steps": args.steps,
        }, "rows": rows}, out, indent=2, sort_keys=True)
        out.write("\n")
    print("wrote %s" % out_path, file=sys.stderr)


if __name__ == "__main__":
    main()
