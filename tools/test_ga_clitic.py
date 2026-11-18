"""Tests for clitic splitting. Standard library only.

Every pattern here was found in MoirfEolas by the Stage 2 audit, except the
curly-apostrophe cases, which MoirfEolas does not contain but real text does.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from ga_clitic import (  # noqa: E402
    CLITIC_TAG, add_clitic_readings, clitic_analysis, rebuild_clitic,
    split_clitic,
)


class TestSplitClitic(unittest.TestCase):
    def test_d_lower_and_upper(self):
        self.assertEqual(split_clitic("d'ól"), ("d'", "ól"))
        self.assertEqual(split_clitic("D'ól"), ("D'", "ól"))

    def test_m_and_b(self):
        self.assertEqual(split_clitic("m'athair"), ("m'", "athair"))
        self.assertEqual(split_clitic("m'intinn"), ("m'", "intinn"))
        self.assertEqual(split_clitic("b'fhearr"), ("b'", "fhearr"))
        self.assertEqual(split_clitic("B'fhearr"), ("B'", "fhearr"))
        self.assertEqual(split_clitic("B'ionann"), ("B'", "ionann"))

    def test_curly_apostrophe(self):
        self.assertEqual(split_clitic("d’fhág"), ("d’", "fhág"))
        self.assertEqual(split_clitic("D’ól"), ("D’", "ól"))

    def test_modifier_letter_apostrophe(self):
        self.assertEqual(split_clitic("dʼfhág"), ("dʼ", "fhág"))

    def test_all_caps(self):
        self.assertEqual(split_clitic("D'FHÁG"), ("D'", "FHÁG"))

    def test_parts_concatenate_back(self):
        for w in ("d'ól", "D'fhág", "m'athair", "b'fhearr", "d’fhág",
                  "d'fho-alt", "d'Fhógraí", "d'infhaighteacht"):
            clitic, rest = split_clitic(w)
            self.assertEqual(clitic + rest, w, w)

    def test_eclipsed_b_is_not_a_clitic_at_position_zero(self):
        # 'mb'fhéidir' is eclipsis m on the clitic b'; the FST reads the
        # eclipsis and the clitic is only visible in its base.
        self.assertIsNone(split_clitic("mb'fhéidir"))
        self.assertEqual(split_clitic("b'fhéidir"), ("b'", "fhéidir"))

    def test_rejects_non_clitic_apostrophes(self):
        self.assertIsNone(split_clitic("you'd"))
        self.assertIsNone(split_clitic("pe'"))
        self.assertIsNone(split_clitic("O'Brien"))

    def test_rejects_too_short(self):
        self.assertIsNone(split_clitic("d'"))
        self.assertIsNone(split_clitic("d'a"))

    def test_rejects_words_without_an_apostrophe(self):
        self.assertIsNone(split_clitic("dol"))
        self.assertIsNone(split_clitic("bhean"))


class TestCliticAnalysis(unittest.TestCase):
    def test_lenition_stacks_under_the_clitic(self):
        self.assertEqual(clitic_analysis("d'fhág"), ("d'", "fág", True))
        self.assertEqual(clitic_analysis("b'fhearr"), ("b'", "fearr", True))
        self.assertEqual(clitic_analysis("D'fhéadfaí"),
                         ("D'", "féadfaí", True))

    def test_no_lenition_before_a_vowel(self):
        self.assertEqual(clitic_analysis("d'ól"), ("d'", "ól", False))
        self.assertEqual(clitic_analysis("d'athair"), ("d'", "athair", False))
        self.assertEqual(clitic_analysis("m'intinn"),
                         ("m'", "intinn", False))

    def test_round_trip(self):
        for w in ("d'fhág", "d'ól", "m'athair", "b'fhearr", "D'fhéadfaí",
                  "d’fhág", "d'Fhógraí", "d'fho-alt", "D'FHÁG"):
            clitic, stem, lenited = clitic_analysis(w)
            self.assertEqual(rebuild_clitic(clitic, stem, lenited), w, w)

    def test_capitalised_stem_keeps_its_case(self):
        self.assertEqual(clitic_analysis("d'Fhógraí"), ("d'", "Fógraí", True))


class TestAddCliticReadings(unittest.TestCase):
    def test_reading_is_added_in_front(self):
        out, n = add_clitic_readings(["d'fhág"], {"d'fhág": []})
        self.assertEqual(n, 1)
        self.assertEqual(out["d'fhág"], [(CLITIC_TAG, "fhág")])

    def test_existing_readings_are_kept_after(self):
        out, _ = add_clitic_readings(
            ["b'fhearr"], {"b'fhearr": [("Len", "b'fearr")]})
        self.assertEqual(out["b'fhearr"],
                         [(CLITIC_TAG, "fhearr"), ("Len", "b'fearr")])

    def test_non_clitics_are_untouched(self):
        demut = {"bhean": [("Len", "bean")]}
        out, n = add_clitic_readings(["bhean"], demut)
        self.assertEqual(n, 0)
        self.assertEqual(out["bhean"], [("Len", "bean")])

    def test_no_duplicate_reading_on_a_second_pass(self):
        out, _ = add_clitic_readings(["d'ól"], {"d'ól": []})
        out2, _ = add_clitic_readings(["d'ól"], out)
        self.assertEqual(out2["d'ól"], [(CLITIC_TAG, "ól")])


if __name__ == "__main__":
    unittest.main(verbosity=2)
