# Irish next steps: working notes

Running log of what lives where, decisions taken and why, and what is still
open. Started after the 13 November call with Jane Adkins. Target: LoResLM at
EACL 2027, deadline 7 January 2027.

## Where things live

| Thing | Path |
| --- | --- |
| Mutation FST (core, C++) | `~/code/Scalpel/mutation_fst.{h,cpp}`, branch `irish-mutations` |
| Lenitable/s-follower predicates | `~/code/Scalpel/mutation_fst.cpp:23-32` |
| Bridge binary source | `~/code/morphology/tools/irish/ga_demutate_main.cpp` |
| Bridge binary (built, arm64) | `~/code/morphology/ga_demutate` |
| Layer 1 split algebra + inverse | `~/code/morphology/scripts/ga_mutation_split.py` |
| Prefix-lenition extension (new) | `~/code/morphology/scripts/ga_lenition_prefix.py` |
| Scoring harness (hybrid wrapper) | `~/code/morphology/scripts/moirfeolas_score.py` |
| Jane's repo (read only) | `~/code/morphology/external/tknzn4Irish` |
| MoirfEolas | `external/tknzn4Irish/data/MoirfEolas/MoirfEolas.csv` (35,517 rows) |
| Prefix/suffix inventories | `external/tknzn4Irish/data/MoirfEolas/Affixes/` (512 prefixes) |
| GA token list | `external/tknzn4Irish/data/GA/unique_ga_tokens.txt` (71,588) |
| Trained tokenizers | `external/tknzn4Irish/tokenizers/<family>/` |
| `lenition_candidates.csv` generator | `scripts/irish_lenition_candidates.py` |
| Reshaped file sent to Jane | `~/data/tok/results/irish/corrected/for_jane/lenition_candidates.csv` |
| Results from this run | `~/code/morphology/results/irish/` |

A second clone of Jane's repo sits at `~/code/tknzn4Irish-patches`, identical
to the vendored one at commit `0f31695`. The vendored copy is the one the
harness reads.

`~/data` is not a git repository, so anything written there is unversioned.
That is why the file for Jane went to a new `for_jane/` subdirectory rather
than over the original export, which is also the input to
`scripts/moirfeolas_lenition_sample.py`.

## Stage 0: baselines reproduce

Both reference rows reproduce exactly, no rounding gap, on
`MoirfEolas.csv` at commit `9fed382` (28 October, "cleaning data"):

| system | overall | uru_only | prefix_only | suffix_only | uru+prefix | uru+suffix | prefix+suffix | all_affixes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Unigram 8k | 43.32 | 50.00 | 66.12 | 40.70 | 43.03 | 50.83 | 46.81 | 35.98 |
| Unigram 8k + Scalpel | 45.46 | 77.78 | 66.12 | 40.70 | 66.67 | 63.00 | 46.81 | 50.17 |

Command (her protocol, `--exclude-single-token`, which is the harness
default):

```
python3 scripts/moirfeolas_score.py --families UnigramLM --sizes 8k \
    --systems backoff hybrid --out results/irish/baseline_her_protocol.json
```

`n=33385` for the back-off and `n=33480` for the hybrid. The two systems are
filtered by their own token counts, which is what her CLI does, so the word
sets differ slightly; `--matched-set` exists for the controlled comparison.

All-words protocol, same run with `--all-words`
(`results/irish/baseline_all_words.json`):

| system | overall | uru_only | prefix_only | suffix_only | uru+prefix | uru+suffix | prefix+suffix | all_affixes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Unigram 8k | 40.72 | 50.00 | 57.23 | 37.94 | 41.19 | 49.97 | 44.07 | 35.38 |
| Unigram 8k + Scalpel | 42.85 | 77.78 | 57.23 | 37.94 | 66.67 | 63.00 | 44.07 | 50.17 |

Branch `irish-next-steps` created in `~/code/morphology`. Nothing committed
in `~/code/Scalpel` yet: Stage 1 did not need a core change, for the reason
in the next section.

## Decisions

**D1. The prefix list does not go in `mutation_fst.cpp`.** The header commits
that module to being a pure application layer, "no triggers, no lexicon, no
heuristics", and its semantics are pinned both to a Python reference
(`src/mutations.py`) and to a shared attested-form test file, with the C++
runner required to reproduce the Python pass count exactly. A list of
lenition-triggering prefixes is a trigger inventory. Putting it there would
break the contract and desynchronise two implementations and a test file for
no gain. Instead `scripts/ga_lenition_prefix.py` locates the boundary with
the closed list and applies the *existing* lenition rule at that offset. The
mutation logic is untouched; only where it is applied moves. Standard library
only, so Scalpel's zero-dependency rule holds.

**D2. Lenition site validity is taken from the FST, not re-derived.** The
first pass used a loose "consonant followed by h" test and charged Scalpel
with six misses at depth 0. All six were the test's fault: `mutation_fst.cpp`
lenites s only before a vowel, l, n or r, so `shciatháin` and `Shmutánach`
are correctly refused, and `ghhairdín` is a doubled-h typo where surface and
analyser disagree about which h was inserted. `lenition_site()` now mirrors
`is_lenitable()` and `is_s_follower()` exactly, and doubled h is excluded as
degenerate. Depth-0 recall is 100.0% once the measurement is right.

**D3. Short prefixes need an attested stem; a hyphen substitutes for that.**
Two- and three-character prefixes (`do`, `fo`, `in`, `so`) also match the
opening of monomorphemic words whose stem contains a C+h digraph, which is
how `rotha` becomes `ro` + `ta`. Prefixes of length <= 2 therefore only fire
when the delenited stem is attested in MoirfEolas or the GA list. An explicit
hyphen is a boundary the writer put there, so it vouches for the cut on its
own. Threshold set from the sweep in `lenition_depth.md`: 2 and 3 both hold
precision at 99.9%, and 2 recovers four more points of recall.

**D4. A prefix match may not cross a mutation boundary.** Irish writes an
initial mutation in front of a retained capital, so `nua` matched
`nUachtarán` case-insensitively and returned `nUa` + `ctarán`. Any interior
capital inside the matched region now rejects the match. This also kills
`tSeáin` and `nGaeilge`.

**D5. Recall and precision are reported against the lowercase subset.** See
the benchmark defect below. Capitalised words have no prefix or urú
annotation, so the gold layout cannot adjudicate a firing on one. Precision
is quoted on the lowercase subset, where the annotation is reliable, and the
capitalised firings are reported separately with a hand-checked sample.

**D6. The clitic rule ships on by default; the prefix list does not.** The
clitic split raises four categories and lowers none (uru_only 77.78 to
100.00, uru+prefix 66.67 to 73.81, uru+suffix 63.00 to 66.05, all_affixes
50.17 to 52.95, overall 45.46 to 45.98), so there was no reason to hide it
behind a flag. `--no-clitics` reproduces the old numbers exactly, which is
the ablation. The prefix list stays off because MoirfEolas does not annotate
lenition, so it cannot be credited and can only perturb other categories.

**D7. m' and b' are split even though the gold does not mark them.** The urú
column records d'/D' (639 words) but leaves m'/b'/B' empty (13 words), so
splitting those inserts a boundary the metric cannot credit. Measured: the
scores are identical to d'-only at two decimal places, so it costs nothing,
and the clitics are real. Worth telling Jane, since it looks like a gap in
the annotation rather than a decision.

**D8. The analyser abort is worked around, not fixed.** The bridge calls
`abort()` on `n-APP`, `n-EOS`, `n-IOTA` and `t-AMHÁIN`: analysis accepts a
hyphenated marker before an all-caps stem, generation uppercases the inserted
marker and drops the hyphen, and the self-check at mutation_fst.cpp:201
fails. `demutate_all` now bisects a failing chunk, sets the offenders aside
and reports them, so one word cannot take a 272,000-word run with it. The
real fix belongs in `inverse_ok_cp`, and it is **blocked**: mutation_fst.h
pins semantics to `src/mutations.py` and `tests/attested_mutations.tsv` and
requires the C++ runner to reproduce the Python pass count exactly, and
neither file is in this checkout. Flagged for Oscar.

**D9. Training-time pre-tokenization is not adopted.** It fixes the
missing-marker problem (marker coverage 11-13 of 16 goes to 13-15) and still
loses to inference-only in 10 of 12 cells. Reported rather than buried; the
expectation going in was the opposite.

**D10. Fertility under `scalpel_inf` is optimistic and said so.** It counts
the injected marker as one token, but eight of the sixteen markers are in no
vocabulary trained on raw text, so a real encoder emits more. The
`scalpel_train` column is the honest one.

## Benchmark defect found: MoirfEolas does not annotate capitalised words

The prefix and urú columns are effectively empty for capitalised forms, while
the suffix column is populated for all of them:

| column | capitalised (6,969 words) | lowercase (28,548 words) |
| --- | --- | --- |
| urú | 26 (0.4%) | 5,335 (18.7%) |
| prefix | 25 (0.4%) | 9,898 (34.7%) |
| suffix | 6,968 (100.0%) | 27,434 (96.1%) |

So the annotation pipeline that produced urú and prefix was case-sensitive
and skipped capitalised input; the suffix pipeline was not. Consequences:

- 310 of the 355 extension firings that disagree with the gold layout are on
  capitalised words the gold simply cannot mark. Spot-checking them finds
  almost all correct (`Príomhfhostaithe`, `Comhdhálacha`, `Ard-Chúirt`,
  `Míbhuntáistí`). They are not false positives, they are sites the benchmark
  cannot express.
- Any urú or prefix category score is computed over lowercase words only in
  practice. `uru_only` rests on 27 words (see the harness's category counts),
  which is worth saying out loud in the paper.

This is a question for Jane, not something to fix here: her repo is read
only. Listed in `for_jane.md`.

## Open / TODO

- **TODO (Mingram).** "Mingram" from the call notes cannot be placed. It does
  not appear anywhere in `jndkns01/tknzn4Irish`: not in the working tree at
  `0f31695`, not in any file, and not in the full git history
  (`git log --all --grep`). The six families her repo ships are BPE,
  Byte-BPE, SentencePieceBPE, SentencePieceULM, UnigramLM and WordPiece. Not
  invented or substituted. Needs one question to Jane.
- Depth 2+ lenition (452 sites) is still at 0%. These words carry urú or a
  clitic in front of the prefix, so the extension never sees the prefix at
  index 0. Composing the existing initial-mutation strip with the prefix pass
  would reach them; not done yet.
- Open-class noun-noun compounding ("ceannlitir") is out of reach of any
  closed list, and is not counted in the extension's denominator.
- `so-` versus borrowed `soch-` (Greek socio-) is unresolved and is the only
  disagreement with the gold layout left at the default setting.
- **The analyser abort fix is blocked** on `src/mutations.py` and
  `tests/attested_mutations.tsv` (decision D8). Worked around for now.
- No Irish sentence corpus in the checkout, so the Stage 4 round-trip corpus
  is reconstructed from the word-per-line GA dump. Rerun on Jane's parallel
  data when it arrives.
- `suffix_only` falls by 0.01 to 0.11 under the pre-tokenizer in 9 of 12
  Stage 3 cells. Small and one-directional, so real. Not diagnosed further
  than "fixing a boundary at the front changes how the back-off splits the
  rest".

## Test and command inventory

89 tests, all passing:

```
python3 tools/test_ga_mutation_split.py     # 33, pre-existing
python3 tools/test_ga_lenition_prefix.py    # 24, Stage 1
python3 tools/test_ga_clitic.py             # 18, Stage 2
python3 tools/test_ga_pipeline.py           # 14, Stage 3/4
```

Everything run in this session, in order:

```
# Stage 0
python3 scripts/moirfeolas_score.py --families UnigramLM --sizes 8k \
    --systems backoff hybrid --out results/irish/baseline_her_protocol.json
python3 scripts/moirfeolas_score.py --families UnigramLM --sizes 8k \
    --systems backoff hybrid --all-words \
    --out results/irish/baseline_all_words.json

# Stage 1
python3 scripts/irish_lenition_depth.py --json results/irish/lenition_depth.json

# Stage 2
python3 scripts/moirfeolas_score.py --families UnigramLM --sizes 8k \
    --systems backoff hybrid --no-clitics          # the ablation
python3 scripts/moirfeolas_score.py --families UnigramLM --sizes 8k \
    --systems backoff hybrid --out results/irish/stage2_clitics.json

# Stage 3
python3 scripts/irish_boundary_survival.py --sizes 8k \
    --out results/irish/boundary_survival.json
python3 scripts/moirfeolas_score.py --sizes 8k --systems backoff hybrid
python3 scripts/irish_train_tokenizers.py
python3 scripts/irish_tokenizer_generality.py

# Stage 4
python3 scripts/make_mt_configs.py
python3 scripts/make_roundtrip_corpus.py
python3 scripts/tokenize_mt.py --input results/irish/roundtrip_corpus.txt \
    --config unigram8k-scalpel --output /tmp/rt.tok --round-trip
```
