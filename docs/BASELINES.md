# Standard subword baselines, measured against the analyzer

This is the baseline half of the tokenizer comparison. It trains BPE,
WordPiece and Unigram at three vocabulary sizes on the same corpus, and scores
all nine of them plus the analyzer under four lexicon settings with one
scorer, on one word list, so the numbers can be put in the same table.

No language model is trained here. Everything below is intrinsic: compression,
round trip, vocabulary use, and agreement with two morphological gold
standards.

## Reproducing it

```bash
make && make test
python3 tools/make_splits.py                         # about 4 minutes
~/venvs/tok/bin/python tools/train_baselines.py      # about 20 minutes
~/venvs/tok/bin/python tools/score_tokenizer.py      # about 8 minutes
~/venvs/tok/bin/python tools/suffix_report.py \
    --lexicon data/closed_class.tsv \
    --lexicon ~/data/morph/lexicons/en_100k.tsv --derived-first
```

`make_splits.py` and the analyzer side are standard library only.
`train_baselines.py` and `score_tokenizer.py` need the virtualenv, for the
libraries the baselines are trained with and for the spreadsheet reader
MorphoLex needs. Results land in `~/data/tok/results/baselines.json` and
`disagreements.tsv`, and the per-suffix report in `suffix_report.json`.
Nothing is written inside the repository.

The second half of this document, after the baseline tables, is the run with
the derivation fixes in, which writes `baselines_v2.json`.

## Environment

| Piece | Version |
|-------|---------|
| Python | 3.9.6, in `~/venvs/tok`, outside the repository |
| `sentencepiece` | 0.2.2 |
| `tokenizers` (Hugging Face) | 0.22.2 |

Nothing else was installed. The analyzer side of the comparison runs on the
standard library alone, as `tools/fst_tokenizer.py` always has.

## Data

| What | Where | Size |
|------|-------|------|
| Training text | `~/data/corpora/babylm/train_100M.txt` | 538,254,632 bytes, 11.6M lines |
| Evaluation text | `~/data/corpora/babylm/test.txt` | 51,457,874 bytes, 1.11M lines |
| Lexicons | `~/data/morph/lexicons/en_100k.tsv`, `en_all.tsv` | |
| Gold, independent | `~/data/morph/morpholex/MorphoLex-en/MorphoLEX_en.xlsx` | |
| Gold, in distribution | `~/data/morph/morphynet/eng.inflectional.v1.tsv`, `eng.derivational.v1.tsv` | |

The corpora are read and never written to or copied into the repository. All
outputs go under `~/data/tok/`.

No tokenizer saw `test.txt` during training.

## The shared pretokenizer

`tools/pretokenize.py`. Whitespace separates units and is not itself a unit.
Between two runs of whitespace, a unit is a maximal run of characters from one
of three classes:

| Class | Pattern |
|-------|---------|
| letters | `[^\W\d_]+` |
| digits | `\d+` |
| other | `(?:[^\s\w]|_)+` |

Nothing is lowercased, nothing is stripped, and no character other than the
separating whitespace is dropped. So `don't` is three units, `don`, `'`, `t`;
`U.S.A.` is six; `3.14` is three; `cats,` is two; and `42kg` is two.

An **alphabetic word token** is a unit the letters branch matched. That is the
unit every system is scored on.

`tools/make_splits.py` applies the rule once and writes what everything else
reads:

| File | Contents |
|------|----------|
| `~/data/tok/splits/train_pretok.txt.gz` | one line per training line, units joined by single spaces |
| `~/data/tok/splits/test_words.txt.gz` | the alphabetic word tokens of `test.txt`, in order |
| `~/data/tok/splits/test_types.tsv` | the same tokens as a type list with counts |
| `~/data/tok/splits/test_units.tsv` | every test unit with counts, punctuation and digits included |

The baselines are trained on `train_pretok.txt.gz` with their own pre-tokenizers
set to plain whitespace splitting, which is the only way to be certain all
three saw the same units.

### Test split

| | |
|---|---|
| alphabetic word tokens | 9,647,333 |
| alphabetic word types | 125,852 |
| all units | 13,153,999 |
| distinct characters | 370 |

## Training the baselines

`tools/train_baselines.py`. Nine models, each trained in its own subprocess,
one at a time, so the time and memory reported for each is its own.

Shared by all three: input is `train_pretok.txt.gz`, each library's own
splitting is plain whitespace over those units, no normalization, special
tokens `<unk>`, `<s>`, `</s>`, `<pad>` at ids 0 to 3, and full byte coverage.

| | BPE | WordPiece | Unigram |
|---|---|---|---|
| library | `tokenizers` | `tokenizers` | `sentencepiece` |
| byte coverage | `byte_fallback=True`, with the 256 `<0xXX>` pieces forced into the vocabulary | byte level pre-tokenizer, `initial_alphabet` the 256 byte symbols, both forms of each filled in after training | `byte_fallback=True` |
| marker | none | `##` on continuations | `▁` on the first piece |
| normalization | none | none | `normalization_rule_name=identity`, `remove_extra_whitespaces=False` |
| input | all 11,588,684 lines | all 11,588,684 lines | a fixed random sample of 5,000,000 lines, seed 20251013 |

Everything not in that table is the library default for the version named
above.

Two details worth recording. `max_sentence_length` was raised to 24,000 bytes
for sentencepiece: the default of 4,192 would have silently dropped the 382
training lines longer than that. And of the 5,000,000 sampled lines
sentencepiece loaded 4,943,582, having dropped the blank ones, which are
blank lines of the original corpus that hold no units.

The sample is drawn by selection sampling in one pass over the file, so it is
exactly 5,000,000 lines, uniform, and identical on every run for a given seed.
All three Unigram sizes train on the same sample.

| model | nominal | actual vocabulary | wall time | peak RSS |
|-------|---------|-------------------|-----------|----------|
| BPE 8k | 8,000 | 8,000 | 37.4 s | 472 MB |
| BPE 16k | 16,000 | 16,000 | 39.3 s | 504 MB |
| BPE 32k | 32,000 | 32,000 | 50.6 s | 449 MB |
| WordPiece 8k | 8,000 | 8,074 | 56.6 s | 403 MB |
| WordPiece 16k | 16,000 | 16,074 | 71.4 s | 408 MB |
| WordPiece 32k | 32,000 | 32,074 | 78.0 s | 382 MB |
| Unigram 8k | 8,000 | 8,000 | 331.4 s | 3.7 GB |
| Unigram 16k | 16,000 | 16,000 | 283.9 s | 4.3 GB |
| Unigram 32k | 32,000 | 32,000 | 251.3 s | 5.7 GB |

Total training time is 20 minutes, of which Unigram is 15. The WordPiece
sizes are 74 over nominal: those are the byte forms the trainer left out, see
decision 4.

## Scoring

`tools/score_tokenizer.py` reaches all thirteen systems through one interface,
`encode(word) -> list[str]`, and computes every metric the same way for each.

A piece is a vocabulary entry. Its **surface** is the text it contributes to
the word: `##iness` contributes `iness`, `▁cit` contributes `cit`, and a byte
fallback piece contributes part of a character. Surfaces are what every metric
is computed on, so a marker never counts as a character and never moves a
boundary. Each system's surfaces are checked against its library's own decoder
on all 490,133 strings encoded, and the two agree everywhere.

### Compression and round trip

9,647,333 alphabetic word tokens, 125,852 types. `pcs/token` is pieces per
word token weighted, `pcs/type` is the same per type. `pcs/1kB` is pieces per
1000 bytes of raw `test.txt` with the whole pretokenized stream encoded,
punctuation and digits included.

| system | vocab | pcs/token | pcs/type | chr/pc | byte/pc | pcs/1kB | round trip |
|--------|------:|----------:|---------:|-------:|--------:|--------:|-----------:|
| bpe-8k | 8,000 | 1.178 | 2.979 | 3.341 | 3.343 | 289.76 | 100% |
| bpe-16k | 16,000 | 1.099 | 2.623 | 3.582 | 3.583 | 274.59 | 100% |
| bpe-32k | 32,000 | 1.053 | 2.284 | 3.737 | 3.738 | 265.87 | 100% |
| wordpiece-8k | 8,074 | 1.187 | 3.111 | 3.315 | 3.316 | 291.68 | 100% |
| wordpiece-16k | 16,074 | 1.107 | 2.734 | 3.556 | 3.558 | 276.17 | 100% |
| wordpiece-32k | 32,074 | 1.057 | 2.365 | 3.722 | 3.724 | 266.72 | 100% |
| unigram-8k | 8,000 | 1.178 | 3.177 | 3.341 | 3.342 | 290.24 | 100% |
| unigram-16k | 16,000 | 1.101 | 2.795 | 3.574 | 3.576 | 275.52 | 100% |
| unigram-32k | 32,000 | 1.056 | 2.427 | 3.725 | 3.727 | 266.94 | 100% |
| analyzer-builtin | n/a | 1.052 | 1.055 | 3.739 | 3.741 | 265.45 | 100% |
| analyzer-en_100k | n/a | 1.106 | 1.255 | 3.558 | 3.559 | 275.54 | 100% |
| analyzer-en_100k-derived | n/a | 1.136 | 1.279 | 3.463 | 3.465 | 281.18 | 100% |
| analyzer-en_all-derived | n/a | 1.137 | 1.286 | 3.462 | 3.463 | 281.28 | 100% |

Round trip is exact for all thirteen, on all 490,133 strings encoded, which is
every test unit plus every gold form. No offenders to print.

The type-weighted column is where the two families separate. Every baseline
shatters rare words into three or more pieces, because a rare word is not in a
32k vocabulary; the analyzer gives 1.29 pieces per type at worst, because a
word it cannot analyze is emitted whole. The token-weighted column hides that:
frequent words are short and in every vocabulary.

### Vocabulary use

| system | vocab | used on test | used % | word tokens emitted whole | distinct pieces emitted on words |
|--------|------:|-------------:|-------:|--------------------------:|---------------------------------:|
| bpe-8k | 8,000 | 7,392 | 92.4% | 88.3% | 7,053 |
| bpe-16k | 16,000 | 15,292 | 95.6% | 93.1% | 14,760 |
| bpe-32k | 32,000 | 30,604 | 95.6% | 96.2% | 29,691 |
| wordpiece-8k | 8,074 | 7,782 | 96.4% | 88.0% | 7,474 |
| wordpiece-16k | 16,074 | 15,570 | 96.9% | 92.8% | 15,037 |
| wordpiece-32k | 32,074 | 30,593 | 95.4% | 95.9% | 29,702 |
| unigram-8k | 8,000 | 7,829 | 97.9% | 89.4% | 7,466 |
| unigram-16k | 16,000 | 15,769 | 98.6% | 93.7% | 15,213 |
| unigram-32k | 32,000 | 31,158 | 97.4% | 96.4% | 30,181 |
| analyzer-builtin | n/a | 125,501 | n/a | 94.9% | 120,313 |
| analyzer-en_100k | n/a | 106,826 | n/a | 89.6% | 101,638 |
| analyzer-en_100k-derived | n/a | 105,680 | n/a | 86.6% | 100,492 |
| analyzer-en_all-derived | n/a | 105,622 | n/a | 86.6% | 100,434 |

`used on test` counts distinct pieces over the whole pretokenized stream;
`distinct pieces emitted on words` counts them over the alphabetic word tokens
only. The analyzer has no fixed vocabulary, so there is no share to report,
only a count. Answer 2 below takes that seriously.

### Morphological alignment

Boundary precision, recall and F1 over internal boundaries, exact match over
whole segmentations, and the alternation tolerant variant. Every system is
scored on the same forms and against the same alternation mask: 30,514 of the
390,122 gold forms involve an alternation.

**MorphoLex, 10,058 forms. Independent: built by psycholinguists from a
different pipeline and never used to build a lexicon.**

| system | P | R | F1 | exact | P alt | R alt | F1 alt |
|--------|--:|--:|---:|------:|------:|------:|-------:|
| bpe-8k | 0.472 | 0.689 | 0.560 | 0.373 | 0.473 | 0.690 | 0.561 |
| bpe-16k | 0.536 | 0.636 | 0.581 | 0.441 | 0.536 | 0.637 | 0.582 |
| bpe-32k | 0.582 | 0.537 | 0.559 | 0.418 | 0.583 | 0.538 | 0.560 |
| wordpiece-8k | 0.446 | 0.713 | 0.549 | 0.307 | 0.446 | 0.714 | 0.549 |
| wordpiece-16k | 0.516 | 0.666 | 0.582 | 0.407 | 0.517 | 0.667 | 0.582 |
| wordpiece-32k | 0.603 | 0.593 | 0.598 | 0.432 | 0.604 | 0.594 | 0.599 |
| unigram-8k | 0.493 | 0.772 | 0.602 | 0.365 | 0.494 | 0.772 | 0.602 |
| **unigram-16k** | 0.566 | 0.712 | **0.631** | 0.433 | 0.566 | 0.713 | 0.631 |
| unigram-32k | 0.638 | 0.617 | 0.628 | 0.439 | 0.639 | 0.618 | 0.628 |
| analyzer-builtin | 0.964 | 0.030 | 0.058 | 0.033 | 0.975 | 0.030 | 0.059 |
| analyzer-en_100k | 0.946 | 0.081 | 0.149 | 0.089 | 0.952 | 0.081 | 0.150 |
| analyzer-en_100k-derived | 0.945 | 0.126 | 0.223 | 0.137 | 0.951 | 0.127 | 0.224 |
| analyzer-en_all-derived | 0.928 | 0.130 | 0.228 | 0.138 | 0.935 | 0.131 | 0.229 |

**MorphyNet inflection, 187,083 forms. IN DISTRIBUTION: mined from Wiktionary,
which is also what `tools/build_lexicon.py` builds the lexicon from. Read as
an upper bound, not as accuracy.**

| system | P | R | F1 | exact | P alt | R alt | F1 alt |
|--------|--:|--:|---:|------:|------:|------:|-------:|
| bpe-8k | 0.064 | 0.172 | 0.093 | 0.015 | 0.077 | 0.208 | 0.112 |
| bpe-16k | 0.059 | 0.137 | 0.082 | 0.020 | 0.073 | 0.169 | 0.102 |
| bpe-32k | 0.048 | 0.096 | 0.064 | 0.024 | 0.063 | 0.126 | 0.084 |
| wordpiece-8k | 0.129 | 0.379 | 0.193 | 0.025 | 0.143 | 0.420 | 0.214 |
| wordpiece-16k | 0.127 | 0.320 | 0.181 | 0.040 | 0.147 | 0.371 | 0.210 |
| wordpiece-32k | 0.130 | 0.284 | 0.178 | 0.057 | 0.152 | 0.332 | 0.209 |
| **unigram-8k** | 0.168 | 0.535 | **0.256** | 0.030 | 0.183 | 0.580 | 0.278 |
| unigram-16k | 0.165 | 0.457 | 0.243 | 0.047 | 0.180 | 0.497 | 0.264 |
| unigram-32k | 0.166 | 0.402 | 0.235 | 0.064 | 0.182 | 0.441 | 0.257 |
| analyzer-builtin | 0.577 | 0.032 | 0.060 | 0.010 | 0.577 | 0.032 | 0.060 |
| analyzer-en_100k | 0.781 | 0.413 | 0.540 | 0.309 | 0.781 | 0.413 | 0.541 |
| analyzer-en_100k-derived | 0.781 | 0.415 | 0.542 | 0.311 | 0.782 | 0.416 | 0.543 |
| **analyzer-en_all-derived** | 0.871 | 0.854 | **0.862** | 0.741 | 0.871 | 0.854 | 0.863 |

**MorphyNet derivation, 203,374 forms. IN DISTRIBUTION, same caveat.**

| system | P | R | F1 | exact | P alt | R alt | F1 alt |
|--------|--:|--:|---:|------:|------:|------:|-------:|
| bpe-8k | 0.226 | 0.652 | 0.336 | 0.109 | 0.228 | 0.657 | 0.338 |
| bpe-16k | 0.255 | 0.629 | 0.363 | 0.173 | 0.257 | 0.634 | 0.365 |
| bpe-32k | 0.288 | 0.602 | 0.389 | 0.247 | 0.290 | 0.606 | 0.392 |
| wordpiece-8k | 0.187 | 0.588 | 0.284 | 0.079 | 0.190 | 0.595 | 0.288 |
| wordpiece-16k | 0.200 | 0.546 | 0.293 | 0.125 | 0.203 | 0.554 | 0.297 |
| wordpiece-32k | 0.222 | 0.519 | 0.311 | 0.179 | 0.226 | 0.527 | 0.316 |
| unigram-8k | 0.237 | 0.751 | 0.360 | 0.099 | 0.239 | 0.759 | 0.364 |
| unigram-16k | 0.264 | 0.738 | 0.389 | 0.152 | 0.267 | 0.745 | 0.393 |
| **unigram-32k** | 0.294 | 0.722 | **0.418** | 0.208 | 0.297 | 0.729 | 0.422 |
| analyzer-builtin | 0.784 | 0.035 | 0.066 | 0.026 | 0.785 | 0.035 | 0.066 |
| analyzer-en_100k | 0.800 | 0.226 | 0.353 | 0.183 | 0.800 | 0.226 | 0.353 |
| analyzer-en_100k-derived | 0.802 | 0.234 | 0.363 | 0.191 | 0.803 | 0.235 | 0.363 |
| analyzer-en_all-derived | 0.783 | 0.256 | 0.386 | 0.210 | 0.783 | 0.256 | 0.386 |

The alternation tolerant columns barely move anything. That is the expected
result and it is worth saying why: `tools/eval_lexicon.py` converts both
MorphyNet files to surface boundaries by taking the longest common prefix of
the first morph and the surface form, which is the same convention the
analyzer's stem piece follows, and MorphoLex rows are kept only when their
canonical morphemes concatenate to the word exactly. All three gold standards
therefore already agree with the analyzer about where an alternation puts the
boundary. The variant is reported because the brief asks for it, and the
answer it gives is that the convention is not costing anybody anything. The
baselines gain more from it than the analyzer does, up to 0.021 F1 on
inflection, because their boundaries are near misses more often.

### Analyzer coverage and gates

Token weighted over the test word tokens.

| system | coverage | decomposed | lexicon_stem | irregular | no_analysis | passthrough |
|--------|---------:|-----------:|-------------:|----------:|------------:|------------:|
| analyzer-builtin | 24.5% | 5.1% | 14.6% | 4.9% | 75.4% | 0.0% |
| analyzer-en_100k | 86.9% | 10.4% | 71.5% | 5.1% | 13.0% | 0.0% |
| analyzer-en_100k-derived | 86.9% | 13.4% | 68.8% | 4.8% | 13.0% | 0.0% |
| analyzer-en_all-derived | 87.0% | 13.4% | 68.7% | 4.8% | 13.0% | 0.0% |

Type weighted, coverage is 8.3%, 57.8%, 57.8% and 59.1%. The gap between the
two is the whole story of this layer: it reaches most of the running text and
a little over half of the vocabulary.

`passthrough` is 0.0% by construction. These are the alphabetic word tokens,
and passthrough is for input with no letters in it.

## The four questions

### 1. At matched vocabulary size, analyzer against Unigram

**The sizes cannot be matched.** The analyzer emits between 100,434 and
120,313 distinct pieces on the test word tokens, three to four times the
largest baseline trained here. There is no Unigram model in this comparison
at the analyzer's working inventory, and reporting one as if there were would
be the dishonest version of this answer. What follows is the comparison
against each Unigram size, with the inventory difference stated alongside.

Taking `analyzer-en_100k`, the setting the brief names as the default:

| against | pieces per word token | MorphoLex F1 | inflection F1 | derivation F1 |
|---------|----------------------|--------------|---------------|---------------|
| unigram-8k | 1.106 vs 1.178, analyzer **0.072 better** | 0.149 vs 0.602, analyzer **0.453 worse** | 0.540 vs 0.256, analyzer **0.284 better** | 0.353 vs 0.360, analyzer 0.007 worse |
| unigram-16k | 1.106 vs 1.101, analyzer 0.005 worse | 0.149 vs 0.631, analyzer **0.482 worse** | 0.540 vs 0.243, analyzer **0.297 better** | 0.353 vs 0.389, analyzer 0.036 worse |
| unigram-32k | 1.106 vs 1.056, analyzer 0.050 worse | 0.149 vs 0.628, analyzer **0.479 worse** | 0.540 vs 0.235, analyzer **0.305 better** | 0.353 vs 0.418, analyzer 0.065 worse |

And taking `analyzer-en_all-derived`, the strongest setting:

| against | pieces per word token | MorphoLex F1 | inflection F1 | derivation F1 |
|---------|----------------------|--------------|---------------|---------------|
| unigram-8k | 1.137 vs 1.178, analyzer **0.041 better** | 0.228 vs 0.602, analyzer **0.374 worse** | 0.862 vs 0.256, analyzer **0.606 better** | 0.386 vs 0.360, analyzer 0.026 better |
| unigram-16k | 1.137 vs 1.101, analyzer 0.036 worse | 0.228 vs 0.631, analyzer **0.403 worse** | 0.862 vs 0.243, analyzer **0.619 better** | 0.386 vs 0.389, analyzer 0.003 worse |
| unigram-32k | 1.137 vs 1.056, analyzer 0.081 worse | 0.228 vs 0.628, analyzer **0.400 worse** | 0.862 vs 0.235, analyzer **0.627 better** | 0.386 vs 0.418, analyzer 0.032 worse |

The gap runs in both directions and the direction depends entirely on which
gold standard is asked.

On compression the two are close. The analyzer at 1.106 to 1.137 pieces per
word token sits between Unigram 8k and Unigram 16k, while holding an
inventory three times larger than Unigram 32k. Per type the analyzer is far
ahead, 1.26 to 1.29 against 2.43 to 3.18, but that is a direct consequence of
emitting an unanalyzable word whole rather than of segmenting it better.

On inflection the analyzer wins by a wide margin, 0.86 against 0.24 at the
top. That number is in distribution and should be read as an upper bound: the
en_all lexicon and the MorphyNet gold are two views of Wiktionary.

On the independent gold the analyzer loses by a wide margin, 0.23 against
0.63, and the shape of the loss is the interesting part. The analyzer's
precision on MorphoLex is 0.93, higher than any baseline's 0.64, while its
recall is 0.13 against Unigram's 0.62. It is not putting boundaries in the
wrong place, it is putting almost none in.

The reason is not the one that would be easy to assume. Every MorphoLex form
here has at least two morphemes, 1.173 boundaries on average, and
`analyzer-en_100k-derived` returns 84.5% of them as a single piece. But only
15.7% of the forms are ones it cannot reach at all: the other 68.8 points are
words it *does* find, and reports as one `lexicon_stem` piece because the
whole form is itself a stored lexeme. That
is decision 5 in `docs/SEGMENTATION.md`, where a stored whole form outranks a
derivational reading, playing out at scale on an independent gold standard.
`en_all` with `--derived-first` cuts the unreachable share to 8.8% and barely
moves the single-piece share, 83.8%, which confirms it: the recall ceiling
here is the ranking policy, not lexicon size.

Multi-layer gold is a second, smaller factor. 1,567 of the 10,058 forms have
three or more morphemes (`enorm|ous|ly`, `typo|graph|ic|al|ly`) and the
analyzer strips one affix layer, so even when it does split it can only reach
one of the two boundaries.

A tokenizer built on this layer would want that precision; a boundary F1
table does not reward it.

### 2. The analyzer's vocabulary is not fixed

| system | nominal vocabulary | distinct pieces emitted on the test word tokens |
|--------|-------------------:|------------------------------------------------:|
| bpe-8k | 8,000 | 7,053 |
| bpe-16k | 16,000 | 14,760 |
| bpe-32k | 32,000 | 29,691 |
| wordpiece-8k | 8,074 | 7,474 |
| wordpiece-16k | 16,074 | 15,037 |
| wordpiece-32k | 32,074 | 29,702 |
| unigram-8k | 8,000 | 7,466 |
| unigram-16k | 16,000 | 15,213 |
| unigram-32k | 32,000 | 30,181 |
| analyzer-builtin | none | **120,313** |
| analyzer-en_100k | none | **101,638** |
| analyzer-en_100k-derived | none | **100,492** |
| analyzer-en_all-derived | none | **100,434** |

The analyzer emits 100,434 distinct stems and affixes on test at its best
setting, 3.3 times Unigram 32k's 30,181, and that is on one 51 MB test set
rather than a bound. Its true inventory is open: any word it cannot analyze is
emitted as a piece of its own, so the inventory grows with the text.

Three things follow. Its compression figures are not comparable to a 32k
model's at face value, since it is buying them with an inventory it never had
to fit in a budget. Its type-weighted 1.29 pieces per word is close to the
trivial segmentation, which would be 1.00 with one piece per word type and an
inventory of 125,852. And the right use of this layer is not as a drop-in
replacement for a fixed-size vocabulary but as a source of boundaries that a
fixed-size vocabulary is then built to respect.

Note the direction of travel: a bigger lexicon makes the inventory *smaller*,
120,313 to 100,434, because words it would otherwise emit whole get
decomposed into stems and affixes it already has.

### 3. The best baseline on morphological alignment

**Unigram, at a size that depends on the gold standard. On the independent
one, Unigram 16k.**

| gold | best baseline | F1 | runner up |
|------|---------------|---:|-----------|
| MorphoLex (independent) | unigram-16k | 0.631 | unigram-32k 0.628 |
| MorphyNet inflection | unigram-8k | 0.256 | unigram-16k 0.243 |
| MorphyNet derivation | unigram-32k | 0.418 | bpe-32k 0.389, unigram-16k 0.389 |

Unigram is first on every gold standard, which is the one clean result in the
whole table. Taking exact match instead of F1 moves the winner to bpe-16k on
MorphoLex (0.441 against unigram-32k's 0.439) and to unigram-32k on
inflection, but Unigram is at or within 0.002 of the top everywhere.

The size effect is a precision and recall trade, not a quality trend. Going
from 8k to 32k raises precision on MorphoLex from 0.493 to 0.638 and drops
recall from 0.772 to 0.617: a bigger vocabulary stores more whole words, so it
cuts less often and is right more often when it does. F1 peaks in the middle.
On inflection, where the gold has exactly one boundary per word and it is
near the end, the smallest vocabulary wins outright, because it is the one
that still splits.

WordPiece beats BPE on inflection at every size and loses to it on derivation
at every size. That is the `##` marker: WordPiece's vocabulary distinguishes
word-initial from word-internal pieces, so a suffix like `##ing` is a single
entry it can reach for, while BPE's vocabulary stores `ing` once and has no
reason to prefer using it at the end of a word.

### 4. Twenty words where the analyzer and Unigram 32k disagree

`analyzer-en_100k-derived` against `unigram-32k`, both shown as surfaces.
These are drawn from the 20,908 words where the two differ and a gold
standard has an opinion; the full dump is
`~/data/tok/results/disagreements.tsv`, ordered by test frequency.

**The analyzer is right, ten cases.** All ten are words Unigram 32k has stored
whole, so it emits no boundary at all.

| word | test count | analyzer | unigram-32k | gold |
|------|-----------:|----------|-------------|------|
| `government` | 1,168 | `govern\|ment` | `government` | MorphoLex `govern\|ment` |
| `player` | 1,499 | `play\|er` | `player` | MorphoLex `play\|er` |
| `quickly` | 1,061 | `quick\|ly` | `quickly` | MorphoLex `quick\|ly` |
| `certainly` | 952 | `certain\|ly` | `certainly` | MorphoLex `certain\|ly` |
| `completely` | 537 | `complete\|ly` | `completely` | MorphoLex `complete\|ly` |
| `higher` | 542 | `high\|er` | `higher` | MorphoLex `high\|er` |
| `coming` | 3,526 | `com\|ing` | `coming` | MorphyNet `com\|ing` |
| `making` | 2,841 | `mak\|ing` | `making` | MorphyNet `mak\|ing` |
| `called` | 6,197 | `call\|ed` | `called` | MorphyNet `call\|ed` |
| `things` | 6,038 | `thing\|s` | `things` | MorphyNet `thing\|s` |

Six of the ten are checked against the independent gold. `coming` and
`making` are the `e_deletion` alternation, and the analyzer puts the boundary
at `com|ing`, which is where the gold conversion puts it too.

**The analyzer is wrong or fell back, ten cases.**

| word | test count | analyzer | unigram-32k | gold | what went wrong |
|------|-----------:|----------|-------------|------|-----------------|
| `is` | 89,742 | `i\|s` | `is` | MorphyNet derivation `i\|s` | `--derived-first` promotes any decomposition, so the most frequent word in the corpus is split into a letter and a plural. The gold agrees, which says more about MorphyNet's derivational rows than about the split |
| `his` | 33,496 | `hi\|s` | `his` | MorphyNet inflection `hi\|s` | the same failure, same gold problem |
| `number` | 3,403 | `numb\|er` | `number` | MorphyNet inflection `numb\|er` | a spurious comparative of `numb`, again credited by the gold |
| `disputes` | 48 | `dis\|put\|es` | `dispute\|s` | `dispute\|s` | an etymological prefix taken as live, and then the stem is wrong too |
| `rewards` | 27 | `re\|ward\|s` | `reward\|s` | `reward\|s` | `re-` is not a live prefix in `reward` |
| `kites` | 32 | `kit\|es` | `kite\|s` | `kite\|s` | the wrong stem wins, `kit` plus an epenthetic `es` |
| `faction` | 20 | `fac\|tion` | `fact\|ion` | MorphoLex `fact\|ion` | the boundary is one character early, and no alternation is involved so the tolerant variant does not forgive it |
| `playhouse` | 54 | `playhouse` | `play\|house` | MorphoLex `play\|house` | fell back: the analyzer does not do compounds |
| `drunkard` | 40 | `drunkard` | `drunk\|ard` | MorphoLex `drunk\|ard` | fell back: `-ard` is not a suffix it knows |
| `switchboard` | 35 | `switchboard` | `switch\|board` | MorphoLex `switch\|board` | fell back: compound again |

Two patterns, and they pull in opposite directions. The errors at the top of
the list are over-analysis driven by `--derived-first`, and they are
concentrated in exactly the highest frequency words, so they cost far more
token-weighted than their three rows suggest: `is` and `his` alone are 1.3% of
the test tokens. The errors at the bottom are the analyzer declining to split
a word it has no analysis for, and compounds are the biggest single class of
those. A tokenizer built on this layer would want a guard on short function
words and a compound splitter, in that order.

## Decisions

Judgment calls made while building this, and why.

1. **An apostrophe is punctuation and is split off.** `don't` becomes `don`,
   `'`, `t`. The alternative, keeping word-internal apostrophes as
   `tools/eval_lexicon.py` does, would have been defensible, but the brief for
   the shared pretokenizer is to split punctuation off from words and an
   apostrophe is punctuation. The cost is that clitics arrive as the separate
   word tokens `t`, `s`, `ll` and `re`, which every system then has to handle
   alike. Since the rule is shared, it cannot favour one system over another.

2. **A unit is a maximal run of one character class, for all three classes.**
   So `!!` is one unit and `U.S.A.` is six. Treating punctuation runs the same
   way as letter and digit runs keeps the rule to a single sentence with no
   special cases. It does mean a unit like `,"` exists.

3. **Scoring is done per type and weighted by frequency, not by re-encoding
   the token stream.** Every tokenizer here encodes a unit independently of
   its context, so encoding the 125,852 test word types once and multiplying
   by their counts gives numerically identical token-weighted figures to
   encoding all 9.6M tokens, at a seventy-fifth of the cost.
   `test_words.txt.gz` is still written in order, because the brief asks for
   it and because it is the artifact a later experiment would stream.

4. **WordPiece is run over a byte level pre-tokenizer, because `tokenizers`
   has no byte fallback for it.** BPE and Unigram both take `byte_fallback`,
   so an input character the model never saw is spelled out as `<0xXX>`
   pieces and decodes back exactly. WordPiece has no such option: a word
   holding one unknown character becomes a single `<unk>` and is lost. The
   test text contains 36 characters the training text never shows, two of
   them letters, so round trip would not have been 100%.

   Running WordPiece over a byte level pre-tokenizer gives it the same
   property by another route. Its alphabet becomes the 256 byte symbols
   rather than the 702 characters of the training text, so any input at all
   can be spelled, and the byte level decoder puts the characters back. For
   ASCII, which is every word scored against a gold standard here, the pieces
   are identical to what plain WordPiece would have produced.

   One gap had to be closed by hand. The trainer adds an alphabet symbol in
   word initial form but adds the `##` continuation form only for symbols it
   saw inside a word, and a byte symbol missing its continuation form still
   forces an `<unk>`. Both forms of all 256 are filled in after training,
   which puts WordPiece a few dozen entries above its nominal size. The exact
   sizes are in the table.

5. **An alternation is a property of the word, so the alternation tolerant
   variant uses one mask for all thirteen systems.** The tolerance is meant
   to forgive a boundary one character off where the stem changed spelling,
   and whether `cities` involves a change of spelling does not depend on
   which tokenizer is reading it. The analyzer is the only system here that
   reports alternations, so the mask is taken once from its richest setting,
   `en_all` with `--derived-first`, and then applied unchanged to every row
   of the table, baselines included. Taking each analyzer setting's own mask
   would have given the four analyzer rows four different metrics and left
   the baselines with none.

6. **The four analyzer settings load no `data/closed_class.tsv`.** The
   lexicon evaluation in `docs/LEXICON.md` loads it alongside every built
   lexicon, since no mined lexicon supplies a determiner. The brief for this
   comparison names four settings and that file is not among them, so it is
   left out and the function words fall through to `no_analysis`. That costs
   the analyzer coverage on the most frequent tokens in the corpus, and the
   coverage column should be read with that in mind.

7. **Question 1 is answered by refusing to match the sizes.** The analyzer's
   working inventory on test is 100,434 pieces at its best setting, against
   32,000 for the largest baseline trained here. Nothing in the comparison is
   at a matched size, and training a 100k Unigram model to force one would
   have been a different experiment: at that size a unigram model stores most
   of the corpus whole and the compression question stops being interesting.
   The comparison is reported against all three Unigram sizes instead, with
   the inventory gap stated next to it, and answer 2 does the honest
   accounting.

8. **The worked examples use `analyzer-en_100k-derived` against
   `unigram-32k`.** `en_100k` is the lexicon the brief names, and
   `--derived-first` is the setting that actually attempts a decomposition,
   so it is the one whose splits can be judged right or wrong. The default
   ranking policy would have filled the table with words reported whole,
   which shows the same thing as a fallback without being as informative.
   `~/data/tok/results/disagreements.tsv` holds all 20,908 of them.

9. **The large intermediates are gzip compressed.** Housekeeping on this
   machine compressed `train_pretok.txt` and `test_words.txt` in place while
   a training run was reading them, which killed the run. Writing them
   compressed in the first place removes the failure mode, and the libraries
   are fed from a line iterator rather than by path. It also keeps the whole
   of `~/data/tok` to a few hundred megabytes. All nine baselines were
   retrained after this change so that the times and memory figures below
   come from the code as it stands.

10. **A piece spelled `##` at the start of a word is the text `##`, not a
    continuation marker.** The WordPiece format does not escape its marker,
    so the vocabulary entry for a word-internal `#` is spelled exactly like
    the word `##`, which occurs in the test text as a unit of the shared
    pretokenizer. Reading it as a marker and stripping it loses the word, and
    that is what `tokenizers` own decoder does: `##` was the one string out of
    490,133 that would not round-trip for WordPiece 16k and 32k.

    The scorer strips the prefix only from a piece after the first, since the
    first piece of a word is never a continuation by construction. That makes
    round trip exact for all thirteen systems and leaves the scorer's reading
    of the pieces slightly more careful than the library's. Both readings are
    computed and compared on every string encoded, and `##` is the only place
    they ever differed.

## After the derivation fixes

Three changes, measured over the same thirteen systems, the same word list and
the same scorer. `~/data/tok/results/baselines_v2.json` holds every number
below; the tables above are the run before them and are left as they were.

1. **A guard on the `--derived-first` promotion.** A decomposition outranks a
   stored whole word only when four conditions hold: the word is not closed
   class, the lemma is at least three characters, the lemma is a lexeme under
   the part of speech the affix attaches to, and for a one or two character
   affix the whole word is not more frequent than the stem.
   `docs/LEXICON.md` has the conditions and the measurement behind the fourth.

2. **Twenty-eight derivational suffixes instead of five.** `-able`, `-al`,
   `-ful`, `-less`, `-ous`, `-ic`, `-ive`, `-ish`, `-y`, `-ity`, `-ism`,
   `-ist`, `-ion`, `-ation`, `-ance`, `-ence`, `-hood`, `-ship`, `-dom`,
   `-ize`, `-ise`, `-ify` and `-en` joined `-ness`, `-ly`, `-er`, `-tion` and
   `-ment`, with the same three orthographic alternations inflection uses and
   stacking up to two suffixes plus an inflection.

3. **A hygiene filter on the MorphyNet gold.** The rows that credited `i|s`
   for `is` and `ha|s` for `has` are dropped. `--unfiltered-gold` turns it off.

And one change of configuration: **all four analyzer settings now load
`data/closed_class.tsv`**, which decision 6 above left out. Without a
`FUNCTION` reading of `is` there is no stored whole word for the guard to keep,
and `i|s` was the single most frequent error in the table. It also raises token
coverage from 86.9% to 92.8%, so the coverage and compression columns are not
comparable with the run above; the gold columns are.

### The gold after the filter

| Gold | Rows | Kept before | Kept after | Dropped |
|------|-----:|------------:|-----------:|--------:|
| MorphyNet inflection | 649,593 | 219,025 | 218,835 | 190 |
| MorphyNet derivation | 225,131 | 208,534 | 199,439 | 9,095 |

| Reason | Inflection | Derivation |
|--------|-----------:|-----------:|
| the stem is under three characters | 173 | 167 |
| the stem is not a lemma in the inflectional file | 0 | 8,892 |
| the form is in `data/closed_class.tsv` | 17 | 65 |

Scorable forms go from 187,083 to 186,947 and from 203,374 to 194,771. The
inflectional file loses almost nothing, which is the expected result: its stems
are its own first column, so the second test cannot fire and only `i`, `ha`,
`wa`, `u` and the function words go. The derivational file loses 4.4% of its
rows, nearly all of them to a source word that appears nowhere in MorphyNet's
own inflection table.

MorphoLex is not filtered. It is the independent gold and it already drops
every row whose morphemes do not concatenate.

**What the filter changes, both ways.** `--unfiltered-gold` scores the same
systems against the unfiltered rows, and the aggregate barely moves:

| | Unfiltered P | Filtered P | Unfiltered F1 | Filtered F1 |
|---|---:|---:|---:|---:|
| **MorphyNet derivation** | | | | |
| unigram-32k | 0.294 | 0.297 | 0.418 | 0.420 |
| analyzer-en_100k | 0.766 | **0.788** | 0.523 | 0.521 |
| analyzer-en_100k-derived | 0.770 | **0.791** | 0.549 | 0.548 |
| analyzer-en_all-derived | 0.748 | **0.770** | 0.572 | 0.572 |
| **MorphyNet inflection** | | | | |
| unigram-32k | 0.166 | 0.166 | 0.235 | 0.235 |
| analyzer-en_100k | 0.678 | 0.680 | 0.555 | 0.555 |
| analyzer-en_all-derived | 0.790 | 0.792 | 0.826 | 0.827 |

Two points of derivational precision, and F1 to within 0.003. That is the
right size: 9,095 rows out of 225,131 cannot move an aggregate far. The filter
is worth having anyway, because the rows it drops are the ones that decided
individual judgments: the unfiltered gold says `i|s` is the correct
segmentation of `is`, and a table of worked examples that cites it as evidence
is citing a mining artifact. It moves the analyzer rows more than the baseline
rows because the dropped rows are short-stem splits, which is exactly what a
Unigram model does anyway and what the analyzer has stopped doing.

### Compression and round trip

The analyzer settings, with Unigram 16k and 32k for reference.

| system | pcs/token | pcs/type | chr/pc | pcs/1kB | distinct pieces | round trip |
|--------|----------:|---------:|-------:|--------:|----------------:|-----------:|
| unigram-16k | 1.101 | 2.795 | 3.574 | 275.52 | 15,213 | 100% |
| unigram-32k | 1.056 | 2.427 | 3.725 | 266.94 | 30,181 | 100% |
| analyzer-builtin | 1.054 | 1.076 | 3.733 | 265.79 | 118,658 | 100% |
| analyzer-en_100k | 1.091 | 1.287 | 3.608 | 272.65 | 98,701 | 100% |
| analyzer-en_100k-derived | 1.111 | 1.339 | 3.542 | 276.43 | 95,632 | 100% |
| analyzer-en_all-derived | 1.111 | 1.346 | 3.541 | 276.52 | 95,585 | 100% |

Round trip is exact for all thirteen systems on all 482,177 strings encoded,
and the library decoders agree with the scorer on every one.

The analyzer emits fewer pieces per token than it did (1.111 against 1.136 at
`en_100k` with the flag) while decomposing more words, because the guard stopped
it splitting `is`, `his` and `number`, which are among the most frequent tokens
in the corpus. Its working inventory also shrank, from 100,492 distinct pieces
to 95,632, for the same reason answer 2 gave before: a stem plus an affix it
already has replaces a word it used to emit whole.

### Morphological alignment

**MorphoLex, 10,058 forms. Independent.**

| system | P | R | F1 | exact | P alt | R alt | F1 alt |
|--------|--:|--:|---:|------:|------:|------:|-------:|
| **unigram-16k** | 0.566 | 0.712 | **0.631** | 0.433 | 0.568 | 0.715 | 0.633 |
| unigram-32k | 0.638 | 0.617 | 0.628 | 0.439 | 0.640 | 0.619 | 0.629 |
| analyzer-builtin | 0.967 | 0.092 | 0.169 | 0.087 | 0.971 | 0.093 | 0.169 |
| analyzer-en_100k | 0.972 | 0.232 | 0.374 | 0.215 | 0.974 | 0.232 | 0.375 |
| analyzer-en_100k-derived | 0.975 | 0.377 | 0.544 | 0.344 | 0.977 | 0.378 | 0.545 |
| analyzer-en_all-derived | 0.973 | 0.382 | **0.548** | 0.347 | 0.975 | 0.382 | 0.549 |

**MorphyNet inflection, 186,947 forms. IN DISTRIBUTION.**

| system | P | R | F1 | exact |
|--------|--:|--:|---:|------:|
| unigram-16k | 0.165 | 0.457 | 0.243 | 0.047 |
| unigram-32k | 0.166 | 0.402 | 0.235 | 0.064 |
| analyzer-builtin | 0.502 | 0.042 | 0.078 | 0.010 |
| analyzer-en_100k | 0.680 | 0.468 | 0.555 | 0.293 |
| analyzer-en_100k-derived | 0.682 | 0.470 | 0.557 | 0.296 |
| **analyzer-en_all-derived** | 0.792 | 0.865 | **0.827** | 0.684 |

**MorphyNet derivation, 194,771 forms. IN DISTRIBUTION.**

| system | P | R | F1 | exact |
|--------|--:|--:|---:|------:|
| unigram-16k | 0.266 | 0.736 | 0.391 | 0.157 |
| unigram-32k | 0.297 | 0.722 | 0.420 | 0.214 |
| analyzer-builtin | 0.702 | 0.065 | 0.120 | 0.041 |
| analyzer-en_100k | 0.788 | 0.389 | 0.521 | 0.306 |
| analyzer-en_100k-derived | 0.791 | 0.419 | 0.548 | 0.333 |
| **analyzer-en_all-derived** | 0.770 | 0.455 | **0.572** | 0.367 |

The analyzer now leads the derivational gold outright, 0.572 against Unigram
32k's 0.420, where before the fixes it trailed at 0.386. On the inflectional
gold it loses 0.035 F1 at the top, 0.862 to 0.827, and the loss is precision,
0.871 to 0.792: that gold marks one boundary per word, the inflectional one, so
every derivational boundary the new rules add inside a form like `abacists` is
counted as a false positive. `abac|ist|s` against a gold of `abacist|s` is the
shape of almost all of it.

### Analyzer coverage and gates

Token weighted, over the test word tokens.

| system | coverage | decomposed | lexicon_stem | irregular | no_analysis |
|--------|---------:|-----------:|-------------:|----------:|------------:|
| analyzer-builtin | 69.5% | 5.2% | 62.2% | 2.1% | 30.5% |
| analyzer-en_100k | 92.8% | 8.8% | 81.1% | 2.9% | 7.2% |
| analyzer-en_100k-derived | 92.8% | 10.8% | 79.1% | 2.9% | 7.2% |
| analyzer-en_all-derived | 92.8% | 10.8% | 79.1% | 2.9% | 7.2% |

Type weighted, coverage is 10.4%, 58.8%, 58.8% and 60.0%.

The jump in coverage against the table above is `data/closed_class.tsv`, not
the rules. The share of tokens that come out decomposed fell, 13.4% to 10.8%,
and that is the two of them together: the file gives a function word a stored
reading and the guard stops the decomposition outranking it. The words that
stopped splitting are the most frequent in the corpus, so they move a lot of
token weight even though there are not many of them. Type weighted, the
decomposed share went the other way, which is where the suffixes show up:
26.2% of types to 31.2%.

### Before and after, on the independent gold

| | Before | After | |
|---|---:|---:|---|
| analyzer-builtin | 0.058 | **0.169** | F1 |
| analyzer-en_100k | 0.149 | **0.374** | F1 |
| analyzer-en_100k-derived | 0.223 | **0.544** | F1 |
| analyzer-en_all-derived | 0.228 | **0.548** | F1 |
| analyzer-en_100k-derived | 0.945 | **0.975** | precision |
| analyzer-en_100k-derived | 0.126 | **0.377** | recall |

Recall triples and precision rises. That is the result this work was for, and
the direction matters: a rule set that only ever fires when the lexicon holds
the stem buys recall without paying for it.

### Per-suffix agreement with MorphoLex

`tools/suffix_report.py` credits a suffix with a MorphoLex form when the
analyzer's primary reading of that form ends in it, and scores the boundaries
that reading predicts. Run with `data/closed_class.tsv` plus `en_100k` and
`--derived-first`. "newly" counts the test types the suffix splits that the
five-suffix rule set did not.

| suffix | gold forms | P | R | F1 | exact | newly |
|--------|-----------:|--:|--:|---:|------:|------:|
| `-ness` | 603 | 0.994 | 0.846 | 0.914 | 475 | 404 |
| `-ly` | 1,012 | 0.990 | 0.746 | 0.850 | 684 | 1,290 |
| `-er` | 424 | 0.988 | 0.937 | 0.962 | 391 | 11 |
| `-tion` | 4 | **0.000** | 0.000 | 0.000 | 0 | 1 |
| `-ment` | 83 | 0.954 | 1.000 | 0.976 | 79 | 0 |
| `-able` | 133 | 0.957 | 0.964 | 0.960 | 122 | 350 |
| `-al` | 101 | 0.962 | 0.795 | 0.871 | 71 | 498 |
| `-ful` | 131 | 0.970 | 0.978 | 0.974 | 124 | 219 |
| `-less` | 199 | 0.995 | 0.985 | 0.990 | 195 | 314 |
| `-ous` | 3 | 1.000 | 1.000 | 1.000 | 3 | 172 |
| `-ic` | 88 | 0.961 | 0.772 | 0.856 | 60 | 290 |
| `-ive` | 33 | 1.000 | 1.000 | 1.000 | 33 | 219 |
| `-ish` | 81 | 0.988 | 0.952 | 0.970 | 77 | 194 |
| `-y` | 332 | 0.982 | 0.971 | 0.976 | 319 | 1,408 |
| `-ity` | 114 | 1.000 | 0.789 | 0.882 | 85 | 368 |
| `-ism` | 108 | 0.982 | 0.727 | 0.835 | 69 | 275 |
| `-ist` | 100 | 0.981 | 0.777 | 0.867 | 71 | 266 |
| `-ion` | 59 | 0.983 | 0.906 | 0.943 | 53 | 828 |
| `-ation` | 0 | n/a | n/a | n/a | 0 | 368 |
| `-ance` | 27 | 0.964 | 1.000 | 0.982 | 26 | 109 |
| `-ence` | 0 | n/a | n/a | n/a | 0 | 30 |
| `-hood` | 22 | 1.000 | 1.000 | 1.000 | 22 | 49 |
| `-ship` | 63 | 1.000 | 0.716 | 0.834 | 39 | 134 |
| `-dom` | 15 | 1.000 | 1.000 | 1.000 | 15 | 35 |
| `-ize` | 96 | 0.980 | 0.770 | 0.862 | 67 | 134 |
| `-ise` | 0 | n/a | n/a | n/a | 0 | 68 |
| `-ify` | 22 | 0.955 | 0.955 | 0.955 | 21 | 46 |
| `-en` | 40 | 1.000 | 1.000 | 1.000 | 40 | 102 |

Every suffix in the inventory scores at least 0.954 except `-tion`. Three more
were dropped on an earlier pass of this same table and one was restricted:

| Dropped or restricted | P then | On | Why |
|---|---:|---:|---|
| `-ible` dropped | 0.000 | 1 form | No free base in English. `visible` came out as `vis\|ible` off the verb `vise` |
| `-sion` dropped | 0.000 | 1 form | `-ion` reaches the same words through a better cut, `confus\|ion` and `tens\|ion`. `-sion` only added `emul\|sion` |
| `-ard` dropped | 0.400 | 5 forms | Its three measurable splits were compounds in `-yard`: `churchy\|ard`, `woody\|ard`, `bricky\|ard` |
| `-ise` restricted to a four letter stem | 0.000 | 1 form | Keeps `real\|ise`, drops `now\|ise` |
| `-ous` after the stacking limits | 0.667 | 4 forms | `re\|pet\|it\|ious` went when a two-suffix chain was made to need a four letter root |

`-tion` is the one the test flags that stays, and `docs/LEXICON.md` decision 29
says why: its four remaining splits are the boundary one character early,
`fac|tion` where the gold says `fact|ion`, and dropping it would withdraw the
analysis of every form the generator ever produced through it. The new `-ion`
rule takes over wherever the base is a verb, which is where the gold agrees.

The precision in this table is measured only over forms MorphoLex says are
derived, because a row whose morphemes do not concatenate, and a row with a
single morpheme, are both dropped before scoring. So a suffix pays nothing here
for firing on a word that has no boundary at all, and `read|y`, `part|y` and
`fin|ish` cost nothing in the column while costing a tokenizer something. The
residue below is the honest other half of this table.

### The remaining recall gap, counted

7,352 gold boundaries are missed at `analyzer-en_100k-derived`, out of 11,797.
Every one of them is in exactly one bucket, compounds tested first, since
`play|house` is a compound whether or not the analyzer reached the word:

| Cause | Boundaries | Share |
|-------|-----------:|------:|
| a compound of two free-standing words | 3,773 | 51.3% |
| an affix the rule set does not have | 1,253 | 17.0% |
| the inner layer of a multi-layer form | 1,050 | 14.3% |
| an affix it does have and did not strip | 716 | 9.7% |
| the word has no analysis at all | 560 | 7.6% |

**Compounds are now the majority of the gap.** `air|base`, `above|board`,
`switch|board`, `play|house`, `church|yard`: a boundary where both sides are
free-standing words of three letters or more and the right side is not an affix
the rules know. The analyzer does not do compounds at all, and this is the
first time that has been the largest single thing wrong with it.

**The affixes still unknown are 1,253 boundaries**, and the list is short and
specific: `-ate` (`abomin|ate`, `accur|ate`), `-ee` (`absent|ee`), `-ian`
(`academ|ic|ian`), `-acy` (`accur|acy`), `-ics` (`acoust|ics`), `-or`
(`act|or`), `-ward` (`after|ward`), `-i`, `-n`, `-in`. Another dozen rules
would reach most of it, and the per-suffix table is how each would be judged.

**Multi-layer forms account for 1,050.** MorphoLex has 1,567 forms with three
or more morphemes; the analyzer returns three or more pieces for 247 of them
and is exactly right on 230. The limit is not the two-layer cap but the 0.9
factor: where the intermediate form is a lexeme of its own, the one-layer
reading outranks the two-layer one, so `nationality` is reported as
`national|ity` and `nation|al|ity` sits under it in `--nbest 3`.

**716 boundaries use an affix the rules have and did not strip**, which is the
ranking and the guard: `absolutely` stays whole because the stored adverb
outweighs `absolute`, which is the fourth condition doing exactly what it was
measured to do.

### What is still wrong

The three errors in the worked examples above are fixed: `is`, `his` and
`number` all come out whole. What the fixes cost, in test tokens, worst first:

| word | count | analyzer | why |
|------|------:|----------|-----|
| `only` | 8,549 | `on\|ly` | `one` plus `-ly` with the silent e deleted. MorphyNet credits it; a tokenizer should not want it |
| `ready` | 2,541 | `read\|y` | `-y` on a real noun, and the wrong etymology |
| `party` | 1,974 | `part\|y` | the same |
| `Mary` | 1,318 | `Mar\|y` | `mare` plus `-y`. The analyzer has no notion of a proper noun |
| `letter` | 1,298 | `let\|ter` | the agentive `-er` on a doubling stem, where `let` is the more frequent word |
| `broken` | 1,011 | `broke\|n` | `-en` on the adjective `broke`, ranked over the stored participle of `break` |
| `married` | 990 | `married` | the opposite error: the fourth condition blocks `marr\|ied`, because `married` is more frequent than `marry` |

`only` is the largest single one and it comes from the silent-e candidate being
tried for every suffix rather than only the vowel-initial ones. Narrowing it
would withdraw the analysis of 227 forms in the regression baseline, and
`argu|ment` and `judg|ment` show the exception is real English, so it was left
alone: `docs/LEXICON.md` decision 30.

*Four of these seven are fixed by the orthography fixes below, which also
re-list what is left.*

Over the whole test split, the twenty-eight suffixes change 7,992 of the
125,852 types and 143,018 of the 9,647,333 tokens, 1.5%, against the
five-suffix rule set with the same guard. The tables above are what that buys.

### The four questions, after the fixes

1. **At matched vocabulary size.** Still not matched: 95,632 distinct pieces
   against 32,000, and the gap closed by 5% rather than by design. Against
   unigram-16k, `analyzer-en_all-derived` is now 0.010 worse on pieces per word
   token (1.111 against 1.101), 0.083 worse on MorphoLex F1 (0.548 against
   0.631), 0.584 better on inflection and 0.181 better on derivation. Before
   the fixes the MorphoLex gap was 0.403.

2. **The analyzer's vocabulary is not fixed.** Unchanged as an answer, and the
   direction of travel is the same: better rules shrink the inventory, 100,492
   pieces to 95,632, because a word that used to be emitted whole is now a stem
   plus an affix the layer already had.

3. **The best baseline on morphological alignment.** Unchanged: Unigram, and
   unigram-16k on the independent gold at 0.631. The baselines were not
   retrained and their numbers move only where the gold was filtered.

4. **Twenty words where the two disagree.** `~/data/tok/results/`
   `disagreements.tsv`, 19,908 words, is regenerated. The top of it is now
   almost entirely the analyzer being right and unigram-32k storing the word
   whole: `called`, `things`, `laughs`, `looks`, `years`, `coming`, `asked`,
   `says`, `looking`, `makes`. The one wrong split at the top is `only`.

### Decisions, after the fixes

These are in addition to the ten above, which still stand except where noted.

11. **All four analyzer settings now load `data/closed_class.tsv`, which
    reverses decision 6.** That decision left the file out because the brief
    for the first round named four settings and did not name it, and the cost
    was visible in the worked examples: with no `FUNCTION` reading of `is`,
    `--derived-first` promoted `i|s`, and `is` and `his` together are 1.3% of
    the test tokens. The guard on that ranking settles which of two readings
    wins and cannot invent the one it is supposed to keep, so the file has to
    be there for the fix to apply. The cost is that token coverage jumps from
    86.9% to 92.8% and the coverage and compression columns are no longer
    comparable with the earlier run. The gold columns are, since MorphoLex and
    MorphyNet contain almost no function words, and the filter now drops the
    few they did contain.

12. **The hygiene filter is on by default and off behind a flag.** The
    numbers it changes are the ones that were wrong: a gold that credits
    `i|s` for `is` is not measuring segmentation quality. Defaulting to the
    filtered gold and keeping `--unfiltered-gold` for the comparison puts the
    better measurement in the path of least resistance and leaves the worse
    one reproducible. Both readers report what they dropped and why on every
    run.

13. **The filter is not applied to MorphoLex.** It is the independent gold,
    it was not mined, and it already drops every row whose morphemes do not
    concatenate to the word. Filtering it further would mean deciding which of
    its rows a psycholinguist got wrong, which is exactly the judgment that
    would stop it being independent.

14. **A derivation row's stem is its source word, even for a prefix.** The
    filter asks whether the stem is three characters and whether it is a lemma
    somewhere in MorphyNet's inflection table. For a suffix row the source word
    and the first morph are the same string. For a prefix row the first morph
    is the prefix, and dropping `un|kind` because `un` is two letters long
    would be reading the rule against its purpose. The inflection rows use the
    first morph, where that distinction does not arise.

15. **The per-suffix precision in the table above is measured over the forms
    MorphoLex says are derived, and that is a one-sided test.** A row whose
    morphemes do not concatenate is dropped from the gold, and so is a row with
    a single morpheme, so a suffix that fires on a word with no boundary at all
    pays nothing in the column. `read|y`, `part|y` and `fin|ish` are each worth
    a thousand test tokens and none of them appears in it. The residue section
    counts them instead, by hand and by frequency, because no gold here can.

## After the orthography fixes

Three changes, measured with the same scorer on the same word list. Only the
four analyzer settings were re-scored, since nothing here touches a baseline
model; `~/data/tok/results/baselines_orthography.json` holds every number
below, and the nine baseline rows are the ones in the tables above.

1. **Silent e is deleted only before a vowel-initial suffix.** A
   consonant-initial one keeps it, except in the ten words English really does
   spell with it gone: `argument`, `judgment`, `acknowledgment`,
   `abridgment`, `truly`, `duly`, `wholly`, `introduction`, `production` and
   `reduction`. `docs/LEXICON.md` decision 33 lists the 227 baseline forms
   this withdrew and why the other 217 are not words.

2. **Two conditions on `-y`.** The stem has to be at least as frequent as the
   whole word, and the whole word must not be in `data/closed_class.tsv`.
   Decision 34 has the measurement and says why `ready` and `party` still
   split.

3. **Two more conditions on the `--derived-first` guard.** A reading the rules
   assembled is never promoted over a stored irregular form (decision 35,
   which is `broken`), and a reading that doubles a consonant to reach a
   suffix that does not make verbs is not promoted when the whole word is a
   verb of its own (decision 36, which is `letter` and the whole `man|ner`
   family with it).

### Compression and round trip

| system | pcs/token | pcs/type | chr/pc | pcs/1kB | distinct pieces | round trip |
|--------|----------:|---------:|-------:|--------:|----------------:|-----------:|
| analyzer-builtin | 1.054 | 1.076 | 3.732 | 265.83 | 118,586 | 100% |
| analyzer-en_100k | 1.091 | 1.287 | 3.608 | 272.64 | 98,691 | 100% |
| analyzer-en_100k-derived | 1.109 | 1.338 | 3.548 | 276.11 | 95,716 | 100% |
| analyzer-en_all-derived | 1.110 | 1.345 | 3.546 | 276.20 | 95,667 | 100% |

Round trip is exact for all four systems on all 482,177 strings encoded, and
the library decoders agree with the scorer on every one.

Pieces per token falls, 1.111 to 1.109 at `en_100k` with the flag, for the
reason the guard always gives: the words that stopped splitting are frequent
ones. The two settings with the flag gain 84 and 82 pieces of working
inventory, 95,632 to 95,716 and 95,585 to 95,667, which is the same fact from
the other side: a word that used to be a stem plus an affix the layer already
had is now a piece of its own. The two default-ranking settings go the other
way by a smaller margin, 118,658 to 118,586 and 98,701 to 98,691: there the
only words that change are the handful the narrowing reached, and the stems it
withdrew were pieces too.

### Morphological alignment

**MorphoLex, 10,058 forms. Independent.** Unigram 16k at F1 0.631 is still
the best baseline and was not re-scored.

| system | P | R | F1 | exact | P alt | R alt | F1 alt |
|--------|--:|--:|---:|------:|------:|------:|-------:|
| analyzer-builtin | 0.971 | 0.092 | 0.168 | 0.087 | 0.972 | 0.092 | 0.169 |
| analyzer-en_100k | 0.971 | 0.231 | 0.374 | 0.214 | 0.974 | 0.232 | 0.375 |
| analyzer-en_100k-derived | 0.976 | 0.376 | 0.543 | 0.343 | 0.978 | 0.377 | 0.544 |
| analyzer-en_all-derived | 0.974 | 0.381 | **0.548** | 0.346 | 0.976 | 0.381 | 0.549 |

**MorphyNet inflection, 186,947 forms. IN DISTRIBUTION.**

| system | P | R | F1 | exact |
|--------|--:|--:|---:|------:|
| analyzer-builtin | 0.501 | 0.043 | 0.078 | 0.010 |
| analyzer-en_100k | 0.678 | 0.470 | 0.555 | 0.292 |
| analyzer-en_100k-derived | 0.679 | 0.471 | 0.557 | 0.295 |
| **analyzer-en_all-derived** | 0.790 | 0.865 | **0.826** | 0.682 |

**MorphyNet derivation, 194,771 forms. IN DISTRIBUTION.**

| system | P | R | F1 | exact |
|--------|--:|--:|---:|------:|
| analyzer-builtin | 0.702 | 0.066 | 0.121 | 0.041 |
| analyzer-en_100k | 0.788 | 0.389 | 0.521 | 0.306 |
| analyzer-en_100k-derived | 0.791 | 0.419 | 0.548 | 0.333 |
| **analyzer-en_all-derived** | 0.770 | 0.455 | **0.572** | 0.367 |

### Before and after, on the independent gold

Four decimals, because every move here is in the fourth place.

| system | P before | P after | R before | R after | F1 before | F1 after | exact before | exact after |
|--------|---------:|--------:|---------:|--------:|----------:|---------:|-------------:|------------:|
| analyzer-builtin | 0.9671 | **0.9706** | 0.0923 | 0.0922 | 0.1685 | 0.1684 | 877 | 876 |
| analyzer-en_100k | 0.9719 | 0.9712 | 0.2318 | 0.2315 | 0.3744 | 0.3739 | 2,159 | 2,157 |
| analyzer-en_100k-derived | 0.9752 | **0.9762** | 0.3768 | 0.3759 | 0.5436 | 0.5428 | 3,456 | 3,452 |
| analyzer-en_all-derived | 0.9728 | **0.9742** | 0.3816 | 0.3808 | 0.5482 | 0.5475 | 3,486 | 3,482 |

This is the shape the fixes were meant to have, and it is a small shape. Each
of them withdraws a reading rather than adding one, so recall can only fall
and precision can only rise, and F1 holds to within a thousandth because
almost nothing withdrawn was credited by the gold. `analyzer-en_100k` is the
one row where precision falls too, by 0.0007, which is the three forms below
with no compensating gain, since the default ranking had nothing of the
`let|ter` kind to give up.

Exactly which gold forms were lost is short enough to list. Under `en_100k`
with the flag, three exact matches go and none arrives:

| form | count | before | after | why |
|------|------:|--------|-------|-----|
| `envelopment` | 12 | `envelop\|ment` | `envelopment` | the reading went through `envelope` and the deleted e, because `envelop` without the e is not a lexeme |
| `primly` | 5 | `prim\|ly` | `primly` | through `prime`, for the same reason: `prim` is not a lexeme in `en_100k` |
| `primness` | 0 | `prim\|ness` | `primness` | the same word again |

All three are a missing lexicon entry showing through a withdrawn
alternation, not a boundary the rules got wrong.

The token-weighted side is not in any of these tables, because MorphoLex
weights every form equally. Over the test split, with `en_100k` and the flag,
457 of the 125,852 types change and 23,102 tokens with them: 435 types and
22,931 tokens stop splitting, 22 types and 171 tokens move a boundary, and
nothing starts splitting that did not before. `only` and `letter` are 9,847 of
those two on their own, and `Mary`, `broken`, `manner` and `hammer` another
3,244.

What it costs in the same units is five words. `live|s` (1,164) and `beat|en`
(153) go to condition 5: both are correct splits, and both are also stored
whole, as the plural of `life` and the participle of `beat`. `aw|ful` (441
with the capitalised form), `nois|y` (163) and `tast|y` (122) go to the
narrowing, where the e deletion before a consonant-initial suffix is real and
the word is not on the exception list. The list is a list and not a pattern,
so that is the bargain it makes; `docs/LEXICON.md` decision 33 says why no
pattern was available.

### Analyzer coverage and gates

Token weighted, over the test word tokens.

| system | coverage | decomposed | lexicon_stem | irregular | no_analysis |
|--------|---------:|-----------:|-------------:|----------:|------------:|
| analyzer-builtin | 69.5% | 5.2% | 62.2% | 2.1% | 30.4% |
| analyzer-en_100k | 92.8% | 8.8% | 81.1% | 2.9% | 7.2% |
| analyzer-en_100k-derived | 92.8% | 10.6% | 79.3% | 2.9% | 7.2% |
| analyzer-en_all-derived | 92.8% | 10.6% | 79.2% | 2.9% | 7.2% |

Coverage does not move: none of the three fixes can withdraw the last reading
of a word, because each of them either narrows one candidate among several or
re-ranks readings the analyzer already produced. The decomposed share falls
from 10.8% to 10.6% of tokens, which is the 22,931 tokens above.

### What is still wrong

Of the seven in the table before, four are fixed: `only`, `Mary`, `letter`
and `broken` all come out whole. `ready` and `party` still split and decision
34 says why no frequency test can hold them; `married` is untouched.

The rest of this is what a pass over the highest-frequency splits in the test
split turns up. Most of it predates this round and was simply not listed,
because the old table was scoped to what the previous round's fixes cost.
Counts are test tokens of that exact spelling, and
`analyzer-en_100k-derived` and `analyzer-en_all-derived` agree on every row:

| word | count | analyzer | why |
|------|------:|----------|-----|
| `come` | 13,890 | `co\|me` | the prefix `co-` on `me`, which a mined lexicon carries as a noun. Prefix stripping has no guard of its own |
| `Come` | 3,077 | `Co\|me` | the same word, capitalised |
| `ready` | 2,541 | `read\|y` | `-y` on a real noun, and the wrong etymology. The frequency condition passes because `read` really is the commoner word |
| `SIS` | 2,047 | `SI\|S` | the two-letter noun `si` plus a plural. `sis` is not a lexeme, so there is no whole-word reading for the guard to keep and nothing it can do |
| `party` | 1,974 | `part\|y` | as `ready` |
| `understand` | 1,932 | `under\|stand` | the prefix `under-` on `stand`, which is the etymology and not the word |
| `beautiful` | 1,521 | `beaut\|iful` | the right boundary is `beauti\|ful`. The y-to-i reversal leaves the i in the piece after the stem |
| `business` | 1,419 | `bus\|iness` | the same displacement, off `busy` |
| `question` | 1,351 | `quest\|ion` | `-ion` on the verb `quest`, which is not where `question` comes from |
| `department` | 1,328 | `de\|part\|ment` | `depart\|ment` is right and `de-` is one layer too far |
| `present` | 1,326 | `pre\|sent` | the prefix again, on the past participle of `send` |
| `CHILDES` | 1,173 | `CHILD\|ES` | the corpus name, read as `child` plus a sibilant plural. The noun plural path accepts `-es` on any noun, which is the documented exception in decision 5 |
| `married` | 990 | `married` | the opposite error, unchanged from the table above: condition 4 blocks `marr\|ied` because `married` is more frequent than `marry` |

Two patterns account for most of it.

**Prefix stripping has no guard.** `come`, `understand`, `present` and
`department` are all a real prefix on a real stem where the word is not the
sum of the two, and the six conditions that settle the suffix side are not
asked on the prefix side at all. That is the largest single thing left:
`co|me` on its own is more test tokens than `only`, `Mary`, `letter` and
`broken` together, which are 12,176.

**The y-to-i reversal cuts one character early.** `beaut|iful` and
`bus|iness` are the same shape as `fac|tion`: the alternation moves a
character out of the stem and into the piece after it, and the gold puts the
boundary after the i. `Segmenter::shortest_affix_bytes` already compensates
for that when it measures an affix for the guard; the piece boundary itself
does not.

What is not in the table, deliberately: `get|ting`, `late|r`, `cr|ied` and
`fl|ies` are the documented surface conventions for consonant doubling,
e deletion and y-to-i, not errors. SEGMENTATION.md states each of them, and a
reader comparing this list against a gold that spells morphemes rather than
slices will see them as disagreements.

### Decisions, orthography fixes

16. **The four analyzer settings were re-scored and the nine baseline models
    were not.** Nothing in this round touches a trained model, a vocabulary
    file or the pretokenizer, so a baseline row would come back byte for byte
    and the run would take twenty minutes to prove it. `--only` names the four
    settings and the output goes to its own JSON, which leaves
    `baselines_v2.json` as the complete thirteen-system run it was.

17. **The "What is still wrong" table now lists errors this round did not
    cause.** The old table was scoped to what the previous round's fixes cost,
    which was the right scope for it and the wrong scope for a section called
    "what is still wrong": `co|me` is 13,890 test tokens and was never in it.
    The rows above are marked by cause rather than by which round introduced
    them, and the two patterns they fall into are named, because that is the
    next piece of work rather than a list of exceptions.
