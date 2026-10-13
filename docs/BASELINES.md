# Standard subword baselines, measured against the analyzer

This is the baseline half of the tokenizer comparison. It trains BPE,
WordPiece and Unigram at three vocabulary sizes on the same corpus, and scores
all nine of them plus the analyzer under four lexicon settings with one
scorer, on one word list, so the numbers can be put in the same table.

No language model is trained here. Everything below is intrinsic: compression,
round trip, vocabulary use, and agreement with two morphological gold
standards.

## Environment

| Piece | Version |
|-------|---------|
| Python | 3.9.6, in `~/venvs/tok`, outside the repository |
| `sentencepiece` | 0.2.2 |
| `tokenizers` (Hugging Face) | 0.22.2 |

Nothing else was installed. The analyzer side of the comparison runs on the
standard library alone, as `tools/fst_tokenizer.py` always has.

## Data

| What | Where | Size |
|------|-------|------|
| Training text | `~/data/corpora/babylm/train_100M.txt` | 538,254,632 bytes, 11.6M lines |
| Evaluation text | `~/data/corpora/babylm/test.txt` | 51,457,874 bytes, 1.11M lines |
| Lexicons | `~/data/morph/lexicons/en_100k.tsv`, `en_all.tsv` | |
| Gold, independent | `~/data/morph/morpholex/MorphoLex-en/MorphoLEX_en.xlsx` | |
| Gold, in distribution | `~/data/morph/morphynet/eng.inflectional.v1.tsv`, `eng.derivational.v1.tsv` | |

The corpora are read and never written to or copied into the repository. All
outputs go under `~/data/tok/`.

No tokenizer saw `test.txt` during training.

## The shared pretokenizer

`tools/pretokenize.py`. Whitespace separates units and is not itself a unit.
Between two runs of whitespace, a unit is a maximal run of characters from one
of three classes:

| Class | Pattern |
|-------|---------|
| letters | `[^\W\d_]+` |
| digits | `\d+` |
| other | `(?:[^\s\w]|_)+` |

Nothing is lowercased, nothing is stripped, and no character other than the
separating whitespace is dropped. So `don't` is three units, `don`, `'`, `t`;
`U.S.A.` is six; `3.14` is three; `cats,` is two; and `42kg` is two.

An **alphabetic word token** is a unit the letters branch matched. That is the
unit every system is scored on.

`tools/make_splits.py` applies the rule once and writes what everything else
reads:

| File | Contents |
|------|----------|
| `~/data/tok/splits/train_pretok.txt.gz` | one line per training line, units joined by single spaces |
| `~/data/tok/splits/test_words.txt.gz` | the alphabetic word tokens of `test.txt`, in order |
| `~/data/tok/splits/test_types.tsv` | the same tokens as a type list with counts |
| `~/data/tok/splits/test_units.tsv` | every test unit with counts, punctuation and digits included |

The baselines are trained on `train_pretok.txt` with their own pre-tokenizers
set to plain whitespace splitting, which is the only way to be certain all
three saw the same units.

### Test split

| | |
|---|---|
| alphabetic word tokens | 9,647,333 |
| alphabetic word types | 125,852 |
| all units | 13,153,999 |
| distinct characters | 370 |

## Decisions

Judgment calls made while building this, and why.

1. **An apostrophe is punctuation and is split off.** `don't` becomes `don`,
   `'`, `t`. The alternative, keeping word-internal apostrophes as
   `tools/eval_lexicon.py` does, would have been defensible, but the brief for
   the shared pretokenizer is to split punctuation off from words and an
   apostrophe is punctuation. The cost is that clitics arrive as the separate
   word tokens `t`, `s`, `ll` and `re`, which every system then has to handle
   alike. Since the rule is shared, it cannot favour one system over another.

2. **A unit is a maximal run of one character class, for all three classes.**
   So `!!` is one unit and `U.S.A.` is six. Treating punctuation runs the same
   way as letter and digit runs keeps the rule to a single sentence with no
   special cases. It does mean a unit like `,"` exists.

3. **Scoring is done per type and weighted by frequency, not by re-encoding
   the token stream.** Every tokenizer here encodes a unit independently of
   its context, so encoding the 125,852 test word types once and multiplying
   by their counts gives numerically identical token-weighted figures to
   encoding all 9.6M tokens, at a seventy-fifth of the cost.
   `test_words.txt.gz` is still written in order, because the brief asks for
   it and because it is the artifact a later experiment would stream.

4. **WordPiece is run over a byte level pre-tokenizer, because `tokenizers`
   has no byte fallback for it.** BPE and Unigram both take `byte_fallback`,
   so an input character the model never saw is spelled out as `<0xXX>`
   pieces and decodes back exactly. WordPiece has no such option: a word
   holding one unknown character becomes a single `<unk>` and is lost. The
   test text contains 36 characters the training text never shows, two of
   them letters, so round trip would not have been 100%.

   Running WordPiece over a byte level pre-tokenizer gives it the same
   property by another route. Its alphabet becomes the 256 byte symbols
   rather than the 702 characters of the training text, so any input at all
   can be spelled, and the byte level decoder puts the characters back. For
   ASCII, which is every word scored against a gold standard here, the pieces
   are identical to what plain WordPiece would have produced.

   One gap had to be closed by hand. The trainer adds an alphabet symbol in
   word initial form but adds the `##` continuation form only for symbols it
   saw inside a word, and a byte symbol missing its continuation form still
   forces an `<unk>`. Both forms of all 256 are filled in after training,
   which puts WordPiece a few dozen entries above its nominal size. The exact
   sizes are in the table.

5. **An alternation is a property of the word, so the alternation tolerant
   variant uses one mask for all thirteen systems.** The tolerance is meant
   to forgive a boundary one character off where the stem changed spelling,
   and whether `cities` involves a change of spelling does not depend on
   which tokenizer is reading it. The analyzer is the only system here that
   reports alternations, so the mask is taken once from its richest setting,
   `en_all` with `--derived-first`, and then applied unchanged to every row
   of the table, baselines included. Taking each analyzer setting's own mask
   would have given the four analyzer rows four different metrics and left
   the baselines with none.

6. **The four analyzer settings load no `data/closed_class.tsv`.** The
   lexicon evaluation in `docs/LEXICON.md` loads it alongside every built
   lexicon, since no mined lexicon supplies a determiner. The brief for this
   comparison names four settings and that file is not among them, so it is
   left out and the function words fall through to `no_analysis`. That costs
   the analyzer coverage on the most frequent tokens in the corpus, and the
   coverage column should be read with that in mind.

7. **The large intermediates are gzip compressed.** Housekeeping on this
   machine compressed `train_pretok.txt` and `test_words.txt` in place while
   a training run was reading them, which killed the run. Writing them
   compressed in the first place removes the failure mode, and the libraries
   are fed from a line iterator rather than by path. It also keeps the whole
   of `~/data/tok` to a few hundred megabytes. All nine baselines were
   retrained after this change so that the times and memory figures below
   come from the code as it stands.
