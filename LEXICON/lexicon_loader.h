#ifndef LEXICON_LOADER_H
#define LEXICON_LOADER_H

#include "lexicon_fsa.h"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

/**
 * LexiconLoader: Load lexicon entries from a TSV file
 *
 * TSV format (tab-separated):
 *   stem  POS  StemClass  [FEATURE=form ...]  [WEIGHT=N.N]
 *
 * Examples:
 *   cat      NOUN    REG
 *   city     NOUN    Y_FINAL
 *   mouse    NOUN    IRR_MOUSE   PLURAL=mice
 *   go       VERB    IRR_GO      PAST=went   PAST_PART=gone
 *   walk     VERB    REG
 *   walk     NOUN    REG
 *   the      FUNCTION REG
 *   quickly  ADVERB  REG         WEIGHT=0.96
 *
 * POS is NOUN, VERB, ADJECTIVE (or ADJ), ADVERB (or ADV), or FUNCTION (or
 * FUNC) for a closed-class word. WEIGHT sets the ambiguity ranking weight,
 * default 1.0; see docs/LEXICON.md for the scale.
 *
 * Lines beginning with '#' are comments. Blank lines are ignored. Unparsable
 * lines are skipped with a warning on stderr rather than aborting the load,
 * because a generated lexicon of half a million entries should not be lost to
 * one bad row.
 */
class LexiconLoader {
public:
    static bool load_from_tsv(const std::string& filepath, LexiconFSA& lexicon) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open lexicon file: " << filepath << std::endl;
            return false;
        }

        std::string line;
        int line_num = 0;

        // A generated lexicon can be hundreds of thousands of lines long, so a
        // warning per bad line would bury everything else. The first few are
        // printed and the rest are counted.
        const int max_warnings = 10;
        int warnings = 0;

        while (std::getline(file, line)) {
            line_num++;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;

            std::istringstream ss(line);
            std::string stem, pos_str, sc_str;

            if (!(ss >> stem >> pos_str >> sc_str)) continue;

            PartOfSpeech pos = parse_pos(pos_str);
            StemClass sc = parse_stem_class(sc_str);

            if (sc == StemClass::UNKNOWN && sc_str != "UNKNOWN") {
                if (++warnings <= max_warnings) {
                    std::cerr << "Warning: " << filepath << ":" << line_num
                              << ": unknown stem class '" << sc_str << "'"
                              << std::endl;
                }
                continue;
            }

            auto lex = std::make_shared<Lexeme>(stem, pos, sc);

            // Parse optional FEATURE=form pairs
            std::string feat_pair;
            while (ss >> feat_pair) {
                // Check for WEIGHT=N.N
                if (feat_pair.substr(0, 7) == "WEIGHT=") {
                    try {
                        float w = std::stof(feat_pair.substr(7));
                        lex->set_weight(w);
                    } catch (...) {}
                    continue;
                }
                auto eq = feat_pair.find('=');
                if (eq == std::string::npos) continue;
                std::string feat_str = feat_pair.substr(0, eq);
                std::string form = feat_pair.substr(eq + 1);
                lex->add_irregular_form(parse_feature(feat_str), form);
            }

            lexicon.insert_lexeme(lex);
        }

        if (warnings > max_warnings) {
            std::cerr << "Warning: " << filepath << ": "
                      << (warnings - max_warnings)
                      << " further lines skipped for the same reason"
                      << std::endl;
        }

        return true;
    }

private:
    static PartOfSpeech parse_pos(const std::string& s) {
        if (s == "NOUN") return PartOfSpeech::NOUN;
        if (s == "VERB") return PartOfSpeech::VERB;
        if (s == "ADJECTIVE" || s == "ADJ") return PartOfSpeech::ADJECTIVE;
        if (s == "ADVERB" || s == "ADV") return PartOfSpeech::ADVERB;
        if (s == "FUNCTION" || s == "FUNC") return PartOfSpeech::FUNCTION;
        return PartOfSpeech::NOUN;
    }

    static StemClass parse_stem_class(const std::string& s) {
        if (s == "REG") return StemClass::REG;
        if (s == "Y_FINAL") return StemClass::Y_FINAL;
        if (s == "SIBILANT") return StemClass::SIBILANT;
        if (s == "DOUBLE_CONS") return StemClass::DOUBLE_CONS;
        if (s == "SILENT_E") return StemClass::SILENT_E;
        if (s == "IRR_MOUSE") return StemClass::IRR_MOUSE;
        if (s == "IRR_CHILD") return StemClass::IRR_CHILD;
        if (s == "IRR_SHEEP") return StemClass::IRR_SHEEP;
        if (s == "IRR_OX") return StemClass::IRR_OX;
        if (s == "IRR_GO") return StemClass::IRR_GO;
        if (s == "IRR_BE") return StemClass::IRR_BE;
        if (s == "IRR_HAVE") return StemClass::IRR_HAVE;
        if (s == "IRR_DO") return StemClass::IRR_DO;
        if (s == "IRR_SING") return StemClass::IRR_SING;
        if (s == "IRR_RING") return StemClass::IRR_RING;
        if (s == "IRR_SWIM") return StemClass::IRR_SWIM;
        return StemClass::UNKNOWN;
    }

    static Lexeme::Feature parse_feature(const std::string& s) {
        if (s == "PLURAL")    return Lexeme::Feature::PLURAL;
        if (s == "PAST")      return Lexeme::Feature::PAST;
        if (s == "PAST_PART") return Lexeme::Feature::PAST_PART;
        if (s == "PRES_PART") return Lexeme::Feature::PRES_PART;
        if (s == "THIRD_SG")  return Lexeme::Feature::THIRD_SG;
        if (s == "COMP")      return Lexeme::Feature::COMP;
        if (s == "SUP")       return Lexeme::Feature::SUP;
        return Lexeme::Feature::PLURAL; // default fallback
    }
};

#endif // LEXICON_LOADER_H
