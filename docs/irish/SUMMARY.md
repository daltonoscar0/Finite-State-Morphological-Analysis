# Scalpel Irish: where things stand after the 13 November call

Branch `irish-next-steps` in `~/code/morphology`, nothing pushed. Every
number below was produced in this run; the path to the file it came from is
given. 92 tests pass in morphology, 15 in seanchai, 14 in the C++ runner.

## Baselines reproduce exactly

Both reference rows, on MoirfEolas at commit `9fed382`, her protocol
(`--exclude-single-token`). No rounding gap.
`results/irish/baseline_her_protocol.json`.

| system | overall | uru_only | prefix_only | suffix_only | uru+prefix | uru+suffix | prefix+suffix | all_affixes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **words in category** | 33,385/33,480 | **27** | 760 | 20,633 | 201/210 | 3,374/3,432 | 6,726 | 1,664/1,692 |
| Unigram 8k | 43.32 | 50.00 | 66.12 | 40.70 | 43.03 | 50.83 | 46.81 | 35.98 |
| Unigram 8k + Scalpel | 45.46 | 77.78 | 66.12 | 40.70 | 66.67 | 63.00 | 46.81 | 50.17 |

`uru_only` is **27 words**, so it moves in whole steps and a change there is
worth far less than the same change in `suffix_only` (20,633 words). Where two
counts are given they are back-off / hybrid, which differ because
`--exclude-single-token` is applied per system.

All-words protocol, same run with `--all-words`
(`results/irish/baseline_all_words.json`): 40.72 and 42.85 overall.

## 1. Scalpel sees lenition only at the first consonant of a word

`docs/irish/lenition_depth.md`, per-site data in
`results/irish/lenition_sites_v2.csv` (9,890 rows).

| bucket | candidate sites | identified | agreement |
| --- | --- | --- | --- |
| 0, word-initial (`bhean`) | 6,802 | 6,801 | 100.0% (by construction) |
| 1, one morpheme in front (`seanbhean`, `an-mhaith`) | 2,636 | 0 | **0.0%** |
| 2+, two or more (`gcomhpháirtithe`) | 452 | 0 | **0.0%** |

**These are not recall figures.** MoirfEolas has no lenition annotation, so
the sites are *candidates* proposed by `irish_lenition_depth.py` from the
prefix and urú columns using the same validity rule as the FST. Depth-0
agreement is close to guaranteed by construction, both sides looking for a
valid séimhiú at position 0 by the same test, and 31 of those 6,802
candidates are flagged in the earlier candidates file as fossilised th-/ch-
(13) or English and Latin look-alikes (18), so they are not lenition at all.

**The real finding is the other two rows: 0 of 3,088 non-initial sites**,
31.2% of the total, where the script proposes a site and the FST has no
mechanism that could reach it. Hyphenated and unhyphenated counted
separately: 497 and 2,366 sites at depth >= 1, both at 0%, of which 225 are
clitic-fronted.

This is by design, not a bug: `mutation_fst.h` commits the module to being a
pure application layer with "no triggers, no lexicon, no heuristics", and it
only inspects the first consonant of what it is handed.

**False positives**, over Scalpel's 7,228 word-initial lenition claims:
62.9% have an attested stem, 36.6% have a stem attested nowhere (mostly
proper nouns carrying a real séimhiú, `Chiarraí`, `Mhacha`), 18 are
English/Latin look-alikes (`Shrine`, `Pharmacopeia`), 13 are fossilised
th-/ch- (`Thar`, `Chuige`, `thrí`). A fifth class, stem-internal digraphs,
cannot be a Scalpel error because it never looks past position 0; it shows up
instead as noise in the measurement, from spurious short gold prefixes
(`a` 612 rows, `r` 399) that make `aghaidh` and `rotha` present as sites.

### The extension, off by default

`scripts/ga_lenition_prefix.py`. A closed list of 61 leniting prefixes
locates one morpheme boundary and the *existing* lenition rule is applied at
that offset; composed with the fronting mutations the FST already reads, it
also reaches depth 2+. The prefix list does not go in `mutation_fst.cpp`: it
is a trigger inventory and that module's contract forbids one.

| bucket | flag off | flag on |
| --- | --- | --- |
| 0, word-initial | 100.0% | 100.0% |
| 1, one morpheme in front | 0.0% | **75.2%** |
| 2+ | 0.0% | **68.4%** |
| depth >= 1 combined | 0.0% | **74.2%** |

**In-sample, and now cross-validated.** On the 27,806 fully-lowercase words:
2,286 firings, 2,282 on a layout-derived site, agreement 74.82%, on-layout
99.83%. Those are in-sample numbers, because the prefix list, the dropped
`ain` and the guard threshold were all chosen on this same benchmark.
Five-fold cross-validation, remaking both choices on each training fold:
agreement **74.83% mean, 72.90 to 77.21 range**; on-layout **99.82% mean,
99.56 to 100.00**. The guard came out at 2 on every fold and no prefix was
dropped on any fold, so the tuning did not overfit. 300 firings on GA forms
outside MoirfEolas are in `results/irish/prefix_extension_handcheck.csv` for
hand-checking, with no precision claimed from them.

Default behaviour is unchanged with the flag on or off, because nothing in
the scored path calls it.

## 2. The d' forms are now split, and it is a clear win

"D' stuff" meant elided proclitics. The data: 670 MoirfEolas rows contain an
apostrophe, all straight, none curly; the inventory is d'/D' (646), mb' (9),
m' (5), b' (5), B' (3) and two junk rows. Scalpel returned **no reading for
any of them**. 172 of the 659 the rule fires on have lenition stacked under
the clitic (170 on `f`, as `d'fh-` predicts); all 659 round-trip.

`scripts/ga_clitic.py`, on by default. `results/irish/stage2_clitics.json`.

| | overall | uru_only | prefix_only | suffix_only | uru+prefix | uru+suffix | prefix+suffix | all_affixes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| before | 45.46 | 77.78 | 66.12 | 40.70 | 66.67 | 63.00 | 46.81 | 50.17 |
| after | **45.98** | **100.00** | 66.12 | 40.70 | **73.81** | **66.05** | 46.81 | **52.95** |
| Δ | +0.52 | +22.22 | 0 | 0 | +7.14 | +3.05 | 0 | +2.78 |

No category falls. `--no-clitics` reproduces the old row exactly. Both
apostrophe shapes are accepted on input even though MoirfEolas has no curly
ones, because the GA corpus has 11,102 of them.

## 3. The gain is not a Unigram artefact

`docs/irish/tokenizer_generality.md`,
`results/irish/tokenizer_generality.csv` (96 rows). 24 tokenizers trained
here (Unigram, BPE, WordPiece x 8k/16k/32k/64k x raw/pre-tokenized corpus) in
4.6 minutes.

Overall, her protocol. Scalpel helps **all 12 cells**:

| model | 8k | 16k | 32k | 64k |
| --- | --- | --- | --- | --- |
| Unigram | +2.01 | +2.63 | +3.27 | +4.28 |
| BPE | +3.91 | +4.36 | +5.01 | +5.98 |
| WordPiece | +5.40 | +6.07 | +6.43 | +6.99 |

The gain **grows with vocabulary size** and is **smallest for Unigram**, so
reporting only Unigram 8k understates the method. On Jane's own six families
at 8k the same holds: BPE 35.85→40.98, Byte-BPE 38.19→44.00,
SentencePieceBPE 38.05→43.86, SentencePieceULM 42.84→47.58, UnigramLM
43.32→45.98, WordPiece 35.23→41.69.

The mechanism, from Jane's absorption script at 8k (`uru_absorbed_%`):
WordPiece buries **72.39%** of all urú material inside a token, BPE 50.67%,
Unigram 27.74%. With Scalpel in front all three go to roughly zero (0.06,
0.02, 0.30). That is also why WordPiece gains most and Unigram least.

Fertility cost is negligible: +0.01 to +0.03 tokens per word with the marker
charged as one token, and at most +0.019 more when it is charged at the piece
count the model really produces. No model emits `[UNK]` for a marker it is
missing: WordPiece gives `b'` as `b` + `##'`, so an absent marker costs one
extra token and never information.

**Training-time pre-tokenization is not worth it.** It loses to
inference-only in **10 of 12** cells, winning only WordPiece 8k and 16k by
+0.24 and +0.26. It does fix marker coverage (11-13 of 16 markers in
vocabulary, up to 13-15), and still loses. Reported because the expectation
was the opposite.

**A fourth condition: markers as added tokens.** The candidate MT setup, the
raw-trained model given the sixteen markers as added tokens without
retraining. It is **never better** than inference-only and is worse by 0.45
to 0.86 wherever it changes anything, because an added token matches anywhere
in the string and so re-splits stems containing the marker. Ranking on this
metric: inference-only > added tokens > training on split text. No MT default
is set here.

**Where it is worse, and why.** `suffix_only` falls in 9 of 12 cells, by
−0.01 to −0.11 (worst: WordPiece 64k, 31.85 → 31.74). Run down to the word
in `results/irish/suffix_only_regressions.csv`: for Unigram, **not one shared
word loses its suffix boundary at any size**. The movement is a denominator
effect, because `--exclude-single-token` is applied per system and words the
plain back-off left whole (`b'fhéidir`, `nAchtanna`, `nAirí`) become two
tokens under layer 1 and so enter the average, scoring 0.000 to 0.050 mean
suffix credit against 0.307 to 0.415 for the words already there. WordPiece 8k
is the only cell with a genuine status change, net −1 word of 21,253.
Rerunning the table under the harness's existing `--matched-set` would remove
the denominator effect; described in `tokenizer_generality.md`, not done.

## 4. The MT pipeline is ready

`docs/irish/MT_PIPELINE.md`. One command, 84 named configurations in
`configs/mt_configs.json`.

```
python3 scripts/tokenize_mt.py --input ga.txt --config unigram8k-scalpel \
    --output ga.tok --round-trip
```

`detok(tok(x)) == x` exact on **all 12,000 lines** of
`results/irish/roundtrip_corpus.txt`, for all eleven configurations tested,
covering every family. The separate detokenize invocation reproduces the
input file byte for byte. Output is byte-identical across runs; model sha256,
library versions and throughput go to a sidecar JSON.

**Throughput 9,200 to 12,400 lines/s**, single process, Scalpel costing about
10%. Measured at load average 3.9 to 5.1, so a working figure rather than a
clean benchmark. A million lines is under two minutes.

## What changed in the FST layer

Nothing in `mutation_fst.cpp`. All four changes are in the layer-1 wrapper,
standard library only, so Scalpel's core stays zero-dependency:

| file | what | default |
| --- | --- | --- |
| `scripts/ga_clitic.py` | split d'/m'/b', identify the lenition under them | **on** |
| `scripts/ga_lenition_prefix.py` | lenition after a closed prefix list, composed with fronting mutations | **off** |
| `scripts/ga_pipeline.py` | lossless, convention-correct pre-tokenization for running text | n/a, new |
| `scripts/ga_mutation_split.py` | survive the analyser aborting on a word | **on** |

## Still open

1. **Mingram is unplaced.** It appears nowhere in `jndkns01/tknzn4Irish`:
   not in the working tree at `0f31695`, not in any file, not in the full git
   history. Nothing was substituted. One question for Jane.
2. **MoirfEolas's urú and prefix columns are filled almost only for
   lowercase forms.** An observation, not a verdict: it may be a deliberate
   scoping decision and only Jane knows. The rates, from
   `results/irish/capitalisation_fill_rates.json`:

   | column | capitalised (6,969) | lowercase (28,548) |
   | --- | --- | --- |
   | urú | 26 (0.4%) | 5,335 (18.7%) |
   | prefix | 25 (0.4%) | 9,898 (34.7%) |
   | suffix | 6,968 (100.0%) | 27,434 (96.1%) |

   MoirfEolas also holds no case variants at all, so there is no
   identical-form pair to compare. `results/irish/capitalised_annotation_examples.csv`
   instead gives 100 pairs over 100 distinct prefixes: a capitalised word with
   an empty prefix column beside a lowercase word carrying that same prefix,
   annotated (606 such pairs exist). This is why agreement above is quoted on
   the lowercase subset. A question for Jane, not a defect to fix here.
3. ~~The analyser aborts on four forms and the fix is blocked.~~ **Fixed on
   22 November.** The parity files were in `~/code/seanchai`. The case matrix
   showed ten aborting cells rather than four, all the same shape;
   `inverse_ok` now checks membership of the realisation set, in both
   implementations, because the Python reference had the identical bug.
   412,865 fuzz inputs: no aborts, no dropped readings, exact Python/C++
   parity. See decision D8 in `NOTES.md`.

4. **Depth 2+ lenition** is at 68.4%, and open-class noun-noun compounding
   (`ceannlitir`) is out of reach of any closed list.
5. **`so-` versus `soch-`** is the one unresolved ambiguity in the prefix
   list.
6. **The round-trip corpus is reconstructed**, not real line breaks. The UD
   Irish-IDT treebank that `attested_mutations.tsv` was mined from is **not on
   this machine** (searched `~` excluding Library/node_modules/.venv/
   site-packages for `*UD_Irish*`, `*ga_idt*` and `*.conllu`: nothing), so the
   `# text =` lines could not be used. The existing 12,000-line corpus stands.
   Rerun on Jane's parallel data when it arrives.
7. ~~`suffix_only` regression is measured but not diagnosed.~~ **Diagnosed**:
   mostly a per-system filter artefact, not lost boundaries. See above.
