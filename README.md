# Morphology

A finite-state morphological analyzer for English. Given raw text or individual words, it segments, looks up stems, and produces linguistic analyses like `cats → cat +N +PL` or `walked → walk +V +PAST`.

## Features

- **Inflectional morphology**, noun plurals, verb conjugations (past, present participle, 3rd person singular), adjective comparatives/superlatives
- **Orthographic alternations**, y->i (city->cities), consonant doubling (stop->stopped), silent e deletion (hope->hoping), epenthetic vowel (kiss->kisses)
- **Irregular forms**, suppletion (go->went), umlaut plurals (mouse->mice), zero-marking (sheep->sheep), and more
- **Derivational morphology**, suffixes (-ness, -ly, -er, -tion, -ment) and prefixes (un-, re-, dis-, pre-, etc.)
- **Ambiguity handling**, returns all valid analyses with weight-based ranking (e.g. `flies -> fly +N +PL | fly +V +3SG`)
- **Text pipeline**, sentence segmentation and tokenization via Scalpel, then morphological analysis per token

## Architecture

The analyzer is built on finite-state automata:

- **LexiconFSA**, a trie storing ~1825 lexemes for O(n) stem lookup
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

The lexicon is loaded from hardcoded entries in `LANGUAGES/english_configuration.h` and supplemented by `data/english_lexicon.tsv`. Each entry has a stem, part of speech, and stem class controlling which orthographic rules apply:

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

The TSV format for supplementary entries:

```
stem	POS	StemClass	FEATURE=form	WEIGHT=N.N
go	V	REG	PAST=went	WEIGHT=1.0
```

## Tests

```bash
./test_analyzer    # 70+ integration tests across all morphological phenomena
./test_infra       # FSA/FST and symbol table unit tests
./test_lex         # lexicon loading and rule application tests
```

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
│   └── derivation_rule.h           # Derivational morphology
├── SYMBOLS/                        # Symbol table
├── LANGUAGES/                      # Language-specific config
│   └── english_configuration.h     # English lexicon & rules
├── PIPELINE/                       # Scalpel bridge
├── OUTPUT/                         # Output formatting
└── data/
    └── english_lexicon.tsv         # Supplementary lexicon
```
