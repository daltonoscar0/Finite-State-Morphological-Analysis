"""Tests for the prefix-lenition extension. Standard library only."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from ga_lenition_prefix import (  # noqa: E402
    delenite, lenition_site, prefix_lenition, prefix_lenition_composed,
)

# Enough of an attestation set to let the short prefixes fire.
ATTESTED = {"maith", "bean", "cathair", "meas", "beag", "cuir", "fag",
            "ceolaithe", "corraithe", "creidte", "car", "dalacha",
            "fostaithe", "bunscoileanna", "mathair", "seolaithe"}


class TestLenitionSite(unittest.TestCase):
    def test_accepts_the_eight_plain_lenitables(self):
        for c in "bcdfgmpt":
            self.assertTrue(lenition_site(c + "hx"), c)

    def test_s_only_before_vowel_l_n_r(self):
        for ok in ("shean", "shláinte", "shnámh", "shruth", "shoitheach"):
            self.assertTrue(lenition_site(ok), ok)
        # s does not lenite before another stop: these are not séimhiú.
        for bad in ("shciatháin", "shmutánach", "shpriocanna", "shtad"):
            self.assertFalse(lenition_site(bad), bad)

    def test_rejects_unlenitable_radicals(self):
        for bad in ("lhx", "nhx", "rhx", "hhx", "ahx"):
            self.assertFalse(lenition_site(bad), bad)

    def test_needs_an_h(self):
        self.assertFalse(lenition_site("bean"))
        self.assertFalse(lenition_site("b"))
        self.assertFalse(lenition_site(""))

    def test_delenite_is_the_inverse(self):
        self.assertEqual(delenite("bhean"), "bean")
        self.assertEqual(delenite("mháthair"), "máthair")


class TestPrefixLenition(unittest.TestCase):
    def test_unhyphenated_prefix(self):
        self.assertEqual(prefix_lenition("seanbhean", ATTESTED),
                         ("sean", "bean"))
        self.assertEqual(prefix_lenition("príomhchathair", ATTESTED),
                         ("príomh", "cathair"))

    def test_hyphenated_prefix(self):
        self.assertEqual(prefix_lenition("an-mhaith", ATTESTED),
                         ("an-", "maith"))
        self.assertEqual(prefix_lenition("droch-mheas", ATTESTED),
                         ("droch-", "meas"))

    def test_linking_vowel_before_hyphen(self):
        # 'seana-mháthair' inserts a linking vowel between prefix and hyphen.
        self.assertEqual(prefix_lenition("seana-mháthair", ATTESTED),
                         ("seana-", "mathair"[:1] + "áthair"))

    def test_longest_prefix_wins(self):
        # 'príomh' must beat any shorter string that also matches.
        pre, _ = prefix_lenition("príomhchathair", ATTESTED)
        self.assertEqual(pre, "príomh")

    def test_casing_is_preserved(self):
        self.assertEqual(prefix_lenition("Príomhchathair", ATTESTED),
                         ("Príomh", "cathair"))
        self.assertEqual(prefix_lenition("Ard-Chúirt", ATTESTED),
                         ("Ard-", "Cúirt"))

    def test_does_not_cross_a_mutation_boundary(self):
        # 'nua' must not match the eclipsis in 'nUachtarán' and return
        # 'nUa' + 'ctarán'. Same for the t-prefix on a capital.
        self.assertIsNone(prefix_lenition("nUachtarán", ATTESTED))
        self.assertIsNone(prefix_lenition("tSeáin", ATTESTED))
        self.assertIsNone(prefix_lenition("nGaeilge", ATTESTED))

    def test_all_caps_is_not_a_mutation_boundary(self):
        # An all-caps rendering must still split; only a lowercase marker
        # before a retained capital signals a mutation.
        self.assertEqual(prefix_lenition("SEANBHEAN", {"bean"}),
                         ("SEAN", "BEAN"))

    def test_no_prefix_means_no_firing(self):
        self.assertIsNone(prefix_lenition("bhean", ATTESTED))
        self.assertIsNone(prefix_lenition("cathair", ATTESTED))

    def test_prefix_without_lenition_does_not_fire(self):
        self.assertIsNone(prefix_lenition("príomhoifig", ATTESTED))
        self.assertIsNone(prefix_lenition("an-deas", ATTESTED))

    def test_stem_too_short_is_rejected(self):
        # 'rotha' would otherwise give 'ro' + 'ta'.
        self.assertIsNone(prefix_lenition("rotha", ATTESTED))

    def test_short_prefix_needs_an_attested_stem(self):
        # 'fo' is two characters, so it only fires on a known stem.
        self.assertIsNone(prefix_lenition("fochlár", set()))
        self.assertEqual(prefix_lenition("fochuir", {"cuir"}), ("fo", "cuir"))

    def test_hyphen_vouches_for_a_short_prefix(self):
        # An explicit hyphen is the writer's own boundary, so the
        # attestation test is not needed.
        self.assertEqual(prefix_lenition("fo-chlár", set()), ("fo-", "clár"))

    def test_prefix_plus_stem_rebuilds_the_surface(self):
        for w in ("seanbhean", "an-mhaith", "príomhchathair", "Ard-Chúirt",
                  "droch-mheas"):
            pre, stem = prefix_lenition(w, ATTESTED)
            self.assertEqual(pre + stem[:1] + "h" + stem[1:], w, w)


class TestComposed(unittest.TestCase):
    """Lenition behind a fronting mutation as well as a prefix."""

    def test_eclipsis_then_prefix(self):
        # 'gcomhpháirtithe' is g + comh + pháirtithe.
        got = prefix_lenition_composed(
            "gcomhpháirtithe", [("Ecl", "comhpháirtithe")], {"páirtithe"})
        self.assertEqual(got, ("g", "comh", "páirtithe"))

    def test_h_prefix_then_prefix(self):
        got = prefix_lenition_composed(
            "hinfheistíocht", [("HPref", "infheistíocht")], {"feistíocht"})
        self.assertEqual(got, ("h", "in", "feistíocht"))

    def test_three_parts_rebuild_the_surface(self):
        for word, readings, att in (
            ("gcomhpháirtithe", [("Ecl", "comhpháirtithe")], {"páirtithe"}),
            ("hinfheistíocht", [("HPref", "infheistíocht")], {"feistíocht"}),
        ):
            front, pre, stem = prefix_lenition_composed(word, readings, att)
            rebuilt = front + pre + stem[:1] + "h" + stem[1:]
            self.assertEqual(rebuilt, word, word)

    def test_lenition_reading_is_not_a_fronting_tag(self):
        # Len does not prepend, so it must not be used as a front to strip.
        self.assertIsNone(prefix_lenition_composed(
            "bhean", [("Len", "bean")], {"bean"}))

    def test_no_prefix_under_the_mutation(self):
        self.assertIsNone(prefix_lenition_composed(
            "nGaeilge", [("Ecl", "Gaeilge")], {"Gaeilge"}))

    def test_ain_is_not_a_prefix(self):
        # 'ainmhí' is one morpheme; splitting it as ain + mhí is wrong.
        self.assertIsNone(prefix_lenition("ainmhí", {"mí"}))
        self.assertIsNone(prefix_lenition("ainmhithe", {"mithe"}))


if __name__ == "__main__":
    unittest.main(verbosity=2)
