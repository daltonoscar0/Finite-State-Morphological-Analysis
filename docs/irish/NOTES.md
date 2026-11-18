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
