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
            RULES/verb_rule.h \
            SYMBOLS/symbol.h

.PHONY: all clean test

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

clean:
	rm -f analyzer test_infra test_lex test_analyzer test_seg
