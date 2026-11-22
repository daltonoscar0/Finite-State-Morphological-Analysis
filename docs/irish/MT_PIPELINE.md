# Tokenizing the MT data

One command, any Stage 3 configuration, lossless. Ready for Jane's parallel
data the day it arrives.

## The commands

List the configurations (84 of them):

```
python3 scripts/tokenize_mt.py --list
```

Tokenize:

```
python3 scripts/tokenize_mt.py \
    --input  /path/to/ga.txt \
    --config unigram8k-scalpel \
    --output /path/to/ga.tok
```

Tokenize and check the round trip at the same time (reports every line that
fails, not a pass rate):

```
python3 scripts/tokenize_mt.py \
    --input  /path/to/ga.txt \
    --config unigram8k-scalpel \
    --output /path/to/ga.tok \
    --round-trip
```

Detokenize:

```
python3 scripts/tokenize_mt.py \
    --input  /path/to/ga.tok \
    --config unigram8k-scalpel \
    --output /path/to/ga.detok \
    --detokenize
```

Input is one sentence per line. Output is one line per input line, tokens
separated by a single space.

## Configuration names

`configs/mt_configs.json`, regenerate with `python3 scripts/make_mt_configs.py`.

| name pattern | meaning |
| --- | --- |
| `<model><size>-plain` | Stage 3 matched model, no Scalpel |
| `<model><size>-scalpel` | Stage 3 matched model, Scalpel at inference |
| `<model><size>-scalpeltrained` | model trained on the pre-tokenized corpus, Scalpel at inference |
| `jane-<family><size>-plain` | Jane's shipped model, no Scalpel |
| `jane-<family><size>-scalpel` | Jane's shipped model, Scalpel at inference |

The fourth scoring condition in `tokenizer_generality.md`, markers as added
tokens, is not a config here: it was measured and is never better than
inference-only, so there was no reason to ship it as an option.

`<model>` is `unigram`, `bpe` or `wordpiece`; `<size>` is `8k`, `16k`, `32k`,
`64k`. Jane's families are `unigramlm`, `bpe`, `byte-bpe`,
`sentencepiecebpe`, `sentencepieceulm`, `wordpiece`, with `43k` instead of
`64k` for `sentencepieceulm`.

**On the metric, inference-only is the best of the three** we can measure:
it beats training-time pre-tokenization in 10 of 12 cells, and beats
registering the markers as added tokens in every cell where that changes
anything (by 0.45 to 0.86). `unigram8k-scalpel` is also the configuration the
reproduced baseline rows use. See `tokenizer_generality.md`.

**No default is set here for the MT run.** CríochScore is a
morpheme-boundary metric, not a translation metric, and the eight markers
missing from Jane's vocabularies are a real representational gap even though
they cost only one extra token each and never an `[UNK]`. That trade is for
Jane and Oscar to settle.

## Losslessness

`detok(tok(x)) == x` is exact on **all 12,000 lines** of
`results/irish/roundtrip_corpus.txt`, for every configuration tested:

| config | lines | failures | lines/s |
| --- | --- | --- | --- |
| unigram8k-plain | 12,000 | 0 | 10,740 |
| unigram8k-scalpel | 12,000 | 0 | 9,691 |
| bpe8k-scalpel | 12,000 | 0 | 10,340 |
| wordpiece8k-scalpel | 12,000 | 0 | 12,207 |
| wordpiece8k-scalpeltrained | 12,000 | 0 | 12,403 |
| jane-unigramlm8k-scalpel | 12,000 | 0 | 9,220 |
| jane-wordpiece8k-scalpel | 12,000 | 0 | 11,887 |
| jane-sentencepieceulm8k-scalpel | 12,000 | 0 | 11,847 |
| jane-sentencepiecebpe8k-scalpel | 12,000 | 0 | 12,330 |
| jane-byte-bpe8k-scalpel | 12,000 | 0 | 10,276 |
| unigram8k-scalpeltrained | 12,000 | 0 | 6,561 |

The separate detokenize invocation also reproduces the input file byte for
byte (`cmp` clean), not just line by line in memory.

Losslessness is by construction rather than by convention: the pre-tokenizer
emits a *partition* of the line, so concatenating the units returns the input
exactly, and the family's word-boundary convention is applied as a separately
invertible step. See the module docstring in `scripts/ga_pipeline.py`.

### The test corpus, and its one caveat

`results/irish/roundtrip_corpus.txt`, built by
`scripts/make_roundtrip_corpus.py` (seed 20261118). Coverage of the required
categories, counted over the written lines:

| category | lines |
| --- | --- |
| punctuation stuck to a word | 10,294 |
| hyphenated forms | 3,390 |
| capitalised mutated forms (`nGaeilge`, `tSeáin`, `hÉireann`) | 2,810 |
| `d'`/`m'`/`b'` with a straight apostrophe | 2,504 |
| `d'`/`m'`/`b'` with a **curly** apostrophe | 2,216 |

**Caveat.** There is no Irish sentence corpus in this checkout. The lines are
reconstructed from `data/GA/all_ga_input.txt`, which is 4,682,103 lines of
one word per line, in original order, with punctuation still attached
(`áirithe,`, `nGaeilge.`, `d’imeacht.`). So consecutive lines are consecutive
words of real Irish and joining a run of them gives a real line rather than
an invented one, but the *line breaks* and the whitespace are mine, not the
source's. Twelve adversarial lines are appended by hand for whitespace the
word-per-line corpus cannot supply (tabs, double spaces, leading and trailing
space, empty and whitespace-only lines).

The UD Irish-IDT treebank that `attested_mutations.tsv` was mined from would
have given real sentence text through its `# text =` lines, but it is **not on
this machine**: a search of `~` (excluding Library, node_modules, .venv and
site-packages) for `*UD_Irish*`, `*ga_idt*` and `*.conllu` returns nothing. So
the reconstructed corpus stands.

**Rerun the round trip on Jane's data when it lands.** Her line breaks and
whitespace are the real test, and `--round-trip` is one flag. Note that the
leading/trailing-space bug below was found by this corpus and not by hand, so
the exercise is worth repeating on real input.

### A bug this found

The convention families (`SentencePiece*`, `Byte-BPE`, `WordPiece`) dropped a
**leading or trailing single space**, 2 lines in 12,000. A single space
between two chunks is carried implicitly by the next chunk's word-initial
marker, or by the absence of a continuation marker, so it is not emitted; but
a space at the start or end of a line has no neighbouring chunk to carry it.
Fixed by only taking the implicit route when the space sits between two
chunks. `tools/test_ga_pipeline.py` now covers it.

## Determinism

- Byte-identical output across runs: verified by running
  `unigram8k-scalpel` over the 12,000-line corpus twice and comparing
  sha256 (`51a0afa9e852180afc402f99…` both times).
- No RNG is consulted at tokenization time. `Tokenizer.encode` takes the
  Viterbi best path for Unigram and does no subword sampling, and neither
  BPE nor WordPiece has a stochastic mode. The seed recorded in the sidecar
  (20261118) pins the *training* run, not this one.
- Model files are pinned by sha256 in `configs/mt_configs.json` and the
  digest of the model actually used is written to the sidecar on every run.

## The sidecar

Every run writes `<output>.meta.json`:

```json
{
  "config": "unigram8k-scalpel",
  "family": "UnigramLM",
  "model_path": ".../plain_unigram_8000.json",
  "model_sha256": "81b6754978fa3a6547ffa700e3444debd8d52ec169d523c453599a0ecd495e5a",
  "scalpel": true,
  "n_lines": 12000,
  "n_tokens": 463369,
  "lines_per_second": 10684.4,
  "train_seed": 20261118,
  "round_trip_checked": false,
  "versions": {"python": "3.9.6", "tokenizers": "0.22.2", "sentencepiece": "0.2.2"}
}
```

## Throughput, and how long a full run takes

**4,600 to 12,400 lines per second**, single process, on this machine, across
two measurement rounds. The spread is mostly machine load, not configuration:
the same `unigram8k-scalpel` config measured 9,691 lines/s at load average
3.9 and 6,554 lines/s at load average 8.6. Scalpel itself costs about 10%
(`unigram8k-plain` 10,740 against `unigram8k-scalpel` 9,691 in the same
round). Detokenization is about 46,000 lines/s.

Treat all of these as working figures rather than a clean benchmark. At a
conservative 5,000 lines/s:

| corpus size | wall clock at 5,000 l/s | at 10,000 l/s |
| --- | --- | --- |
| 100,000 lines | ~20 seconds | ~10 seconds |
| 1 million lines | ~3.3 minutes | ~1.7 minutes |
| 10 million lines | ~33 minutes | ~17 minutes |

So any plausible size of parallel corpus is a single-digit-minutes job, and
the FST pass over the vocabulary is amortised: layer 1 is run once over the
distinct word cores in the input, not per line.

**The analyser no longer aborts on anything** (fixed 22 November, decision D8
in `NOTES.md`). `scripts/ga_mutation_split.py` keeps its bisection as a safety
net, but a set-aside word is now reported on stderr and makes the run exit
non-zero unless `--allow-skips` is passed, so a run cannot quietly tokenize
fewer words than it was handed. On the full GA corpus the skip list is empty:
272,349 distinct forms, all 272,349 analysed. If Jane's data ever produces a
skip, the run will say so and stop rather than carry on.

## Dependencies

```
pip install -r requirements-harness.txt
pip install -e external/tknzn4Irish
```

The pre-tokenizer itself (`ga_pipeline`, `ga_mutation_split`, `ga_clitic`,
`ga_lenition_prefix`) is standard library only. The pinned packages are
needed only to load the subword models.
