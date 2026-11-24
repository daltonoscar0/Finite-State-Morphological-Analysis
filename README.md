# Morphology

[![tests](https://github.com/daltonoscar0/Finite-State-Morphological-Analysis/actions/workflows/tests.yml/badge.svg)](https://github.com/daltonoscar0/Finite-State-Morphological-Analysis/actions/workflows/tests.yml)

A finite-state morphological analyzer for English. Given raw text or individual words, it segments, looks up stems, and produces linguistic analyses like `cats → cat +N +PL` or `walked → walk +V +PAST`.

## Agreement with MorphoLex

Scored against MorphoLex, an independent gold standard, on 12,828 forms. These
are the latest numbers in [docs/BASELINES.md](docs/BASELINES.md) — the round
after the prefix guard, measured with the fixed MorphoLex reader.

| Lexicon setting | P | R | F1 | exact |
|---|--:|--:|---:|------:|
| built-in (1,825 lexemes) | 0.950 | 0.111 | 0.199 | 0.103 |
| `en_100k` | 0.969 | 0.257 | 0.406 | 0.232 |
| `en_100k`, derived first | 0.976 | 0.380 | 0.547 | 0.335 |
| `en_all`, derived first | 0.976 | **0.385** | **0.552** | 0.337 |

The shape of that result matters more than the F1: precision is high and recall
is low. The analyzer splits what it knows and declines the rest, so it is wrong
rarely and silent often. Recall is bounded by lexicon coverage, which is why the
figure moves from 0.111 to 0.385 as the lexicon grows.

On MorphyNet, which is in-distribution rather than independent, the same
`en_all` setting reaches F1 0.838 on inflection (186,947 forms) and 0.554 on
derivation (194,771 forms).

`docs/BASELINES.md` also compares these against BPE, WordPiece and Unigram at
three vocabulary sizes, and records what the prefix guard cost on the
derivational gold.

## Features

- **Inflectional morphology**, noun plurals, verb conjugations (past, present participle, 3rd person singular), adjective comparatives/superlatives
- **Orthographic alternations**, y->i (city->cities), consonant doubling (stop->stopped), silent e deletion (hope->hoping), epenthetic vowel (kiss->kisses)
- **Irregular forms**, suppletion (go->went), umlaut plurals (mouse->mice), zero-marking (sheep->sheep), and more
- **Derivational morphology**, twenty-eight suffixes (-ness, -ly, -er, -tion, -ment, -able, -al, -ful, -less, -ity, -ism, -ize and the rest) and fifteen prefixes (un-, re-, dis-, pre-, etc.), stacking up to two suffixes plus an inflection
- **Ambiguity handling**, returns all valid analyses with weight-based ranking (e.g. `flies -> fly +N +PL | fly +V +3SG`)
- **Text pipeline**, sentence segmentation and tokenization via Scalpel, then morphological analysis per token

## Architecture

The analyzer is built on finite-state automata:

- **LexiconFSA**, a trie storing the lexemes for O(n) stem lookup: 1,825 built in, or several hundred thousand with `--lexicon` (see [docs/LEXICON.md](docs/LEXICON.md))
- **MorphRule FSTs**, finite-state transducers encoding inflectional rules per stem class (regular, y-final, sibilant, silent-e, double-consonant, irregular)
- **DerivationRule**, handles derivational suffixes and prefixes
- **Scalpel**, an external FSM-based tokenizer for text segmentation (sentence splitting, word/number/punctuation classification)

Analysis proceeds in two phases: text segmentation (Scalpel), then morphological analysis (stem lookup, suffix/prefix stripping, orthographic reversal, irregular form matching, deduplication).

## Requirements

- C++17 compatible compiler (GCC or Clang)
- [Scalpel](../Scalpel/) tokenizer (expected at `../Scalpel/` relative to this directory)

## Building

```bash
make          # builds: analyzer, test_analyzer, test_infra, test_lex
make clean    # remove binaries
```

## Usage

### Interactive mode

```bash
./analyzer
```

Type text or individual words at the prompt. Special commands:

| Command  | Description                          |
|----------|--------------------------------------|
| `:help`  | Show available commands              |
| `:stats` | Print lexicon and automaton stats    |
| `:quit`  | Exit                                 |

Example session:

```
> The cats walked quickly.
[Sentence 1]
The        → (no analysis)
cats       → cat +N +PL
walked     → walk +V +PAST
quickly    → quick +ADJ [-ly ->ADV]
.          → (punctuation)
```

### Batch mode

Pass words as arguments:

```bash
./analyzer cats dogs cities flies
```

```
cats   → cat +N +PL
dogs   → dog +N +PL
cities → city +N +PL
flies  →
  fly +N +PL
  fly +V +3SG
```

## Output format

Analyses use standard linguistic notation:

| Pattern                      | Meaning                    | Example               |
|------------------------------|----------------------------|-----------------------|
| `stem +POS +FEATURE`        | Inflected form             | `cat +N +PL`         |
| `stem +POS +FEAT1 +FEAT2`   | Multiple features          | `walk +V +PAST`      |
| `prefix- + stem +POS`       | Prefixed form              | `un- + kind +ADJ`    |
| `stem +POS [-suffix ->POS]` | Derivational morphology    | `dark +ADJ [-ness ->N]` |

### Inflectional features

- **Nouns**: `+PL` (plural)
- **Verbs**: `+3SG` (3rd person singular), `+PAST` (past tense), `+PRESP` (present participle), `+PASTP` (past participle)
- **Adjectives**: `+COMP` (comparative), `+SUP` (superlative)

## Lexicon

The lexicon is loaded from hardcoded entries in `LANGUAGES/english_configuration.h`, supplemented by `data/english_lexicon.tsv`, and extended by any number of `--lexicon FILE` arguments. Each entry has a stem, part of speech, and stem class controlling which orthographic rules apply:

| Stem class     | Behavior                        | Example             |
|----------------|---------------------------------|---------------------|
| `REG`          | Regular inflection              | cat → cats          |
| `Y_FINAL`      | Consonant+y alternation         | city → cities       |
| `SIBILANT`     | Epenthetic vowel before -s      | kiss → kisses       |
| `DOUBLE_CONS`  | Final consonant doubling        | stop → stopped      |
| `SILENT_E`     | Silent e deletion               | hope → hoping       |
| `IRR_MOUSE`    | Umlaut plural                   | mouse → mice        |
| `IRR_CHILD`    | -ren plural                     | child → children    |
| `IRR_SHEEP`    | Zero-marked plural              | sheep → sheep       |
| `IRR_OX`       | -en plural                      | ox → oxen           |
| `UNKNOWN`      | No inflection claimed           | water, abstract     |

The TSV format for supplementary entries:

```
stem	POS	StemClass	FEATURE=form	WEIGHT=N.N
go	V	REG	PAST=went	WEIGHT=1.0
```

## Tests

```bash
make test          # builds and runs all four binaries, stops on the first failure
```

Or individually:

```bash
./test_analyzer    # 70+ integration tests across all morphological phenomena
./test_infra       # FSA/FST and symbol table unit tests
./test_lex         # lexicon loading and rule application tests
./test_seg         # surface segmentation, including text mode
```

The tool tests are kept separate so that `make && make test` needs only a
compiler. They are standard library only and download nothing:

```bash
make test-tools    # gold conversions, including the MorphoLex parser
```

Both targets run in CI. The baseline training and scoring scripts do **not**:
they need a virtualenv and gold-standard data stored outside the repository
(see the reproduction steps in [docs/BASELINES.md](docs/BASELINES.md)).

## Project structure

```
├── main.cpp                        # Entry point
├── Makefile                        # Build configuration
├── ANALYSIS/                       # Morphological analysis engine
│   ├── analyzer.h                  # Core analyzer
│   ├── analysis.h                  # Analysis result data structure
│   ├── surface_fsa.h               # Surface form FSA
│   └── tokenizer.h                 # Internal tokenizer
├── AUTOMATA/                       # Finite-state automata
│   ├── automaton.h                 # Base automaton class
│   ├── fsa.h                       # Finite-State Acceptor
│   ├── fst.h                       # Finite-State Transducer
│   ├── state.h                     # State nodes
│   └── transition.h                # Transitions
├── LEXICON/                        # Dictionary management
│   ├── lexicon_fsa.h               # Trie-based lexicon
│   ├── lexeme.h                    # Dictionary entries
│   ├── stem_class.h                # Morphological stem categories
│   └── lexicon_loader.h            # TSV loader
├── RULES/                          # Morphological rules
│   ├── morph_rule.h                # Base inflectional rule
│   ├── plural_rule.h               # Noun plurals
│   ├── verb_rule.h                 # Verb inflection
│   ├── adj_rule.h                  # Adjective degrees
│   ├── derivation_rule.h           # Derivational morphology
│   ├── orthography.h               # Spelling-derived suffixation
│   └── form_generator.h            # Lexeme to its surface forms
├── SYMBOLS/                        # Symbol table
├── LANGUAGES/                      # Language-specific config
│   └── english_configuration.h     # English lexicon & rules
├── PIPELINE/                       # Scalpel bridge
├── OUTPUT/                         # Output formatting
├── data/
│   ├── english_lexicon.tsv         # Supplementary lexicon
│   ├── closed_class.tsv            # Determiners, prepositions, pronouns
│   └── lexicon_sample.tsv          # Sample of a generated lexicon
└── tools/
    ├── fst_tokenizer.py            # Long-lived analyzer client
    ├── score_segmentation.py       # Boundary scorer
    ├── make_freq.py                # Word frequency table
    ├── build_lexicon.py            # Wiktionary to lexicon TSV
    └── eval_lexicon.py             # Coverage and boundary F1
```

## Surface segmentation

See [docs/SEGMENTATION.md](docs/SEGMENTATION.md) for the `--segment-jsonl` and `--segment-text` output formats and the scorer.

## Scaling the lexicon

See [docs/LEXICON.md](docs/LEXICON.md) for `--lexicon`, the closed-class list,
building a lexicon of several hundred thousand entries from Wiktionary, the
`--derived-first` ranking policy, and the evaluation harness.

```bash
./analyzer --selfcheck                             # gaps between the rules and the lexicon
./analyzer --lexicon data/closed_class.tsv \
           --lexicon ~/data/morph/lexicons/en_100k.tsv --segment-jsonl
python3 tools/eval_lexicon.py --all                # coverage and boundary F1
```

## License

MIT. See [LICENSE](LICENSE).
