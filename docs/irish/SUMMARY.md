# Scalpel Irish: where things stand after the 13 November call

Branch `irish-next-steps` in `~/code/morphology`, nothing pushed. Every
number below was produced in this run; the path to the file it came from is
given. 89 tests pass.

## Baselines reproduce exactly

Both reference rows, on MoirfEolas at commit `9fed382`, her protocol
(`--exclude-single-token`). No rounding gap.
`results/irish/baseline_her_protocol.json`.

| system | overall | uru_only | prefix_only | suffix_only | uru+prefix | uru+suffix | prefix+suffix | all_affixes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Unigram 8k | 43.32 | 50.00 | 66.12 | 40.70 | 43.03 | 50.83 | 46.81 | 35.98 |
| Unigram 8k + Scalpel | 45.46 | 77.78 | 66.12 | 40.70 | 66.67 | 63.00 | 46.81 | 50.17 |

All-words protocol, same run with `--all-words`
(`results/irish/baseline_all_words.json`): 40.72 and 42.85 overall.

## 1. Scalpel sees lenition only at the first consonant of a word

`docs/irish/lenition_depth.md`, per-site data in
`results/irish/lenition_sites_v2.csv` (9,890 rows).

| bucket | sites | identified | recall |
| --- | --- | --- | --- |
| 0, word-initial (`bhean`) | 6,802 | 6,801 | **100.0%** |
| 1, one morpheme in front (`seanbhean`, `an-mhaith`) | 2,636 | 0 | **0.0%** |
| 2+, two or more (`gcomhpháirtithe`) | 452 | 0 | **0.0%** |

3,088 sites, 31.2% of the total, are invisible to it. Word-initially it is as
good as the orthography allows. Hyphenated and unhyphenated counted
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

`scripts/ga_lenition_prefix.py`. A closed list of 56 leniting prefixes
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

Precision, measured on the 27,806 fully-lowercase words where the gold layout
is reliable: **2,286 firings, 2,282 on a gold-layout site, 99.83%**. One
genuine false positive (`sochar`, the `so-`/`soch-` ambiguity). Default
behaviour is unchanged with the flag on or off, because nothing in the scored
path calls it.

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
`results/irish/tokenizer_generality.csv` (72 rows). 24 tokenizers trained
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

Fertility cost is negligible: +0.01 to +0.03 tokens per word.

**Training-time pre-tokenization is not worth it.** It loses to
inference-only in **10 of 12** cells, winning only WordPiece 8k and 16k by
+0.24 and +0.26. It does fix marker coverage (11-13 of 16 markers in
vocabulary, up to 13-15), and still loses. Reported because the expectation
was the opposite.

**Where it is worse.** `suffix_only` falls in 9 of 12 cells, by −0.01 to
−0.11 (worst: WordPiece 64k, 31.85 → 31.74). Small but one-directional, so a
real effect. It is the only category that ever regresses, and the same −0.01
appears on Jane's models at 8k.

## 4. The MT pipeline is ready

`docs/irish/MT_PIPELINE.md`. One command, 84 named configurations in
`configs/mt_configs.json`.

```
python3 scripts/tokenize_mt.py --input ga.txt --config unigram8k-scalpel \
    --output ga.tok --round-trip
```

`detok(tok(x)) == x` exact on **all 12,000 lines** of
`results/irish/roundtrip_corpus.txt`, for all ten configurations tested,
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
2. **MoirfEolas does not annotate capitalised words.** urú and prefix are
   filled for 0.4% of the 6,969 capitalised forms against 18.7% and 34.7% of
   the 28,548 lowercase ones, while suffix is filled for 100%. So that
   annotation pass was case-sensitive. It is why precision above is quoted on
   the lowercase subset. Jane's call, her repo.
3. **The analyser aborts on four forms** and the fix is blocked.
   `n-APP`, `n-EOS`, `n-IOTA`, `t-AMHÁIN` trip the self-check at
   mutation_fst.cpp:201. Correcting `inverse_ok_cp` needs `src/mutations.py`
   and `tests/attested_mutations.tsv`, which mutation_fst.h names as the
   parity reference and which are not in this checkout.
4. **Depth 2+ lenition** is at 68.4%, and open-class noun-noun compounding
   (`ceannlitir`) is out of reach of any closed list.
5. **`so-` versus `soch-`** is the one unresolved ambiguity in the prefix
   list.
6. **The round-trip corpus is reconstructed**, not real line breaks. Rerun
   on Jane's parallel data.
7. **`suffix_only` regression** is measured but not diagnosed.
