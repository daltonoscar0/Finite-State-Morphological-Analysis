# Scalpel on the corrected MoirfEolas

Re-run of the Irish hybrid against Jane Adkins' MoirfEolas benchmark and
CríochScore metric, on the corrected dataset she published on 28 October 2026.
Everything in `docs/IRISH.md` predates that correction and predates the
protocol finding in step 3 below. Treat this file as the current numbers and
`docs/IRISH.md` as superseded.

## Inventory

What already existed in this repository before this round.

| what | path |
|------|------|
| Irish initial-mutation FST | `~/code/Scalpel/mutation_fst.{h,cpp}` (sibling repo, not this one) |
| bridge binary source | `tools/irish/ga_demutate_main.cpp` |
| built bridge | `ga_demutate` (repo root, untracked, now gitignored) |
| baseline scoring driver | `tools/irish/run_criochscore.py`, retired |
| hybrid scoring driver | `tools/irish/run_scalpel.py`, retired |
| full sweep driver | `tools/irish/run_sweep.py`, retired |
| earlier write-up | `docs/IRISH.md` |
| earlier results | `~/data/tok/results/irish/` |

**Cached dataset.** There was no cached copy of MoirfEolas anywhere under
`~/code/morphology` or `~/data`. The earlier run cloned `tknzn4Irish` to
`tools/irish/tknzn4Irish` and that clone no longer exists, so there was
nothing to delete. The four stale result files in `~/data/tok/results/irish/`
were renamed with a `STALE_PRECORRECTION_` prefix rather than deleted, so the
old numbers stay auditable but cannot be picked up by a path that expects the
old names:

```
STALE_PRECORRECTION_results_baselines.json
STALE_PRECORRECTION_results_scalpel_8k.json
STALE_PRECORRECTION_results_sweep.json
STALE_PRECORRECTION_uru_disagreements.csv
```

New results for this round go to `~/data/tok/results/irish/corrected/`.

**Retired code.** The three Python drivers under `tools/irish/` are deleted.
They reimplemented her MoirfEolas loader and they expected the clone at
`tools/irish/tknzn4Irish`, so both their metric and their paths are wrong for
this round. They are replaced by `scripts/moirfeolas_score.py`,
`scripts/moirfeolas_error_analysis.py`, `scripts/moirfeolas_email.py` and
`scripts/irish_lenition_candidates.py`, which is also where the rule for this
round puts wrappers around her code.
`tools/irish/ga_demutate_main.cpp`, the bridge to Scalpel's FST, is kept
unchanged.

## The benchmark

```bash
git clone https://github.com/jndkns01/tknzn4Irish external/tknzn4Irish
```

Scored against commit **`0f31695c5315ce213cf4d5986a9c9c002ed740ef`**
("Update README.md", 28 Oct 2026 14:56:12 +0000), which is `main` as of
4 November 2026.

`external/` is gitignored rather than vendored. The clone is 207 MB and the
rule for this round is that nothing inside it is edited, so there is no reason
for it to live in a commit. The commit hash above is the reproducibility
anchor instead.

### Which dataset file

The repository ships two MoirfEolas tables, and they are not interchangeable:

| file | rows | what it is |
|------|-----:|------------|
| `data/MoirfEolas/MoirfEolas.csv` | 35,517 | the paper's dataset, with the 28 October corrections applied |
| `data/MoirfEolas/MoirfEolas_replicable.csv` | 41,195 | rebuilt from the repo's own corpus with a different English filter |

`MoirfEolas.csv` is the one that reproduces her reference numbers. Its history
in her repo is `616cc70` (paper dataset added, 35,575 rows), `40c92d6`
(2 rows changed), `9fed382` ("cleaning data", down to 35,517 rows). That last
commit is the correction.

Affix coverage of the corrected table, which is unchanged from what
`docs/IRISH.md` recorded:

| | count | of 35,517 |
|---|---:|---:|
| words with an urú | 5,361 | 15.1% |
| words with a prefix | 9,923 | 27.9% |
| words with a suffix | 34,402 | 96.9% |

Every row has at least one affix.

### Environment

`~/venvs/tok` (Python 3.9.6) with `tokenizers` 0.22.2, `sentencepiece` 0.2.2
and `pandas` 2.3.3. pandas was added for this round specifically so that her
`moirfeolas.load_moirfeolas` can be used unmodified; the earlier run
reimplemented the loader to avoid the dependency. Her package is reached by
`PYTHONPATH=external/tknzn4Irish/src` and invoked as
`python -m criochscore.cli`, so `pip install -e .` never runs and nothing is
written inside her repo.

## Step 3: reproducing her Unigram 8k baseline

Her scoring code is used as-is: `criochscore.cli`, `scoring.py`,
`absorption.py`, `tokenizers_io.py` and `moirfeolas.py` are all hers, loaded
from the clone. No part of the metric is reimplemented in this round.

The gate was to reproduce **43.32%** for Unigram 8k. It reproduces exactly, on
all eight figures, and the protocol detail that makes it reproduce is
`--exclude-single-token`:

```bash
export PYTHONPATH=external/tknzn4Irish/src
~/venvs/tok/bin/python -m criochscore.cli score \
    --moirfeolas external/tknzn4Irish/data/MoirfEolas/MoirfEolas.csv \
    --tokenizer external/tknzn4Irish/tokenizers/UnigramLM/unigram_tokenizer_8k.json \
    --model-type standard --no-special-token --exclude-single-token \
    --output-dir ~/data/tok/results/irish/corrected/baselines
```

| | overall | uru_only | prefix_only | suffix_only | uru+prefix | uru+suffix | prefix+suffix | all_affixes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Jane's reference | 43.32 | 50.00 | 66.12 | 40.70 | 43.03 | 50.83 | 46.81 | 35.98 |
| reproduced here | 43.32 | 50.00 | 66.12 | 40.70 | 43.03 | 50.83 | 46.81 | 35.98 |

Eight of eight, to two decimal places.

### What the earlier run got wrong

`docs/IRISH.md` reported 40.72% for Unigram 8k and concluded that the
repository shipped "a third dataset" that was neither the paper's nor the
corrected one. That conclusion was wrong. The earlier run was already reading
the corrected `MoirfEolas.csv` (its affix counts, 5,361 / 9,923 / 34,402, are
exactly the corrected table's). The entire 40.72 to 43.32 gap is the
`--exclude-single-token` flag, which the earlier run did not pass and her
reference numbers do.

The full probe, Unigram 8k, both dataset files against both modes:

| dataset | all words | `--exclude-single-token` |
|---------|----------:|-------------------------:|
| `MoirfEolas.csv` | 40.72 | **43.32** |
| `MoirfEolas_replicable.csv` | 40.43 | 43.37 |

The replicable table lands at 43.37%, within rounding distance of 43.32% on
the overall figure alone. Checking only the headline number would not have
identified the dataset; the category breakdown does, since the replicable
table has no `uru_only` category at all. This is the reason the gate was run
on all eight figures rather than one.

`--exclude-single-token` drops 2,132 of 35,517 words at this setting, leaving
33,385. A word the tokenizer emits whole cannot show boundary behaviour, so it
scores 0 on each affix it has and drags the mean down. This flag matters for
the hybrid comparison and is dealt with explicitly in step 4.

### Her three reference rows do not share one protocol

Two of her three rows reproduce exactly, and they need different flags.

| row | reproduces at | with |
|-----|--------------:|------|
| Unigram 8k | 43.32 | `--exclude-single-token`, no lowercasing |
| gaBERT | 17.88 | all words, **plus** `--do-lower-case` |
| Morfessor | not reproduced | no Morfessor model ships in the repo |

gaBERT:

```bash
~/venvs/tok/bin/python -m criochscore.cli score \
    --moirfeolas external/tknzn4Irish/data/MoirfEolas/MoirfEolas.csv \
    --tokenizer DCU-NLP/bert-base-irish-cased-v1 --model-type huggingface \
    --special-token "##" --do-lower-case \
    --output-dir ~/data/tok/results/irish/corrected/baselines
```

That gives 17.88 / 50.00 / 29.21 / 17.27 / 8.57 / 14.61 / 22.06 / 9.61, eight
of eight again. Under `--exclude-single-token` gaBERT is 29.59 instead, and
without `--do-lower-case` it is 23.00. So the gaBERT row and the Unigram row
in her 28 October table are not mutually comparable, and this is worth asking
her about before anything goes in a paper next to them.

Everything below uses the Unigram protocol, since Unigram is the row the
hybrid is actually competing with and the only one whose back-off ships in
her repo.

## Step 4: the hybrid

```bash
c++ -std=c++17 -O2 -I$HOME/code/Scalpel -o ga_demutate \
    tools/irish/ga_demutate_main.cpp $HOME/code/Scalpel/mutation_fst.cpp
~/venvs/tok/bin/python scripts/moirfeolas_score.py \
    --systems backoff hybrid fst_only identity \
    --out ~/data/tok/results/irish/corrected/her_protocol.json \
    --dump-per-word ~/data/tok/results/irish/corrected/per_word_her_protocol.json
```

About a minute. `scripts/moirfeolas_score.py` builds the token streams and
hands them to her `find_morphological_info`, `calculate_morphological_score`
and `compute_absorption`. It applies the same single-token filter her CLI
applies, copied from `cli.py`.

Layer 1 fires on 4,760 of 35,517 words (13.4%).

### Overall CríochScore, all 23 settings

| family | 8k | 16k | 32k | 43k | 64k |
|--------|----|-----|-----|-----|-----|
| BPE | 35.85 → **40.45** | 31.68 → **36.63** | 27.94 → **33.71** | | 23.47 → **31.92** |
| Byte-BPE | 38.19 → **43.50** | 33.61 → **39.08** | 29.36 → **35.29** | | 26.67 → **33.80** |
| SentencePieceBPE | 38.05 → **43.36** | 33.39 → **38.88** | 28.95 → **34.90** | | 26.00 → **33.52** |
| SentencePieceULM | 42.84 → **47.11** | 37.51 → **42.29** | 32.84 → **38.00** | 31.71 → **36.97** | |
| UnigramLM | **43.32** → **45.46** | 39.04 → **41.95** | 34.61 → **38.49** | | 33.96 → **37.80** |
| WordPiece | 35.23 → **41.17** | 31.99 → **37.84** | 28.71 → **34.81** | | 25.63 → **33.16** |

All 23 improve, by between 2.14 and 8.45 points. `UnigramLM 8k = 43.32` is
Jane's reference cell. SentencePieceULM ships at 43k rather than 64k.

Two of the paper's findings hold on the corrected data and in the hybrid
column too: Unigram is the strongest conventional family, and the smallest
vocabulary is the best one.

**Best conventional setting 43.32, best hybrid setting 47.11**, both at 8k.
Against her own best reference row the headline gain is +3.79 points; against
the same back-off it is +4.27.

### The two headline settings by category

| system | overall | uru_only | prefix_only | suffix_only | uru+prefix | uru+suffix | prefix+suffix | all_affixes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Unigram 8k alone | 43.32 | 50.00 | 66.12 | 40.70 | 43.03 | 50.83 | 46.81 | 35.98 |
| Unigram 8k + Scalpel | **45.46** | 77.78 | 66.12 | 40.70 | 66.67 | 63.00 | 46.81 | 50.17 |
| SP-ULM 8k alone | 42.84 | 50.00 | 63.21 | 42.59 | 26.52 | 36.10 | 48.28 | 29.09 |
| SP-ULM 8k + Scalpel | **47.11** | 77.78 | 63.21 | 42.60 | 65.00 | 64.71 | 48.28 | 52.39 |

Every category that contains an urú moves; `prefix_only`, `suffix_only` and
`prefix+suffix` do not move at all. That is what the construction predicts,
and it is a check rather than a result.

The categories are very unevenly sized, and the two the hybrid moves most are
the two smallest. Denominators under her protocol at Unigram 8k:

| category | words |
|----------|------:|
| suffix_only | 20,633 |
| prefix+suffix | 6,726 |
| uru+suffix | 3,374 |
| all_affixes | 1,664 |
| prefix_only | 760 |
| uru+prefix | 201 |
| uru_only | 27 |

`uru_only` is 27 words, so its 50.00 → 77.78 is 13.5 of 27 going to 21 of 27.
Nothing should be concluded from that column on its own. `uru+suffix` at
3,374 words and `all_affixes` at 1,664 are where the gain actually lives.

It is also the honest reading of the headline: the suffix column is 96.9% of
the data, Scalpel has no Irish derivational morphology, and the hybrid's
suffix score is its back-off's suffix score. 47.11% is a mutation result
wearing an aggregate's clothing.

### The single-token filter does not confound the comparison

`--exclude-single-token` filters on each system's own token count, so the
hybrid and its back-off are scored on slightly different word sets: a word
the back-off emits whole but the hybrid splits enters the hybrid's set and
not the back-off's. At Unigram 8k that is 33,480 words against 33,385.

Scored on one identical word set instead, filtered by the back-off's token
count for both systems:

```bash
~/venvs/tok/bin/python scripts/moirfeolas_score.py --matched-set \
    --systems backoff hybrid \
    --out ~/data/tok/results/irish/corrected/matched_set.json
```

| setting | her protocol | matched word set | difference |
|---------|-------------:|-----------------:|-----------:|
| Unigram 8k + Scalpel | 45.46 | 45.44 | 0.02 |
| SP-ULM 8k + Scalpel | 47.11 | 47.10 | 0.01 |

Two hundredths of a point. The headline numbers can be read as a like-for-like
comparison.

## Step 5: absorption

Her `absorption.py`, reached two ways. The wrapper can write her summary
format so her own `criochscore absorption` subcommand runs on it unmodified:

```bash
~/venvs/tok/bin/python scripts/moirfeolas_score.py \
    --families UnigramLM --sizes 8k --systems backoff hybrid \
    --summary-dir ~/data/tok/results/irish/corrected/summaries

~/venvs/tok/bin/python -m criochscore.cli absorption \
    --summary ~/data/tok/results/irish/corrected/summaries/backoff_UnigramLM_8k_morphological_summary.json \
              ~/data/tok/results/irish/corrected/summaries/hybrid_UnigramLM_8k_morphological_summary.json \
    --moirfeolas external/tknzn4Irish/data/MoirfEolas/MoirfEolas.csv \
    --output-dir ~/data/tok/results/irish/corrected
```

Both routes agree to the digit.

| system | urú absorbed | urú mapping | suffix fully | suffix split | prefix fully | prefix split |
|--------|-------------:|------------:|-------------:|-------------:|-------------:|-------------:|
| Unigram 8k alone | 45.97 | 3.32 | 32.85 | 24.05 | 35.69 | 4.34 |
| Unigram 8k + Scalpel | **12.26** | **0.00** | 33.05 | 23.93 | 33.86 | 4.34 |
| SP-ULM 8k alone | 80.42 | 22.07 | 35.38 | 21.22 | 35.83 | 4.12 |
| SP-ULM 8k + Scalpel | **12.26** | **0.00** | 36.04 | 21.51 | 34.93 | 3.97 |

Eclipsis absorption is the failure mode the layer exists to remove, and it
goes to 12.26% from wherever it started. It is the same 12.26% for every
back-off, because layer 1 does not depend on layer 2. Urú mapping, the
"fused with the next letter" case such as `bhf`, goes to exactly zero.

Affix absorption barely moves in either direction, which is the same
statement as the unchanged prefix and suffix columns.

## Step 6: ablations

The filter has to come off for this table. Under `--exclude-single-token` the
FST-alone system is scored on only the 4,760 words it splits, and the
no-segmentation system is scored on nothing at all, so neither row means
anything. All-words mode puts every system on the same 35,517 words.

```bash
~/venvs/tok/bin/python scripts/moirfeolas_score.py --all-words \
    --systems backoff hybrid fst_only identity \
    --out ~/data/tok/results/irish/corrected/all_words.json \
    --dump-per-word ~/data/tok/results/irish/corrected/per_word_all_words.json
```

| system | overall | uru_only | prefix_only | suffix_only | uru+prefix | uru+suffix | prefix+suffix | all_affixes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| no segmentation at all | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| mutation FST alone | 5.97 | 55.56 | 0.00 | 0.00 | 45.24 | 43.68 | 0.00 | 30.14 |
| Unigram 8k alone | 40.72 | 50.00 | 57.23 | 37.94 | 41.19 | 49.97 | 44.07 | 35.38 |
| FST + Unigram 8k | 42.85 | 77.78 | 57.23 | 37.94 | 66.67 | 63.00 | 44.07 | 50.17 |
| SP-ULM 8k alone | 40.81 | 50.00 | 57.23 | 40.39 | 25.00 | 35.21 | 46.20 | 28.30 |
| FST + SP-ULM 8k | 45.07 | 77.78 | 57.23 | 40.39 | 65.00 | 64.71 | 46.20 | 52.39 |

The attribution is clean. The FST alone gets 5.97% because it scores zero on
both affix columns and 96.9% of the data has a suffix; it is strong only
where it is competent, reaching 55.56% on `uru_only` and 45.24% on
`uru+prefix` with no statistical model at all. The subword tokenizer alone
gets 40.72%. Neither layer carries the result, and the hybrid is not
explained by either one of them.

These all-words numbers are also the direct check against the earlier run:
`docs/IRISH.md` reported 40.81 → 45.07 for SentencePieceULM 8k and 40.72 →
42.85 for UnigramLM 8k, which is what came out again here to two decimals.
The earlier round's arithmetic was right; only its protocol and its reading of
which dataset it had were wrong.

## Step 7: error analysis

```bash
~/venvs/tok/bin/python scripts/moirfeolas_error_analysis.py \
    --per-word ~/data/tok/results/irish/corrected/per_word_all_words.json \
    --out ~/data/tok/results/irish/corrected/error_analysis.json
```

Run on the all-words dump, so that both systems have an opinion about all
35,517 words and every word is comparable.

### There are no words where the hybrid is worse than Unigram 8k

The task asked for 50 of them. There are none.

| comparison | worse | better | identical |
|------------|------:|-------:|----------:|
| hybrid vs Unigram 8k, same back-off | **0** | 1,807 | 33,710 |
| hybrid vs SP-ULM 8k, same back-off | 222 | 3,372 | 31,923 |

With Unigram 8k as the back-off, layer 1 does not cost a single word. That is
not luck and it is not a bug in the comparison; it follows from three things.
Layer 1 declines rather than guesses, so it emits no wrong mutation material
anywhere in the dataset (0 of 5,361, below). Her `classify_prefix` starts at
token index 1 exactly when the gold marks an urú, so a correct urú split
leaves the prefix test aligned instead of shifted. And Unigram 8k happens to
re-segment demutated bases the same way it segments the surface form, which
is the part that does not hold for SentencePieceULM.

So the buckets below are 50 sampled from the 1,807 better, and then, because
a one-sided table is not an error analysis, 50 sampled from the 222 words the
best hybrid setting does make worse. Sampling is `random.Random(20261104)`
over the sorted word list, so it is reproducible.

### 50 better, hybrid vs Unigram 8k

All 1,807 fall in one bucket, and all 50 sampled do too.

| bucket | of 1,807 | in sample |
|--------|---------:|----------:|
| eclipsis handling, urú recovered | 1,807 | 50 |
| lenition handling | 0 | 0 |
| lexicon gap | 0 | 0 |
| over-segmentation | 0 | 0 |
| other | 0 | 0 |

| word | gold | Unigram 8k | hybrid |
|------|------|-----------|--------|
| `bPríomh-Acht` | `b` / — / `t` | `bPríomh \| - \| Acht` → 0.00 | `b \| Príomh \| - \| Acht` → 0.50 |
| `bPaitinní` | `b` / — / `í` | `bP \| a \| it \| inn \| í` → 0.50 | `b \| Pa \| it \| inn \| í` → 1.00 |
| `n-iomlán` | `n-` / — / `án` | `n \| - \| iomlán` → 0.25 | `n- \| iomlán` → 0.50 |

Three mechanisms inside that one bucket, worth separating by eye: the
eclipsis letter unfused from a following capital (`bP` to `b` + `P`), the
hyphen reattached to the mutation rather than left stranded (`n` + `-` to
`n-`), and the urú peeled off a long first token (`gcomhaont` to `g` +
`comhaont`).

### 50 worse, hybrid vs SentencePieceULM 8k

| bucket | of 222 | in sample |
|--------|-------:|----------:|
| base re-encoding | 171 | 44 |
| over-segmentation | 49 | 6 |
| eclipsis handling (gold marks no urú) | 2 | 0 |
| lenition handling | 0 | 0 |
| lexicon gap | 0 | 0 |
| other | 0 | 0 |

**base re-encoding**, 171. Layer 2 encodes the demutated base in isolation
and segments it differently from how it segmented the surface form. This is
the cost of decision 2 and it has nothing to do with the mutation itself: the
urú is right in all 171.

| word | gold | SP-ULM 8k | hybrid |
|------|------|-----------|--------|
| `bpota` | `b` / — / `ta` | `b \| po \| ta` → 1.00 | `b \| pota` → 0.50 |
| `dtairbhe` | `d` / — / `e` | `d \| tair \| bh \| e` → 1.00 | `d \| tairbhe` → 0.50 |
| `bpoiblíocht` | `b` / — / `íocht` | `b \| po \| ib \| l \| íocht` → 1.00 | `b \| poiblí \| ocht` → 0.50 |

In every case the base is short enough that the 8k vocabulary holds it whole,
so the suffix boundary the surface form happened to expose disappears.

**over-segmentation**, 49. Two of the three examples are the empty-token
defect described below, not a segmentation choice.

| word | gold | SP-ULM 8k | hybrid |
|------|------|-----------|--------|
| `dtaisbeánfar` | `d` / `tais` / `far` | `d \| tais \| be \| án \| far` → 1.00 | `d \| ␀ \| tais \| be \| án \| far` → 0.83 |
| `dtaisbeántar` | `d` / `tais` / `tar` | `d \| tais \| be \| án \| tar` → 1.00 | `d \| ␀ \| tais \| be \| án \| tar` → 0.83 |
| `hais` | `h` / `ais` / `ais` | `h \| ais` → 1.00 | `h \| a \| is` → 0.67 |

**eclipsis handling where the gold marks no urú**, 2. `nIarnród` and
`nIarnróid`. Real eclipsis of a capitalised vowel, which the gold does not
record, so the correct split is scored as an error. The same gap produces 3
wins elsewhere.

### The urú column on its own

Of the 5,361 words the gold gives an urú:

| | count | share |
|---|---:|---:|
| Scalpel recovers it exactly | 4,700 | 87.7% |
| Scalpel proposes no split | 661 | 12.3% |
| Scalpel proposes different material | **0** | 0% |

Zero wrong splits, on the corrected data as on the old. The 661 misses are
four specific things:

| cause | count | examples |
|-------|------:|----------|
| `d'` and `D'`, no rule in Scalpel | 639 | `d'fhéach`, `D'eisigh`, `d'fharraigí` |
| `n-` before a consonant, non-standard spelling | 15 | `n-dara`, `n-domhan`, `n-gliú` |
| `t-` before a consonant, non-standard spelling | 3 | `t-samhradh`, `t-saoróg` |
| gold error | 4 | `téad`, `tuartha`, `tiarratas`, `tiompar` |

### Lenition and the lexicon gap cannot appear in the buckets

Both are cases where layer 1 declines, and declining leaves the word
byte-identical, so neither can change a score. They are the two reasons the
layer passes up a word, reported separately:

| | count |
|---|---:|
| lenition-only reading, discarded by design | 6,803 |
| of those, where the gold marks an urú | **0** |
| gold marks an urú and layer 1 has no reading | 661 |

The zero is the number that matters. Discarding lenition on 6,803 words costs
nothing measurable on this benchmark, because MoirfEolas never records
lenition in the urú column. Decision 3 survives the correction intact.

### Two gold standard problems, unchanged by the correction

**Scalpel finds 60 urú the gold misses**, all eclipsis of a capitalised vowel
written without a hyphen: `nAcht`, `nAfracánach`, `nIasacht`, `nUachtarán`.
The `SPECIAL_PREFIXES` table in `affixes.py` maps the bare `n` to
`["d","g","D","G"]`, so eclipsed vowels are reachable only through the
hyphenated `n-` entry and these 60 fall through. They are real eclipsis and
the hybrid is penalised for splitting them correctly.

**Four rows mark an urú that is not there.** `téad` (rope), `tuartha`,
`tiarratas` and `tiompar` are ordinary t-initial words read as `t` plus a
vowel. The mirror of the same gap: the rule that catches `tAthair` has no
case condition.

Both lists are in `error_analysis.json` under `uru_column`.

## A defect in her scoring code, left in place

`remove_specials` strips a leading `▁` from the first token unconditionally.
When sentencepiece emits `▁` as a standalone piece, which it does for
`taisbeánfar` → `['▁', 'tais', 'be', 'án', 'far']`, the first token becomes
the empty string, and an empty first token then fails `classify_uru` and
`classify_prefix` on position.

| setting | words with an empty token, baseline | hybrid |
|---------|------------------------------------:|-------:|
| SentencePieceULM 8k | 3,605 | 4,350 |
| SentencePieceULM 43k | 1,259 | 1,611 |
| SentencePieceBPE 8k | 113 | 140 |
| Byte-BPE 8k | 107 | 134 |
| UnigramLM, all sizes | 0 | 0 |
| BPE and WordPiece, all sizes | 0 | 0 |

**This is not fixed and not compensated for.** It is her code, it affects her
published baseline exactly as much as it affects the hybrid, and patching it
would mean the numbers above no longer compare to anything she has reported.
It is raised with her instead.

The practical consequence is that the Unigram comparison is the clean one and
the SentencePieceULM comparison is not, which is the reason `UnigramLM 8k` is
the row quoted against her reference even though `SentencePieceULM 8k` is the
better headline. It also accounts for most of the 49 over-segmentation
regressions above.

## Decisions

1. **Her code runs the metric, all of it.** `scoring.py`, `absorption.py`,
   `tokenizers_io.py` and `moirfeolas.py` are imported from the clone. The
   earlier round reimplemented `load_moirfeolas` to avoid pandas; pandas is
   now installed in `~/venvs/tok` and her loader is used instead, so no part
   of the metric is a reimplementation. Her package is reached by
   `PYTHONPATH`, never `pip install -e .`, so nothing is written inside her
   repository.

2. **The back-off sees the base, not the surface.** For a split word layer 2
   encodes `críochnaithe`, not `gcríochnaithe`. The alternative, encoding the
   surface and cutting the first token, would leave layer 2 conditioning on a
   mutated form it was trained to see whole. This costs 171 words at
   SentencePieceULM 8k and 0 at Unigram 8k, measured above, and it is the one
   decision here with a quantified price.

3. **Lenition is analysed and discarded.** Scalpel reads `bhean` as
   `Len:bean` and this round throws it away, on 6,803 words. MoirfEolas marks
   no lenition, and the count of discarded lenition readings that the gold
   would have credited is 0, so the discard is free on this benchmark. It is
   an argument for extending the benchmark, not the tokenizer.

4. **The `d'` rule stays unimplemented.** 639 of the 661 misses are `d'`.
   `d'` is the past-tense verbal particle, not an initial mutation, and
   `mutation_fst` is documented as a pure mutation layer with no lexicon and
   no triggers. Adding a particle would be the first exception to that
   contract, so it is left out on purpose rather than by oversight.

5. **`--exclude-single-token` on, because that is what reproduces her row.**
   It is a defensible choice on its own terms, since a word emitted whole
   cannot show boundary behaviour, but the reason it is on here is
   reproduction and nothing else. All-words numbers are reported alongside
   throughout, and the matched-set control shows the flag does not confound
   the hybrid delta.

6. **The empty-token defect is reported, not patched.** See above.

7. **`external/` is gitignored rather than vendored.** 207 MB, and the rule
   for this round is that nothing in it is edited. The commit hash is the
   anchor.

## Reproducing every number in this file

```bash
cd ~/code/morphology

# 1. the benchmark
git clone https://github.com/jndkns01/tknzn4Irish external/tknzn4Irish
git -C external/tknzn4Irish checkout 0f31695c5315ce213cf4d5986a9c9c002ed740ef

# 2. the environment
~/venvs/tok/bin/pip install pandas transformers   # tokenizers, sentencepiece already present

# 3. layer 1
c++ -std=c++17 -O2 -I$HOME/code/Scalpel -o ga_demutate \
    tools/irish/ga_demutate_main.cpp $HOME/code/Scalpel/mutation_fst.cpp

export PYTHONPATH=external/tknzn4Irish/src
R=~/data/tok/results/irish/corrected

# 4. the reproduction gate: her Unigram 8k row, 43.32
~/venvs/tok/bin/python -m criochscore.cli score \
    --moirfeolas external/tknzn4Irish/data/MoirfEolas/MoirfEolas.csv \
    --tokenizer external/tknzn4Irish/tokenizers/UnigramLM/unigram_tokenizer_8k.json \
    --model-type standard --no-special-token --exclude-single-token \
    --output-dir $R/baselines

# 5. her gaBERT row, 17.88, on the other protocol
~/venvs/tok/bin/python -m criochscore.cli score \
    --moirfeolas external/tknzn4Irish/data/MoirfEolas/MoirfEolas.csv \
    --tokenizer DCU-NLP/bert-base-irish-cased-v1 --model-type huggingface \
    --special-token "##" --do-lower-case --output-dir $R/baselines

# 6. the hybrid and the ablations, three framings
~/venvs/tok/bin/python scripts/moirfeolas_score.py \
    --systems backoff hybrid hybrid_splinter fst_only identity \
    --out $R/her_protocol.json --dump-per-word $R/per_word_her_protocol.json
~/venvs/tok/bin/python scripts/moirfeolas_score.py --matched-set \
    --systems backoff hybrid --out $R/matched_set.json
~/venvs/tok/bin/python scripts/moirfeolas_score.py --all-words \
    --systems backoff hybrid hybrid_splinter fst_only identity \
    --out $R/all_words.json --dump-per-word $R/per_word_all_words.json

# 7. absorption through her own CLI
~/venvs/tok/bin/python scripts/moirfeolas_score.py \
    --families UnigramLM --sizes 8k --systems backoff hybrid --summary-dir $R/summaries
~/venvs/tok/bin/python -m criochscore.cli absorption \
    --summary $R/summaries/backoff_UnigramLM_8k_morphological_summary.json \
              $R/summaries/hybrid_UnigramLM_8k_morphological_summary.json \
    --moirfeolas external/tknzn4Irish/data/MoirfEolas/MoirfEolas.csv --output-dir $R

# 8. the d' rule, priced in both frames
~/venvs/tok/bin/python scripts/moirfeolas_score.py --simulate-dprime \
    --systems hybrid --out $R/dprime_her_protocol.json
~/venvs/tok/bin/python scripts/moirfeolas_score.py --simulate-dprime --all-words \
    --systems hybrid --out $R/dprime_all_words.json

# 9. the lenition export for Jane
~/venvs/tok/bin/python scripts/irish_lenition_candidates.py

# 10. error analysis
~/venvs/tok/bin/python scripts/moirfeolas_error_analysis.py \
    --per-word $R/per_word_all_words.json --out $R/error_analysis.json

# 11. gaBERT under all four protocol combinations
~/venvs/tok/bin/python scripts/moirfeolas_gabert.py

# 12. the round trip, and the patched-scorer comparison
~/venvs/tok/bin/python scripts/moirfeolas_roundtrip.py
~/venvs/tok/bin/python scripts/moirfeolas_patched_scorer.py

# 13. the two written outputs, both generated from the JSON above
~/venvs/tok/bin/python scripts/moirfeolas_email.py            # long version
~/venvs/tok/bin/python scripts/moirfeolas_email.py --short    # one screen
~/venvs/tok/bin/python scripts/moirfeolas_callsheet.py        # docs/CALL_2026-11-13.md
~/venvs/tok/bin/python scripts/moirfeolas_lenition_sample.py  # docs/lenition_sample.md

# tests, standard library only, no venv needed
make test-tools
```

Step 6 must include `hybrid_splinter` in `--systems` for steps 9 and 10 to
have the lenition numbers available.

Total runtime about five minutes. Nothing is written inside
`external/tknzn4Irish`; results go to `~/data/tok/results/irish/corrected/`
and the only repository output is `docs/moirfeolas_results_email.txt`.

## What did not reproduce, and what was guessed

**Morfessor did not reproduce, at all.** No Morfessor model ships in
`tknzn4Irish` and the repo has no code to train one. Her row
(37.29 / 72.22 / 60.93 / 33.92 / 26.90 / 45.70 / 43.37 / 27.12) is taken on
trust and is not compared against anything here. It is also the row whose
`uru_only` figure, 72.22%, is most out of line with the other two at 50.00%,
and `uru_only` has only 27 words in it, before or after the single-token
filter. 72.22% is 19.5 of 27 and 50.00% is 13.5 of 27. A category that thin
should not be used to separate systems, in her table or in the hybrid's.

**Her three reference rows do not share a protocol.** Unigram 8k needs
`--exclude-single-token`; gaBERT needs all-words plus `--do-lower-case`.
Both reproduce exactly under those different settings, so this is not a
rounding question. Which protocol she intends for the table is the first
thing to ask her.

**Guessed: model type and special-token marker per family.** Her repo does
not record which `--model-type` and `--special-token` each shipped tokenizer
needs. The table in `scripts/moirfeolas_score.py` was inherited from the
earlier round. It is not really a guess for UnigramLM, since `standard` plus
`--no-special-token` reproduces her row on eight figures, but for the other
five families it is inference from the file format and the family name. If
`Byte-BPE` were meant to be scored with `--special-token "Ġ"` the numbers
would not change, because her byte decoder strips `Ġ` before
`remove_specials` sees it, but SentencePieceBPE and WordPiece would move if
their markers are wrong.

**Guessed: which of the two CSVs she means.** `MoirfEolas.csv`, on the
strength of an eight-of-eight match. `MoirfEolas_replicable.csv` gives 43.37%
overall, close enough that the headline alone cannot tell them apart.

**Guessed: that "Unigram 8k" means `UnigramLM`, not `SentencePieceULM`.**
Both are unigram models and both ship at 8k. `UnigramLM` is the one that
reproduces her number, so this one is settled by the data.

**Not attempted: anything downstream.** This is intrinsic alignment only, the
same scope as the paper. No language model was trained. Her proposed MT
experiments are where the question of whether any of this matters downstream
gets answered.

**Unresolved: the 60 missed eclipsed capitalised vowels and the 4 spurious
`t` rows.** Both look like a one-line change to `SPECIAL_PREFIXES` in
`affixes.py`, and both penalise the hybrid for being right. Not patched,
because nothing inside `external/tknzn4Irish` is edited in this round.

## Open questions for Jane

1. Which protocol do the 28 October numbers use? Unigram 8k reproduces only
   with `--exclude-single-token` and gaBERT only without it and with
   `--do-lower-case`. Should the table be regenerated on one setting?
2. Can the Morfessor model, or the script that trains it, go in the repo? It
   is the only reference row that cannot be checked.
3. `remove_specials` can produce an empty first token on sentencepiece
   models, 3,605 of 35,517 words at SentencePieceULM 8k, which then fails
   every position-anchored comparison. Worth a patch, and if so the
   SentencePiece rows in the paper would move.
4. The 60 eclipsed capitalised vowels the gold misses, and the 4 rows that
   mark an urú on an ordinary t-initial word. Both look like one case
   condition in `SPECIAL_PREFIXES`. Worth a patch?
5. Is the `n-dara` / `t-samhradh` spelling intentional in the corpus, or an
   artifact of the ParaCrawl extraction? `ndara` and `tsamhradh` are the
   Caighdeán forms, and these 18 words are the only misses that are not `d'`.
6. Lenition: Scalpel analyses it already, and discarding it costs nothing
   measurable because MoirfEolas does not mark it. Is extending MoirfEolas to
   mark lenition something worth doing together, and does it belong in the
   LoResLM submission or after it?
7. She asked how Scalpel is built and whether it is Irish-only. It is not:
   the mutation layer is Irish, the tokenizer and segmenter are language
   neutral, and the morphological analyzer on top is English. See
   `docs/BASELINES.md`.

## Lenition, and the rearranging variant

Added 10 November, after Jane said she wants to account for lenition in the
dataset.

### Why lenition cannot be a boundary split

Eclipsis, the h-prefix, the t-prefix and `d'` all prepend material, so the
marker is a genuine prefix and `marker + base` reconstructs the surface
exactly. Lenition does not: it inserts an `h` after the first consonant, so
`bhean` reads as `bean` with the `h` sitting at index 1. No boundary split can
isolate it, which is why `pick()` required `word.endswith(base)` and why
lenition fell out of the hybrid by construction rather than by choice.

`marker_and_base()` trims the shared prefix and the shared suffix to find the
added material in both cases, and reports whether `marker + base == word`.
The `hybrid_splinter` system drops that constraint, so its token stream no
longer concatenates back to the word. That is the Splinter-style
rearrangement: move the mutation marker out, leave the stem contiguous.

`hybrid_splinter` is a strict superset of `hybrid`. All 4,760 concatenative
readings are preserved identically and 6,803 lenition-only words are added,
for 11,563 of 35,517 (32.6%).

### What it costs against the current gold

```bash
~/venvs/tok/bin/python scripts/moirfeolas_score.py \
    --systems backoff hybrid hybrid_splinter fst_only identity \
    --out ~/data/tok/results/irish/corrected/her_protocol.json
```

| system | overall | uru_only | prefix_only | suffix_only | uru+prefix | uru+suffix | prefix+suffix | all_affixes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Unigram 8k + Scalpel | 45.46 | 77.78 | 66.12 | 40.70 | 66.67 | 63.00 | 46.81 | 50.17 |
| Unigram 8k + Scalpel + lenition | 44.32 | 77.78 | 66.12 | **38.95** | 66.67 | 63.00 | 46.81 | 50.17 |

Every column is identical to two decimals except `suffix_only`, which falls
1.75 points. That is not a coincidence and it is the whole argument: **none of
the 6,803 lenited words carries a gold urú or a gold prefix**, so the only
column that can move is the suffix one, and it moves down because the stem is
now encoded in isolation. The split is linguistically correct and the gold has
nowhere to record it.

The cost by setting, `hybrid` to `hybrid_splinter`:

| family | 8k | 16k | 32k | 43k | 64k |
|--------|---:|----:|----:|----:|----:|
| UnigramLM | −1.14 | −1.30 | −1.44 | | −1.54 |
| SentencePieceULM | −0.96 | −1.21 | −1.23 | −1.36 | |
| SentencePieceBPE | −0.72 | −1.45 | −2.63 | | −4.65 |
| Byte-BPE | −0.70 | −1.44 | −2.74 | | −4.40 |
| WordPiece | −0.72 | −1.49 | −3.01 | | −4.95 |
| BPE | −0.78 | −1.57 | −2.90 | | −5.57 |

Smallest at 8k, which is the setting that matters, and growing with
vocabulary size because a larger vocabulary is more likely to hold the stem
whole and therefore has more suffix boundary to lose.

This is the number to watch if MoirfEolas gains a lenition column. The marker
is recovered exactly on all 6,803 by construction, so a column that credits
it would turn the largest measured cost in this file into the largest gain.

### The candidate export

```bash
~/venvs/tok/bin/python scripts/irish_lenition_candidates.py
```

Writes `~/data/tok/results/irish/corrected/lenition_candidates.csv`, one row
per lenition reading, for checking in bulk and for seeding the column.

| | count | share |
|---|---:|---:|
| lenition candidates | 6,803 | 19.2% of the table |
| stem occurs on its own in `data/GA` | 4,245 | 62.4% |
| stem is itself a MoirfEolas row | 3,005 | 44.2% |
| word has a competing reading | 0 | 0% |
| word already carries a gold urú | 0 | 0% |

Lenited consonants: `c` 1,855, `f` 757, `t` 733, `b` 634, `d` 626, `m` 626,
`s` 589, `g` 513, `p` 470. Exactly the nine that can take a séimhiú and
nothing else, which is the cheapest available check that the export is not
over-generating.

Zero ambiguous readings: every word that also had an eclipsis reading was
taken by the ordinary ranking first, so the export and the scored system
never disagree about which words are lenited.

## The `d'` rule, priced under her protocol

`docs/IRISH.md` measured this at 45.07 to 45.52, which is the all-words frame
and not comparable to anything in her table. Re-measured:

```bash
~/venvs/tok/bin/python scripts/moirfeolas_score.py --simulate-dprime \
    --systems hybrid --out ~/data/tok/results/irish/corrected/dprime_her_protocol.json
```

| system | overall | uru_only | uru+prefix | uru+suffix | all_affixes |
|---|---:|---:|---:|---:|---:|
| Unigram 8k + Scalpel | 45.46 | 77.78 | 66.67 | 63.00 | 50.17 |
| Unigram 8k + Scalpel + `d'` | **45.98** | 100.00 | 73.81 | 66.05 | 52.95 |
| SP-ULM 8k + Scalpel | 47.11 | 77.78 | 65.00 | 64.71 | 52.39 |
| SP-ULM 8k + Scalpel + `d'` | **47.58** | 100.00 | 71.43 | 67.45 | 55.04 |

Worth +0.52 and +0.47, and it takes `uru_only` to 100.00. The all-words run
of the same flag gives 45.52 for SP-ULM 8k, which is the old figure to the
digit, so the simulation matches what the earlier round did.

Decision 4 stands: `d'` is a verbal particle, not an initial mutation, and
`mutation_fst` is a pure mutation layer. But the gold records it in the urú
column, and if Jane considers that correct then the case for keeping it out
of a mutation layer is weaker than it looked. Worth settling on the call.

## Pre-call gate, 12 November 2026

Re-verified before the 13 November call, with `ga_demutate` rebuilt from
`~/code/Scalpel` at `fac4da4` rather than reusing the binary from the earlier
session. `external/tknzn4Irish` is at `0f31695`, clean, and the working tree
is clean.

| | expected | got |
|---|---:|---:|
| Unigram 8k alone | 43.32 | 43.32 |
| Unigram 8k + Scalpel | 45.46 | 45.46 |
| SP-ULM 8k + Scalpel | 47.11 | 47.11 |
| `hybrid_splinter` Unigram 8k | 44.32 | 44.32 |
| Unigram 8k + Scalpel + `d'` | 45.98 | 45.98 |

Five of five.

## Round trip: is the splinter split lossless?

Added 12 November. A Splinter-style rearrangement is only legitimate if a
decoder can undo it, so this is the first thing that will be asked about
`hybrid_splinter`. The plain hybrid is lossless by construction, because
`pick()` refuses any reading whose marker is not a literal prefix.
`hybrid_splinter` drops that constraint to reach lenition, so for that scheme
losslessness is a claim and not a guarantee.

The inverse lives in `scripts/ga_mutation_split.py`, which is now a
standard-library-only module holding all of layer 1 so the split algebra can
be tested without her scorer or pandas installed:

```python
def rebuild(tag, marker, base):
    if tag == "Len":
        return base[:1] + marker + base[1:]   # the h goes back inside
    return marker + base                      # everything else prepends
```

```bash
~/venvs/tok/bin/python scripts/moirfeolas_roundtrip.py
```

| scheme | tag | words | rebuilt exactly | failures |
|--------|-----|------:|----------------:|---------:|
| concat | Ecl | 3,195 | 3,195 | 0 |
| concat | HPref | 1,066 | 1,066 | 0 |
| concat | TPref | 499 | 499 | 0 |
| concat | **total** | **4,760** | **4,760** | **0** |
| splinter | Ecl | 3,195 | 3,195 | 0 |
| splinter | HPref | 1,066 | 1,066 | 0 |
| splinter | TPref | 499 | 499 | 0 |
| splinter | Len | 6,803 | 6,803 | 0 |
| splinter | **total** | **11,563** | **11,563** | **0** |

16,323 round trips, zero failures, no exceptions to list.

### Does the inverse need the tag?

No, on this data. The only ambiguous marker is `h`: an h-prefix sits before a
vowel (`hainm` is `h` + `ainm`) and a lenition `h` sits after a consonant
(`bhean` is `bean` with an `h` after the `b`). Every other marker belongs to
exactly one mutation, since lenition's marker is always `h`. Reading the first
letter of the stem separates the two cases:

```python
def rebuild_untagged(marker, base):
    if marker == "h" and base and base[0] not in VOWELS:
        return base[:1] + marker + base[1:]
    return marker + base
```

That also recovers 11,563 of 11,563, and the discriminator is genuinely
exercised: 1,066 h-prefixes and 6,803 lenitions, all with marker `h`, are
separated only by the stem's first letter.

**The conservative reading is still to carry the tag.** The untagged inverse
works because of an Irish phonological rule, that lenition applies to
consonants and the h-prefix to vowels, and not because of any property of the
encoding. A stem that began with a vowel and took lenition, or a loanword
spelled outside the Caighdeán, would break it silently. The tag costs one
symbol and removes the assumption, so `hybrid_splinter` emits and relies on
the tag; `rebuild_untagged` exists to answer the question, not to be used.

Covered by `tools/test_ga_mutation_split.py`, 25 tests, standard library only,
wired into `make test-tools`.

## A pluggable layer 1

Added 12 November, so Elaine Uí Dhonnchadha's analyser can be compared
without her toolchain being installed here.

Layer 1 is now a provider: a callable taking the word list and returning
`{word: [(tag, base), ...]}` in candidate order, plus a dict of diagnostics.
Two ship:

| `--layer1` | what it reads |
|------------|---------------|
| `scalpel` (default) | the mutation FST, through the `ga_demutate` bridge |
| `tsv` | `word <tab> tag <tab> base` rows from `--layer1-tsv` |

One row per reading, so a word with several candidates has several rows and
their file order is the candidate order. A header row, blank lines and `#`
comments are skipped. A row with fewer than three fields is an error rather
than a silent skip.

`--dump-layer1-tsv` writes the current provider's readings in that format,
which doubles as the example to hand to another analyser's author:

```bash
~/venvs/tok/bin/python scripts/moirfeolas_score.py \
    --dump-layer1-tsv ~/data/tok/results/irish/corrected/layer1_scalpel.tsv
~/venvs/tok/bin/python scripts/moirfeolas_score.py \
    --layer1 tsv --layer1-tsv ~/data/tok/results/irish/corrected/layer1_scalpel.tsv \
    --systems backoff hybrid hybrid_splinter fst_only identity --out /tmp/via_tsv.json
```

Scalpel emits 11,990 readings over the 35,517 words. Scoring those back
through the TSV provider produces a result file byte-identical to the native
provider's, which is the self-test that the two paths agree.

**Unknown tags are dropped, not guessed.** The ranking in `pick` is by tag
and an unrecognised tag has no defined position in it, so a row tagged
`ECLIPSIS` rather than `Ecl` is counted and reported rather than mapped onto
something. Whoever supplies the TSV has to use Scalpel's tag names
(`Ecl`, `HPref`, `TPref`, `Len`, `Dpart`). This is the conservative choice
and it is a guess about what a second analyser will emit: a mapping table
would be friendlier, but silently renaming another system's categories is
how a comparison stops meaning anything.

### Regression

The refactor must not move a number. Both result files were regenerated and
compared against the versions from before it:

| file | sha256 before | sha256 after | verdict |
|------|---------------|--------------|---------|
| `her_protocol.json` | `37c035ba…` | `37c035ba…` | identical |
| `all_words.json` | `d409368c…` | `d409368c…` | identical |

Byte-for-byte, not just to two decimals.

## Two patches for her repo, prepared not sent

Added 12 November. Both are in `patches/`, as `git format-patch` files
carrying my git identity and nothing else. They were made in a separate
working copy at `~/code/tknzn4Irish-patches`; `external/tknzn4Irish` is never
touched, and that copy's `origin` has been removed so nothing can be pushed
by accident. Her existing `tests/` layout and plain-pytest style are matched.

```
patches/0001-Do-not-leave-an-empty-first-token-when-stripping-a-s.patch
patches/0001-Make-the-following-letter-test-case-sensitive-so-a-b.patch
patches/0002-Read-eclipsis-of-a-capitalised-vowel-written-without.patch
```

Branch `fix-special-prefixes-case` is deliberately two commits rather than
one, because the two halves have very different standing and she should be
able to take the first without buying the second.

### Patch (a), the empty first token

`remove_specials` strips a leading `▁` and keeps the result even when it is
empty. A standalone `▁` piece therefore becomes a token that carries none of
the word but still occupies position 0, so `classify_uru` fails outright and
`classify_prefix` is read one slot late and degrades from "tokenized exactly"
to "tokenized in parts". The patch drops the emptied token, as long as
something is left.

**These are patched-scorer numbers. They are not comparable with any other
table in this file, and not with anything she has published, because the code
that produced them is not the code that produced those.**

| row | her code | with patch (a) | delta | empty-token words before |
|-----|---------:|---------------:|------:|-------------------------:|
| UnigramLM 8k | 43.32 | 43.32 | 0.00 | 0 |
| SentencePieceULM 8k | 42.84 | 43.65 | **+0.81** | 3,605 |
| SentencePieceULM 16k | 37.51 | 38.30 | +0.79 | 2,700 |
| SentencePieceULM 32k | 32.84 | 33.51 | +0.67 | 1,555 |
| SentencePieceULM 43k | 31.71 | 32.33 | +0.61 | 1,259 |
| SentencePieceBPE 8k | 38.05 | 38.08 | +0.02 | 113 |
| Byte-BPE 8k | 38.19 | 38.19 | 0.00 | 107 |
| WordPiece 8k | 35.23 | 35.23 | 0.00 | 0 |

UnigramLM is the control and does not move, which is the check that the patch
only touches what it claims to. Every SentencePiece row gains, and the four
SentencePiece rows in her paper would move by between 0.61 and 0.81 points.

**A third instance of the same bug is not patched.** Byte-BPE still shows 107
empty-token words afterwards, because those come from `decode_bpe_token`,
which strips `Ġ` with `str.replace` and returns `''` for a standalone `Ġ`
token. That is a different function on a different code path. It is left
alone because the brief was `remove_specials`, and because a second opinion
on the right fix is worth having first. Worth raising on the call.

### Patch (b), the case condition

Two commits.

**Commit 1, `Make the following-letter test case-sensitive`.** The table in
`SPECIAL_PREFIXES` distinguishes `t` from `T` and `a` from `A`, but
`_apply_special_prefixes` compares with `.lower()` on both sides, so every
case distinction in it is discarded. `_find_candidates`, the other pass over
the same table, already compares case-sensitively, so the two disagreed.
With the comparison fixed, the bare `t` entry can drop lower-case vowels: the
t-prefix before a lower-case vowel is written with a hyphen (`an t-arán`), so
a bare `t` before one is just an ordinary word. `D` and `G` had to be added
to the `n-` entry, which had been matching `n-Dia` only through the folded
lower-case `d`.

Measured over the 35,517 words, rebuilding the affix table:

| | rows |
|---|---:|
| rows whose reading changes | 874 |
| of those, now matching her corrected `MoirfEolas.csv` | **871** |
| of those, now differing from it | 3 |

The three are `téad`, `tuartha` and `tiarratas`, which are the spurious `t`
rows already identified above: ordinary t-initial words that the gold marks
with an urú. So the patch disagrees with the gold on exactly the rows where
the gold is wrong, and agrees with it on 871 rows that she appears to have
corrected by hand in the `cleaning data` commit. This commit is the
uncontroversial half.

**Commit 2, `Read eclipsis of a capitalised vowel`.** Eclipsis of a vowel
takes a hyphen when the vowel is lower case (`ár n-athair`) and none when it
is capitalised (`i nÉirinn`), so the bare `n` entry needs the capitals. It
depends on commit 1: without the case-sensitive comparison, adding `A` to the
`n` list would read `naoi` and `nua` as eclipsis.

This commit changes 52 further rows, all gaining urú `n`, and **all 52
disagree with her current gold**, which marks no urú on them. That is the
disagreement already recorded above, where Scalpel finds eclipsis the gold
misses. The patch is a proposal that those readings are right, not a bug fix,
and it should not be merged unless she agrees. Keeping it as a separate
commit is the point.

### Effect of patch (b) on scoring the shipped table

None, to two decimals, on every row measured. Patch (b) changes how a
MoirfEolas table is *built*, and `MoirfEolas.csv` is a data file that the
scorer reads rather than regenerates. Its only path into scoring is
`compute_absorption`, which imports `SPECIAL_PREFIXES` for the urú-mapping
figure, and that does not move either on these rows. The 874 and 52 row
counts above are the real measurement of what it does.

```bash
~/venvs/tok/bin/python scripts/moirfeolas_patched_scorer.py
```

## Pre-call session, 12 November: what failed and what was guessed

Everything asked for in this round completed. The items below are the places
where a judgement was made rather than a fact established.

**Guessed: that `--exclude-single-token` is the protocol to use.** It
reproduces her Unigram row on eight of eight figures, so it is right for that
row. It is also contradicted by her gaBERT row, which needs the opposite.
Scoring the hybrid under the Unigram protocol is a choice, and it is the
first question on the call sheet.

**Guessed: that `d'fh` should keep its special case under the case-sensitive
comparison.** `_apply_special_prefixes` previously folded case on both sides,
and the `d'` entry lists `fh`, `Fh` and `FH` but not `fH`. The patch leaves
that as it is, on the grounds that three explicit spellings is the author
signalling intent. A word spelled `d'fHéach` would now miss, and none occurs
in the data.

**Guessed: that unknown tags in a `--layer1 tsv` file should be dropped.**
The alternative is a mapping table from another analyser's categories onto
Scalpel's, which would be friendlier and would also quietly decide what a
second analyser means by each of its labels. Dropping and reporting is the
conservative option; the count is printed so nothing is lost silently.

**Not done: the stratified lenition sample is not a correctness judgement.**
60 rows are laid out for checking by eye and 10 short unattested stems are
listed as the place to look first. One of those, `bhur`, is a clear false
positive. Nobody has gone through the other 6,802.

**Not patched: a third empty-token source.** `decode_bpe_token` returns `''`
for a standalone `Ġ`, which is the same defect as patch (a) in a different
function, and it accounts for the 107 empty-token words Byte-BPE still has
after patch (a) is applied. Out of scope for the brief, and worth a second
opinion before fixing.

**Not reproducible, still: Morfessor.** No model and no training script in
the repo, so one of her three reference rows cannot be checked at all.

**Unverified: Splinter itself.** The rearranging variant here is built from
the description in my own email to her rather than from a reading of the
Splinter paper. The construction stands on its own and round-trips exactly,
but calling it "Splinter-style" is a claim about someone else's method that
has not been checked against their paper.

**A correction to the record.** My email of 10 November described the layer
as one that "identifies and separates initial mutations (lenition, eclipsis,
and prefixed h- and t-)". It identifies lenition but the scored system does
not separate it. Every headline number in this file is eclipsis and the h-
and t-prefixes only. That needs saying on the call before the figures are
read, because the wording suggests four mutations are being split and three
are.
