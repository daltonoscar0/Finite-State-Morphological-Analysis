#!/usr/bin/env python3
"""Unit tests for the gold conversions in tools/eval_lexicon.py.

    python3 tools/test_eval_lexicon.py

Standard library only, and no data files: every parse below is quoted from the
MorphoLex spreadsheet, with the word it belongs to, so the test is readable
without the workbook in front of it.

The MorphoLex parser is what these mostly cover. It reads a hierarchical parse
and returns surface morphemes left to right, and it used to read the suffix
direction only, which silently dropped every row carrying a prefix. Both
readers are tested: the current one, and the legacy one that is kept so the
numbers recorded before the fix can be reproduced.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from eval_lexicon import (BOUND_ROOT_MARK, flatten_morpholex,  # noqa: E402
                          flatten_morpholex_legacy, longest_common_prefix,
                          read_morpholex)


class FlattenSuffixForms(unittest.TestCase):
    """The shapes the parser already handled, which must not move."""

    def test_root_and_suffix(self):
        self.assertEqual(flatten_morpholex("{(dark)}>ness>"),
                         ["dark", "ness"])

    def test_three_stacked_suffixes(self):
        # voyeuristically
        self.assertEqual(flatten_morpholex("{(voyeur)}>ist>>ic>>ly>"),
                         ["voyeur", "ist", "ic", "ly"])

    def test_compound_of_three_roots(self):
        # workmanlike
        self.assertEqual(flatten_morpholex("{(work)}(man){(like)}"),
                         ["work", "man", "like"])

    def test_compound_with_suffixes(self):
        # typographically
        self.assertEqual(flatten_morpholex("{(typo)}(graph)>ic>>al>>ly>"),
                         ["typo", "graph", "ic", "al", "ly"])

    def test_nesting_inside_the_braces(self):
        # theologically
        self.assertEqual(flatten_morpholex("{(theo)(log)>ic>>al>}>ly>"),
                         ["theo", "log", "ic", "al", "ly"])

    def test_single_morpheme(self):
        self.assertEqual(flatten_morpholex("{(come)}"), ["come"])


class FlattenPrefixForms(unittest.TestCase):
    """The prefix direction, which the legacy reader could not see."""

    def test_bare_prefix(self):
        # unhappy
        self.assertEqual(flatten_morpholex("<un<{(happy)}"), ["un", "happy"])

    def test_bare_prefix_inside_the_braces(self):
        # review. MorphoLex writes the prefix either side of the brace, and
        # both mean the same thing about the surface.
        self.assertEqual(flatten_morpholex("{<re<(view)}"), ["re", "view"])

    def test_prefix_with_a_stacked_suffix(self):
        # upholder
        self.assertEqual(flatten_morpholex("<up<{(hold)}>er>"),
                         ["up", "hold", "er"])

    def test_prefix_with_two_stacked_suffixes(self):
        # unceremoniously
        self.assertEqual(flatten_morpholex("<un<{(ceremony)}>ious>>ly>"),
                         ["un", "ceremony", "ious", "ly"])

    def test_two_prefixes_nested(self):
        # undeceive
        self.assertEqual(flatten_morpholex("<un<{<de<(ceive)}"),
                         ["un", "de", "ceive"])

    def test_two_prefixes_in_a_row(self):
        # undischarged
        self.assertEqual(flatten_morpholex("<un<<dis<{(charge)}"),
                         ["un", "dis", "charge"])

    def test_prefix_on_a_compound(self):
        # microbiologist
        self.assertEqual(flatten_morpholex("<micro<{(bio)(log)>ist>}"),
                         ["micro", "bio", "log", "ist"])

    def test_prefix_inside_a_root_group(self):
        # triplicate
        self.assertEqual(flatten_morpholex("{(<tri<(plic)>ate>)}"),
                         ["tri", "plic", "ate"])

    def test_the_order_is_the_order_in_the_word(self):
        # The property the caller's concatenation check relies on.
        for parse, word in (("<un<{(happy)}", "unhappy"),
                            ("{<re<(view)}", "review"),
                            ("<up<{(hold)}>er>", "upholder"),
                            ("<un<{<de<(ceive)}", "undeceive"),
                            ("<micro<{(bio)(log)>ist>}", "microbiologist"),
                            ("{(<tri<(plic)>ate>)}", "triplicate")):
            self.assertEqual("".join(flatten_morpholex(parse)), word)


class FlattenRejects(unittest.TestCase):
    """What the parser refuses, rather than guessing at."""

    def test_bound_root_marker(self):
        # cohere. The underscore says the morpheme is not the free word
        # "here", so the row is dropped rather than read as if it were.
        self.assertEqual(flatten_morpholex("{<co<(_here)}"), [])
        self.assertIn(BOUND_ROOT_MARK, "{<co<(_here)}")

    def test_stray_bracket(self):
        # daly, the one row in the database with a bracket typo
        self.assertEqual(flatten_morpholex("{(daly)]>"), [])

    def test_digits_and_punctuation(self):
        self.assertEqual(flatten_morpholex("{(ab2)}>ness>"), [])
        self.assertEqual(flatten_morpholex("{(it's)}"), [])

    def test_empty_parse(self):
        self.assertEqual(flatten_morpholex(""), [])


class LegacyReader(unittest.TestCase):
    """The legacy parser, kept to reproduce the numbers from before the fix."""

    def test_prefix_rows_come_back_empty(self):
        for parse in ("<un<{(happy)}", "{<re<(view)}", "<up<{(hold)}>er>",
                      "<un<{<de<(ceive)}", "<un<<dis<{(charge)}",
                      "<micro<{(bio)(log)>ist>}", "{(<tri<(plic)>ate>)}"):
            self.assertEqual(flatten_morpholex_legacy(parse), [])

    def test_suffix_rows_are_unchanged(self):
        for parse in ("{(dark)}>ness>", "{(voyeur)}>ist>>ic>>ly>",
                      "{(work)}(man){(like)}",
                      "{(typo)}(graph)>ic>>al>>ly>",
                      "{(theo)(log)>ic>>al>}>ly>", "{(come)}"):
            self.assertEqual(flatten_morpholex_legacy(parse),
                             flatten_morpholex(parse))

    def test_an_unknown_reader_is_an_error(self):
        with self.assertRaises(ValueError):
            read_morpholex("ignored.xlsx", "sideways")


class SurfaceBoundaryConversion(unittest.TestCase):
    """The helper the MorphyNet readers use, for completeness."""

    def test_longest_common_prefix(self):
        self.assertEqual(longest_common_prefix("microtome", "microtoming"), 8)
        self.assertEqual(longest_common_prefix("city", "cities"), 3)
        self.assertEqual(longest_common_prefix("walk", "walked"), 4)
        self.assertEqual(longest_common_prefix("go", "went"), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
