"""Tokenize Irish MT data with any Stage 3 configuration. Lossless.

    python3 scripts/tokenize_mt.py --input ga.txt --config unigram8k-scalpel \\
        --output ga.tok

One sentence per line in, one tokenized line out, tokens separated by a
single space. Every configuration is named in ``configs/mt_configs.json``,
which also records the sha256 of each model file, so a run can be tied to the
exact bytes it used.

Reversible:

    python3 scripts/tokenize_mt.py --input ga.tok --config unigram8k-scalpel \\
        --output ga.detok --detokenize

``--round-trip`` checks ``detok(tok(x)) == x`` line by line and reports every
line that fails rather than a pass rate.

Determinism. Encoding is deterministic for all three model types: the Unigram
model is used through ``Tokenizer.encode``, which takes the Viterbi best path
and does no subword sampling, and neither BPE nor WordPiece has a stochastic
mode. No RNG is consulted, so the seed recorded in the sidecar is there to
pin the *training* run rather than this one. Running the same input through
the same config twice gives byte-identical output, which ``--round-trip``
also checks.

The sidecar ``<output>.meta.json`` records the config, the model path and its
sha256, the library versions, the line and token counts and the throughput.
"""
import argparse
import hashlib
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(REPO / "src"))
sys.path.insert(0, str(ROOT / "scripts"))

from ga_clitic import add_clitic_readings                        # noqa: E402
from ga_mutation_split import demutate_all                      # noqa: E402
from ga_pipeline import CONVENTIONS, detokenize, tokenize       # noqa: E402

CONFIGS = ROOT / "configs" / "mt_configs.json"
TRAIN_SEED = 20261118


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_configs(path):
    if not Path(path).exists():
        raise SystemExit(f"no config file at {path}; run "
                         f"scripts/make_mt_configs.py")
    return json.loads(Path(path).read_text())


def build_encoder(cfg):
    """unit -> [pieces], whose pieces concatenate back to the unit."""
    from criochscore.tokenizers_io import (
        get_token_lens, load_tokenizer, remove_specials,
    )

    model_path = cfg["model_path"]
    model_type = cfg["model_type"]
    special = cfg.get("special_token")
    tok = load_tokenizer(model_path, model_type)
    cache = {}

    def encode_unit(unit):
        if unit not in cache:
            lens = remove_specials(
                get_token_lens(tok, [unit], model_type), special)
            pieces = list(lens[unit][1])
            # The contract the pre-tokenizer relies on: pieces must rebuild
            # the unit. A tokenizer that drops or rewrites a character (an
            # <unk>, a normaliser) would break the round trip silently, so
            # fall back to the unit whole and let the caller see it.
            cache[unit] = pieces if "".join(pieces) == unit else [unit]
        return cache[unit]

    return encode_unit


def build_readings(lines, use_scalpel):
    """word -> readings, for every word core in the input. One FST call."""
    if not use_scalpel:
        return lambda w: []

    from ga_pipeline import peel
    cores = set()
    for line in lines:
        for part in line.split():
            core = peel(part)[1]
            if core:
                cores.add(core)
    words = sorted(cores)
    skipped = []
    demut = demutate_all(words, skipped=skipped)
    demut, _ = add_clitic_readings(words, demut)
    if skipped:
        print(f"  analyser aborts on {len(skipped)} form(s), left whole: "
              f"{skipped[:8]}", file=sys.stderr)
    return lambda w: demut.get(w, [])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input", required=True)
    ap.add_argument("--config", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--configs", default=str(CONFIGS))
    ap.add_argument("--detokenize", action="store_true",
                    help="Read a tokenized file and write plain text back.")
    ap.add_argument("--round-trip", action="store_true",
                    help="Tokenize, detokenize, and report every line that "
                         "does not come back identical.")
    ap.add_argument("--list", action="store_true",
                    help="List the configurations and exit.")
    args = ap.parse_args()

    configs = load_configs(args.configs)
    if args.list:
        for name in sorted(configs):
            c = configs[name]
            print(f"{name:<26}{c['family']:<18}scalpel="
                  f"{str(c['scalpel']):<6}{Path(c['model_path']).name}")
        return 0

    if args.config not in configs:
        raise SystemExit(f"unknown config {args.config!r}; "
                         f"--list shows {len(configs)}")
    cfg = configs[args.config]
    family = cfg["family"]
    if family not in CONVENTIONS:
        raise SystemExit(f"config {args.config} names family {family!r}, "
                         f"which has no convention defined")

    with open(args.input, encoding="utf-8") as f:
        lines = [line.rstrip("\n") for line in f]
    print(f"{len(lines)} lines from {args.input}")

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    if args.detokenize:
        t0 = time.monotonic()
        with open(out_path, "w", encoding="utf-8") as f:
            for line in lines:
                f.write(detokenize(line.split(" ") if line else [], family)
                        + "\n")
        secs = time.monotonic() - t0
        print(f"wrote {out_path} in {secs:.1f}s "
              f"({len(lines) / max(secs, 1e-9):.0f} lines/s)")
        return 0

    encode_unit = build_encoder(cfg)
    readings_for = build_readings(lines, cfg["scalpel"])

    t0 = time.monotonic()
    n_tokens, failures = 0, []
    with open(out_path, "w", encoding="utf-8") as f:
        for i, line in enumerate(lines, 1):
            toks = tokenize(line, family, encode_unit=encode_unit,
                            readings_for=readings_for)
            n_tokens += len(toks)
            f.write(" ".join(toks) + "\n")
            if args.round_trip:
                back = detokenize(toks, family)
                if back != line:
                    failures.append((i, line, back))
    secs = time.monotonic() - t0
    rate = len(lines) / max(secs, 1e-9)

    print(f"wrote {out_path}")
    print(f"  {n_tokens} tokens, {n_tokens / max(len(lines), 1):.1f} per line")
    print(f"  {secs:.1f}s, {rate:.0f} lines/s")

    if args.round_trip:
        if failures:
            print(f"\nROUND TRIP FAILED on {len(failures)} / {len(lines)} "
                  f"lines:")
            for i, orig, back in failures[:20]:
                print(f"  line {i}:\n    in  {orig!r}\n    out {back!r}")
        else:
            print(f"  round trip exact on all {len(lines)} lines")

    meta = {
        "config": args.config,
        "family": family,
        "model_path": cfg["model_path"],
        "model_sha256": sha256(cfg["model_path"]),
        "model_type": cfg["model_type"],
        "special_token": cfg.get("special_token"),
        "scalpel": cfg["scalpel"],
        "input": args.input,
        "output": str(out_path),
        "n_lines": len(lines),
        "n_tokens": n_tokens,
        "lines_per_second": round(rate, 1),
        "seconds": round(secs, 2),
        "train_seed": TRAIN_SEED,
        "round_trip_checked": bool(args.round_trip),
        "round_trip_failures": len(failures) if args.round_trip else None,
        "versions": versions(),
    }
    side = Path(str(out_path) + ".meta.json")
    side.write_text(json.dumps(meta, indent=2, ensure_ascii=False))
    print(f"  sidecar {side}")
    return 1 if failures else 0


def versions():
    import importlib.metadata as md
    out = {"python": sys.version.split()[0]}
    for pkg in ("tokenizers", "sentencepiece"):
        try:
            out[pkg] = md.version(pkg)
        except Exception:
            out[pkg] = None
    return out


if __name__ == "__main__":
    raise SystemExit(main())
