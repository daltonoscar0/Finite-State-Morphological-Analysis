"""Scalpel's Irish initial-mutation layer: the split algebra and its inverse.

Standard library only, and no dependency on Jane Adkins' scorer, so the
transformation can be tested on its own. ``scripts/moirfeolas_score.py``
imports everything here.

A mutation reading is ``(tag, marker, base)``. ``marker`` is the material the
mutation added and ``base`` is the stem underneath it. Eclipsis, the h- and
t-prefixes and the d' particle all prepend, so ``marker + base`` rebuilds the
surface. Lenition does not: it inserts an h after the radical consonant, so
``bhean`` reads as ``bean`` with the h at index 1 and no boundary split can
isolate it. ``rebuild`` inverts both.
"""
import subprocess
from pathlib import Path

BRIDGE = Path(__file__).resolve().parents[1] / "ga_demutate"

# Tags whose material MoirfEolas records in the urú column. "Len" is excluded:
# the gold does not mark lenition, so emitting it can only cost.
SPLIT_TAGS = ("Ecl", "HPref", "TPref")

# Lenition last, so eclipsis still outranks it on a word that reads as both.
SPLIT_TAGS_WITH_LENITION = SPLIT_TAGS + ("Len",)

# The d' / D' verbal particle. Scalpel has no rule for it (decision 4) and
# --simulate-dprime injects it as a synthetic reading to price that decision.
DPRIME_TAG = "Dpart"


# --------------------------------------------------------------------------
# Layer 1: Scalpel's mutation FST, via the bridge binary.
# --------------------------------------------------------------------------

def demutate_all(words):
    """word -> [(tag, base), ...], in the analyser's own candidate order."""
    proc = subprocess.run(
        [str(BRIDGE)], input="\n".join(words),
        capture_output=True, text=True, check=True,
    )
    out = {}
    for line in proc.stdout.splitlines():
        parts = line.split("\t")
        out[parts[0]] = [tuple(c.split(":", 1)) for c in parts[1:]]
    return out


def add_dprime(words, demut):
    """Inject a synthetic d'/D' reading, to price decision 4.

    d' is the past-tense verbal particle, not an initial mutation, so
    mutation_fst has no rule for it. The gold records it in the urú column
    anyway, and it is 639 of the 661 words layer 1 misses.
    """
    out = dict(demut)
    for w in words:
        if w[:2] in ("d'", "D'") and len(w) > 2:
            out[w] = [(DPRIME_TAG, w[2:])] + list(out.get(w, []))
    return out


def pick(word, candidates, tags=SPLIT_TAGS, concatenative_only=True):
    """Choose one mutation reading, or None.

    Eclipsis outranks lenition: the over-generating analyser reads 'bhfear'
    as both Len:bfear and Ecl:fear, and only the second is real. Tags are
    tried in the order given, so whichever list is passed sets the ranking.
    A candidate that fails the concatenative test is skipped rather than
    accepted, so a later candidate can still win its tag.
    """
    for tag in tags:
        for cand_tag, base in candidates:
            if cand_tag != tag or not base or base == word:
                continue
            marker, _, concat = marker_and_base(word, base)
            if not marker or (concatenative_only and not concat):
                continue
            return cand_tag, base
    return None


def marker_and_base(word, base):
    """Split ``word`` into the material a mutation added and the stem.

    Eclipsis, h-prefix, t-prefix and d' all prepend, so the marker is a
    genuine prefix and marker + base reconstructs the surface exactly.
    Lenition does not: it inserts an h after the first consonant, so
    'bhean' reads as 'bean' and the added 'h' sits at index 1. Trimming the
    shared prefix and suffix finds the added material in both cases.

    Returns ``(marker, base, concatenative)``. ``concatenative`` is True when
    marker + base == word, which is what the plain hybrid requires.
    """
    i = 0
    while i < len(word) and i < len(base) and word[i] == base[i]:
        i += 1
    j = 0
    while (j < len(word) - i and j < len(base) - i
           and word[len(word) - 1 - j] == base[len(base) - 1 - j]):
        j += 1
    marker = word[i:len(word) - j]
    return marker, base, marker + base == word


# Irish vowels, including the long forms. Used only by the tag-free inverse,
# to tell an h-prefix (before a vowel) from a lenition h (after a consonant).
VOWELS = set("aeiouáéíóúAEIOUÁÉÍÓÚ")


def rebuild(tag, marker, base):
    """Inverse of ``marker_and_base``: rebuild the surface from a split.

    Prepending mutations put the marker in front. Lenition inserts its h
    after the radical consonant, so the marker goes back at index 1.
    """
    if tag == "Len":
        return base[:1] + marker + base[1:]
    return marker + base


def rebuild_untagged(marker, base):
    """Rebuild without being told the tag, from the token stream alone.

    The only ambiguity is a marker of 'h': an h-prefix precedes a vowel
    ('hainm' is h + ainm) and a lenition h follows a consonant ('bhean' is
    bean with an h after the b). Reading the first letter of the stem
    separates them, which works on Irish but is a phonological rule rather
    than a property of the encoding. See docs/MOIRFEOLAS.md.
    """
    if marker == "h" and base and base[0] not in VOWELS:
        return base[:1] + marker + base[1:]
    return marker + base


def mutation_splits(words, demut, tags=SPLIT_TAGS, concatenative_only=True):
    """word -> (tag, marker, base) for every word layer 1 fires on.

    With ``concatenative_only`` the marker must be a literal prefix of the
    surface, so the token stream still concatenates back to the word. That
    is the plain hybrid, and it excludes lenition by construction. Dropping
    the constraint is the rearranging variant.
    """
    split = {}
    for w in words:
        chosen = pick(w, demut.get(w, []), tags, concatenative_only)
        if not chosen:
            continue
        tag, base = chosen
        marker, base, _ = marker_and_base(w, base)
        split[w] = (tag, marker, base)
    return split


