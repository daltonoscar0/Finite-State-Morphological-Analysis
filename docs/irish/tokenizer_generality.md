# Is the gain a Unigram artefact?

No. The pre-tokenizer helps **every** subword model at **every** vocabulary
size tested, and Unigram 8k is the weakest case of the twelve, not the best.
Pre-tokenizing the corpus before *training* as well as at inference is,
however, not worth it: it loses to inference-only in 10 of 12 cells.

Raw numbers: `results/irish/tokenizer_generality.csv` (72 rows, both
protocols). Per-cell JSON: `results/irish/tokenizer_generality.json`.
Scripts: `scripts/irish_train_tokenizers.py`,
`scripts/irish_tokenizer_generality.py`. Models and manifest:
`~/data/tok/results/irish/stage3/models/`.

## What was trained

Jane's repo ships trained models but no training script, so the matched set
was trained here: Unigram, BPE and WordPiece at 8k, 16k, 32k and 64k, each in
two conditions (raw corpus, Scalpel-pre-tokenized corpus). 24 cells, 4.6
minutes total. Corpus: `data/GA/all_ga_input.txt`, 4,682,103 lines,
272,349 distinct forms. Everything except the input stream is held identical
across cells: same sample, same seed (20261118), same special tokens, same
`WhitespaceSplit` pre-tokenizer, same trainer library.

These absolute numbers are **not** comparable to Jane's published rows. Her
models were trained by her with her own settings; these exist only so the
three conditions differ in exactly one thing. Her shipped models remain the
reference, and they still reproduce her rows exactly (see `NOTES.md`).

The vocabulary sizes are the ones her repo ships: 8k, 16k, 32k, 64k for BPE,
Byte-BPE, UnigramLM, WordPiece and SentencePieceBPE; SentencePieceULM has
8k, 16k, 32k and 43k instead of 64k.

## Headline: the gain holds everywhere

Overall CríochScore, her protocol (`--exclude-single-token`). `inf` is
Scalpel at inference only, `train` is Scalpel at training and inference.

| model | vocab | plain | +Scalpel (inf) | Δ | +Scalpel (train+inf) | Δ | train − inf |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Unigram | 8k | 44.72 | 46.73 | **+2.01** | 46.67 | +1.95 | −0.06 |
| Unigram | 16k | 39.87 | 42.50 | **+2.63** | 42.46 | +2.59 | −0.04 |
| Unigram | 32k | 35.50 | 38.77 | **+3.27** | 38.38 | +2.88 | −0.39 |
| Unigram | 64k | 32.08 | 36.36 | **+4.28** | 35.83 | +3.75 | −0.53 |
| BPE | 8k | 38.83 | 42.74 | **+3.91** | 42.60 | +3.77 | −0.14 |
| BPE | 16k | 34.17 | 38.53 | **+4.36** | 38.36 | +4.19 | −0.17 |
| BPE | 32k | 30.58 | 35.59 | **+5.01** | 35.32 | +4.74 | −0.27 |
| BPE | 64k | 27.57 | 33.55 | **+5.98** | 33.13 | +5.56 | −0.42 |
| WordPiece | 8k | 37.92 | 43.32 | **+5.40** | 43.56 | +5.64 | **+0.24** |
| WordPiece | 16k | 33.54 | 39.61 | **+6.07** | 39.87 | +6.33 | **+0.26** |
| WordPiece | 32k | 30.53 | 36.96 | **+6.43** | 36.61 | +6.08 | −0.35 |
| WordPiece | 64k | 28.03 | 35.02 | **+6.99** | 34.73 | +6.70 | −0.29 |

Two patterns worth the paper:

- **The gain grows with vocabulary size.** +2.01 at Unigram 8k, +6.99 at
  WordPiece 64k. A bigger vocabulary absorbs more mutations into whole-word
  pieces, so there is more for the pre-tokenizer to recover.
- **The gain is smallest for Unigram.** Unigram's own segmentation already
  respects some mutation boundaries; BPE and WordPiece do not. Reporting only
  Unigram 8k understates the method.

Across the six families Jane ships, at 8k, the same holds (her models, her
protocol, `--systems backoff hybrid`): BPE 35.85→40.98, Byte-BPE
38.19→44.00, SentencePieceBPE 38.05→43.86, SentencePieceULM 42.84→47.58,
UnigramLM 43.32→45.98, WordPiece 35.23→41.69.

## Training-time pre-tokenization is not worth it

`scalpel_train` beats `scalpel_inf` in **2 of 12** cells (WordPiece 8k and
16k, by +0.24 and +0.26) and loses the other 10, by up to −0.53. Reported
plainly because the expectation going in was the opposite.

What it does buy is marker coverage. Eight of the sixteen markers layer 1
emits are absent from every one of Jane's six vocabularies at every size
(`d'`, `D'`, `b'`, `B'`, `m'`, `h-`, `n-`, `t-`; see
`results/irish/boundary_survival.json`). Training on the pre-tokenized corpus
puts most of them in, at 8k:

| model | markers in vocab, plain | markers in vocab, scalpel-trained |
| --- | --- | --- |
| BPE | 13 / 16 | 15 / 16 |
| Unigram | 11 / 16 | 13 / 16 |
| WordPiece | 12 / 16 | 14 / 16 |

The ones still missing are the rarest (`m'` 5 occurrences in MoirfEolas,
`B'` 3, `h-`). So the training condition fixes the representational problem
and still loses on the metric, which says the cost is elsewhere: splitting
the corpus removes longer pieces from the vocabulary, and `prefix_only` and
`all_affixes` drop accordingly (e.g. BPE 64k `prefix_only` 41.46 → 38.28).

## Where it is worse, reported plainly

`suffix_only` falls under the pre-tokenizer in **9 of 12** cells:

| model | vocab | plain | +Scalpel (inf) | Δ |
| --- | --- | --- | --- | --- |
| WordPiece | 64k | 31.85 | 31.74 | −0.11 |
| WordPiece | 32k | 33.22 | 33.17 | −0.05 |
| BPE | 64k | 26.72 | 26.68 | −0.04 |
| WordPiece | 16k | 34.67 | 34.64 | −0.03 |
| Unigram | 64k | 30.69 | 30.67 | −0.02 |
| Unigram | 32k, BPE 16k, BPE 32k, WordPiece 8k | | | −0.01 each |

Small, but consistent and one-directional, so it is a real effect rather than
noise: fixing a boundary at the front of the word changes how the back-off
tokenizes the remainder, and occasionally it loses a suffix boundary it would
otherwise have found. It is the only category that ever regresses. On Jane's
own models at 8k the same thing appears, at −0.01 for BPE and WordPiece.

## Fertility

Mean tokens per word over the scored set. The pre-tokenizer costs almost
nothing.

| model | vocab | plain | +Scalpel (inf) | +Scalpel (train+inf) |
| --- | --- | --- | --- | --- |
| Unigram | 8k | 3.44 | 3.45 | 3.49 |
| Unigram | 64k | 2.58 | 2.60 | 2.61 |
| BPE | 8k | 2.96 | 2.99 | 2.97 |
| BPE | 64k | 2.34 | 2.37 | 2.33 |
| WordPiece | 8k | 3.14 | 3.18 | 3.16 |
| WordPiece | 64k | 2.45 | 2.46 | 2.44 |

One caveat on the `scalpel_inf` column: it counts the injected marker as a
single token, which is what the metric sees but not what a real encoder would
produce for the eight markers absent from the vocabulary. True fertility
under `scalpel_inf` is therefore slightly higher than shown. Under
`scalpel_train` the markers are mostly in the vocabulary, so that column is
the honest one.

## Absorption

Urú absorption (Jane's `uru_absorbed_%`: the share of urú material swallowed
inside a token rather than exposed at a boundary), her protocol, 8k:

| model | plain | +Scalpel (inf) | +Scalpel (train+inf) |
| --- | --- | --- | --- |
| Unigram | 27.74 | 0.30 | 0.00 |
| BPE | 50.67 | 0.02 | 0.02 |
| WordPiece | 72.39 | 0.06 | 0.06 |

This is the mechanism behind the whole result, stated directly: WordPiece 8k
buries 72% of all urú material inside a token, and the pre-tokenizer takes
that to essentially zero. It also explains why WordPiece gains most and
Unigram least: Unigram was already absorbing the least.

## Boundary survival per family

`scripts/irish_boundary_survival.py`, over the 5,419 words layer 1 splits:

| family | stream rebuilds the word | stray convention marker | missing '##' | markers in vocab |
| --- | --- | --- | --- | --- |
| BPE | 5,419 / 5,419 | 0 | n/a | 8 / 16 |
| Byte-BPE | 5,419 / 5,419 | 0 | n/a | 8 / 16 |
| SentencePieceBPE | 5,419 / 5,419 | 0 | n/a | 8 / 16 |
| SentencePieceULM | 5,419 / 5,419 | 0 | n/a | 8 / 16 |
| UnigramLM | 5,419 / 5,419 | 0 | n/a | 8 / 16 |
| WordPiece | 5,419 / 5,419 | 0 | **5,419** | 8 / 16 |

So the boundary is never lost and never accompanied by a stray marker, but
two things needed fixing for anything beyond scoring:

1. WordPiece got no `##` on the stem, on every split word, because the hybrid
   encodes the stem as a standalone word. Invisible to CríochScore, which
   strips `##` anyway; ill-formed for a model.
2. The eight absent markers would be re-split by a real encoder, moving the
   boundary.

Both are fixed in `scripts/ga_pipeline.py`, which emits a convention-correct
stream per family and encodes each unit separately so a boundary cannot be
re-merged. 14 tests in `tools/test_ga_pipeline.py`, including one per family
for each of the three failure modes.

## Small denominators

`uru_only` rests on 27 words under her protocol, which is why it moves in
whole steps (50.00, 82.69, 100.00) and why a 100.00 there should not be read
as a strong result. The harness prints the per-category counts.

## Mingram

Not found. See the TODO in `NOTES.md`: it appears nowhere in
`jndkns01/tknzn4Irish`, in the working tree at `0f31695` or in the full git
history, and nothing has been substituted for it.
