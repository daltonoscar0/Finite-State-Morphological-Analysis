"""Per-family tests for the pre-tokenizer: boundary survival and round-trip.

One test per family for each of the three failure modes the boundary-survival
check named, plus the round-trip that Stage 4 depends on.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from ga_pipeline import (  # noqa: E402
    CONVENTIONS, detokenize, layer1_units, peel, tokenize, units_of_line,
)

FAMILIES = sorted(CONVENTIONS)

LINES = [
    "Chuaigh an bhean go dtí an siopa.",
    "D'fhág sé an teach ar maidin.",
    "Tá an-mhaith ag an bpríomh-chathair.",
    "Labhair sé i nGaeilge le hÉireann agus tSeáin.",
    "d’fhág sé rud, agus \"luachana\"!",
    "Seanbhean, príomhchathair; droch-mheas?",
    "Tab\there and  double  spaces.",
    "m'athair agus b'fhearr liom é.",
    "nGaeilge",
    "",
    "   ",
    "Aon-fhocal.",
    # A leading or trailing space has no neighbouring chunk to carry it, so
    # the convention families used to drop it. Found by the 12,000-line
    # round trip, not by hand.
    " Leading space, nGaeilge.",
    "Trailing space, hÉireann. ",
    " both ",
    "\tleading tab",
]

# Readings good enough to exercise the mutation path without the FST.
READINGS = {
    "bhean": [("Len", "bean")],
    "nGaeilge": [("Ecl", "Gaeilge")],
    "hÉireann": [("HPref", "Éireann")],
    "tSeáin": [("TPref", "Seáin")],
    "bpríomh-chathair": [("Ecl", "príomh-chathair")],
}


def readings_for(word):
    return READINGS.get(word, [])


class TestPeel(unittest.TestCase):
    def test_punctuation_is_peeled_both_ends(self):
        self.assertEqual(peel('"focal"!'), ('"', "focal", '"!'))
        self.assertEqual(peel("(b)"), ("(", "b", ")"))

    def test_clitic_apostrophe_stays_in_the_core(self):
        self.assertEqual(peel("d'fhág"), ("", "d'fhág", ""))
        self.assertEqual(peel("d’fhág"), ("", "d’fhág", ""))

    def test_internal_hyphen_stays(self):
        self.assertEqual(peel("an-mhaith"), ("", "an-mhaith", ""))

    def test_bare_word(self):
        self.assertEqual(peel("focal"), ("", "focal", ""))


class TestUnits(unittest.TestCase):
    def test_units_partition_the_line(self):
        for line in LINES:
            self.assertEqual("".join(units_of_line(line, readings_for)),
                             line, line)

    def test_clitic_becomes_its_own_unit(self):
        self.assertEqual(layer1_units("d'fhág", []), ["d'", "fhág"])
        self.assertEqual(layer1_units("m'athair", []), ["m'", "athair"])

    def test_mutation_becomes_its_own_unit(self):
        self.assertEqual(layer1_units("nGaeilge", READINGS["nGaeilge"]),
                         ["n", "Gaeilge"])
        self.assertEqual(layer1_units("hÉireann", READINGS["hÉireann"]),
                         ["h", "Éireann"])

    def test_unanalysed_word_stays_whole(self):
        self.assertEqual(layer1_units("focal", []), ["focal"])


class TestRoundTrip(unittest.TestCase):
    """detok(tok(x)) == x, for every family."""

    def test_round_trip_per_family(self):
        for family in FAMILIES:
            for line in LINES:
                got = detokenize(tokenize(line, family, readings_for=readings_for),
                                 family)
                self.assertEqual(got, line, f"{family}: {line!r}")

    def test_round_trip_with_a_subword_encoder(self):
        # A real encoder splits units further; the round trip must survive it.
        def enc(unit):
            return [unit[i:i + 2] for i in range(0, len(unit), 2)] or [unit]

        for family in FAMILIES:
            for line in LINES:
                got = detokenize(
                    tokenize(line, family, encode_unit=enc,
                             readings_for=readings_for), family)
                self.assertEqual(got, line, f"{family}: {line!r}")


class TestBoundarySurvival(unittest.TestCase):
    """The three failure modes, one test each, per family."""

    def test_mutation_boundary_is_present(self):
        # 'nGaeilge' must come out as two adjacent pieces, not one.
        for family in FAMILIES:
            toks = tokenize("nGaeilge", family, readings_for=readings_for)
            self.assertEqual(len(toks), 2, family)

    def test_no_stray_boundary_marker_inside_a_word(self):
        # Only the first piece of a chunk may carry the word-initial marker,
        # and no piece inside a word may carry one.
        for family in FAMILIES:
            conv = CONVENTIONS[family]
            if not conv["initial"]:
                continue
            toks = tokenize("nGaeilge le hÉireann", family,
                            readings_for=readings_for)
            inside = [t for t in toks[1:]
                      if t.startswith(conv["initial"])]
            # Marked pieces may only be chunk-initial, and there are two
            # further chunks after the first.
            self.assertEqual(len(inside), 2, f"{family}: {toks}")

    def test_wordpiece_continuation_marker_is_emitted(self):
        # The scored hybrid omits '##' on every stem; here it must be there.
        toks = tokenize("nGaeilge", "WordPiece", readings_for=readings_for)
        self.assertEqual(toks, ["n", "##Gaeilge"])

    def test_marker_is_not_merged_into_the_stem(self):
        # Units are encoded separately, so even an encoder that would happily
        # span the boundary cannot.
        def greedy(unit):
            return [unit]

        for family in FAMILIES:
            toks = tokenize("d'fhág", family, encode_unit=greedy,
                            readings_for=readings_for)
            self.assertEqual(len(toks), 2, f"{family}: {toks}")
            self.assertIn("d'", toks[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
