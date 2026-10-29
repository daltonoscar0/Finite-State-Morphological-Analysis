CXX      = g++
CXXFLAGS = -std=c++17 -Wall -I.

SCALPEL_DIR = ../Scalpel
SCALPEL_SRC = $(SCALPEL_DIR)/tokenizer.cpp \
              $(SCALPEL_DIR)/sentence_segmenter.cpp \
              $(SCALPEL_DIR)/char_classes.cpp

MORPH_SRC = SYMBOLS/symbol.cpp \
            PIPELINE/text_pipeline.cpp

# Every header the binaries compile in. The analyzer is almost entirely
# header-only, so these are listed as prerequisites of the targets that use
# them: without that, editing ANALYSIS/analyzer.h left a stale binary in place
# and the test suite kept passing against the old code. The compile lines below
# name their sources explicitly rather than using $^, which would otherwise
# hand these .h files to the compiler.
MORPH_HDR = ANALYSIS/analysis.h \
            ANALYSIS/analyzer.h \
            ANALYSIS/segmentation.h \
            ANALYSIS/segment_driver.h \
            ANALYSIS/surface_fsa.h \
            ANALYSIS/text_segmenter.h \
            ANALYSIS/tokenizer.h \
            AUTOMATA/automaton.h \
            AUTOMATA/fsa.h \
            AUTOMATA/fst.h \
            AUTOMATA/state.h \
            AUTOMATA/transition.h \
            LANGUAGES/english_configuration.h \
            LEXICON/closed_class.h \
            LEXICON/lexeme.h \
            LEXICON/lexicon_fsa.h \
            LEXICON/lexicon_loader.h \
            LEXICON/stem_class.h \
            OUTPUT/jsonl_print.h \
            OUTPUT/pretty_print.h \
            PIPELINE/text_pipeline.h \
            RULES/adj_rule.h \
            RULES/derivation_rule.h \
            RULES/form_generator.h \
            RULES/morph_rule.h \
            RULES/orthography.h \
            RULES/plural_rule.h \
            RULES/prefix_rule.h \
            RULES/verb_rule.h \
            SYMBOLS/symbol.h

# ── Interpreters for the measurement stages ─────────────────────────────────
# The analyzer side is standard library only. The tokenizer libraries and the
# MorphoLex spreadsheet reader live in ~/venvs/tok, and torch lives in
# ~/venvs/lm, both outside the repository. Override on the command line if
# they are somewhere else.
TOK_PY = $(HOME)/venvs/tok/bin/python
LM_PY  = $(HOME)/venvs/lm/bin/python

MATCHED_DIR = $(HOME)/data/tok/matched
LM_STEPS    = 6000

.PHONY: all clean test test-tools \
        matched matched-pretokenize matched-tokenizers matched-intrinsic \
        matched-lm-benchmark matched-lm matched-results

all: analyzer test_infra test_lex test_analyzer test_seg

# ── Main analyzer (Scalpel + morphology) ────────────────────────────────────
analyzer: main.cpp $(MORPH_SRC) $(SCALPEL_SRC) $(MORPH_HDR)
	$(CXX) $(CXXFLAGS) -o $@ main.cpp $(MORPH_SRC) $(SCALPEL_SRC)

# ── Infrastructure tests (FSA, FST, SymbolTable) ─────────────────────────────
test_infra: test_infrastructure.cpp SYMBOLS/symbol.cpp $(MORPH_HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_infrastructure.cpp SYMBOLS/symbol.cpp

# ── Lexicon + rules tests ─────────────────────────────────────────────────────
test_lex: test_lexicon_rules.cpp SYMBOLS/symbol.cpp $(MORPH_HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_lexicon_rules.cpp SYMBOLS/symbol.cpp

# ── Comprehensive integration tests ──────────────────────────────────────────
test_analyzer: test_analyzer.cpp SYMBOLS/symbol.cpp $(MORPH_HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_analyzer.cpp SYMBOLS/symbol.cpp

# ── Surface segmentation tests ───────────────────────────────────────────────
# Links the Scalpel bridge as well, because the text-mode tests exercise
# tokenize_line.
test_seg: test_segmentation.cpp $(MORPH_SRC) $(SCALPEL_SRC) $(MORPH_HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_segmentation.cpp $(MORPH_SRC) $(SCALPEL_SRC)

# ── Run every test binary ────────────────────────────────────────────────────
# Fails on the first non-zero exit status.
test: all
	./test_infra
	./test_lex
	./test_analyzer
	./test_seg

# ── Tool tests ───────────────────────────────────────────────────────────────
# Not part of `test`, which stays free of a Python dependency: `make && make
# test` builds and checks the analyzer with a compiler and nothing else. The
# tools need python3 anyway, so a caller touching them runs this as well.
test-tools:
	python3 tools/test_eval_lexicon.py

clean:
	rm -f analyzer test_infra test_lex test_analyzer test_seg

# ── The matched-vocabulary experiment ────────────────────────────────────────
# One target per stage, in order. Each reads what the one before it wrote, so
# they can be run separately and re-run without repeating the stage before.
# Everything lands under $(MATCHED_DIR); nothing is written inside the
# repository except results/matched_vocab.md.

matched-pretokenize: analyzer
	$(TOK_PY) scripts/pretokenize.py --out-dir $(MATCHED_DIR)

matched-tokenizers:
	$(TOK_PY) scripts/train_tokenizers.py --out-dir $(MATCHED_DIR)

matched-intrinsic: analyzer
	$(TOK_PY) scripts/score_intrinsic.py --matched-dir $(MATCHED_DIR)

# Runs one cell for a few hundred steps and extrapolates to all twelve, so the
# cost of the LM stage is known before it is paid.
matched-lm-benchmark:
	$(LM_PY) scripts/train_lm.py --matched-dir $(MATCHED_DIR) \
	    --steps $(LM_STEPS) --benchmark 200

matched-lm:
	$(LM_PY) scripts/train_lm.py --matched-dir $(MATCHED_DIR) \
	    --steps $(LM_STEPS)

matched-results:
	$(TOK_PY) scripts/results.py --matched-dir $(MATCHED_DIR)

matched: matched-pretokenize matched-tokenizers matched-intrinsic \
         matched-lm matched-results
