# Scaling the lexicon

The analyzer started with 1,825 lexemes compiled into
`LANGUAGES/english_configuration.h` as hard-coded lists, plus about 50
supplementary entries in `data/english_lexicon.tsv`. That is enough to
demonstrate the rules and far too little to run a tokenizer over real text:
almost every word falls through. This document covers the work of getting the
same rules to run on a lexicon two orders of magnitude larger, and the
judgment calls made along the way.

The surface segmentation contract in [SEGMENTATION.md](SEGMENTATION.md) is
unchanged. Pieces still concatenate to the input, and `gate` and `source` still
mean what they meant.

## Where the lexemes come from

| Source | Entries | Notes |
|--------|---------|-------|
| `LANGUAGES/english_configuration.h` | 1,784 | Hard-coded lists, grouped by part of speech and stem class |
| `data/english_lexicon.tsv` | 41 | Supplement, exercises multi-POS and every stem class |
| `--lexicon FILE` | any | Added in this work, see below |

1,825 in total, which is the count the analyzer prints at startup. A stem
listed under two parts of speech counts twice, since each is its own lexeme.

The TSV format is one entry per line, tab or space separated:

```
stem  POS  StemClass  [FEATURE=form ...]  [WEIGHT=N.N]
```

```
cat      NOUN   REG
city     NOUN   Y_FINAL
knife    NOUN   IRR_MOUSE    PLURAL=knives
go       VERB   IRR_GO       PAST=went   PAST_PART=gone
quickly  ADVERB REG          WEIGHT=0.8
```

`POS` is one of `NOUN`, `VERB`, `ADJECTIVE` (or `ADJ`), `ADVERB` (or `ADV`),
`FUNCTION` (or `FUNC`), the last added by this work for the closed class.
`StemClass` is one of `REG`, `Y_FINAL`, `SIBILANT`, `DOUBLE_CONS`, `SILENT_E`,
`IRR_MOUSE`, `IRR_CHILD`, `IRR_SHEEP`, `IRR_OX`, `IRR_GO`, `IRR_BE`,
`IRR_HAVE`, `IRR_DO`, `IRR_SING`, `IRR_RING`, `IRR_SWIM`. `FEATURE` is one of
`PLURAL`, `PAST`, `PAST_PART`, `PRES_PART`, `THIRD_SG`, `COMP`, `SUP`.

Lines starting with `#` are comments. Blank lines are ignored.

## Reading the output back: `--selfcheck`

```bash
./analyzer --selfcheck
```

Expands every lexeme in the loaded lexicon into its surface forms, runs each
one back through analysis, and reports the ones that come back with no
analysis, grouped by the part of speech, stem class and feature they were
generated from. A form the system's own rules produce and its own rules then
reject is a gap in the rules or in the lexicon, not a property of real text,
so this is the diagnostic for closing those gaps.

Generation lives in `RULES/form_generator.h` and is shared with the property
test in `test_segmentation.cpp`, so the two cannot drift apart.

### The 458 forms the analyzer could not read back

Before this work, 458 of the 10,866 forms generated from the lexicon came back
`no_analysis`. Grouped by cause:

| Forms | Cause |
|-------|-------|
| 183 | An irregular verb was shut out of the productive rules entirely: `breaks`, `breaking`, `goes`, `going`, `drinks`, `being`, `doing` |
| 84 | A silent-e verb does take a plain `-s`: `achieves`, `hopes`, `judges` |
| 38 | A sibilant verb does take a plain `-ed`: `fixed`, `passed`, `addressed` |
| 37 | A doubling verb does take a plain `-s`: `stops`, `begs`, `drops` |
| 100 | The generator applied regular orthography to stems whose class carries none: `gos`, `chooseing`, `haveed`, `farer` |
| 10 | The generator invented forms that are not English: `delaier` (from `delay`), `lightd` and `lighing` (from a wrong lexicon entry) |
| 6 | `light` was listed as `VERB SILENT_E` but has no final e |

Four fixes, and the remainder is zero:

1. **The stem class says how a suffix attaches, not whether the feature
   exists.** `match_suffix_to_features` tested the class as a whitelist of
   features and wrote the list too narrowly. It now names the classes whose
   surface form really is stem plus suffix. The classes whose surface form is
   something else are still excluded there and reached by the reversal passes
   instead, which is where `cities`, `stopped` and `hoping` were always
   handled.

2. **An irregular lexeme's orthography is read off its spelling.**
   `RULES/orthography.h`. `StemClass` carries two different kinds of
   information in one field: for a regular lexeme it says how suffixes attach,
   for an irregular one it says which forms are stored instead. A verb needs
   both at once. `choose` stores `chose` and `chosen` and also ends in a silent
   e, so its participle is `choosing`; `go` stores `went` and also takes `-es`,
   so its third person singular is `goes`. With one field the irregularity won
   and the orthography was lost. It is now recovered from the spelling, which
   is where it was visible all along, and only for lexemes whose stated class
   is `IRR_*`: a regular lexeme keeps the class it states, unchanged.

3. **Lexical storage blocks the productive rule for the feature it stores.**
   Widening alone would accept `breaked` and `haves`. `break` holds `broke` for
   `+PAST`, so `-ed` yields nothing; it holds no third person singular, so
   `-s` yields `+3SG`.

4. **Nothing is generated whose orthography the lexicon does not determine.**
   See the decisions below.

## Loading a bigger lexicon

```bash
./analyzer --lexicon ~/data/morph/lexicons/en_100k.tsv --segment-jsonl
./analyzer --lexicon data/closed_class.tsv \
           --lexicon ~/data/morph/lexicons/en_100k.tsv --segment-text
```

`--lexicon` is repeatable and the files are loaded in the order given, after
the built-in lists. With no `--lexicon`, the loaded lexicon is the same 1,825
lexemes it has always been and every output is byte for byte what it was.

A stem that arrives twice becomes two lexemes, which is already how the
built-in lists handle a word that is both a noun and a verb. Two entries that
agree produce the same analysis twice and the analyzer's deduplication
collapses them; two that disagree about the stem class produce both readings,
which is the same ambiguity the lexicon has always been able to express.

`--load-stats` prints the lexicon size, the load time and the peak resident
set size to stderr, which is where the numbers below come from.

### Cost at scale

Measured on one machine, loading a generated lexicon on top of the built-in
lists, so the lexeme counts include those 1,825. Analysis throughput is 20,000
words through `--segment-jsonl`, excluding load time.

| Lexemes | Trie nodes | Load before | Load after | RSS before | RSS after | Words/s |
|---------|-----------:|------------:|-----------:|-----------:|----------:|--------:|
| 1,825 | 6k | 5 ms | 5 ms | 3 MB | 3 MB | 31,600 |
| 11,825 | 22k | 75 ms | 47 ms | 9 MB | 7 MB | 25,000 |
| 101,825 | 252k | 918 ms | 457 ms | 74 MB | 52 MB | 19,100 |
| 451,856 | 1,497k | 5,708 ms | 2,832 ms | 383 MB | 260 MB | 11,800 |

Three things were costing most of that, and none of them was the trie itself:

1. **Every trie node carried a `LexiconState` annotation object.** A 450k
   lexicon has 1.5 million nodes and only 450k of them hold a lexeme, so two
   thirds of the annotations, their heap allocations and their hash map entries
   existed to say nothing. Annotations are now created for terminal states
   only. `get_annotation` returns null for an interior state, which is what it
   has always returned for a state it does not know, and nothing outside
   `LEXICON/lexicon_fsa.h` reads it.

2. **Every byte of every stem went through the symbol table.** That meant
   building a one-character `std::string` and hashing it, four million times on
   load and a quadratic number of times per word during analysis, since
   analysis tries every stem length and looks up four candidate stems for each.
   `LexiconFSA` now caches the 256 single-byte surface symbols in an array,
   filled lazily so no symbol is created that the lexicon does not use. The
   symbol table keys its registry on the representation alone, so a one-byte
   representation names exactly one symbol and the cache cannot disagree with
   it.

3. **Walking the trie paid a reference count update per edge examined.**
   `FSA::step` goes through the virtual `Transition::matches`, which takes its
   argument as a `shared_ptr` by value, so every candidate edge cost an atomic
   increment and decrement. Symbol equality is ID equality, so `LexiconFSA`
   now compares IDs directly through a new non-virtual `Transition::input_id`.
   `FSA::step` is untouched and still serves the rule FSTs.

The irregular reverse index did not need changing. It holds one entry per
stored irregular form, not per lexeme, so it stays proportional to the
irregular vocabulary rather than to the lexicon.

What is left is inherent to a pointer-chasing trie of one-node-per-character:
about 580 bytes per lexeme, spread across a `unique_ptr` state, a `unique_ptr`
transition, a one-element `vector` of outgoing edges per node, and the lexeme
itself. Cutting it further means giving the lexicon a packed representation
instead of the generic `Automaton` state and edge objects, which is a larger
change than this work needed: 2.8 seconds and 260 MB for 450k entries is one
load at startup for a process that then runs for the length of a corpus.

## The closed class

`data/closed_class.tsv`, 324 words: determiners, prepositions, conjunctions,
pronouns, auxiliaries and modals, particles, numerals and common
interjections.

The Wiktionary dump this project builds from carries only noun, verb,
adjective and adverb, so no built lexicon can supply a single determiner or
preposition, and those are the most frequent words in any English text. That
makes them the largest hole a built lexicon leaves, and the reason `the` is
the canonical `no_analysis` example in
[SEGMENTATION.md](SEGMENTATION.md).

They load through `--lexicon`, not by default:

```bash
./analyzer --lexicon data/closed_class.tsv --segment-jsonl
```

All 324 report `gate: lexicon_stem`. That took three things, which is why the
file alone was not enough:

- **A part of speech no rule matches.** `PartOfSpeech::FUNCTION`, one new enum
  value. See decision 6.
- **`WEIGHT=1.3`**, the top of the frequency-derived scale below. Four of the
  words are also real inflected forms, and without a weight above the ordinary
  1.0 the tie-break would report `being` as `be` plus `-ing` and `used` as
  `use` plus `-d`. Those readings are still available under `--nbest 2`.
- **No derivational prefix on a function word.** Otherwise `unto` comes apart
  as `un-` plus the preposition `to`, and `upon` as `up-` plus `on`. English
  derivational prefixes attach to open-class stems, so the analyzer now
  declines, and the form generator stops generating the prefixed variants to
  match. No lexeme is `FUNCTION` unless a closed-class file is loaded, so
  nothing in the default lexicon is affected.

## Building a lexicon from Wiktionary

Two scripts, both of which write outside the repository. Nothing they produce
is committed except the 180-line reference sample in
`data/lexicon_sample.tsv`.

```bash
# wordfreq lives in a virtualenv outside the repository
python3 -m venv ~/data/morph/.venv
~/data/morph/.venv/bin/pip install wordfreq

~/data/morph/.venv/bin/python tools/make_freq.py \
    --wiktionary ~/data/morph/wiktionary/wikt_en.jsonl \
    --out ~/data/morph/freq_en.tsv

python3 tools/build_lexicon.py \
    --wiktionary ~/data/morph/wiktionary/wikt_en.jsonl \
    --freq ~/data/morph/freq_en.tsv \
    --out-dir ~/data/morph/lexicons \
    --sizes 5000,20000,50000,100000,all
```

`tools/make_freq.py` needs `wordfreq`; `tools/build_lexicon.py` is standard
library only. If `wordfreq` cannot be installed, `make_freq.py --corpus FILE`
counts frequencies from a corpus instead and says so in the output header and
on stderr, because counts from one corpus are not interchangeable with
wordfreq's blend of several.

`make_freq.py` found a Zipf frequency for 289,023 of the 420,850 lowercase
a-to-z headwords in the dump. A word it has never seen gets no row, and the
builder reads a missing row as the floor of the scale, 0.70.

### What comes out

| Lexicon | Words | Entries |
|---------|------:|--------:|
| `en_5k.tsv` | 5,000 | 8,736 |
| `en_20k.tsv` | 20,000 | 29,719 |
| `en_50k.tsv` | 50,000 | 65,285 |
| `en_100k.tsv` | 100,000 | 118,758 |
| `en_all.tsv` | 409,889 | 437,753 |

Sizes are vocabulary sizes, by frequency rank. A word listed under more than
one part of speech brings all its entries in together, which is why there are
more entries than words.

| Stem class | Entries | Share |
|------------|--------:|------:|
| `NOUN REG` | 158,588 | 36.2% |
| `ADJECTIVE UNKNOWN` | 122,177 | 27.9% |
| `NOUN UNKNOWN` | 79,235 | 18.1% |
| `ADVERB REG` | 21,875 | 5.0% |
| `VERB SILENT_E` | 13,628 | 3.1% |
| `VERB REG` | 13,167 | 3.0% |
| `NOUN Y_FINAL` | 10,603 | 2.4% |
| `NOUN SIBILANT` | 9,941 | 2.3% |
| `VERB DOUBLE_CONS` | 2,491 | 0.6% |
| `VERB SIBILANT` | 1,834 | 0.4% |
| `ADJECTIVE Y_FINAL` | 1,778 | 0.4% |
| `VERB Y_FINAL` | 1,342 | 0.3% |
| the rest | 1,183 | 0.3% |

`ADJECTIVE UNKNOWN` is the second largest group because most English
adjectives form their comparative with "more" rather than with `-er`, and the
dump lists no synthetic comparative for them. `NOUN UNKNOWN` is a noun with no
plural listed.

### Choosing a stem class

Every candidate class is a claim about how suffixes attach, and the dump lists
the forms, so the claim is testable. The builder generates what each permitted
class predicts, keeps the class that reproduces the most of the listed forms,
and writes out whatever it still fails to reproduce as an explicit
`FEATURE=form`, which the rules read as blocking the productive rule for that
feature.

```
break   VERB  REG          PAST=broke  PAST_PART=broken  WEIGHT=1.09
run     VERB  DOUBLE_CONS  PAST=ran    PAST_PART=run     WEIGHT=1.11
go      VERB  SIBILANT     PAST=went   PAST_PART=gone    WEIGHT=1.15
choose  VERB  SILENT_E     PAST=chose  PAST_PART=chosen  WEIGHT=1.07
city    NOUN  Y_FINAL                                    WEIGHT=1.12
```

So `breaks` and `breaking` decompose while `breaked` does not analyze;
`ridding` decomposes while `riding` stays the participle of `ride`; `goes`
decomposes as `go|es` with `alt: epenthesis`.

### The self-check

`build_lexicon.py` regenerates every entry's forms from the class it chose
plus the forms it stored, and compares them against the forms the dump listed.

```
listed forms checked:          313,207
reproduced:                    313,207
mismatched:                          0  (0.0000%)
forms the format cannot hold:    8,368  (2.7%)
entries with explicit forms:    11,567
entries with no forms listed:  223,518
```

The zero is close to a tautology, since the class was chosen to maximise
matches and the remainder was stored. It is checked anyway, because the
prediction tables in `build_lexicon.py` duplicate the orthography in
`RULES/orthography.h`, and a duplicate that drifts is exactly what a build
should notice.

The 8,368 are forms the format has no slot for: `index` lists both `indexes`
and `indices` and there is one `PLURAL` field. They are counted, not hidden.

The check against the real rules rather than against a Python copy of them is
`--selfcheck`, and it passes at every size:

| Lexicon | Lexemes | Generated forms | No analysis |
|---------|--------:|----------------:|------------:|
| built-in | 1,825 | 10,684 | 0 |
| `en_5k` | 10,885 | 59,221 | 0 |
| `en_20k` | 31,868 | 165,784 | 0 |
| `en_50k` | 67,434 | 327,319 | 0 |
| `en_100k` | 120,907 | 536,849 | 0 |
| `en_all` | 439,902 | 1,756,780 | 0 |

Each row is the built-in lists plus `data/closed_class.tsv` plus that file.

## The weight scale

Weights are the analyzer's ambiguity ranking, and the existing scale is set by
two facts: an entry with no `WEIGHT` has weight 1.0, and a derivational reading
is multiplied by 0.9. So 1.0 is an ordinary word, and a derived reading of an
ordinary stem scores 0.9 and loses to any stored whole word of ordinary
frequency. A built lexicon maps Zipf frequency onto the same scale:

```
weight = clamp(0.70 + 0.075 * zipf, 0.70, 1.30)
```

| Zipf | Example | Weight |
|------|---------|-------:|
| 7.7 | the | 1.28 |
| 6.0 | time | 1.15 |
| 4.0 | ordinary word | 1.00 |
| 3.0 | 1 per million | 0.92 |
| 1.0 | rare | 0.77 |

Zipf 4 maps to exactly 1.0, so an ordinary word keeps the weight the
hand-written lexicon gives it, and the 0.9 derivation penalty keeps meaning
what it meant. The range is deliberately narrow: frequency breaks ties, it
does not override the morphology. A derived reading of a common stem (0.9 times
1.15, so 1.035) does outrank a stored rare word (0.77), which is the behaviour
a tokenizer wants, and `--derived-first` below makes that policy explicit
rather than a side effect of the arithmetic.

## Evaluation

```bash
python3 tools/eval_lexicon.py --all --stages morpholex \
    --lexicon ~/data/morph/lexicons/en_100k.tsv \
    --json ~/data/morph/eval.json
```

Standard library only. It drives the analyzer through
`tools/fst_tokenizer.py`, which keeps one process alive per configuration.
Four stages, each runnable on its own with `--stages`:

| Stage | What it measures |
|-------|------------------|
| `corpus` | Coverage on running text, token weighted and type weighted, plus the 100 most frequent words the analyzer fails on |
| `gold` | Boundary precision, recall and F1 against MorphyNet, inflection and derivation separately |
| `curve` | Both of the above at every lexicon size, as a table and as JSON |
| `morpholex` | The independent held-out check, optional |

`data/closed_class.tsv` is loaded alongside every lexicon by default, since no
built lexicon can supply a determiner. `--closed-class ''` turns that off.

### On independence

MorphyNet is mined from Wiktionary, which is also what `build_lexicon.py`
builds from. Scores against the MorphyNet gold for words that are in the
lexicon are therefore optimistic: the gold and the lexicon are two views of
one source, and agreeing with yourself is not evidence. Every MorphyNet
number here is labelled in-distribution and should be read as an upper bound
and as a regression check, not as accuracy on unseen text.

MorphoLex is independent, built by psycholinguists through a different
pipeline, and is the honest held-out number. Its licence is non-commercial,
so it is used for evaluation only and never to build a lexicon.

### The corpus

`~/data/morph/corpus/sample.txt`: 5,009,526 characters of the raw train split
of WikiText-103, saved outside the repository with the `datasets` package.
777,416 alphabetic tokens, 40,229 types.

An alphabetic token is a run of letters, which may carry an internal
apostrophe. Requiring a letter first is what keeps a clitic out of the counts:
WikiText writes possessives and negations with a space, as `Federer 's` and
`do n't`, so a bare `[A-Za-z]+` pattern turns every one of them into a
one-letter token. `s` alone came to 7,607 tokens, one percent of the corpus,
every one of them scored as a failure on a word that was never a word.

### Converting the gold to surface boundaries

MorphyNet segments against the lemma, not the surface form: `microtome|ing`
for `microtoming`. The surface boundary is the length of the longest common
prefix of the first morph and the surface form, which puts the boundary at 8
and gives `microtom|ing`. For a derivational suffix the boundary is the
longest common prefix of source and target; for a prefix it is the length of
the affix.

That is the same convention the analyzer uses, by construction: its stem piece
is the longest prefix of the surface word shared with the lemma. The two agree
on where an alternation puts the boundary, so no tolerance for being one
character out is needed, and `--alt-tolerant` is not used.

| Gold | Rows | Usable | Distinct forms |
|------|-----:|-------:|---------------:|
| MorphyNet inflection | 649,593 | 219,025 | 187,083 |
| MorphyNet derivation | 225,131 | 208,534 | 203,374 |
| MorphoLex | 68,623 | 10,058 | 10,058 |

An inflection row carries a boundary only when its segmentation has two
morphs; the other 429,051 rows are the lemma's own row, which has nothing to
say about a boundary.

MorphoLex gives each morpheme in its canonical form rather than as a slice of
the surface word, which its data dictionary states explicitly. A row is
therefore kept only when its morphemes concatenate to the word exactly:
`agreement` gives `agree` plus `ment` and concatenates, `cities` gives `city`
plus `s` and does not. 16,189 rows are dropped for that reason and 30,309 more
have a single morpheme and no boundary to score. Rows that do not concatenate
are counted and excluded rather than repaired by guesswork, because guessing is
what would stop an independent gold standard being independent. The surviving
set is biased towards words with no orthographic alternation, and towards
multi-root compounds like `aero|naut|ic|al`, which is worth remembering when
reading the number.

## Decisions

Judgment calls made while scaling the lexicon, and why. The decisions in
[SEGMENTATION.md](SEGMENTATION.md) still stand; these are in addition.

1. **Consonant doubling is never inferred from spelling, so an irregular stem
   that might double leaves `-ing` and `-ed` undetermined, and neither the
   generator nor the analyzer will touch them.** Whether a stem doubles its
   final consonant depends on stress: `begin` gives `beginning`, `listen` does
   not give `listenning`, and the spelling does not show the difference. For a
   regular lexeme the `DOUBLE_CONS` class states the answer. For an irregular
   one the class is spent on the irregularity, so the answer is simply not in
   the data, and the honest response is to decline rather than guess. The
   generator therefore does not produce `puting` or `begining`, and the
   analyzer does not accept them. The `-s` forms are still both generated and
   accepted, because a plain `-s` doubles nothing, so `puts` and `begins` are
   certain.

   This also keeps `riding` the participle of `ride` alone. `rid` would have to
   double its consonant to reach it, and before this test existed the widened
   rules read `riding` as `rid` plus `-ing`.

2. **The noun plural path is left permissive, so `sheeps` and `mouses` keep the
   analyses they had.** Blocking is applied to verb and adjective inflection,
   where it is being introduced, and not to noun plurals. That path accepts
   `-s` on any noun, including the zero-plural and umlaut classes, so `sheeps`,
   `mouses`, `childs` and `foots` all analyze as plurals. Blocking there would
   be the linguistically correct thing and it would withdraw analyses from
   inputs that already had them, which the output contract fixes in place.
   Correctness is available as a one-line change (drop the part-of-speech test
   around the blocking loop in `match_suffix_to_features`) whenever withdrawing
   those analyses is acceptable.

   Those forms are no longer generated, since generation does apply blocking,
   so the property test no longer asserts on them. The analyzer's behaviour on
   them is unchanged.

3. **A y-to-i variant is generated only for `Y_FINAL` stems.** `delay` is a
   regular verb that happens to end in y, so its agent noun is `delayer`, not
   `delaier`. The generator used to apply the alternation on the strength of
   the final letter alone, while the analyzer has always required the class, so
   the two disagreed and the generator won four unanalyzable forms.

4. **`light` as a verb is `REG`, not `SILENT_E`.** It has no final e, so the
   silent-e rules read one that is not there and produced `lightd`, `lighed`,
   `lighing`, `ligher` and `lighment`. The verb is regular: lights, lighted,
   lighting. The irregular past `lit` is a separate entry this file does not
   carry.

5. **Fixing the 458 changed the output of 29 words that already had an
   analysis, and the work went ahead anyway.** The brief asks for the gaps to
   be closed and for inputs that already had an analysis to keep their output
   byte for byte. Those two cannot both hold here: `stops` and `hopes` gain
   `+3SG` from one and the same rule change, and `hopes` already had `+PL` from
   the noun. 305 of the 11,366 records in the fixed regression word list gained
   an analysis where there was none, and 50 records over 29 words changed.
   Every one of the 29 is a consequence of the tie-break documented as decision
   3 in [SEGMENTATION.md](SEGMENTATION.md), not of a new convention, and in
   every case the word gained a reading that is correct English:

   - 17 words are noun and verb homographs that gained `+3SG` beside `+PL`.
     The pieces are identical and only the label moved, because the tie-break
     orders tied analyses by label and `SUFFIX_3SG` sorts before `SUFFIX_PL`:
     `bears`, `changes`, `costs`, `drums`, `flags`, `hates`, `hopes`,
     `increases`, `judges`, `lights`, `logs`, `maps`, `pins`, `pits`, `rings`,
     `springs`, `winds`.
   - 12 words gained an inflected reading that the tie-break prefers over the
     stored whole-word one, because it prefers more pieces at equal weight:
     `building`, `feeling`, `meaning` and their `un-` and `re-` variants now
     decompose; `leaves` is read as `leave` plus `-s` rather than the stored
     plural of `leaf`; `outcomes` and `outputs` follow `outcome` and `output`,
     which the baseline already split as `out|come` and `out|put`.

   This is the same stored-versus-derived question Stage 5 below turns into a
   flag. Nothing about the record format changed, and no input lost an
   analysis.

6. **One new part of speech, `FUNCTION`, rather than eight.** The closed-class
   list needs a category that no inflectional or derivational rule matches.
   Tagging those words `NOUN` would make `thes` a plural, and `ADVERB` would
   work mechanically while being false. The obvious alternative is to add
   `DETERMINER`, `PRONOUN`, `PREPOSITION`, `CONJUNCTION`, `AUXILIARY`,
   `PARTICLE`, `NUMERAL` and `INTERJECTION`, which is what the comment on the
   enum has always suggested.

   One value was added instead. The four open classes exist in this enum
   because each selects an inflectional paradigm, which is the only thing the
   rules ever ask a part of speech for. A function word has no paradigm, so one
   uninflecting category covers all of them as far as morphology is concerned,
   and the difference between a determiner and a preposition is syntax that no
   rule in this system would read. The finer grouping is kept where a human can
   see it, as comment headings in `data/closed_class.tsv`. Adding the eight
   properly is a one-line-each change if a later stage grows a syntax that
   needs them.

7. **`data/closed_class.tsv` is not loaded by default.** It would be more
   convenient if it were, and it would change the output of inputs that already
   have an analysis: `is`, `was`, `been`, `had`, `does`, `being`, `having`,
   `doing` and `used` all analyze today through `be`, `have`, `do` and `use`,
   and a `FUNCTION` entry at weight 1.3 outranks them. The segmentation
   contract fixes those outputs in place, so the file is opt-in. Any corpus
   evaluation should pass it, and `tools/eval_lexicon.py` does.

8. **Generated entries never use the `IRR_*` stem classes.** An `IRR_*` class
   says which forms are stored without saying how the rest are spelled, and
   losing the orthography that way is exactly what left the analyzer unable to
   read back its own output. A generated entry states an orthographic class and
   lists its exceptions, which carries strictly more information:
   `run VERB DOUBLE_CONS PAST=ran PAST_PART=run` says both that the past is
   `ran` and that the participle is `running`. The hand-written entries still
   use `IRR_*` and still work, through the spelling-derived orthography of
   decision 2 above.

9. **A form counts as an inflection only when its tag set is exactly the
   canonical one.** The brief asks for forms tagged alternative, obsolete,
   dialectal, archaic or misspelling to be dropped. Requiring an exact match
   on `["plural"]`, `["participle","past"]` and the rest does that and more,
   since every variant carries an extra tag, including `rare`, `nonstandard`
   and the regional ones. It is stricter in the safe direction: the cost of
   missing a rare plural is an entry with no plural, while the cost of
   admitting one is a wrong rule. 190,363 of the 266,762 noun lines carry a
   plural tagged exactly `["plural"]`.

10. **A headword that is only an inflection of another lemma is dropped,
    12,278 of them.** Wiktionary documents inflected forms as headwords, so the
    dump has noun entries for `years`, `things`, `men` and `comes`. Loading
    those as lexemes teaches the analyzer that `years` is a word in its own
    right, and a stored whole word outranks a decomposition, so exactly the
    words a tokenizer most needs to split would stop splitting.

    A headword is treated as one of these when some other lemma lists it as an
    inflection and it lists no inflection of its own. The second condition is
    what makes it safe: `saw` is the past of `see` and also a noun with its own
    plural `saws`, so it stays, while `arms` has no plural of its own, so it
    goes and still analyzes as `arm` plus `-s`. Matching is across parts of
    speech, so the noun `comes` is recognised as the third person singular of
    the verb `come`. A form that is its own inflection does not count, or every
    zero-plural noun would be dropped. `--keep-form-of` turns the filter off.

11. **The most frequent candidate form wins, not the one a regular class
    predicts.** The dump carries two verbs spelled `break`: the ordinary one
    with `broke` and `broken`, and a second with the regularized `breaked`.
    Merging the etymologies gives `PAST` two candidates and the lexicon has one
    slot. Preferring the candidate a class predicts, to keep the entry regular,
    looks tidy and is exactly backwards: it discards every irregular past the
    dump also regularizes somewhere, and `break` came out as plain `REG` with
    no stored past at all. Frequency separates the two cleanly, by four orders
    of magnitude, and it is how ties are broken everywhere else here. Being
    predicted by a class now only breaks a tie between equally frequent
    candidates.

12. **Deduplication keeps the highest weight, not the first occurrence.** This
    was a real bug as soon as two lexicons were loaded. The same stem then
    arrives twice, once from the built-in lists at the default weight of 1.0
    and once from a generated file at its frequency-derived weight, and the
    built-in lists load first. Keeping the first occurrence pinned
    `run +V +PRES_PART` at 1.0, where the generated entry says 1.11, so
    `running` lost to its own stored noun entry and came out as one piece.
    `breaking` and `choosing` had the same problem. With the default lexicon
    alone every weight is 1.0, so the default output is unchanged.

13. **Wiktionary's marginal open-class senses are kept.** The dump has `the`
    and `to` as adverbs, `and` as a noun and a verb, and a verb `to in` whose
    participle is `inning`. These are real entries for real if peripheral
    senses, and they help as often as they hurt: they are why `the` and `of`
    analyze at all without `data/closed_class.tsv`. Filtering them would need
    sense-level evidence, and the only field that looks like it, `sense_tags`,
    is a union over all of a word's senses and so cannot say that a word is
    marginal in every one of them. Frequency is the available signal, and it is
    already in the weight.

14. **A noun is never offered `SILENT_E` or `DOUBLE_CONS`.** A noun plural
    neither deletes a silent e nor doubles a consonant: `house` gives `houses`
    and `bus` gives `buses`, which `REG` and `SIBILANT` already predict. The
    first version offered every class to every part of speech and labelled
    35,047 nouns `SILENT_E`, a class that made no prediction about them and
    told a reader nothing.
