# Is the gain a Unigram artefact?

No. The pre-tokenizer helps **every** subword model at **every** vocabulary
size tested, and Unigram 8k is the weakest case of the twelve, not the best.
Pre-tokenizing the corpus before *training* as well as at inference is,
however, not worth it: it loses to inference-only in 10 of 12 cells. Nor is
registering the markers as added tokens, which never wins and costs up to
0.86 where it changes anything.

Raw numbers: `results/irish/tokenizer_generality.csv` (96 rows: 4 conditions x 12
cells x 2 protocols). Per-cell JSON: `results/irish/tokenizer_generality.json`.
Scripts: `scripts/irish_train_tokenizers.py`,
`scripts/irish_tokenizer_generality.py`. Models and manifest:
`~/data/tok/results/irish/stage3/models/`.

## What was trained

Jane's repo ships trained models but no training script, so the matched set
was trained here: Unigram, BPE and WordPiece at 8k, 16k, 32k and 64k, each in
two conditions (raw corpus, Scalpel-pre-tokenized corpus). 24 cells, 4.6
minutes total. A fourth scoring condition, markers as added tokens, needs no
extra training. Corpus: `data/GA/all_ga_input.txt`, 4,682,103 lines,
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

Word counts differ between conditions because `--exclude-single-token` is
applied per system; `n` is given for plain and for inference-only.

| model | vocab | n (plain / inf) | plain | +Scalpel (inf) | Δ | +Scalpel (train+inf) | Δ | train − inf |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Unigram | 8k | 32,737 / 32,913 | 44.72 | 46.73 | **+2.01** | 46.67 | +1.95 | −0.06 |
| Unigram | 16k | 29,909 / 30,338 | 39.87 | 42.50 | **+2.63** | 42.46 | +2.59 | −0.04 |
| Unigram | 32k | 25,909 / 26,860 | 35.50 | 38.77 | **+3.27** | 38.38 | +2.88 | −0.39 |
| Unigram | 64k | 21,847 / 23,449 | 32.08 | 36.36 | **+4.28** | 35.83 | +3.75 | −0.53 |
| BPE | 8k | 33,797 / 33,950 | 38.83 | 42.74 | **+3.91** | 42.60 | +3.77 | −0.14 |
| BPE | 16k | 31,688 / 32,075 | 34.17 | 38.53 | **+4.36** | 38.36 | +4.19 | −0.17 |
| BPE | 32k | 27,740 / 28,684 | 30.58 | 35.59 | **+5.01** | 35.32 | +4.74 | −0.27 |
| BPE | 64k | 21,722 / 23,675 | 27.57 | 33.55 | **+5.98** | 33.13 | +5.56 | −0.42 |
| WordPiece | 8k | 34,203 / 34,339 | 37.92 | 43.32 | **+5.40** | 43.56 | +5.64 | **+0.24** |
| WordPiece | 16k | 32,393 / 32,744 | 33.54 | 39.61 | **+6.07** | 39.87 | +6.33 | **+0.26** |
| WordPiece | 32k | 28,757 / 29,639 | 30.53 | 36.96 | **+6.43** | 36.61 | +6.08 | −0.35 |
| WordPiece | 64k | 23,057 / 24,847 | 28.03 | 35.02 | **+6.99** | 34.73 | +6.70 | −0.29 |

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
| WordPiece | 16k | 34.67 | 34.65 | −0.02 |
| Unigram | 64k | 30.69 | 30.67 | −0.02 |
| Unigram | 32k, BPE 16k, BPE 32k, WordPiece 8k | | | −0.01 each |

It is the only category that ever regresses. The same −0.01 appears on Jane's
own models at 8k for BPE and WordPiece.

### What actually causes it, and it is mostly not lost boundaries

`scripts/irish_suffix_regression.py`, per-word evidence in
`results/irish/suffix_only_regressions.csv` (28 rows). Two mechanisms can move
a category average, and they turn out to be very unequal here:

| cell | suffix_only words shared | shared words whose status changed | words that *entered* the scored set |
| --- | --- | --- | --- |
| Unigram 8k | 20,134 | 0 | 2 |
| Unigram 32k | 15,463 | **0** | 5 |
| Unigram 64k | 13,049 | **0** | 10 |
| WordPiece 8k | 21,253 | 9 (worse 5, better 4, net −1) | 2 |

For Unigram, **not one shared word loses its suffix boundary at any size.**
The entire movement is a denominator effect: `--exclude-single-token` is
applied per system, so a word the plain back-off left as one token is excluded
from its average, and the same word split by layer 1 is two tokens and so
included. Those entrants are almost all clitic and eclipsis forms
(`b'fhéidir`, `nAchtanna`, `nAirí`, `m'iarratas`, `tIonad`) and they score
0.000 to 0.050 mean suffix credit against 0.307 to 0.415 for the words already
in the set, so adding them pulls the average down a hundredth or two. The
arithmetic reproduces the observed dips.

WordPiece 8k is the only cell with a genuine status change, and it is net −1
word of 21,253. The five that got worse all look like this, where the stem is
handed to the back-off as a shorter string and comes back as one piece instead
of two:

```
b'iomchuí    plain   b ' iom chu í      -> suffix 'í' at a boundary
             scalpel b' iomchuí         -> suffix 'í' no longer at a boundary
m'intinn     plain   m ' int inn        -> suffix 'inn' at a boundary
             scalpel m' intinn          -> gone
```

and the four that got better look like this:

```
nOllscoileanna  plain   n O llscoile anna
                scalpel n Ollscoil eanna  -> matches the gold suffix 'eanna'
```

**A fix the wrapper could make, not implemented here.** The denominator effect
is an artefact of comparing two systems filtered by their own token counts.
The harness already has `--matched-set`, which filters every system by the
*back-off's* token count so all systems are scored on one identical word set.
Rerunning the Stage 3 table under `--matched-set` would remove the entrant
effect entirely and leave only the genuine status changes, which on this
evidence means the Unigram dips would disappear and only WordPiece 8k's net −1
would remain. That is a change to how the comparison is reported rather than
to the pre-tokenizer, so it is described and left for a decision.

## Fertility: assumed versus as encoded

Mean tokens per word over the scored set. Two columns for the inference-only
condition, because the scored stream injects the mutation marker as a single
token and that is not always what a real encoder would produce.

| model | vocab | plain | +Scalpel inf (assumed 1 token) | +Scalpel inf (as encoded) | markers OOV | +Scalpel train | +Scalpel added |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Unigram | 8k | 3.44 | 3.452 | 3.468 | 5 | 3.494 | 3.497 |
| Unigram | 16k | 3.055 | 3.075 | 3.093 | 5 | 3.109 | 3.136 |
| Unigram | 32k | 2.783 | 2.806 | 2.825 | 2 | 2.816 | 2.879 |
| Unigram | 64k | 2.576 | 2.602 | 2.624 | 2 | 2.607 | 2.672 |
| Bpe | 8k | 2.956 | 2.991 | 2.992 | 3 | 2.968 | 2.991 |
| Bpe | 16k | 2.662 | 2.694 | 2.694 | 1 | 2.67 | 2.694 |
| Bpe | 32k | 2.455 | 2.488 | 2.488 | 0 | 2.463 | 2.488 |
| Bpe | 64k | 2.341 | 2.365 | 2.365 | 0 | 2.333 | 2.365 |
| Wordpiece | 8k | 3.142 | 3.176 | 3.177 | 4 | 3.158 | 3.194 |
| Wordpiece | 16k | 2.817 | 2.847 | 2.848 | 3 | 2.826 | 2.847 |
| Wordpiece | 32k | 2.593 | 2.619 | 2.619 | 0 | 2.595 | 2.619 |
| Wordpiece | 64k | 2.449 | 2.465 | 2.465 | 0 | 2.437 | 2.465 |

The gap between assumed and as-encoded is **at most 0.019 tokens per word**
(Unigram 8k, 3.452 against 3.468). It is that small because only 5,419 of the
~33k scored words carry a marker at all, and only some markers are missing
from a given vocabulary. The earlier wording here said true fertility was
"slightly higher" without quantifying it; the number is 0.019 at worst.

**What happens to an out-of-vocabulary marker: pieces, never `[UNK]`.** Every
marker is spellable from characters the vocabulary already has, so nothing is
lost and nothing becomes unknown. WordPiece gives `b'` as `b` + `##'` and
`h-` as `h` + `##-`. BPE and Unigram give `b` + `'`. So a missing marker costs
exactly **one extra token**, never information.

Coverage differs sharply between Jane's shipped models and the ones trained
here:

| models | markers missing, of 16 |
| --- | --- |
| Jane's, all six families, every size | **8** (`B'`, `D'`, `b'`, `d'`, `m'`, `h-`, `n-`, `t-`) |
| trained here, raw corpus, 8k | 3 (BPE), 5 (Unigram), 4 (WordPiece) |
| trained here, raw corpus, 32k | 0 (BPE), 2 (Unigram), 0 (WordPiece) |

Hers are uniformly worse because they were trained on running text, where a
bare `d'` is never a whitespace-delimited unit. The ones here were trained on
a word-per-line corpus with `WhitespaceSplit`, which puts short strings in the
vocabulary much more readily. That is a property of my training setup, not a
result.

## The fourth condition: markers as added tokens

The candidate MT setup. Take the raw-trained model, do not retrain it, and
register the sixteen markers as added tokens so it can spell a boundary it
never saw.

| model | vocab | plain | +Scalpel inf | +Scalpel train | +Scalpel added | added − inf |
| --- | --- | --- | --- | --- | --- | --- |
| Unigram | 8k | 44.72 | 46.73 | 46.67 | 45.87 | -0.86 |
| Unigram | 16k | 39.87 | 42.5 | 42.46 | 41.95 | -0.55 |
| Unigram | 32k | 35.5 | 38.77 | 38.38 | 38.23 | -0.54 |
| Unigram | 64k | 32.08 | 36.36 | 35.83 | 35.81 | -0.55 |
| Bpe | 8k | 38.83 | 42.74 | 42.6 | 42.74 | +0.00 |
| Bpe | 16k | 34.17 | 38.53 | 38.36 | 38.53 | +0.00 |
| Bpe | 32k | 30.58 | 35.59 | 35.32 | 35.59 | +0.00 |
| Bpe | 64k | 27.57 | 33.55 | 33.13 | 33.55 | +0.00 |
| Wordpiece | 8k | 37.92 | 43.32 | 43.56 | 42.87 | -0.45 |
| Wordpiece | 16k | 33.54 | 39.61 | 39.87 | 39.61 | +0.00 |
| Wordpiece | 32k | 30.53 | 36.96 | 36.61 | 36.96 | +0.00 |
| Wordpiece | 64k | 28.03 | 35.02 | 34.73 | 35.02 | +0.00 |

**Added tokens are never better than inference-only, and are worse wherever
they change anything.** In the seven cells where every marker was already in
the vocabulary the two conditions are identical by construction (+0.00). In
the five where markers were genuinely missing, adding them *costs* 0.45 to
0.86 points. The reason is that an added token in the `tokenizers` library
matches anywhere in the string, not only where layer 1 put a boundary, so
registering `h-` or `b'` also re-splits stems that happen to contain them.

That same mechanism makes the naive version of this condition destructive. The
first attempt registered all sixteen markers, including one-character ones
like `b` and `t`: `bunreacht` came out as nine single-character pieces and
fertility went from 2.96 to 6.50. The condition is only viable restricted to
the genuinely missing multi-character markers, which is what the numbers above
use.

So on this metric the ranking is: **inference-only > added tokens > training
on split text**. No default is set for MT here; that is Jane's and Oscar's
call, and CríochScore is not the only thing that matters for it.

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

## Category sizes

`uru_only` rests on **27 words** under her protocol. That is why it moves in
whole steps (50.00, 82.69, 100.00) and why a 100.00 there is not a strong
result: it is 27 words out of 33,480. Every category size, from
`results/irish/stage2_clitics.json` (UnigramLM 8k, hybrid, her protocol):

| category | words | share of the 33,480 scored |
| --- | --- | --- |
| uru_only | **27** | 0.08% |
| prefix_only | 760 | 2.3% |
| suffix_only | 20,633 | 61.6% |
| uru+prefix | 210 | 0.6% |
| uru+suffix | 3,432 | 10.3% |
| prefix+suffix | 6,726 | 20.1% |
| all_affixes | 1,692 | 5.1% |

The seven partition the scored set exactly. Only `suffix_only`,
`prefix+suffix` and `uru+suffix` have denominators large enough for a
hundredth of a point to mean anything; `uru_only` and `uru+prefix` move by
whole words. The harness prints these counts on every run.

## Mingram

Not found. See the TODO in `NOTES.md`: it appears nowhere in
`jndkns01/tknzn4Irish`, in the working tree at `0f31695` or in the full git
history, and nothing has been substituted for it.
