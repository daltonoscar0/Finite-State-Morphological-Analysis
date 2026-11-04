# Scalpel on MoirfEolas, measured

> **Superseded by `docs/MOIRFEOLAS.md` (4 November 2026).** This round was
> scored before the `--exclude-single-token` protocol detail was found, so
> every number below is in the all-words framing and none of them line up
> with Jane's reference table. Its claim that the repository ships "a third
> dataset" is also wrong: it was already reading the corrected
> `MoirfEolas.csv`. The arithmetic here reproduces exactly and is kept for
> that reason, but the conclusions are drawn in `docs/MOIRFEOLAS.md`.

Adkins et al. (arXiv 2609.05022) built MoirfEolas, a 35,517 word Irish
reference table of `word, urú, prefix, suffix`, and CríochScore, a metric for
how well a subword tokenizer's splits line up with those three affixes. They
scored six tokenizer families at four vocabulary sizes and found nothing above
41%.

Scalpel already carries an Irish initial-mutation layer, `mutation_fst`, which
analyses eclipsis, the h-prefix and the t-prefix. This round puts that layer in
front of each of their twenty-three trained tokenizers and re-scores with their
own scorer.

**It wins. 45.07% against a best published baseline of 40.81%, and every one of
the twenty-three settings improves.** The gain is entirely in the urú column,
which is the one column a conventional tokenizer has no way to get right.

## Reproducing it

```bash
git clone https://github.com/jndkns01/tknzn4Irish      # ships the data and all 23 models
c++ -std=c++17 -O2 -I../Scalpel -o ga_demutate \
    tools/irish/ga_demutate_main.cpp ../Scalpel/mutation_fst.cpp
~/venvs/tok/bin/python tools/irish/run_sweep.py        # about 3 minutes
```

`tools/irish/run_criochscore.py` scores their models alone,
`run_scalpel.py` scores one family with and without the mutation layer, and
`run_sweep.py` does all twenty-three. Results land in
`~/data/tok/results/irish/`. Nothing is written inside the repository.

Their `scoring.py` and `absorption.py` are used unmodified. Only
`load_moirfeolas` is reimplemented, to avoid a pandas dependency in the 3.9
virtualenv; it has the same semantics, every cell read as text, empty affix
cells as `''`, blank words dropped.

## The metric

CríochScore is word level, not boundary level. For each word that has at least
one of the three affixes, each present affix scores 1 if it is exactly one
token, 0.5 if it is exactly the concatenation of several adjacent tokens, and 0
otherwise. The word's score is the mean over its present affixes, and
CríochScore is the mean over words.

| | count | of 35,517 |
|---|---:|---:|
| words with an urú | 5,361 | 15.1% |
| words with a prefix | 9,923 | 27.9% |
| words with a suffix | 34,402 | 96.9% |

Almost every word has a suffix, so the aggregate is dominated by the suffix
column. The urú column is the smallest of the three and the only one that is a
phonological alternation rather than concatenation.

## The system

Two layers.

**Layer 1, Scalpel's mutation FST.** `ga::demutate` returns candidate
`(tag, base)` readings for a surface form. A reading is taken when it is
eclipsis, an h-prefix or a t-prefix, and the base is a literal suffix of the
surface. The mutation material is emitted as the word's first token and the
base goes to layer 2.

**Layer 2, back-off.** Whatever conventional tokenizer is being tested encodes
the base. Unsplit words go to it whole.

The identity reading is always available, so a word with no mutation reading is
exactly what the baseline would have produced. The hybrid can therefore only
differ from its back-off on words layer 1 fires on, which is 4,760 of 35,517.

**Lenition is deliberately not split.** MoirfEolas does not mark it, for the
reason the paper gives: lenition changes the morpheme internally rather than
prefixing material to it. Splitting `bhean` as `bh|ean` would insert a boundary
the gold cannot credit and would damage the prefix and suffix columns. Scalpel
analyses lenition and this round throws that analysis away. See decision 3.

Eclipsis outranks lenition when both are available. `bhfear` gets two readings
from the over-generating analyser, `Len:bfear` and `Ecl:fear`, and only the
second is real.

## Results

Overall CríochScore, back-off alone against the hybrid.

| family | 8k | 16k | 32k | 64k |
|--------|----|-----|-----|-----|
| BPE | 33.60 → **38.11** | 27.19 → **31.91** | 19.45 → **24.61** | 9.79 → **15.56** |
| Byte-BPE | 36.32 → **41.57** | 29.72 → **34.99** | 21.60 → **27.01** | 14.18 → **19.83** |
| SentencePieceBPE | 36.13 → **41.37** | 29.49 → **34.79** | 21.41 → **26.80** | 12.89 → **18.63** |
| SentencePieceULM | 40.81 → **45.07** | 34.18 → **38.86** | 28.21 → **33.13** | not scored, see below |
| UnigramLM | 40.72 → **42.85** | 34.92 → **37.80** | 29.17 → **32.95** | 28.55 → **32.29** |
| WordPiece | 33.45 → **39.28** | 28.16 → **33.75** | 20.80 → **26.28** | 12.47 → **18.13** |

Every cell improves, by between 2.13 and 5.83 points. The best setting is
SentencePieceULM 8k, 40.81% to 45.07%.

SentencePieceULM ships at 8k, 16k, 32k and 43k rather than 64k, so its fourth
column is left out rather than filled with a size nothing else has.

Two of the paper's findings survive unchanged. Unigram is the strongest
conventional family, and the smallest vocabulary is the best one: a larger
vocabulary stores more whole words, so it cuts less often. Both hold in the
hybrid column too.

The size of the gain runs opposite to the back-off's own urú ability. WordPiece
8k absorbs 98.8% of urú on its own and gains 5.83 points; UnigramLM 8k absorbs
46.9% and gains 2.13. The layer is supplying the same thing to all of them, and
it is worth more to the ones that had less of it.

### Where the gain is

SentencePieceULM 8k, by category.

| category | back-off | hybrid | delta |
|----------|---------:|-------:|------:|
| uru_only | 50.00 | 77.78 | +27.78 |
| prefix_only | 57.23 | 57.23 | 0.00 |
| suffix_only | 40.39 | 40.39 | +0.01 |
| uru+prefix | 25.00 | 65.00 | +40.00 |
| uru+suffix | 35.21 | 64.71 | +29.50 |
| prefix+suffix | 46.20 | 46.20 | 0.00 |
| all_affixes | 28.30 | 52.39 | +24.09 |

Every category containing an urú moves and no category without one does, which
is what the construction predicts and is worth stating as a check rather than a
result.

Urú absorption, the paper's own diagnostic for the failure mode, goes from
80.94% to 12.26% at this setting, and to 12.26% from wherever it started at
every other setting, since layer 1 does not depend on the back-off.

### The urú column on its own

Of the 5,361 words the gold gives an urú:

| | count | share |
|---|---:|---:|
| Scalpel recovers it exactly | 4,700 | 87.7% |
| Scalpel proposes no split | 661 | 12.3% |
| Scalpel proposes different material | 0 | 0% |

Zero wrong splits is the number worth keeping. The layer declines rather than
guesses, which is the same shape as the English analyzer's precision on
MorphoLex.

The 661 misses are four specific things, not a long tail:

| cause | count | examples |
|-------|------:|----------|
| `d'` and `D'`, no rule in Scalpel | 639 | `d'fhéach`, `D'eisigh` |
| `n-` before `d` or `g`, non-standard spelling | 15 | `n-dara`, `n-domhan` |
| `t-` before `s`, non-standard spelling | 3 | `t-samhradh` |
| gold error | 4 | `téad`, `tuartha` |

`d'` is the past-tense verbal particle rather than an initial mutation, which is
why Scalpel does not have it. Simulating the one rule that would catch it takes
urú absorption from 12.26% to 0.34% and the overall score from 45.07% to
45.52%. That is measured but not implemented; see decision 4.

The 15 `n-dara` cases and the 3 `t-samhradh` cases are hyphenations the
Caighdeán does not use, `ndara` and `tsamhradh` being standard. Scalpel accepts
the hyphen before a vowel, which is standard, but not before a consonant.

### Two gold standard problems found

**Scalpel finds 60 urú the gold misses.** All are eclipsis of a capitalised
vowel written without a hyphen: `nIasacht`, `nUachtarán`, `nAcht`,
`nAthbhreithnithe`. The `SPECIAL_PREFIXES` table in `affixes.py` maps the bare
`n` to `["d","g","D","G"]` only, so eclipsed vowels are reachable only through
the hyphenated `n-` entry, and these 60 fall through. They are real eclipsis and
the hybrid is penalised for splitting them correctly.

**Four rows mark an urú that is not there.** `téad` (rope) and `tuartha` are
ordinary t-initial words, read by the affix builder as `t` plus a vowel. This
is the mirror of the same gap: the rule that catches `tAthair` has no case
condition.

Both lists are in `~/data/tok/results/irish/uru_disagreements.csv`, 721 rows,
every word where Scalpel and the gold disagree about the urú column.

## What this does not show

**The suffix column is untouched and it is 96.9% of the data.** Scalpel has no
Irish derivational morphology. The hybrid's suffix score is its back-off's
suffix score to two decimal places. The headline 45.07% is a mutation result
wearing an aggregate's clothing, and the honest way to report it is the category
table above, not the single number.

**The ceiling on this approach is low without suffixes.** If layer 1 recovered
every urú perfectly and nothing else changed, SentencePieceULM 8k would reach
about 45.5%. Everything above that has to come from the suffix and prefix
columns.

**No language model was trained.** This is intrinsic alignment only, the same
scope as the paper. Jane's proposed downstream MT experiments are where the
question of whether any of this matters gets answered.

**The MoirfEolas shipped in the repo is a third dataset.** It is not the
paper's (the README says the English filter differs) and it is not the
corrected one in Jane's email of 28 October, which moves Unigram 8k to 43.3%
or 43.4%. Reproducing her corrected figure is a prerequisite for putting any of
this in a paper. See decision 5.

## Decisions

1. **Their scorer is used unmodified.** `scoring.py` and `absorption.py` are
   imported from a clone rather than reimplemented, so the baseline column of
   the table above is their number and not an approximation of it. The six
   baselines were run first as a check: Unigram comes out top at every size,
   the smallest vocabulary wins, and Unigram 8k lands at 40.72% against the
   paper's 40.55%. That agreement is what licenses the rest.

2. **The back-off sees the base, not the surface.** For a split word, layer 2
   encodes `críochnaithe` rather than `gcríochnaithe`. The alternative, encoding
   the surface and then cutting the first token, would leave the back-off
   conditioning on a mutated form it was trained to see whole. Encoding the base
   is also what a real hybrid would do, since the point of the layer is to hand
   the statistical model a canonical form.

3. **Lenition is analysed and discarded.** Scalpel can read `bhean` as
   `Len:bean`, and this round does not use it. MoirfEolas marks no lenition, so
   every lenition split would be an unrewarded boundary that also shifts the
   prefix column's start index. This is the one place where Scalpel's coverage
   exceeds what the benchmark can measure, and it is the argument for extending
   the benchmark rather than the tokenizer.

4. **The `d'` rule is measured, not shipped.** 639 of the 661 misses are `d'`,
   and a rule for it is three lines. It is left out because `d'` is a verbal
   particle, not an initial mutation, and `mutation_fst` is documented as a
   pure mutation layer with no lexicon and no triggers. Putting a particle in it
   would be the first exception to that contract. The 45.52% it would buy is
   recorded so the decision can be taken on purpose.

5. **The repository's MoirfEolas is scored, not the paper's.** It is the only
   one available. Every number here is therefore internally consistent and not
   directly comparable with either published table, and the gap to Jane's
   corrected figures is an open item rather than a discrepancy to explain away.

## Open questions for Jane

1. Which MoirfEolas produced the 28 October numbers, and can it be shared? The
   repository copy gives UnigramLM 8k 40.72% where the email says 43.32% and the
   text says 43.43%.
2. The 60 missed eclipsed capitalised vowels and the 4 spurious `t` rows look
   like a one-line fix to `SPECIAL_PREFIXES`. Worth a patch?
3. Is the `n-dara` spelling intentional in the corpus, or an artifact of the
   ParaCrawl extraction?
4. Lenition: Scalpel analyses it already. Is extending MoirfEolas to mark it
   something worth doing together, and does it belong in the LoResLM submission
   or after it?
5. She asked how Scalpel is built and whether it is Irish-only. It is not: the
   mutation layer is Irish, the tokenizer and segmenter are language neutral,
   and the morphological analyzer on top of it is English. See `docs/BASELINES.md`.

## The English half, for context

Unchanged since 20 October, all four test binaries green. Current state on the
independent gold, `analyzer-en_all-derived`, 12,828 MorphoLex forms:

| | P | R | F1 |
|---|--:|--:|---:|
| MorphoLex (independent) | 0.976 | 0.385 | 0.552 |
| MorphyNet inflection (in distribution) | 0.807 | 0.870 | 0.838 |
| MorphyNet derivation (in distribution) | 0.802 | 0.423 | 0.554 |

The shape is the same as the Irish result: very high precision, recall limited
by what the rule set reaches. On English the largest remaining gap is compounds,
51.3% of missed boundaries. On Irish it is suffixes, which the layer does not
attempt at all.
