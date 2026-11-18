"""Does the boundary Scalpel inserts survive into each tokenizer's output?

Three things can go wrong between layer 1 and the final token sequence, and
they are different per family because each has its own word-boundary
convention (SentencePiece's '▁' prefix, byte-level BPE's 'Ġ', WordPiece's
'##' continuation):

1. **Re-merged.** The mutation marker is emitted as its own token, but if the
   marker string is not in the tokenizer's vocabulary the downstream encoder
   will split or merge it and the boundary moves. Checked by looking the
   marker up in the vocabulary of each model.
2. **Lost.** The tokens no longer reconstruct the surface, so the boundary is
   in the wrong place. Checked by concatenating the normalised tokens and
   comparing against the word.
3. **Stray marker mid-word.** A convention marker ('▁', 'Ġ', '##') surviving
   inside the sequence, or missing where the convention requires it. The
   scored stream is normalised by Jane's ``remove_specials``, so a stray
   marker there would corrupt the comparison with the gold; a *missing*
   continuation marker does not affect scoring but does make the sequence
   ill-formed for real downstream use, which is what Stage 4 needs.

Reports per family and per size. Nothing here changes behaviour.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(REPO / "src"))
sys.path.insert(0, str(ROOT / "scripts"))

from ga_clitic import add_clitic_readings, CLITIC_TAG            # noqa: E402
from ga_mutation_split import SPLIT_TAGS, demutate_all, mutation_splits  # noqa: E402
from moirfeolas_score import FAMILIES, MOIRFEOLAS, make_backoff  # noqa: E402

from criochscore.moirfeolas import load_moirfeolas               # noqa: E402
from criochscore.tokenizers_io import load_tokenizer             # noqa: E402

CONVENTION_MARKERS = ("▁", "Ġ", "##", "_")


def vocabulary(family, size):
    """The model's own vocabulary, as a set of surface pieces."""
    fname, model_type, _ = FAMILIES[family]
    path = REPO / "tokenizers" / family / fname.format(size=size)
    if not path.exists():
        return None
    tok = load_tokenizer(path, model_type)
    if model_type == "sentencepiece":
        return {tok.id_to_piece(i) for i in range(tok.get_piece_size())}
    return set(tok.get_vocab())


def marker_is_known(marker, vocab, special):
    """Is the marker a single piece the model already has?

    Tried bare and with the family's word-initial convention marker, because
    a mutation marker is word-initial by definition.
    """
    if vocab is None:
        return None
    if marker in vocab:
        return True
    if special and special != "##" and (special + marker) in vocab:
        return True
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--families", nargs="+", default=list(FAMILIES))
    ap.add_argument("--sizes", nargs="+", default=["8k"])
    ap.add_argument("--moirfeolas", default=str(MOIRFEOLAS))
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    affix = load_moirfeolas(args.moirfeolas)
    words = list(affix)
    demut = demutate_all(words)
    demut, _ = add_clitic_readings(words, demut)
    splits = mutation_splits(words, demut, (CLITIC_TAG,) + SPLIT_TAGS)
    print(f"{len(splits)} words carry a layer-1 boundary\n")

    markers = sorted({m for _, m, _ in splits.values()})
    print(f"distinct markers layer 1 emits: {', '.join(repr(m) for m in markers)}\n")

    report = {}
    hdr = (f"{'family':<18}{'size':>5}{'split':>7}{'rebuilt':>9}"
           f"{'stray':>7}{'nocont':>8}{'mk_ok':>7}{'mk_bad':>8}")
    print(hdr)
    for family in args.families:
        _, _, special = FAMILIES[family]
        for size in args.sizes:
            encode = make_backoff(family, size)
            if encode is None:
                continue
            vocab = vocabulary(family, size)

            # What the scored hybrid actually hands the metric.
            to_encode = sorted({splits[w][2] if w in splits else w
                                for w in words})
            enc = encode(to_encode)

            rebuilt = stray = nocont = 0
            bad_examples, stray_examples = [], []
            for w in sorted(splits):
                _, marker, base = splits[w]
                toks = [marker] + enc[base]
                if "".join(toks) == w:
                    rebuilt += 1
                elif len(bad_examples) < 5:
                    bad_examples.append({"word": w, "tokens": toks})

                # A convention marker surviving inside the normalised stream.
                hit = [t for t in toks
                       if any(c in t for c in CONVENTION_MARKERS)]
                if hit:
                    stray += 1
                    if len(stray_examples) < 5:
                        stray_examples.append({"word": w, "tokens": toks})

                # WordPiece requires '##' on every token after the first.
                # enc[base] came from encoding base as a standalone word, so
                # its first piece has none. Harmless for scoring, ill-formed
                # for real use.
                if special == "##" and len(toks) > 1:
                    nocont += 1

            mk_ok = [m for m in markers
                     if marker_is_known(m, vocab, special)]
            mk_bad = [m for m in markers
                      if marker_is_known(m, vocab, special) is False]

            print(f"{family:<18}{size:>5}{len(splits):>7}{rebuilt:>9}"
                  f"{stray:>7}{nocont:>8}{len(mk_ok):>7}{len(mk_bad):>8}")
            report[f"{family}_{size}"] = {
                "n_split": len(splits), "rebuilt": rebuilt,
                "stray_marker": stray, "missing_continuation": nocont,
                "markers_in_vocab": mk_ok, "markers_absent": mk_bad,
                "rebuild_failures": bad_examples,
                "stray_examples": stray_examples,
            }

    print("\nsplit    = words with a layer-1 boundary")
    print("rebuilt  = token stream concatenates back to the surface")
    print("stray    = a convention marker left inside the normalised stream")
    print("nocont   = WordPiece tokens missing the '##' continuation marker")
    print("mk_ok/bad= layer-1 markers present / absent in the vocabulary")

    if args.out:
        d = Path(args.out)
        d.parent.mkdir(parents=True, exist_ok=True)
        d.write_text(json.dumps(report, ensure_ascii=False, indent=2))
        print(f"\nwrote {d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
