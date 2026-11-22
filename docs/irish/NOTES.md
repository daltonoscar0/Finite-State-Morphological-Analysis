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

**D8. The analyser abort is FIXED (22 November).** Was: worked around,
blocked on the parity files. Those turned out to be in `~/code/seanchai`
(remote `daltonoscar0/Morpheme-Aware-Language-Modeling-for-Irish`), which is
where the relative paths in `mutation_fst.h` were rooted all along.

The defect: analysis accepted a vowel-prefixing marker with a hyphen in front
of an all-caps stem (`n-APP`), but generation uppercases the inserted marker
along with the stem and drops the hyphen, giving `NAPP`, so the self-check
could not get from one to the other and raised. The case matrix found ten such
cells, not the four seen in the corpus. `inverse_ok` now tests membership of
the set of orthographic realisations generation is allowed to have, instead of
comparing against a single string; `mutate()` is untouched and `(tag, base)`
is unchanged. Fixed in both implementations, because the Python reference had
the identical bug.

Commits: seanchai `0b4f225` and `20bb275`; Scalpel `23a51f2`, `096307e` and
`2d4eda2`. `demutate_all` keeps its bisection as a safety net but now reports
and raises instead of swallowing, and the skip list is empty on all 272,349
distinct GA forms.

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

# Fix run, 22 Nov

The parity files turned up in `~/code/seanchai` (remote
`daltonoscar0/Morpheme-Aware-Language-Modeling-for-Irish`), which is where the
relative paths in `mutation_fst.h` were rooted all along. That unblocked D8.

Branches for this run: `fix/hyphen-caps-inverse` in seanchai (from `main`
@ 8036b57) and in Scalpel (from `irish-mutations` @ fac4da4). morphology stays
on `irish-next-steps`.

## Layer 0: preflight

Everything matched the expected starting values:

| thing | value |
| --- | --- |
| Python attested | 17200 / 17201 |
| C++ `mutation_tests` | 12 tests, 0 failed, attested 17200/17201 |
| morphology tests | 89 passing (33 + 24 + 18 + 14) |
| Unigram 8k | 43.32 |
| Unigram 8k + Scalpel, `--no-clitics` | 45.46 |
| Unigram 8k + Scalpel, default | 45.98 |
| sha256 of the emailed `lenition_candidates.csv` | `fca730ba9b50c4fc25955b48f72885b68ba29337cfd90d019aac6d346f8c5c26` |

**F1. seanchai was dirty and I asked rather than guessing.** Four uncommitted
deletions I did not make: `build/ga_mutations.foma` and three
`__pycache__/*.pyc`, all committed in 26c288b and since removed from the
working tree, none of them source, all regenerable. Asked, told to proceed and
leave them. Every commit in that repo this run stages explicit paths, never
`-a`, and the four deletions are still exactly as found.

**F2. The one failing attested row is a data artifact, not a defect.**
`fíor-` / lemma `fíor-chorr-Óglach` / NOUN / Len / dev. The surface is a
truncated compound fragment with no séimhiú in it at all, so `demutate('fíor-')`
correctly returns nothing while the row claims tag Len. Unsatisfiable by
construction, and unrelated to the abort. Not touched, per the brief.

## Layer 1: the core fix

**F3. The matrix was worth building: ten cells abort, not four.** Mapping
marker x marker case x hyphen x stem case x stem onset gives 174 cells
(`~/code/seanchai/docs/mutation_case_matrix.md`). Ten abort, and they are all
one shape: a vowel-prefixing marker (`n`, `t`, `h`), a hyphen, and an ALL-CAPS
stem. Marker case is irrelevant. The four corpus forms were a sample of a
class. Python and C++ agreed on all 174 cells before the fix, including which
ten aborted, which is independent evidence the two were genuinely in parity.

**F4. Mechanism: generation enumerates its legal realisations, `inverse_ok`
checks membership.** The alternative was to have each reading carry the
surface realisation of its marker, which would have widened the `(tag, base)`
pair every caller destructures; the brief forbids changing that shape.
Membership keeps `(tag, base)`, keeps the tag inventory, leaves `mutate()`
emitting strict Caighdeán, and confines the change to one new function plus a
rewritten `inverse_ok`. `demutate` is not touched.

The realisation set is deliberately no wider than analysis already was: the
bare marker as generation spells it, its uppercase form only when the stem is
ALL-CAPS, and a following hyphen only for `n`/`t`/`h` before a vowel. That
matters because `inverse_ok` is also the pass criterion for the attested file,
so loosening it further would have quietly moved that count.

**F5. Analysis was not broadened, and that was checked rather than assumed.**
Re-running the matrix after the fix: 0 aborts both sides, 0 disagreements, and
comparing the `(tag, base)` sets across all 174 cells before and after gives
**0 cells changed**. attested stayed at 17200/17201 with the same single
failure.

**F6. A sibling inconsistency found and left alone.** `n-APP`, `N-APP`,
`t-APP`, `T-APP` and `h-APP` all get a reading but `H-APP` does not: the
hyphen-variant branch tests `w[0] == pre` against a lowercase `pre`, so an
uppercase `H` fails it, while `N` and `T` survive via the separate ALL-CAPS
track. Making `H-` behave like `N-` would create a reading for a string that
previously had none, which the brief rules out except by explicit decision.
Recorded in the design note and left; nothing in the corpus produces it.

**F7. New cases went in a second file.** `tests/orthographic_variants.tsv`,
40 rows, same five columns, with the split column carrying the expectation
(`edge` must analyse and invert, `negative` must get no reading) rather than a
treebank split. The attested file is untouched because its row count is
asserted. Both runners load both files and report them separately, and
`mutation_fst.h` now names both files and both counts.

Commits: seanchai `0b4f225`, Scalpel `23a51f2` and `096307e`.

**Layer 1 exit check: passed.**

| check | result |
| --- | --- |
| Python and C++ same counts on both files | attested 17200/17201 both; variants 30/30 positive, 10/10 negative both |
| attested still >= 17200/17201, same failure | yes, `fíor-` |
| the four forms analyse and round-trip, both | yes |
| no abort anywhere in the matrix | 0 of 174, both |
| C++ test count | 12 -> 14 |
| seanchai Python test count | 13 -> 15 |

## Layer 2: no abort is possible on an input

**F8. `demutate` drops and reports; only the test runners assert.** An input
path must not die on one word. `demutate_strict` keeps the hard check and is
what both parity runners call, and `demutate_candidates` exposes the
unverified set so a drop can be told from a non-reading.

**F9. The fuzz harness found two more divergences, neither reachable from the
two test files.** `~/code/seanchai/scripts/fuzz_parity.py`, 412,865 inputs:
every distinct GA form (272,349), every MoirfEolas word (35,517), every
attested surface (4,970), 100,000 synthetic strings over the matrix axes, and
degenerate whitespace. The real corpora were clean from the start; the
synthetic set was not.

- A one-letter ALL-CAPS stem behind a hyphenated marker (`N-A`) was proposed
  by the ALL-CAPS analysis track but had no matching realisation, because the
  uppercase marker was gated on the base being longer than one character.
  20 such drops.
- The `bhf` branch tested `c1 == 'h'` case-sensitively in C++ while the Python
  reference tests `w[:2].lower() == "bh"`, so the two disagreed on a
  mixed-case `bHfill`. 17 mismatches. `mutation_fst.h` makes the Python side
  the authority, so the C++ followed it; that does widen C++ analysis slightly,
  which the brief allows for a sibling inconsistency and which is recorded
  here.

Both are now rows in `orthographic_variants.tsv` (35/35 positive, 10/10
negative) and the run is clean.

**F10. The morphology bisection is loud now.** `demutate_all` reports a
set-aside word on stderr and raises `AnalyserSkippedWords` unless
`--allow-skips` is passed, on all five entry points. Skip list is empty on all
272,349 distinct GA forms, with three tests covering it.

**Layer 2 exit check: passed.** Fuzz clean, parity exact, skip list empty,
92 morphology tests pass (was 89).

## Layer 3: nothing moved

All three baselines reproduce to two decimals. The scalpel-condition models
were retrained because the pre-tokenized corpus had silently excluded the four
aborting forms; they now split, changing 5 of 4,682,103 corpus lines.
**Largest absolute change in any of the 72 cells: 0.0000.** None of the four
forms is in MoirfEolas, so they could never affect the scored set, only the
vocabulary, and 5 lines in 4.7 million did not. Round trip still exact on all
12,000 lines for all eleven configs.

## Layer 4: the measurement problems

**F11. The depth-0 figure was circular and is no longer called recall.** The
sites are candidates this project's own script derives from MoirfEolas's
prefix and urú columns using the same validity rule as the FST, so both sides
look for a valid séimhiú at position 0 by the same test. Renamed to
"agreement with layout-derived candidate sites" throughout, with the real
finding (0 of 3,088 non-initial) stated as the headline. 31 of the 6,802
depth-0 candidates are flagged fossilised or English in the candidates file
and so are not lenition at all.

**F12. The prefix extension was tuned and scored on the same data, and the
cross-validation says it survives.** Five folds, both data-driven choices
remade per training fold: agreement 74.83% mean (72.90 to 77.21) against
74.82% in-sample; on-layout 99.82% mean (99.56 to 100.00) against 99.83%.
Guard 2 on every fold, no prefix dropped on any fold. The in-sample figure is
still labelled in-sample because the list did not change.

**F13. The suffix_only dip is mostly a denominator artefact, not lost
boundaries.** For Unigram at 32k and 64k, zero shared words change status; the
movement comes entirely from words entering the scored set because
`--exclude-single-token` is applied per system. Entrants score 0.000 to 0.050
mean suffix credit against 0.307 to 0.415 for words already there, and the
arithmetic reproduces the observed dips. WordPiece 8k is the only cell with a
genuine status change, net -1 of 21,253. The harness's existing
`--matched-set` would remove the artefact; described, not applied.

**F14. Added tokens are not the MT answer, and the naive version is
destructive.** Registering all sixteen markers took `bunreacht` to nine
single-character pieces and fertility from 2.96 to 6.50, because an added
token in the `tokenizers` library matches anywhere in the string. Restricted
to the genuinely missing multi-character markers it is never better than
inference-only and is worse by 0.45 to 0.86 where it changes anything. No MT
default was chosen, per the brief.

**F15. Fertility understatement is bounded at 0.019 tokens per word.** And no
model emits `[UNK]` for a missing marker: every one is spellable from
characters already in the vocabulary, so WordPiece gives `b` + `##'` and a
missing marker costs exactly one extra token.

**F16. The capitalisation finding is reworded as a question.** Fill rates by
case are in `results/irish/capitalisation_fill_rates.json`. MoirfEolas holds
**no case variants at all**, so an identical-form pairing finds nothing; the
evidence sheet instead pairs a capitalised word having an empty prefix column
with a lowercase word carrying that same prefix annotated, filtered to Jane's
own prefixes whose remainder is itself attested. 606 such pairs, 100 written
over 100 distinct prefixes.

**F17. The UD treebank is not on this machine.** Searched `~` (excluding
Library, node_modules, .venv, site-packages) for `*UD_Irish*`, `*ga_idt*` and
`*.conllu`: nothing. `scripts/gen_tests.py` expects it at
`seanchai/data/UD_Irish-IDT`, which does not exist. So the `# text =` lines
could not be used and the reconstructed 12,000-line corpus stands, with the
caveat recorded in `MT_PIPELINE.md`.

## Layer 5: branch hygiene

**F18. The Irish work DOES depend on `matched-vocab`, so no rebase was done.**
Three files exist on `matched-vocab` and not on `main`, and the Irish work
imports two of them throughout:

| file | introduced by | needed for |
| --- | --- | --- |
| `scripts/ga_mutation_split.py` | `8b2e173` | imported by every Irish script |
| `scripts/moirfeolas_score.py` | `1cd790c` | imported for scoring and FAMILIES |
| `tools/test_ga_mutation_split.py` | `8b2e173` | the 36-test suite |

`moirfeolas_score.py` was then touched by `56cfc6a`, `cca61c5` (which adds
`add_dprime` and `DPRIME_TAG`, both imported), `09275c1` and `d77b606` (which
adds the `PROVIDERS` interface, also imported). So the minimum transplant
would be `1cd790c`, `56cfc6a`, `cca61c5`, `8b2e173` and `d77b606` plus their
own dependencies, which is most of the Irish half of that branch.

**Recommendation: merge `matched-vocab` into `main` first, then
`irish-next-steps`.** `irish-next-steps` is untouched and no
`irish-next-steps-main` was created.

**F19. `scripts/moirfeolas_lenition_share.py` is tracked**, added by `168eac6`
("Reshape the lenition export into the file Jane asked for", 18 November).

**F20. No AI attribution anywhere.** 21 commits across the three repos
(morphology 16, Scalpel 3, seanchai 2), all authored `Oscar Dalton
<daltonoscar0@gmail.com>`, zero matches for claude/anthropic/co-authored-by/
generated-with/copilot/chatgpt in any subject, body or trailer.
