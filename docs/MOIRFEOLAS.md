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
| baseline scoring driver | `tools/irish/run_criochscore.py` |
| hybrid scoring driver | `tools/irish/run_scalpel.py` |
| full sweep driver | `tools/irish/run_sweep.py` |
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
