# Surface segmentation output

The analyzer's original output is a lexical analysis: `cities` becomes
`city +N +PL`. That string is useful to a linguist and useless to a tokenizer
evaluation, because it cannot be concatenated back into the input. Compression,
fertility, round-trip exactness and boundary F1 all need pieces that rebuild
the original text.

`--segment-jsonl` adds a second view of the same analysis. It emits the surface
pieces: substrings of the input word that concatenate back to it byte for byte,
with the linguistic analysis carried alongside as annotation. The lexical
output is unchanged, and the two modes share no formatting code.

## Running it

```bash
make                                   # builds analyzer and the test binaries
echo cities | ./analyzer --segment-jsonl
```

```json
{"word": "cities", "pieces": ["cit", "ies"], "offsets": [0, 3, 6], "labels": ["STEM", "SUFFIX_PL"], "lemma": "city", "alt": "y_to_i", "source": "fst", "gate": "decomposed", "n_analyses": 1, "weight": 1}
```

The mode reads one word per line from stdin and writes exactly one JSON object
per input line to stdout, in order, flushing after every line, so a long-lived
parent process can drive it word by word without deadlocking on a buffered
pipe. Startup notices go to stderr, so stdout carries nothing but records.

Run the analyzer from the repository root. It loads
`data/english_lexicon.tsv` by a relative path.

`--nbest K` (default 1) adds an `alternatives` array holding the other
analyses of the same word in the same record format, ranked by weight.

## Format

Every field is always present.

| Field | Type | Meaning |
|-------|------|---------|
| `word` | string | The input line, unchanged |
| `pieces` | array of strings | Substrings of `word` that concatenate back to it |
| `offsets` | array of integers | Character boundaries, `len(pieces) + 1` of them |
| `labels` | array of strings | One label per piece |
| `lemma` | string | The base form, or `""` when nothing was found |
| `alt` | string | The orthographic alternation involved, or `""` |
| `source` | string | `fst`, `fallback`, or `passthrough` |
| `gate` | string | Reporting-only classification, see below |
| `n_analyses` | integer | How many analyses the analyzer returned for the word |
| `weight` | number | Ranking weight of the reported analysis, 0 when none |

With `--nbest K` greater than 1, an `alternatives` array is added, holding up
to `K - 1` further records with the same fields.

### Invariants

These hold for every input, including garbage, mixed case, digits, and very
long strings.

1. `"".join(pieces) == word`, byte for byte, with case preserved. Lexicon
   matching is case insensitive internally, but the emitted pieces are slices
   of the original input: `Cats` gives `["Cat", "s"]` and `CATS` gives
   `["CAT", "S"]`.
2. `offsets` are Unicode code point boundaries, not byte offsets. There are
   `len(pieces) + 1` of them, the first is 0, the last is the character length
   of the word, and they strictly increase. No piece is empty.
3. `labels` has the same length as `pieces`.
4. `source` is one of `fst`, `fallback`, `passthrough`. `fallback` means
   nothing was found, and then `pieces == [word]`, `labels == ["UNK"]`,
   `gate == "no_analysis"`, `lemma == ""`, and `weight == 0`.
5. `gate` is one of `decomposed`, `lexicon_stem`, `irregular`, `no_analysis`,
   `passthrough`.
6. `alt` is one of `""`, `y_to_i`, `e_deletion`, `consonant_doubling`,
   `epenthesis`.
7. The output is valid JSON for any input. Quotes, backslashes and control
   characters are escaped, and `weight` is always a finite number.

### Labels

| Label | Meaning |
|-------|---------|
| `STEM` | The stem piece |
| `PREFIX` | A derivational prefix: `un`, `re`, `dis` and the rest |
| `SUFFIX_PL` | Noun plural |
| `SUFFIX_PAST` | Past tense or past participle |
| `SUFFIX_ING` | Present participle |
| `SUFFIX_3SG` | Third person singular |
| `SUFFIX_COMP` | Comparative |
| `SUFFIX_SUP` | Superlative |
| `SUFFIX_INFL` | An inflectional suffix with no more specific tag |
| `SUFFIX_DERIV` | A derivational suffix: `-ness`, `-ly`, `-er`, `-tion`, `-ment` |
| `IRREGULAR` | A whole irregular or suppletive form |
| `UNK` | A fallback or passthrough piece |

### Gates

Gate values are reporting only. The analyzer does not decide what a downstream
tokenizer does with them.

| Gate | Meaning |
|------|---------|
| `decomposed` | Two or more pieces |
| `lexicon_stem` | A known stem, one piece, no affix |
| `irregular` | A suppletive or irregular form kept as one piece: `went`, `mice`, `sheep` |
| `no_analysis` | Nothing found, the word is one `UNK` piece |
| `passthrough` | Punctuation, numbers, and other input with no letters in it |

## The segmentation convention

**The stem piece is the longest prefix of the surface word that is shared with
the lemma. Everything after it belongs to the following suffix piece, allomorph
included.**

A prefix piece, when the analysis strips one, comes first and the rule above
applies to what remains. Irregular and suppletive forms are a single piece.

This rule is uniform. It never needs to know which allomorph a suffix took,
and it never produces a piece that is not a slice of the input. It does mean
the boundary sits one character away from where some gold standards put it
whenever an alternation is involved, which is what the `alt` field is for.

### A worked example per alternation type

**No alternation.** The lemma is a prefix of the surface form, so the stem
piece is the whole lemma.

```
walked    lemma walk    shared prefix "walk"    walk | ed      alt ""
cats      lemma cat     shared prefix "cat"     cat  | s       alt ""
hoped     lemma hope    shared prefix "hope"    hope | d       alt ""
```

**`y_to_i`.** The lemma's final `y` surfaces as `i`, so the shared prefix stops
just before it and the `i` goes to the suffix.

```
cities    lemma city    shared prefix "cit"     cit  | ies     alt "y_to_i"
flies     lemma fly     shared prefix "fl"      fl   | ies     alt "y_to_i"
happier   lemma happy   shared prefix "happ"    happ | ier     alt "y_to_i"
```

**`e_deletion`.** The lemma's final silent `e` is dropped before a vowel
initial suffix, so the shared prefix stops just before it.

```
hoping    lemma hope    shared prefix "hop"     hop  | ing     alt "e_deletion"
```

**`consonant_doubling`.** The whole lemma matches, and the extra copy of the
final consonant starts the suffix piece.

```
stopped   lemma stop    shared prefix "stop"    stop | ped     alt "consonant_doubling"
running   lemma run     shared prefix "run"     run  | ning    alt "consonant_doubling"
biggest   lemma big     shared prefix "big"     big  | gest    alt "consonant_doubling"
```

**`epenthesis`.** The whole lemma matches, and the inserted `e` starts the
suffix piece.

```
kisses    lemma kiss    shared prefix "kiss"    kiss | es      alt "epenthesis"
boxes     lemma box     shared prefix "box"     box  | es      alt "epenthesis"
churches  lemma church  shared prefix "church"  church | es    alt "epenthesis"
```

**Prefixes and derivation.** A prefix piece is split off first, and a
derivational suffix is labelled `SUFFIX_DERIV`.

```
unkind        un   | kind                PREFIX | STEM
rewrite       re   | write               PREFIX | STEM
darkness      dark | ness                STEM   | SUFFIX_DERIV
happiness     happ | iness               STEM   | SUFFIX_DERIV   alt "y_to_i"
unhappiness   un | happ | iness          PREFIX | STEM | SUFFIX_DERIV
```

**Irregular and suppletive forms.** One piece, labelled `IRREGULAR`, with the
lemma still reported.

```
went      lemma go       went        gate "irregular"
mice      lemma mouse    mice        gate "irregular"
sheep     lemma sheep    sheep       gate "irregular"
children  lemma child    children    gate "irregular"
oxen      lemma ox       oxen        gate "irregular"
better    lemma good     better      gate "irregular"
```

## Text mode

`--segment-text` reads raw text lines instead of one word per line. Each line
goes to Scalpel's tokenizer, and one record is emitted per token with the same
fields plus two more:

| Field | Type | Meaning |
|-------|------|---------|
| `kind` | string | Scalpel's token class: `WORD`, `PUNCT`, `NUMBER`, `ABBREVIATION`, `CONTRACTION`, `HYPHENATED` |
| `ws_before` | string | The exact whitespace that preceded the token |

Only `WORD` tokens are analyzed. Everything else is emitted as its raw surface
string with source `passthrough`, not as a wrapped tag.

```bash
echo "The cats walked quickly." | ./analyzer --segment-text
```

```json
{"word": "The", "pieces": ["The"], ..., "source": "fallback", "gate": "no_analysis", "kind": "WORD", "ws_before": ""}
{"word": "cats", "pieces": ["cat", "s"], ..., "source": "fst", "gate": "decomposed", "kind": "WORD", "ws_before": " "}
{"word": ".", "pieces": ["."], ..., "source": "passthrough", "gate": "passthrough", "kind": "PUNCT", "ws_before": ""}
```

### Reconstruction

Concatenating `ws_before` followed by the pieces, over a line's records,
reproduces the line exactly. The records tile the input: every byte of it sits
in exactly one record, either inside a piece or inside a `ws_before`.

Two details make that work:

Whitespace at the end of a line has no following token to attach to, so it is
carried forward, along with the line terminator, into the `ws_before` of the
next token, which is usually on the next line. Concatenating every record in
the stream therefore reproduces the whole input, not just each line. Whatever
whitespace is still pending at end of input becomes one final record with an
empty word and no pieces.

Scalpel does not always emit a token for every character. In
`hello 'single'` it drops the opening apostrophe. Any uncovered span that is
not whitespace is recovered as its own `PUNCT` record, so no input character is
lost.

## The Python client

`tools/fst_tokenizer.py` starts one `analyzer --segment-jsonl` process and
keeps it alive, because loading the lexicon costs far more than analyzing a
word.

```python
from fst_tokenizer import FstTokenizer

with FstTokenizer("./analyzer") as tok:
    tok.encode("cities")            # ['cit', 'ies']
    tok.decode(['cit', 'ies'])      # 'cities'
    tok.segment("stopped")          # the full record as a dict
    tok.segment_many(words)         # one record per word, any batch size
```

It is standard library only. Words are sent in chunks bounded by estimated
output size, and each chunk is read back before the next is sent, so a large
batch cannot deadlock. If the analyzer dies, the next call raises
`AnalyzerError` carrying the exit status and the tail of its stderr.

## The scorer

`tools/score_segmentation.py` is standard library only.

```bash
python3 tools/score_segmentation.py --gold data/gold_sample.tsv
python3 tools/score_segmentation.py --gold data/gold_sample.tsv \
    --words corpus_words.txt --json scores.json --alt-tolerant
```

| Option | Meaning |
|--------|---------|
| `--gold FILE` | Gold TSV, one entry per line: the word, a tab, then the pieces joined by `\|` (required) |
| `--words FILE` | Unlabeled word list for the intrinsic metrics, defaults to the gold words |
| `--analyzer PATH` | Analyzer binary, default `./analyzer` |
| `--json PATH` | Also write every score to this path as JSON |
| `--alt-tolerant` | Accept a boundary one character off, see below |

It reports, without needing a gold standard: round-trip exactness, coverage
(the share of records with source `fst`), the share of words in each gate,
fertility in pieces per word, and piece size in characters and in bytes.
Against the gold standard it reports exact match rate and precision, recall
and F1 over internal boundaries, micro-averaged and split by gate.

Internal boundaries exclude the two word edges, which every segmentation
agrees on. A gate whose words are all single pieces contributes no boundaries
at all, so its precision, recall and F1 print as `n/a` rather than zero.

The exit status is 1 if any record fails to round-trip, and the first few
offenders are printed, because every other metric assumes the pieces rebuild
the input.

Gold lines that are blank or start with `#` are ignored. A gold entry whose
pieces do not concatenate to its word is reported as malformed and excluded
from scoring.

### `--alt-tolerant`

With this flag, a predicted boundary counts as correct when it is within one
character of a gold boundary **and** the record's `alt` is non-empty. Matching
stays one to one, so two predicted boundaries can never score against the same
gold boundary.

The flag exists for third-party gold standards that use the other convention
for alternations, writing `citi|es` where this analyzer writes `cit|ies`.
`data/gold_sample.tsv` follows the convention documented above, so the flag
makes no difference to it.

## `data/gold_sample.tsv`

A smoke test for the scorer, nothing more. It is about 40 entries written by
hand to exercise every branch of the scorer and every case in the convention
above. Numbers computed against it say nothing about the analyzer's accuracy
on real text. Use a real gold standard for that.

Five of its entries (`quickly`, `clearly`, `slowly`, `movement`, `teacher`)
are deliberately ones the current lexicon cannot reach, so the scorer visibly
reports a miss instead of silently agreeing with itself. See the fifth decision
below.

## Tests

```bash
make test        # every test binary
make test_seg && ./test_seg
```

`test_segmentation.cpp` has three groups: a table of hand-written expected
segmentations covering every case in the convention, a property test that
expands every lexeme in the loaded lexicon into its inflected and derived
forms and asserts the invariants on each resulting record, and a fuzz group
covering empty strings, mixed case, digits, punctuation, control characters,
very long strings, non-ASCII and invalid UTF-8. It prints counts so the
coverage is visible.

The invariants are re-derived inside the test from the emitted record rather
than delegated to `Segmentation::check()`, so a bug in `check()` cannot hide a
bug in the segmenter. `check()` is then asserted to agree.

## Decisions

Judgment calls made while implementing this, and why.

1. **Input with no letters is `passthrough`; input with at least one letter but
   no analysis is `fallback`.** So `42` and `!!!` are passthrough, while
   `zxqvkj` and `x42` are fallback. The rule is a single test that is easy to
   state and easy to reproduce in a scorer. A mixed string like `x42` could
   defensibly go either way; it is treated as a word the analyzer failed on,
   because it contains one.

2. **Irregular and suppletive forms are always a single piece, even when a
   boundary is orthographically visible.** `children` could be `child|ren` and
   `oxen` could be `ox|en`, but `went` and `mice` cannot be split at all, and
   one uniform rule for everything reached through the lexicon's stored
   irregular forms is easier to score against than a rule that splits some of
   them. The lemma is still reported, so no information is lost.

3. **The primary record is the highest-weighted analysis, with a full
   tie-break order.** Weight is the analyzer's own ranking and stays the
   primary key. Weights tie often, though (`unkind` as a noun and as an
   adjective, the two readings of `sheep`), and `std::sort` is not stable, so
   without further keys the chosen record would depend on the standard library
   implementation. The remaining keys are: more pieces first, then lemma, then
   the label sequence, then gate, then `alt`. Preferring more pieces also means
   that when a decomposed and an undecomposed reading are equally weighted, the
   decomposed one is reported, which is why `unhappiness` comes out as
   `un|happ|iness`.

4. **`n_analyses` counts the analyses the analyzer returned, not the number of
   distinct surface splits.** Two analyses can describe the same split: part of
   speech distinguishes the two readings of `unkind` and does not appear in
   this output. Those collapse in `alternatives`, so `unkind` reports
   `n_analyses` of 2 with no alternatives.

5. **Words whose whole form is a stored lexeme are reported as a single
   `lexicon_stem` piece, even when a derivational reading exists.** The
   analyzer multiplies derivational readings by 0.9, so a stored whole form
   outranks them. `quickly`, `slowly` and `clearly` are all adverb lexemes, and
   `movement` and `teacher` are noun lexemes, so all five come out as one
   piece. `clearly` and `movement` do have the decomposed reading available
   under `--nbest 2`; `quickly`, `slowly` and `teacher` do not, because
   `quick`, `slow` and `teach` are not in the lexicon at all.

   This was left alone rather than reweighted or patched. Changing the weights
   would change the lexical output, which has to stay byte identical, and
   adding the missing adjectives to the lexicon is a lexicon change, not a
   segmentation change. The smallest fix, if the gap matters for a particular
   experiment, is to add `quick`, `slow` and `teach` to
   `data/english_lexicon.tsv` and to lower the weight of the stored `-ly`
   adverbs below 0.9.

6. **Alternation detection reads the lemma's final grapheme as well as the
   `SIBILANT` stem class.** Some stems appear in the lexicon more than once
   under different stem classes: `box` is listed both as a regular noun and as
   a sibilant one, and which entry a lookup finds first depends on insertion
   order. Checking the final grapheme as well makes `boxes` report
   `epenthesis` regardless.

7. **A blank line gives a `passthrough` record with zero pieces and
   `offsets == [0]`.** It cannot be a `fallback` record, because that form
   requires `pieces == [word]` and no piece may be empty.

8. **Invalid UTF-8 is escaped byte by byte as `\u00XX`.** The output has to be
   valid JSON and valid UTF-8, so raw invalid bytes cannot be passed through.
   Round-tripping still holds for a JSON reader, because `word` and the single
   fallback piece are escaped identically and decode to the same string. It
   does not hold at the level of the original raw bytes, which is unavoidable
   for input that is not valid UTF-8 in the first place. Well-formed multi-byte
   sequences are passed through unescaped, so ordinary accented words stay
   readable in the output.

9. **Offsets count each invalid byte as one code point.** This keeps the
   offsets well defined for arbitrary byte input and matches the escaping
   above, which emits exactly one `\u00XX` per invalid byte.

10. **A trailing carriage return is stripped from each input line.** Otherwise
    CRLF input would leave a control character inside every word.

11. **An unrecognized `--` option is an error with exit status 2.** Previously
    every argument was treated as a word to analyze, so `./analyzer --help`
    analyzed the string `--help`. Words given alongside `--segment-jsonl` are
    ignored with a note on stderr, since that mode reads stdin.

12. **The property test generates surface forms from stem classes rather than
    by running the rule FSTs.** The FSTs in `RULES/` are built for recognition
    and expose no generation entry point, so the test applies the same
    stem-class orthography the analyzer reverses. Irregular forms come straight
    out of each lexeme's stored map and derivational forms from the
    `DerivationRule` objects the configuration registers, so those two groups
    do come from the existing rule data.

13. **`make` now also builds `test_seg`, and the `analyzer` and `test_seg`
    targets list the new headers as prerequisites.** Without the header
    prerequisites, editing `ANALYSIS/segmentation.h` left a stale binary in
    place and the test suite silently kept passing against the old code. The
    compile lines name their sources explicitly instead of using `$^`, which
    would otherwise pass the header files to the compiler. This is the pattern
    the existing `test_lex` target already used.

14. **In text mode, whitespace with no following token gets its own record,
    with an empty word and `kind` of `PUNCT`.** Trailing whitespace at end of
    input has to appear somewhere for the input to be reproducible, and
    `ws_before` is the only field that can hold it. `PUNCT` is the closest fit
    among the token classes the format allows, though no Scalpel token is
    involved. The same record shape is used for a span Scalpel skipped,
    where the word is the skipped text rather than empty.

15. **Text mode uses only Scalpel's tokenizer, not its sentence segmenter.**
    The record format has no sentence field, and sentence grouping would not
    change any piece.

16. **`test_seg` links the Scalpel bridge.** The text-mode reconstruction
    invariant is the subtlest part of this work, so it is tested rather than
    only exercised by hand, which means the test binary needs
    `tokenize_line`. The other three test binaries are unchanged.

17. **The repository was not clean when this work started.** `git status`
    listed four tracked build binaries as modified or deleted (`analyzer`,
    `test_analyzer`, `test_infra`, `test_lex`) and no source file. Since those
    are build outputs that `make` regenerates and that must never be staged,
    the work proceeded rather than stopping. None of them is staged in any
    commit on this branch.
