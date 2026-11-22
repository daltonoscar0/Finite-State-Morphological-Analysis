# Where does Scalpel find lenition, and how deep does it look?

The question from the 13 November call. Short answer: **only at the very
first consonant of the word.** Every lenition site with any morpheme in front
of it is missed, and that is 3,088 sites, 31.2% of all 9,890.

**What "agreement" means here, and why it is not recall.** MoirfEolas has no
lenition annotation. The sites counted below are *candidate* sites proposed by
`scripts/irish_lenition_depth.py` from MoirfEolas's prefix and urú columns,
using the same lenition validity rule as the FST itself
(`lenition_site()` mirrors `is_lenitable()` and `is_s_follower()`). So the
depth-0 figure is close to guaranteed by construction: the script looks for a
valid séimhiú at position 0 and the FST looks for a valid séimhiú at position
0, by the same test. Calling it 100% recall would be circular. The real
finding is the other two rows: **0 of 3,088** non-initial sites, where the
script proposes a site and the FST has no mechanism that could ever reach it.
Everything below says "agreement with layout-derived candidate sites" rather
than "recall" for that reason.

All numbers below come from this run. Per-site data:
`results/irish/lenition_sites_v2.csv` (9,890 rows). Summary JSON:
`results/irish/lenition_depth.json`. Script:
`scripts/irish_lenition_depth.py`. Extension:
`scripts/ga_lenition_prefix.py`. Tests: `tools/test_ga_lenition_prefix.py`
(24 passing).

## How a site is located

MoirfEolas does not annotate lenition, so the sites are derived rather than
read off the gold. Two facts make that sound:

1. A séimhiú is orthographically a lenitable consonant followed by `h`. The
   validity test is taken straight from `mutation_fst.cpp` rather than
   re-derived: `is_lenitable()` accepts `b c d f g m p t`, and `s` lenites
   only before a vowel or `l n r`. So `shciatháin` and `Shmutánach` are not
   sites, and the FST is right to refuse them.
2. A séimhiú only ever lands on the first consonant of a morpheme. A C+h
   inside a morpheme (the `mh` of the prefix `comh`, the `th` of `athair`) is
   ordinary spelling. So the morpheme layout decides which C+h sequences are
   real sites, and the layout is what MoirfEolas does annotate.

Layout is read as `[urú material][prefix][stem]`, and depth is the number of
those morphemes preceding the site. 9,890 sites parse; zero rows fail.

## The answer

| bucket | candidate sites | Scalpel identifies | agreement |
| --- | --- | --- | --- |
| 0, word-initial (`bhean`) | 6,802 | 6,801 | 100.0% (by construction) |
| 1, one morpheme in front (`seanbhean`, `an-mhaith`) | 2,636 | 0 | **0.0%** |
| 2+, two or more (`gcomhpháirtithe`) | 452 | 0 | **0.0%** |

Of the 6,802 depth-0 candidate sites, the earlier candidates file flags some
as not being lenition at all: 13 fossilised th-/ch- forms and 18 English or
Latin look-alikes (see the false-positive table below). Those 31 are counted
in the 6,802 and should not be, which is a further reason not to read the
depth-0 row as a performance figure.

Depth >= 1 is 3,088 sites, 31.2% of all 9,890, and the FST finds none of
them. Counted separately as the call asked:

| | candidate sites | identified | agreement |
| --- | --- | --- | --- |
| depth >= 1, hyphenated (`an-mhaith`) | 497 | 0 | 0.0% |
| depth >= 1, unhyphenated (`seanbhean`) | 2,366 | 0 | 0.0% |
| of which clitic-fronted (`d'fhág`) | 225 | 0 | 0.0% |

This is not a bug. `mutation_fst.h` commits the module to being a pure
application layer with "no triggers, no lexicon, no heuristics", and it only
ever inspects the first consonant of the string it is handed. Word-initially
it is as good as the orthography allows: the single depth-0 miss is
`CH-Praghas`, an all-caps hyphenated oddity.

### Samples

Depth 0, identified (30 of 6,801): chleachta, phlandú, dhúshláin, phot,
fhéadfaí, choimín, mhíochaine, pharaiméadar, cheile, bhainistíocht, bheidh,
cheadúnaisí, mhótaraithe, gharáiste, Choigiltis, chomhairleoidh, phráinne,
fhoghlaimíonn, Phreasagallamh, shaoróg, sheola, mhorgáistí, sheisiúin,
thugtaí, mharcáil, Dhílleachtaithe, Fheistí, mhéide, Bhanglaidéis, Chustaim.

Depth 1, missed (30 of 2,636): fothorthaí, féin-mheas, athchláraithe,
athfhruiliú, leath-chíos, eochairfhocail, focheadúnais, príomhchigire,
iarshealbhóra, fodheontas, formhaoirseacht, comhbhrúiteoirí,
comhbheartaithe, neamhghiorraithe, athghairm, neamhdheimhneacht,
cardashoitheach, infhoirceanta, d'fhoirne, ard-bheachtais, athbhliain,
drochshláinte, ró-bhrúchóirithe, sainmhínithe, míbhuntáistí, dícheall,
aisghníomhú, íosghrádú, buan-fhoirinn, déghnéasach.

Depth 2+, missed (30 of 452): gcomhpháirtithe, d'ath-dhíol, hachomhairc,
gcomhcheangal, hathfhoirgniú, hinfheistíocht, han-mhinic, huasmhéideanna,
gcomhpháirtíocht, t-infheisteoir, dtrasphailnítear, ngnáthchleachtais,
gcomhthoghtar, hidirbhearta, n-athfhruileofar, n-imtheorannaithe,
héicea-thocsaineolaíochta, d'ath-mheas, ngnáth-chur, d'athchóiriú,
ndobharlaigh, hachomharc, dtarchuirtear, hidirghabhálacha, t-iarmhéid,
bhfiach-scaireanna, bhfearchéile, ndíchuireann, bhfiadhúlra, t-athshainiú.

## False positives, by type

Scalpel makes 7,228 word-initial lenition claims over MoirfEolas. (The
6,803 in the file sent to Jane is the subset left after eclipsis is allowed
to outrank lenition, which is the filter the scored hybrid applies.)

| type | count | share | examples |
| --- | --- | --- | --- |
| stem attested elsewhere, no flag | 4,549 | 62.9% | shainiúla→sainiúla, chinnfidh→cinnfidh, dhéanfaí→déanfaí |
| stem unattested anywhere | 2,648 | 36.6% | mheicneora→meicneora, Chiarraí→Ciarraí, Mhacha→Macha |
| English or Latin look-alike | 18 | 0.2% | Shrine→Srine, Cherubim→Cerubim, Pharmacopeia→Parmacopeia |
| fossilised th-/ch- | 13 | 0.2% | Thar→Tar, Chuige→Cuige, thrí→trí, Thíos→Tíos |

The large "stem unattested" class is mostly proper nouns carrying a real
séimhiú (`Chiarraí`, `Mhacha`), not errors. The fossilised class is small
because MoirfEolas does not contain `chun`, `chuig`, `the`, `that` or `shop`
at all, so most of the forms one would expect to see never arise.

A fifth type, **digraphs that belong to the stem**, does not appear as a
Scalpel error because Scalpel never looks past position 0. It appears
instead as noise in the *measurement*: gold prefixes like `a` (612 rows),
`r` (399) and `ro` segment monomorphemic words, so `aghaidh`, `rotha`,
`dúchasacha` and `achtúire` present as depth-1 sites that are not sites at
all. They inflate the depth-1 denominator and therefore understate the
extension's agreement below.

## The extension

`scripts/ga_lenition_prefix.py`, **off by default**, nothing imported into
the scored path. A closed list of 61 leniting prefixes locates one morpheme
boundary and the *existing* lenition rule is applied at that offset. The
mutation logic is untouched; only where it is applied moves. Standard library
only. It does not go in `mutation_fst.cpp`: see decision D1 in `NOTES.md`.

Membership is decided by grammar, restricted to prefixes attested in Jane's
own inventory (`data/MoirfEolas/Affixes/final_prefixes.txt`), not by corpus
frequency. A prefix that usually precedes a vowel still lenites when it meets
a consonant.

Two guards, plus composition with the existing fronting mutations:

- Prefixes of length <= 2 fire only when the delenited stem is attested in
  MoirfEolas or the GA list. An explicit hyphen is the writer's own boundary
  and substitutes for that test.
- A match may not contain an interior capital, because Irish writes a
  mutation in front of a retained capital. Without this, `nua` matched
  `nUachtarán` and returned `nUa` + `ctarán`.
- `prefix_lenition_composed` strips an `Ecl`/`HPref`/`TPref` reading the FST
  already produces and runs the prefix pass on the base, which is what
  reaches depth 2+.

### Agreement, flag off versus on

| bucket | candidate sites | off | on |
| --- | --- | --- | --- |
| 0, word-initial | 6,802 | 6,801 (100.0%) | 6,801 (100.0%) |
| 1, one morpheme in front | 2,636 | 0 (0.0%) | 1,983 (**75.2%**) |
| 2+, two or more | 452 | 0 (0.0%) | 309 (**68.4%**) |
| depth >= 1 combined | 3,088 | 0 (0.0%) | 2,292 (**74.2%**) |

All four figures are agreement with layout-derived candidate sites, not
recall against a gold.

Default behaviour is unchanged: depth 0 is identical with the flag on and
off, and no CríochScore number moves, because nothing in the scored path
calls the extension.

### On-layout rate (in-sample)

There is no lenition gold, so precision is measured against the gold
*layout*: a firing is counted against the extension when it lands at an
offset the layout marks no site at. That test is only meaningful where the
layout is reliable, which is the 27,806 fully-lowercase words (see the
capitalisation defect in `NOTES.md`; the gold annotates prefix and urú for
only 0.4% of capitalised forms).

On that subset, at the default setting: **2,286 firings, 2,282 landing on a
layout-derived depth>=1 site, agreement 74.82%, on-layout 99.83%.** Both
figures are **in-sample**: the prefix list, the dropped `ain` and the guard
threshold were all chosen by looking at this same benchmark. See the
cross-validation section below for the out-of-sample figure. The four
disagreements are:

| firing | verdict |
| --- | --- |
| `sochar` → so + car | genuine false positive; `sochar` is one morpheme |
| `sochorraithe` → so + corraithe | correct; `so-chorraithe` is a real so- compound |
| `haerpháirce` → h + aer + pháirce | correct; gold lacks the urú annotation |
| `haerghunna` → h + aer + ghunna | correct |

So one real error in 2,286 firings. The `so-` / borrowed `soch-` (Greek
socio-) ambiguity is the only systematic one left and is unresolved.

### Cross-validation: is the in-sample figure holding up?

The prefix list, the decision to drop `ain` and the `SHORT_PREFIX` threshold
were all chosen by looking at MoirfEolas, so scoring them on MoirfEolas is
in-sample. `scripts/irish_prefix_validation.py` redoes it honestly: five
folds, and on each training fold **both** data-driven choices are remade from
that fold alone (the guard swept and picked, and any prefix whose firings
disagree with the layout more often than they agree dropped). The held-out
fold is then scored. Seed 20261122. Raw numbers:
`results/irish/prefix_validation.json`.

| | agreement | on-layout |
| --- | --- | --- |
| in-sample (list v1, all of MoirfEolas) | 74.82% | 99.83% |
| cross-validated, mean of 5 folds | **74.83%** | **99.82%** |
| cross-validated, range | 72.9 to 77.21% | 99.56 to 100.0% |

The tuning did not overfit in any material way: out-of-sample agreement
(74.83%) matches in-sample (74.82%) to within a hundredth of
a point. The guard came out at **2 on every fold** and **no prefix was dropped
on any fold**, so the two choices were stable rather than fitted to noise.
Total off-layout firings across all five held-out folds: 4.

The in-sample figure is still labelled in-sample everywhere it appears,
because the list itself has not changed.

### Out of benchmark, for hand-checking

Agreement with a layout derived from MoirfEolas says nothing about material
MoirfEolas does not contain. The extension was run over the
100,942 GA corpus forms that
are absent from MoirfEolas and made only of letters, hyphens and apostrophes
(raw corpus tokens carrying punctuation were excluded, so the sheet is
morphology rather than tokenization artifacts). It fires on
4,403 of them.
300 of those firings, sampled with seed
20261122, are in
`results/irish/prefix_extension_handcheck.csv` with an empty `correct`
column. **No precision is reported from it**: an unchecked sample is not
evidence, and the column is for Oscar to fill in.

### Per-prefix review sheet

`results/irish/prefix_list_review.csv`, one row per prefix in the list: how
often it fires on a lowercase MoirfEolas word, how often that agrees with the
layout, five example hits, five example disagreements, and whether the prefix
is in Jane's own inventory. All 61 are in her inventory and all 61 fire at
least once. No prefix currently disagrees more often than it agrees. The list
is unchanged; the sheet exists to be checked by eye.

### Guard sensitivity

Measured on the same fully-lowercase subset. `guard` is the prefix length at
or below which an attested stem is required.

| guard | firings | hits | agreement | off-layout | on-layout |
| --- | --- | --- | --- | --- | --- |
| 0 (no attestation test) | 2,511 | 2,498 | 81.9% | 13 | 99.48% |
| **2 (default)** | **2,286** | **2,282** | **74.8%** | **4** | **99.83%** |
| 3 | 2,140 | 2,136 | 70.0% | 4 | 99.81% |

2 is set as the default because 2 and 3 hold the same on-layout rate and 2
recovers nearly five points of agreement. Cross-validation picked 2 on every
fold independently. Dropping the test altogether buys
seven more points at a third of a point of precision, which is available if
agreement turns out to matter more once Jane's lenition annotation lands.

One prefix was removed on evidence: `ain` split the common `ainmhí` family as
`ain` + `mhí`, and the attestation guard cannot catch it because `mí` and
`mithe` are both real words. It cost six false positives and bought almost
no agreement.

## What is still out of reach

- **Open-class compounding.** Irish forms noun-noun compounds freely and the
  first element lenites the second (`ceannlitir`, `cardashoitheach`). No
  closed list reaches these and they are not in the extension's denominator.
- **Depth 2+ remainder**, 143 sites. Mostly words where the fronting
  mutation has no FST reading, or where the prefix is outside the list.
- **The 169 `d'fh-` forms** in the depth-1 misses are Stage 2's, not the
  prefix list's.
