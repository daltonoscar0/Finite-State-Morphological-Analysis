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

`POS` is one of `NOUN`, `VERB`, `ADJECTIVE` (or `ADJ`), `ADVERB` (or `ADV`).
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
