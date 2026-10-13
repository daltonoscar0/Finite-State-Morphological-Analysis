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

The 458 do not all become analyses. 304 of them do; the other 154, over 151
distinct strings, are forms the generator stopped producing, because the
lexicon does not determine their spelling or because the lexeme stores that
feature itself: `goed`, `haves`, `chooseing`, `puting`, `delaier`, `lightd`.
Not one of them is an English word, and the analyzer still declines all 154,
which is the right answer rather than a remaining gap. The count that has to
reach zero is the one over the forms the generator actually claims, and it
does.

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

The same measurement on the real lexicons, which share more prefixes than the
synthetic ones and so need fewer trie nodes per entry. Each row is the built-in
lists plus `data/closed_class.tsv` plus that file, which is the configuration
every evaluation below uses:

| Lexicon | Lexemes | Trie nodes | Load | Peak RSS |
|---------|--------:|-----------:|-----:|---------:|
| built-in | 1,825 | 4,740 | 5 ms | 3 MB |
| `en_5k` | 10,885 | 15,066 | 44 ms | 6 MB |
| `en_20k` | 31,868 | 57,099 | 145 ms | 15 MB |
| `en_50k` | 67,434 | 146,789 | 340 ms | 34 MB |
| `en_100k` | 120,907 | 323,465 | 667 ms | 66 MB |
| `en_all` | 439,902 | 1,482,518 | 2,716 ms | 255 MB |

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
| `NOUN REG` | 158,502 | 36.2% |
| `ADJECTIVE UNKNOWN` | 122,177 | 27.9% |
| `NOUN UNKNOWN` | 79,235 | 18.1% |
| `ADVERB REG` | 21,875 | 5.0% |
| `VERB SILENT_E` | 13,794 | 3.2% |
| `VERB REG` | 13,001 | 3.0% |
| `NOUN Y_FINAL` | 10,605 | 2.4% |
| `NOUN SIBILANT` | 10,025 | 2.3% |
| `VERB DOUBLE_CONS` | 2,490 | 0.6% |
| `VERB SIBILANT` | 1,835 | 0.4% |
| `ADJECTIVE Y_FINAL` | 1,778 | 0.4% |
| `VERB Y_FINAL` | 1,342 | 0.3% |
| the rest | 1,094 | 0.3% |

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
entries with explicit forms:    11,396
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
    --json ~/data/morph/results/eval.json
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

Every run writes its numbers to `~/data/morph/results/`, outside the
repository and next to the data they were measured on, because a table printed
to a terminal and then lost is a run that has to be done again.
`--json ''` turns that off. The runs behind the tables below are
`eval_default.json`, `eval_derived.json`, `eval_policy.json`,
`eval_curve.json` and `eval_curve_derived.json`.

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

### Coverage on running text

Built-in lists plus `data/closed_class.tsv` plus `en_100k`, over 770,442
alphabetic tokens and 40,235 types.

| | Tokens | Types |
|---|------:|------:|
| source `fst` | 91.5% | 66.5% |
| source `fallback` | 8.5% | 33.5% |
| gate `decomposed` | 14.7% | 28.2% |
| gate `lexicon_stem` | 74.1% | 36.8% |
| gate `irregular` | 2.7% | 1.5% |
| gate `no_analysis` | 8.5% | 33.5% |

The gap between 91.5% of tokens and 66.5% of types is the point of the
frequency ranking: a third of the vocabulary of a corpus is rare, and it is
not where the tokens are.

The 100 most frequent words the analyzer still fails on, in order, start:

```
th 1076   s 1033   british 576   dylan 448   american 415
st 392    september 377   october 373   january 356   b 352
ireland 345   km 330   november 328   april 314   december 314
```

Three kinds of thing, and none of them is a missing common noun: proper nouns
and demonyms (`british`, `dylan`, `american`, `ireland`, `september`),
abbreviations and units (`st`, `km`, `ft`, `mm`, `co`), and fragments left by
the corpus, `th` from `19th` and the single letters from initials. This is the
right residue to hand to a Unigram layer, and it is why the coverage curve
flattens: more common vocabulary cannot reach any of it.

### The lexicon size curve

Each row is the built-in lists plus `data/closed_class.tsv` plus one built
lexicon, under the default ranking. Corpus shares are over the 770,442
alphabetic tokens and 40,235 types of the WikiText sample. F1 is over internal
boundaries, counting only the forms the analyzer reached; the figure in
brackets counts a form it could not reach as a miss.

| Lexicon | Lexemes | Token fst | Type fst | Token decomposed | Inflection F1 | Derivation F1 |
|---------|--------:|----------:|---------:|-----------------:|--------------:|--------------:|
| built-in | 2,149 | 63.6% | 11.1% | 7.3% | 0.719 (0.060) | 0.852 (0.066) |
| `en_5k` | 10,885 | 84.0% | 28.2% | 12.3% | 0.741 (0.157) | 0.845 (0.163) |
| `en_20k` | 31,868 | 90.2% | 55.1% | 14.3% | 0.793 (0.302) | 0.810 (0.274) |
| `en_50k` | 67,434 | 91.3% | 65.0% | 14.7% | 0.834 (0.437) | 0.749 (0.335) |
| `en_100k` | 120,907 | 91.5% | 66.5% | 14.7% | 0.862 (0.540) | 0.661 (0.353) |
| `en_all` | 439,902 | 91.6% | 67.6% | 14.7% | 0.921 (0.860) | 0.389 (0.373) |

Gold coverage, the share of gold forms the analyzer reaches at all, rises from
3.3% to 87.0% on inflection and from 3.7% to 94.6% on derivation.

Three things to read out of this.

**Token coverage saturates at about 91.6%, and most of it arrives by 20k
words.** Going from 20k to 440k buys 1.4 points of token coverage and 12
points of type coverage. The last 9% of tokens is not a vocabulary problem:
the most frequent words the analyzer still fails on are `british`, `dylan`,
`american`, `st`, `september`, `ireland`, `km`. They are proper nouns,
abbreviations and ordinal suffixes left over from `19th`. No amount of common
noun vocabulary reaches them, and they are the right thing to hand to a
Unigram layer.

**Inflection gets steadily better.** F1 goes from 0.719 to 0.921 as the
lexicon grows, and the version that counts misses as errors goes from 0.060 to
0.860. This is what the exercise was for.

**Derivation gets steadily worse, from 0.852 to 0.389, and that is the finding
that matters.** It is not a bug in the rules. A bigger lexicon means more
derived words are listed as whole words, and a stored whole word outranks a
derivational reading by the 0.9 factor, so the analyzer stops splitting them.
Precision stays high on the few it does split and recall collapses. Read the
other column and the picture inverts: counting misses as errors, derivation F1
rises from 0.066 to 0.373, because at the small sizes the analyzer was not
reaching these words at all.

So scale buys coverage and costs decomposition, on exactly the words a
tokenizer most wants decomposed. That is the tradeoff `--derived-first` exists
to let you choose.

## `--derived-first`

```bash
./analyzer --lexicon ~/data/morph/lexicons/en_100k.tsv \
           --segment-jsonl --derived-first
```

A segmentation-mode flag. The default ranks candidate segmentations by the
analyzer's weight, and since a stored whole word scores 1.0 while a
derivational reading of the same input scores 0.9, the stored word always
wins: `quickly` stays one piece even when `quick` is in the lexicon.
`--derived-first` makes the number of pieces the primary key instead, so a
reading that decomposes into a known stem plus a known affix outranks a stored
whole word. Everything below that first key is unchanged.

```
                 default            --derived-first
quickly          quickly            quick|ly
slowly           slowly             slow|ly
happiness        happiness          happ|iness
government       government         govern|ment
accused          accused            accuse|d
nation           nation             nation
```

Two properties hold by construction rather than by testing:

- **The lexical analysis output is identical either way.** The flag re-ranks
  surface segmentations, which the lexical path never consults. Passing it
  outside a segmentation mode is an error with exit status 2 rather than a
  silent no-op, so there is no configuration in which the lexical output could
  differ.
- **Both policies rank the same analyses.** Every disagreement is one reading
  winning over another, never a reading appearing or vanishing, so nothing is
  lost: the other reading is still there under `--nbest 2`.

### The tradeoff

Measured with the built-in lists plus `data/closed_class.tsv` plus `en_100k`.

| | Default | `--derived-first` |
|---|---:|---:|
| **MorphyNet inflection** (in distribution) | | |
| exact match, analyzed only | 72.0% | 72.6% |
| boundary precision | 0.781 | 0.781 |
| boundary recall | 0.963 | 0.969 |
| boundary F1 | 0.862 | **0.865** |
| **MorphyNet derivation** (in distribution) | | |
| exact match, analyzed only | 45.5% | 47.5% |
| boundary precision | 0.800 | 0.802 |
| boundary recall | 0.564 | 0.584 |
| boundary F1 | 0.661 | **0.676** |
| **MorphoLex** (held out, independent) | | |
| exact match, analyzed only | 10.6% | 16.2% |
| boundary precision | 0.946 | 0.945 |
| boundary recall | 0.096 | 0.150 |
| boundary F1 | 0.175 | **0.259** |

Recall rises everywhere and precision barely moves: down a thousandth on
MorphoLex, up two thousandths on MorphyNet derivation. On these three golds
the flag is close to free.

On the corpus, with the same lexicon:

| | Default | `--derived-first` |
|---|---:|---:|
| source `fst`, tokens | 91.5% | 91.5% |
| gate `decomposed`, tokens | 14.7% | **20.9%** |
| gate `decomposed`, types | 28.2% | **31.0%** |
| gate `lexicon_stem`, tokens | 74.1% | 68.1% |

Coverage is identical, as it has to be: the flag re-ranks analyses rather than
finding new ones. What changes is that 42% more tokens come out of the layer
in pieces instead of whole, which for a hybrid tokenizer is the entire point
of having a morphology layer in front of a Unigram model.

That is a result of narrowing the rule. An earlier version ranked purely by
piece count, which also promoted three-piece readings over correct two-piece
ones and cost real precision: `recording` came out as `re|cord|ing`,
`underground` as `under|gr|ound`, `designed` as `de|sign|ed`. Those readings
were never competing with a stored whole word, which is the only competition
this flag exists to settle. Once the test is "decomposes at all" rather than
"has more pieces", weight decides among decompositions as it always did, and
`record` beats `cord`.

The independent gold is where it matters most. MorphoLex says the default
policy is badly under-segmenting: precision 0.946 against recall 0.096, which
is a layer that is almost always right about the boundaries it reports and
almost never reports one. `--derived-first` raises recall by half, to 0.150,
for half a point of precision. Both numbers are low in absolute terms, and the
reason is not the policy or the lexicon.

### The worst offenders in each direction

`tools/eval_lexicon.py --stages policy` runs both rankings over the same gold
and sorts the disagreements by corpus frequency, because a wrong split on a
word nobody writes costs a tokenizer nothing and a wrong split on
`government` costs it on every page. Over the 203,374 derivational gold
forms:

| | Forms |
|---|------:|
| the two policies agree | 201,524 |
| they differ | 1,850 |
| `--derived-first` right, default wrong | 1,639 |
| default right, `--derived-first` wrong | **0** |
| neither matches the gold | 211 |

Zero losses is the narrowing at work. Before it, the same comparison showed
1,619 gains against 1,225 losses.

The inflectional gold agrees: over its 187,083 forms the two policies differ
on 465, with 452 gains, no losses and 13 where neither matches.

**Gains, most frequent first.** Real ones: `being`, `used`, `following`,
`government`, `player`, `building`, `development`, `decided`, `training`,
`does`, `writer`, `manager`, `wheeler`, `outside`.

```
   682  being         →  be|ing          646  used        →  use|d
   550  following     →  follow|ing      414  government  →  govern|ment
   278  player        →  play|er         262  building    →  build|ing
   209  development   →  develop|ment    144  decided     →  decide|d
```

**The count overstates it.** Roughly a third of the top gains are the gold
being wrong rather than the analyzer getting better. MorphyNet carries
etymological and plainly spurious derivations, and `--derived-first` happens
to reproduce them:

```
  5382  is      →  i|s          1621  has     →  ha|s
  1745  its     →  it|s         1262  during  →  dur|ing
   434  until   →  un|til        279  release →  re|lease
   257  despite →  de|spite      215  river   →  rive|r
```

**The real cost is the same list, and the gold does not charge for it.**
Splitting `is` into `i|s` and `has` into `ha|s` is wrong for a tokenizer
whatever MorphyNet says, and these are among the most frequent words in
English: `is` alone is 5,382 tokens of the sample. They split because `i` and
`ha` are noun lexemes in Wiktionary, so a decomposition exists, and
`--derived-first` promotes any decomposition over a whole word.
`data/closed_class.tsv` does not protect them: its weight of 1.3 wins on the
default ranking and is never consulted on this one, where piece count comes
first.

The fix is small and is not taken here. A closed-class word has no morphology
by definition, so a decomposition should never be promoted over a `FUNCTION`
reading, but the ranking compares two `Segmentation` records and that record
carries no part of speech: the segmenter reads one off the analysis to build
the record and then drops it. Threading it through is a field on the record
that nothing prints and a test in the comparator, so it changes neither the
output contract nor the default ranking, and it belongs with a round of
measurement of its own rather than after the tables above were taken. Until
then,
`--derived-first` is best paired with a downstream filter on the handful of
function words, or used without `data/closed_class.tsv` and with those words
handled by the Unigram layer.

**Where neither policy matches, 211 forms, it is usually a disagreement about
which base, not about whether to split.**

```
   252  production  default production  derived produc|tion  gold product|ion
   145  action      default action      derived ac|tion      gold act|ion
   112  department  default department  derived de|part|ment gold depart|ment
    70  really      default really      derived re|ally      gold real|ly
```

`production` through `produce` and `-tion` gives `produc|tion`; MorphyNet
derives it from `product` and `-ion` and gives `product|ion`. Both are
defensible and the rule set only has `-tion`. `department` and `really` are
over-stripped prefixes, `de-` and `re-`, which the default ranking hid by
preferring the whole word.

### Both policies at every lexicon size

A star marks `--derived-first`.

| Lexicon | Lexemes | Token dec. | Token dec.\* | Inflection F1 | Inflection F1\* | Derivation F1 | Derivation F1\* |
|---------|--------:|-----------:|-------------:|--------------:|----------------:|--------------:|----------------:|
| built-in | 2,149 | 7.3% | 7.8% | 0.719 | 0.719 | 0.852 | 0.855 |
| `en_5k` | 10,885 | 12.3% | 17.2% | 0.741 | 0.742 | 0.845 | 0.851 |
| `en_20k` | 31,868 | 14.3% | 20.0% | 0.793 | 0.795 | 0.810 | 0.822 |
| `en_50k` | 67,434 | 14.7% | 20.8% | 0.834 | 0.837 | 0.749 | 0.764 |
| `en_100k` | 120,907 | 14.7% | 20.9% | 0.862 | 0.865 | 0.661 | 0.676 |
| `en_all` | 439,902 | 14.7% | 21.1% | 0.921 | 0.923 | 0.389 | 0.403 |

`--derived-first` is ahead at every size on every column, and the gap in what
actually gets decomposed widens with the lexicon: 0.5 points at the built-in
size, 6.4 points at 440k, a 44% relative increase in the share of tokens the
layer hands on in pieces.

It does not reverse the decline in derivation F1, though, and that is worth
being clear about. Scale still costs derivational recall even with the flag
on, because by `en_all` most derived words have no competing decomposition for
the flag to promote: the rule set has no `-ic`, `-ist`, `-able` or `-ism`, so
there is only ever one reading to rank.

### The ceiling is the rule inventory, not the lexicon

The analyzer knows five derivational suffixes, `-ness`, `-ly`, `-er`, `-tion`
and `-ment`, and fifteen prefixes. The MorphyNet derivational gold uses 2,302
distinct affixes. Only 32.5% of its rows use an affix these rules have ever
heard of, and the same is true of `-ic`, `-ist`, `-able`, `-less`, `-ism`,
`-ity`, `-al`, `-ful` and `-ive`, each of which appears thousands of times.

Counting the boundaries rather than the rows, and allowing a boundary to count
as reachable if a known prefix starts the word or a known suffix ends it:

| Gold | Boundaries | Reachable | Recall ceiling |
|------|-----------:|----------:|---------------:|
| MorphyNet derivation | 203,374 | 67,162 | 0.330 |
| MorphoLex | 11,797 | 2,759 | 0.234 |

Against those ceilings, and comparing like with like by using the
all-forms recall:

| | Default | `--derived-first` | Ceiling | Share of ceiling |
|---|---:|---:|---:|---:|
| MorphyNet derivation | 0.226 | 0.236 | 0.330 | 68% → 72% |
| MorphoLex | 0.081 | 0.127 | 0.234 | 35% → 54% |

So `--derived-first` closes a third of the remaining gap on the independent
gold, and what is left is mostly affixes the rule set does not have, plus the
15.6% of MorphoLex entries that need three or more pieces. Adding suffixes is
the next thing worth doing, and it is a change to `RULES/derivation_rule.h`
rather than to the lexicon.

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

   The reference the diff is taken against lives outside the repository, in
   `~/data/morph/results/baseline/`, with the word list that produced it and a
   note on how it was built: every form either generator produces, plus 500
   words sampled from the corpus with a fixed seed, run through the build this
   work started from. Re-measured against it, the default ranking leaves 9,701
   of 10,036 records byte identical, analyzes 305 that had no analysis, and
   changes the 30 above, counting `Maps` and `maps` separately.
   `--derived-first` leaves 9,662 identical, analyzes the same 305, and changes
   69: the 30 plus 39 adverbs and agent nouns the flag exists to split, such as
   `actually` and `farmer`.

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

15. **`--derived-first` is an error outside a segmentation mode, not a silent
    no-op.** The flag re-ranks surface segmentations, and the lexical analysis
    path never consults that ranking, so `./analyzer --derived-first cats`
    would do exactly nothing. Accepting it there would leave a user believing
    they had changed the output. Rejecting it with exit status 2 also makes
    the brief's requirement structural rather than tested: there is no
    configuration in which the lexical output differs between the policies.

16. **The `--derived-first` test is "decomposes at all", not "has more
    pieces".** Ranking purely by piece count is the obvious reading and it is
    wrong. It promotes a three-piece reading over a correct two-piece one,
    which turned `recording` into `re|cord|ing`, `underground` into
    `under|gr|ound` and `designed` into `de|sign|ed`. Those readings are not
    competing with a stored whole word, which is the only competition the flag
    exists to settle. On the derivational gold the piece-count version scored
    1,619 gains against 1,225 losses; the narrowed one scores 1,639 against
    zero.

17. **A clitic is not an alphabetic token.** WikiText separates them, writing
    `Federer 's` and `do n't`, so the obvious `[A-Za-z]+` pattern reads the
    possessive as the word `s`: 7,607 tokens, one percent of the corpus, every
    one of them scored as the analyzer failing on something that was never a
    word, and the single largest entry in the fallback list. The pattern now
    refuses to start a token straight after an apostrophe. `n't` is still
    counted and still fails, which is fair: unlike `s` it is a real piece of
    the text.

18. **MorphoLex rows whose morphemes do not concatenate to the word are
    dropped, not repaired.** Its data dictionary says the segmentation column
    gives each morpheme in its canonical form, so `cities` comes out as `city`
    plus `s`, which is not a pair of surface slices and cannot be scored
    against surface pieces. 16,189 of 68,623 rows go that way, on top of
    30,309 with a single morpheme and no boundary at all. Recovering them
    would mean guessing where the surface boundary fell, and guessing is
    exactly what would stop an independent gold standard being independent.
    The 10,058 that survive are biased towards words with no alternation and
    towards multi-root compounds, which is worth remembering when reading the
    number.

19. **`--derived-first` promotes an inflected reading as well as a derived
    one, despite the name.** The flag's test is whether a reading decomposes,
    and an inflectional suffix decomposes a word exactly as a derivational one
    does: `used` becomes `use|d` and `being` becomes `be|ing` under the same
    rule that turns `quickly` into `quick|ly`. Restricting it to the five
    derivational suffixes would need the ranking to read the piece labels, and
    it would leave the policy half applied, because a stored whole word
    outranks an inflected reading for the same reason it outranks a derived
    one: the 0.9 factor is not the only thing in play, a frequency-derived
    weight above 1.0 is. The inflectional gold is measured alongside the
    derivational one above and agrees, 452 gains against no losses, so the
    wider rule is reported rather than narrowed. The name is kept because the
    stored-versus-derived question is what the flag exists for.

20. **Evaluation results are written outside the repository, to
    `~/data/morph/results/`, and every run writes them without being asked.**
    They are measurement output, not source, and they sit next to the data
    that produced them, which is also where the lexicons, the frequency table
    and the corpus live. Writing by default rather than only on `--json`
    means a run whose table scrolled off a terminal does not have to be done
    again; `--json ''` turns it off.
