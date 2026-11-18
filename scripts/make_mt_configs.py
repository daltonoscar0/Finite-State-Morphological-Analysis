"""Write configs/mt_configs.json: every configuration tokenize_mt.py accepts.

One entry per (model, vocabulary, Scalpel on/off), covering both the matched
set trained in Stage 3 and the six families Jane ships. Each entry records
the model file and its sha256, so a tokenized output can be tied to the exact
bytes that produced it.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(ROOT / "scripts"))

from moirfeolas_score import FAMILIES                            # noqa: E402

STAGE3 = Path.home() / "data" / "tok" / "results" / "irish" / "stage3" / "models"
OUT = ROOT / "configs" / "mt_configs.json"

# Stage 3 model name -> the family whose convention its output should use.
# The matched set is trained with the `tokenizers` library, so WordPiece
# marks continuation with '##' and the other two mark nothing.
STAGE3_FAMILY = {"unigram": "UnigramLM", "bpe": "BPE",
                 "wordpiece": "WordPiece"}
STAGE3_SPECIAL = {"unigram": None, "bpe": None, "wordpiece": "##"}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def short(size):
    return f"{size // 1000}k" if size % 1000 == 0 else str(size)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--stage3", default=str(STAGE3))
    ap.add_argument("--out", default=str(OUT))
    args = ap.parse_args()

    configs = {}

    # Jane's shipped models, Scalpel on and off.
    for family, (fname, model_type, special) in FAMILIES.items():
        for size in ("8k", "16k", "32k", "43k", "64k"):
            path = REPO / "tokenizers" / family / fname.format(size=size)
            if not path.exists():
                continue
            digest = sha256(path)
            for scalpel in (False, True):
                name = (f"jane-{family.lower()}{size}"
                        f"{'-scalpel' if scalpel else '-plain'}")
                configs[name] = {
                    "family": family,
                    "model_path": str(path),
                    "model_type": model_type,
                    "special_token": special,
                    "scalpel": scalpel,
                    "model_sha256": digest,
                    "source": "jndkns01/tknzn4Irish @ 0f31695",
                }

    # The Stage 3 matched set.
    manifest_path = Path(args.stage3) / "manifest.json"
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        for cell in manifest.values():
            model, size = cell["model"], cell["vocab_size"]
            path = Path(cell["path"])
            if not path.exists():
                continue
            digest = sha256(path)
            # A corpus-pretokenized model is only meaningful with Scalpel in
            # front at inference too, so it gets one entry, not two.
            if cell["condition"] == "scalpel":
                names = [(f"{model}{short(size)}-scalpeltrained", True)]
            else:
                names = [(f"{model}{short(size)}-plain", False),
                         (f"{model}{short(size)}-scalpel", True)]
            for name, scalpel in names:
                configs[name] = {
                    "family": STAGE3_FAMILY[model],
                    "model_path": str(path),
                    "model_type": "standard",
                    "special_token": STAGE3_SPECIAL[model],
                    "scalpel": scalpel,
                    "model_sha256": digest,
                    "source": "scripts/irish_train_tokenizers.py",
                    "trained_on": cell["condition"],
                    "train_seconds": cell["train_seconds"],
                }
    else:
        print(f"note: no Stage 3 manifest at {manifest_path}, "
              f"writing Jane's models only")

    dest = Path(args.out)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(configs, indent=2, ensure_ascii=False,
                               sort_keys=True))
    print(f"wrote {dest} with {len(configs)} configurations")
    for name in sorted(configs)[:6]:
        print(f"  {name}")
    print(f"  ... and {len(configs) - 6} more")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
