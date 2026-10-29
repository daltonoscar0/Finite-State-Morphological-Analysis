"""Stage 5 of the matched-vocabulary experiment: the tables.

Reads `intrinsic.json` from stage 3 and, if it is there, `lm.json` from stage
4, and writes `results/matched_vocab.md`: the twelve-row table, the paired
comparison at each size, and a paragraph saying whether the analyzer helped.

With `--baselines` it writes the same round in the format
`docs/BASELINES.md` uses, to be appended there.
"""

import argparse
import json
import os
import sys

CONDITIONS = ["none", "analyzer"]
MODELS = ["unigram", "bpe"]
SIZES = [8000, 16000, 32000]

GOLD_LABELS = [
    ("morpholex", "MorphoLex, independent"),
    ("morphynet-inflection", "MorphyNet inflection, in distribution"),
    ("morphynet-derivation", "MorphyNet derivation, in distribution"),
]


def expand(path):
    return os.path.expanduser(path) if path else path


def load(matched, name):
    path = os.path.join(matched, "results", name)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def index_rows(payload, key="name"):
    return {row[key]: row for row in payload["rows"]} if payload else {}


def cell_name(condition, model, size):
    return "%s-%s-%dk" % (condition, model, size // 1000)


def fmt(value, places=4):
    return "n/a" if value is None else "%.*f" % (places, value)


def delta(after, before, places=4):
    if after is None or before is None:
        return "n/a"
    return "%+.*f" % (places, after - before)


def main_table(intrinsic, lm):
    rows = index_rows(intrinsic)
    lm_rows = index_rows(lm)
    out = ["| condition | model | nominal | actual | fertility | bytes/piece "
           "| MorphoLex F1 | infl F1 | deriv F1 | compound R | BPC |",
           "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for model in MODELS:
        for size in SIZES:
            for condition in CONDITIONS:
                name = cell_name(condition, model, size)
                row = rows.get(name)
                if not row:
                    continue
                gold = row["gold"]
                bpc = lm_rows.get(name, {}).get("bits_per_character")
                out.append(
                    "| %s | %s | %d | %d | %s | %s | %s | %s | %s | %s | %s |"
                    % (condition, model, row["nominal_vocab_size"],
                       row["actual_vocab_size"],
                       fmt(row["compression"]["fertility"]),
                       fmt(row["compression"]["bytes_per_piece"], 3),
                       fmt(gold["morpholex"]["strict"]["f1"]),
                       fmt(gold["morphynet-inflection"]["strict"]["f1"]),
                       fmt(gold["morphynet-derivation"]["strict"]["f1"]),
                       fmt(row["compounds"]["recall"]),
                       fmt(bpc) if bpc is not None else "n/a"))
    return out


def paired_table(intrinsic, lm):
    """The analyzer minus none difference, at each (model, size)."""
    rows = index_rows(intrinsic)
    lm_rows = index_rows(lm)
    out = ["| model | size | MorphoLex F1 | infl F1 | deriv F1 | compound R "
           "| fertility | BPC |",
           "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for model in MODELS:
        for size in SIZES:
            before = rows.get(cell_name("none", model, size))
            after = rows.get(cell_name("analyzer", model, size))
            if not before or not after:
                continue
            b_lm = lm_rows.get(cell_name("none", model, size), {})
            a_lm = lm_rows.get(cell_name("analyzer", model, size), {})
            out.append("| %s | %dk | %s | %s | %s | %s | %s | %s |" % (
                model, size // 1000,
                delta(after["gold"]["morpholex"]["strict"]["f1"],
                      before["gold"]["morpholex"]["strict"]["f1"]),
                delta(after["gold"]["morphynet-inflection"]["strict"]["f1"],
                      before["gold"]["morphynet-inflection"]["strict"]["f1"]),
                delta(after["gold"]["morphynet-derivation"]["strict"]["f1"],
                      before["gold"]["morphynet-derivation"]["strict"]["f1"]),
                delta(after["compounds"]["recall"],
                      before["compounds"]["recall"]),
                delta(after["compression"]["fertility"],
                      before["compression"]["fertility"]),
                delta(a_lm.get("bits_per_character"),
                      b_lm.get("bits_per_character"))))
    return out


def gold_tables(intrinsic):
    rows = index_rows(intrinsic)
    out = []
    for key, label in GOLD_LABELS:
        size = intrinsic["gold_sizes"].get(key)
        out.append("")
        out.append("**%s, %s forms.**" % (label, "{:,}".format(size)
                                          if size else "?"))
        out.append("")
        out.append("| system | P | R | F1 | exact |")
        out.append("|---|--:|--:|---:|------:|")
        for model in MODELS:
            for s in SIZES:
                for condition in CONDITIONS:
                    row = rows.get(cell_name(condition, model, s))
                    if not row:
                        continue
                    strict = row["gold"][key]["strict"]
                    out.append("| %s | %s | %s | %s | %s |" % (
                        cell_name(condition, model, s),
                        fmt(strict["precision"], 3), fmt(strict["recall"], 3),
                        fmt(strict["f1"], 3),
                        fmt(row["gold"][key]["exact_rate"], 3)))
    return out


def verdict(intrinsic, lm):
    """A paragraph per size, stating whether the analyzer helped."""
    rows = index_rows(intrinsic)
    lm_rows = index_rows(lm)
    lines = []
    for size in SIZES:
        parts = []
        for model in MODELS:
            before = rows.get(cell_name("none", model, size))
            after = rows.get(cell_name("analyzer", model, size))
            if not before or not after:
                continue
            f1_b = before["gold"]["morpholex"]["strict"]["f1"]
            f1_a = after["gold"]["morpholex"]["strict"]["f1"]
            bpc_b = lm_rows.get(cell_name("none", model, size), {}).get(
                "bits_per_character")
            bpc_a = lm_rows.get(cell_name("analyzer", model, size), {}).get(
                "bits_per_character")
            piece = ("%s: MorphoLex F1 %.4f to %.4f (%+.4f)"
                     % (model, f1_b, f1_a, f1_a - f1_b))
            if bpc_b is not None and bpc_a is not None:
                piece += (", BPC %.4f to %.4f (%+.4f, %s)"
                          % (bpc_b, bpc_a, bpc_a - bpc_b,
                             "better" if bpc_a < bpc_b else "worse"))
            parts.append(piece)
        if parts:
            lines.append("At %dk, %s." % (size // 1000, "; ".join(parts)))
    return lines


def render(intrinsic, lm):
    out = ["# Matched-vocabulary comparison", ""]
    out.append("Twelve tokenizers: {none, analyzer} pre-tokenization, "
               "{Unigram, BPE} subword model, {8k, 16k, 32k} vocabulary. "
               "Vocabulary size is matched by construction, so the only "
               "variable inside a (model, size) pair is whether the analyzer "
               "ran in front.")
    out.append("")
    out.append("Fertility is pieces per alphabetic word token and "
               "bytes/piece is over the whole unit stream, both on the "
               "BabyLM dev split. BPC is bits per character of the "
               "pretokenized dev text, the same denominator for every row.")
    out.append("")
    out.extend(main_table(intrinsic, lm))
    out.append("")
    out.append("## Analyzer minus none")
    out.append("")
    out.append("A positive difference is the analyzer winning on the four "
               "gold columns. On fertility and on BPC it is the other way "
               "round: fewer pieces per word is better, and fewer bits per "
               "character is better.")
    out.append("")
    out.extend(paired_table(intrinsic, lm))
    out.append("")
    out.append("## Does it help?")
    out.append("")
    for line in verdict(intrinsic, lm):
        out.append(line)
        out.append("")
    out.append("## Morphological alignment in full")
    out.extend(gold_tables(intrinsic))
    out.append("")

    if lm:
        arch = lm["architecture"]
        out.append("## The language models")
        out.append("")
        out.append("Identical for all twelve except the vocabulary: "
                   "%d layers, d_model %d, %d heads, feed-forward %d, "
                   "sequence %d, batch %d, %d steps, learning rate %g with "
                   "%d warmup steps, weight decay %g, gradient clip %g, "
                   "seed %d."
                   % (arch["n_layer"], arch["d_model"], arch["n_head"],
                      arch["d_ff"], arch["seq_len"], arch["batch"],
                      arch["steps"], arch["learning_rate"],
                      arch["warmup_steps"], arch["weight_decay"],
                      arch["grad_clip"], arch["seed"]))
        out.append("")
        out.append("| system | vocab | total params | non-embedding | "
                   "train tokens | wall clock | BPC |")
        out.append("|---|---:|---:|---:|---:|---:|---:|")
        for model in MODELS:
            for size in SIZES:
                for condition in CONDITIONS:
                    row = index_rows(lm).get(cell_name(condition, model, size))
                    if not row:
                        continue
                    out.append("| %s | %d | %s | %s | %s | %.0f s | %s |" % (
                        row["name"], row["vocab_size"],
                        "{:,}".format(row["total_parameters"]),
                        "{:,}".format(row["non_embedding_parameters"]),
                        "{:,}".format(row["train_tokens_seen"]),
                        row["train_seconds"],
                        fmt(row["bits_per_character"])))
        out.append("")
        out.append("The non-embedding column is identical down the table, "
                   "which is the part of the architecture that could be held "
                   "fixed. The embedding table cannot be: a 32k vocabulary "
                   "needs four times the rows of an 8k one, and that "
                   "difference is the experiment, not a confound to remove.")
        out.append("")
    return "\n".join(out) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--matched-dir", default="~/data/tok/matched")
    parser.add_argument("--out", default="results/matched_vocab.md")
    args = parser.parse_args(argv)

    matched = expand(args.matched_dir)
    intrinsic = load(matched, "intrinsic.json")
    if not intrinsic:
        sys.exit("no intrinsic.json; run scripts/score_intrinsic.py first")
    lm = load(matched, "lm.json")
    if not lm:
        print("no lm.json yet; the BPC columns will read n/a", file=sys.stderr)

    text = render(intrinsic, lm)
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write(text)
    print("wrote %s" % args.out, file=sys.stderr)


if __name__ == "__main__":
    main()
