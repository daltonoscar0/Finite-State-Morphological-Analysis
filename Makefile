CXX      = g++
CXXFLAGS = -std=c++17 -Wall -I.

SCALPEL_DIR = ../Scalpel
SCALPEL_SRC = $(SCALPEL_DIR)/tokenizer.cpp \
              $(SCALPEL_DIR)/sentence_segmenter.cpp \
              $(SCALPEL_DIR)/char_classes.cpp

MORPH_SRC = SYMBOLS/symbol.cpp \
            PIPELINE/text_pipeline.cpp

# Headers for the surface segmentation output. Listed as prerequisites of the
# targets that use them so that editing one forces a rebuild; the compile lines
# below name their sources explicitly rather than using $^, which would
# otherwise hand these .h files to the compiler.
SEG_HDR = ANALYSIS/segmentation.h \
          ANALYSIS/segment_driver.h \
          OUTPUT/jsonl_print.h

.PHONY: all clean test

all: analyzer test_infra test_lex test_analyzer test_seg

# ── Main analyzer (Scalpel + morphology) ────────────────────────────────────
analyzer: main.cpp $(MORPH_SRC) $(SCALPEL_SRC) $(SEG_HDR)
	$(CXX) $(CXXFLAGS) -o $@ main.cpp $(MORPH_SRC) $(SCALPEL_SRC)

# ── Infrastructure tests (FSA, FST, SymbolTable) ─────────────────────────────
test_infra: test_infrastructure.cpp SYMBOLS/symbol.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# ── Lexicon + rules tests ─────────────────────────────────────────────────────
test_lex: test_lexicon_rules.cpp SYMBOLS/symbol.cpp $(PIPELINE_SRC)
	$(CXX) $(CXXFLAGS) -o $@ test_lexicon_rules.cpp SYMBOLS/symbol.cpp

# ── Comprehensive integration tests ──────────────────────────────────────────
test_analyzer: test_analyzer.cpp SYMBOLS/symbol.cpp
	$(CXX) $(CXXFLAGS) -o $@ test_analyzer.cpp SYMBOLS/symbol.cpp

# ── Surface segmentation tests ───────────────────────────────────────────────
test_seg: test_segmentation.cpp SYMBOLS/symbol.cpp $(SEG_HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_segmentation.cpp SYMBOLS/symbol.cpp

# ── Run every test binary ────────────────────────────────────────────────────
# Fails on the first non-zero exit status.
test: all
	./test_infra
	./test_lex
	./test_analyzer
	./test_seg

clean:
	rm -f analyzer test_infra test_lex test_analyzer test_seg
