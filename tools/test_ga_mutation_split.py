#!/usr/bin/env python3
"""Unit tests for the Irish mutation split and its inverse.

    python3 tools/test_ga_mutation_split.py

Standard library only, and no data files: every form below is quoted with the
mutation it illustrates, so the test reads without MoirfEolas in front of it.

The property under test is that the split is lossless. A Splinter-style
rearrangement is only legitimate if a decoder can undo it, and unlike the
plain hybrid the lenition variant does not get that for free: its tokens do
not concatenate back to the surface, because lenition inserts its h inside
the word rather than in front of it.
"""

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "scripts"))

from ga_mutation_split import (  # noqa: E402
    read_tsv_readings,
    SPLIT_TAGS, SPLIT_TAGS_WITH_LENITION, add_dprime, marker_and_base,
    mutation_splits, pick, rebuild, rebuild_untagged,
    demutate_all,
)


class MarkerAndBase(unittest.TestCase):
    """Prepending mutations are concatenative; lenition is not."""

    def test_eclipsis_is_a_prefix(self):
        self.assertEqual(marker_and_base("gcarr", "carr"), ("g", "carr", True))

    def test_eclipsis_of_a_capital_vowel(self):
        self.assertEqual(marker_and_base("nAthair", "Athair"),
                         ("n", "Athair", True))

    def test_eclipsis_digraph(self):
        self.assertEqual(marker_and_base("bhfear", "fear"), ("bh", "fear", True))

    def test_h_prefix(self):
        self.assertEqual(marker_and_base("hainm", "ainm"), ("h", "ainm", True))

    def test_t_prefix(self):
        self.assertEqual(marker_and_base("tsamhradh", "samhradh"),
                         ("t", "samhradh", True))

    def test_hyphenated_eclipsis_keeps_the_hyphen_on_the_marker(self):
        self.assertEqual(marker_and_base("n-iomlan", "iomlan"),
                         ("n-", "iomlan", True))

    def test_lenition_is_an_infix(self):
        # 'bhean' is 'bean' with an h after the radical b, so the marker is
        # not a prefix and marker + base does not rebuild the word.
        self.assertEqual(marker_and_base("bhean", "bean"), ("h", "bean", False))

    def test_lenition_of_a_capital(self):
        self.assertEqual(marker_and_base("Bhabhta", "Babhta"),
                         ("h", "Babhta", False))

    def test_lenition_with_a_long_vowel_in_the_stem(self):
        self.assertEqual(marker_and_base("shainiula", "sainiula"),
                         ("h", "sainiula", False))


class Rebuild(unittest.TestCase):
    """The tagged inverse recovers the surface for every mutation type."""

    CASES = [
        ("Ecl", "gcarr", "carr"),
        ("Ecl", "bhfear", "fear"),
        ("Ecl", "nAthair", "Athair"),
        ("Ecl", "n-iomlan", "iomlan"),
        ("HPref", "hainm", "ainm"),
        ("TPref", "tsamhradh", "samhradh"),
        ("Len", "bhean", "bean"),
        ("Len", "Bhabhta", "Babhta"),
        ("Len", "shainiula", "sainiula"),
        ("Len", "chinnfidh", "cinnfidh"),
        ("Dpart", "d'fheach", "fheach"),
    ]

    def test_round_trip(self):
        for tag, word, base in self.CASES:
            marker, b, _ = marker_and_base(word, base)
            self.assertEqual(rebuild(tag, marker, b), word,
                             f"{tag} {word} -> {marker!r} + {b!r}")

    def test_round_trip_without_the_tag(self):
        # The only ambiguous marker is 'h'. An h-prefix sits before a vowel
        # and a lenition h after a consonant, so the stem's first letter
        # separates them.
        for tag, word, base in self.CASES:
            marker, b, _ = marker_and_base(word, base)
            self.assertEqual(rebuild_untagged(marker, b), word,
                             f"{tag} {word} -> {marker!r} + {b!r}")

    def test_the_ambiguous_marker_is_actually_exercised(self):
        # Both of these have marker 'h'; only the stem tells them apart.
        self.assertEqual(rebuild_untagged("h", "ainm"), "hainm")
        self.assertEqual(rebuild_untagged("h", "bean"), "bhean")


class Ranking(unittest.TestCase):
    """Eclipsis outranks lenition, and the concatenative test is a filter."""

    # What the over-generating analyser returns for 'bhfear': only the
    # second reading is real.
    BHFEAR = [("Len", "bfear"), ("Ecl", "fear")]

    def test_eclipsis_wins_over_lenition(self):
        self.assertEqual(pick("bhfear", self.BHFEAR), ("Ecl", "fear"))

    def test_eclipsis_still_wins_when_lenition_is_allowed(self):
        self.assertEqual(
            pick("bhfear", self.BHFEAR, SPLIT_TAGS_WITH_LENITION,
                 concatenative_only=False),
            ("Ecl", "fear"))

    def test_lenition_is_refused_by_the_concatenative_filter(self):
        self.assertIsNone(pick("bhean", [("Len", "bean")],
                               SPLIT_TAGS_WITH_LENITION))

    def test_lenition_is_taken_when_the_filter_is_dropped(self):
        self.assertEqual(
            pick("bhean", [("Len", "bean")], SPLIT_TAGS_WITH_LENITION,
                 concatenative_only=False),
            ("Len", "bean"))

    def test_lenition_is_never_taken_by_the_default_tag_list(self):
        self.assertIsNone(pick("bhean", [("Len", "bean")], SPLIT_TAGS,
                               concatenative_only=False))

    def test_a_candidate_failing_the_filter_does_not_block_a_later_one(self):
        # If the first Ecl reading is not a prefix, the next one should
        # still be reachable rather than the whole tag being abandoned.
        cands = [("Ecl", "xcarr"), ("Ecl", "carr")]
        self.assertEqual(pick("gcarr", cands), ("Ecl", "carr"))


class Splits(unittest.TestCase):
    """mutation_splits is a strict superset when lenition is allowed."""

    DEMUT = {
        "gcarr": [("Ecl", "carr")],
        "bhean": [("Len", "bean")],
        "bhfear": [("Len", "bfear"), ("Ecl", "fear")],
        "madra": [],
    }
    WORDS = list(DEMUT)

    def test_concatenative_scheme_excludes_lenition(self):
        s = mutation_splits(self.WORDS, self.DEMUT)
        self.assertEqual(set(s), {"gcarr", "bhfear"})

    def test_splinter_scheme_adds_lenition_and_keeps_the_rest(self):
        c = mutation_splits(self.WORDS, self.DEMUT)
        s = mutation_splits(self.WORDS, self.DEMUT, SPLIT_TAGS_WITH_LENITION,
                            concatenative_only=False)
        self.assertEqual(set(s), {"gcarr", "bhfear", "bhean"})
        for word, reading in c.items():
            self.assertEqual(s[word], reading, f"{word} changed reading")

    def test_unanalysed_words_are_left_alone(self):
        s = mutation_splits(self.WORDS, self.DEMUT, SPLIT_TAGS_WITH_LENITION,
                            concatenative_only=False)
        self.assertNotIn("madra", s)

    def test_every_split_rebuilds(self):
        s = mutation_splits(self.WORDS, self.DEMUT, SPLIT_TAGS_WITH_LENITION,
                            concatenative_only=False)
        for word, (tag, marker, base) in s.items():
            self.assertEqual(rebuild(tag, marker, base), word)


class DPrime(unittest.TestCase):
    """The simulated d' reading, which Scalpel does not ship."""

    def test_injects_a_reading_for_the_particle(self):
        out = add_dprime(["d'fheach", "D'eisigh", "madra"], {})
        self.assertEqual(out["d'fheach"], [("Dpart", "fheach")])
        self.assertEqual(out["D'eisigh"], [("Dpart", "eisigh")])
        self.assertNotIn("madra", out)

    def test_keeps_any_reading_the_analyser_already_had(self):
        out = add_dprime(["d'fheach"], {"d'fheach": [("Len", "d'feach")]})
        self.assertEqual(out["d'fheach"],
                         [("Dpart", "fheach"), ("Len", "d'feach")])

    def test_does_not_fire_on_a_bare_particle(self):
        self.assertEqual(add_dprime(["d'"], {}), {})


class TsvProvider(unittest.TestCase):
    """Reading another analyser's batch output as layer 1."""

    def _read(self, text, words=None):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".tsv", delete=False,
                                         encoding="utf-8") as f:
            f.write(text)
            path = f.name
        try:
            return read_tsv_readings(path, words)
        finally:
            os.unlink(path)

    def test_reads_one_reading_per_row(self):
        readings, info = self._read(
            "gcarr\tEcl\tcarr\n"
            "bhean\tLen\tbean\n")
        self.assertEqual(readings["gcarr"], [("Ecl", "carr")])
        self.assertEqual(readings["bhean"], [("Len", "bean")])
        self.assertEqual(info["dropped_by_tag"], {})

    def test_several_rows_for_one_word_keep_file_order(self):
        readings, _ = self._read(
            "bhfear\tLen\tbfear\n"
            "bhfear\tEcl\tfear\n")
        self.assertEqual(readings["bhfear"], [("Len", "bfear"), ("Ecl", "fear")])

    def test_header_row_is_skipped(self):
        readings, _ = self._read("word\ttag\tbase\ngcarr\tEcl\tcarr\n")
        self.assertEqual(list(readings), ["gcarr"])

    def test_blank_and_comment_lines_are_skipped(self):
        readings, _ = self._read("\n# a note\ngcarr\tEcl\tcarr\n\n")
        self.assertEqual(list(readings), ["gcarr"])

    def test_unknown_tags_are_dropped_and_counted_not_guessed(self):
        # Another analyser's tag set has no defined position in the ranking,
        # so an unrecognised tag is reported rather than silently ranked.
        readings, info = self._read(
            "gcarr\tEcl\tcarr\n"
            "foo\tECLIPSIS\tbar\n"
            "baz\tECLIPSIS\tqux\n")
        self.assertEqual(list(readings), ["gcarr"])
        self.assertEqual(info["dropped_by_tag"], {"ECLIPSIS": 2})
        self.assertEqual(info["tags_seen"], ["ECLIPSIS", "Ecl"])

    def test_words_outside_the_scored_list_are_ignored(self):
        readings, _ = self._read(
            "gcarr\tEcl\tcarr\nbhean\tLen\tbean\n", words=["gcarr"])
        self.assertEqual(list(readings), ["gcarr"])

    def test_a_short_row_is_an_error_rather_than_a_silent_skip(self):
        with self.assertRaises(ValueError):
            self._read("gcarr\tEcl\n")

    def test_output_feeds_straight_into_the_split(self):
        readings, _ = self._read(
            "gcarr\tEcl\tcarr\nbhean\tLen\tbean\n")
        words = ["gcarr", "bhean"]
        s = mutation_splits(words, readings, SPLIT_TAGS_WITH_LENITION,
                            concatenative_only=False)
        self.assertEqual(s["gcarr"], ("Ecl", "g", "carr"))
        self.assertEqual(s["bhean"], ("Len", "h", "bean"))


class TestNoSkipsOnTheCorpus(unittest.TestCase):
    """The analyser must not abort on anything in the GA corpus.

    The bisection in demutate_all is a safety net; after the 22 November fix
    to inverse_ok it should never fire. This is the regression test for that:
    if the skip list is ever non-empty again, the four forms (or new ones)
    have come back.
    """

    CORPUS = (Path(__file__).resolve().parents[1] / "external" /
              "tknzn4Irish" / "data" / "GA" / "all_ga_input.txt")

    def test_ga_corpus_has_no_skips(self):
        if not self.CORPUS.exists():
            self.skipTest(f"{self.CORPUS} not present")
        with open(self.CORPUS, encoding="utf-8") as f:
            vocab = sorted({line.strip() for line in f if line.strip()})
        skipped = []
        out = demutate_all(vocab, skipped=skipped)
        self.assertEqual(skipped, [], f"analyser aborted on {skipped}")
        self.assertEqual(len(out), len(vocab),
                         "every input should have come back")

    def test_the_four_known_forms_analyse(self):
        expected = {"n-APP": ("Ecl", "APP"), "n-EOS": ("Ecl", "EOS"),
                    "n-IOTA": ("Ecl", "IOTA"),
                    "t-AMHÁIN": ("TPref", "AMHÁIN")}
        skipped = []
        out = demutate_all(sorted(expected), skipped=skipped)
        self.assertEqual(skipped, [])
        for word, reading in expected.items():
            self.assertIn(reading, out[word], word)

    def test_allow_skips_is_required_to_ignore_a_skip(self):
        # Nothing skips any more, so this checks the signature rather than
        # the behaviour: the flag exists and defaults to refusing.
        import inspect
        sig = inspect.signature(demutate_all)
        self.assertIn("allow_skips", sig.parameters)
        self.assertIs(sig.parameters["allow_skips"].default, False)


if __name__ == "__main__":
    unittest.main(verbosity=2)
