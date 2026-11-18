"""Elided proclitics written with an apostrophe: d', m', b'.

What the call note "D' stuff" meant. The data settles it: 670 MoirfEolas rows
contain an apostrophe, and the clitic inventory is d'/D' (646), mb' (9),
m' (5), b' (5), B' (3), plus two junk rows ('you'', 'pe''). These are the
past-tense and possessive d' ('d'ól', 'd'athair'), possessive m' ('m'intinn')
and the copula b' ('b'fhéidir').

This is not an initial mutation, so it does not belong in
``mutation_fst.cpp``: that header enumerates exactly four mutations and
commits the module to being a pure application layer. A clitic is a separate
morpheme that happens to be written joined, so it is split here, in the same
layer-1 wrapper that owns the prefix list.

Two things stack on 'd'fhág'. The clitic is 'd'' and what remains is 'fhág',
which is 'fág' lenited, because the particle lenites the verb after it. The
gold records the clitic in the urú column and does not record the lenition,
so ``clitic_split`` returns the part CríochScore can credit and
``clitic_analysis`` returns both for the uses that want the whole
decomposition (the MT pipeline, and seeding Jane's lenition column).

Apostrophes. MoirfEolas uses the straight ASCII apostrophe throughout, zero
curly ones, but real text does not, so both are accepted on input and the
surface form is preserved exactly on output.
"""
from ga_lenition_prefix import delenite, lenition_site

# The tag the harness ranks this under. Named for the particle rather than
# for any mutation, because it is not one.
CLITIC_TAG = "Dpart"

# Straight, right single quotation mark, and the modifier letter apostrophe.
# A left quote is not an apostrophe and is deliberately absent.
APOSTROPHES = frozenset("'’ʼ")

# Letters that appear as an elided proclitic before an apostrophe.
CLITIC_LETTERS = frozenset("dmbDMB")

# Below this, what follows the clitic is not a word: 'b'é' is real but gives
# a one-character stem, and splitting it buys nothing.
MIN_REST = 2


def split_clitic(word):
    """``(clitic, rest)`` where ``clitic + rest == word``, or None.

    The clitic keeps its apostrophe and its original case, so the two parts
    concatenate back to the surface exactly.
    """
    if len(word) < 2 + MIN_REST:
        return None
    if word[0] not in CLITIC_LETTERS or word[1] not in APOSTROPHES:
        return None
    rest = word[2:]
    if len(rest) < MIN_REST or not rest[0].isalpha():
        return None
    return word[:2], rest


def clitic_analysis(word):
    """Full decomposition: ``(clitic, stem, lenited)``, or None.

    ``lenited`` says whether the clitic lenited what follows it. When it did,
    ``stem`` is delenited, so 'd'fhág' gives ``("d'", "fág", True)`` and
    'd'ól' gives ``("d'", "ól", False)``. Rebuild with ``rebuild_clitic``.
    """
    split = split_clitic(word)
    if not split:
        return None
    clitic, rest = split
    if lenition_site(rest):
        return clitic, delenite(rest), True
    return clitic, rest, False


def rebuild_clitic(clitic, stem, lenited):
    """Inverse of ``clitic_analysis``.

    The inserted h takes the case of the stem, the same rule ``mutate_cp``
    applies in the FST: an all-caps word gets an all-caps séimhiú, so
    "D'FHÁG" rebuilds as itself rather than as "D'FhÁG".
    """
    if lenited:
        h = "H" if len(stem) > 1 and stem.isupper() else "h"
        stem = stem[:1] + h + stem[1:]
    return clitic + stem


def add_clitic_readings(words, demut, tag=CLITIC_TAG):
    """Put a clitic reading in front of whatever the FST already said.

    Replaces the narrower ``add_dprime``, which only matched a straight
    apostrophe after d or D. The reading is ``(tag, rest)``, so the harness's
    ``marker_and_base`` recovers the clitic as the marker and the split stays
    concatenative. It goes first because a word that reads as a clitic is a
    clitic: 'd'fhág' has no competing analysis worth preferring.
    """
    out = dict(demut)
    n = 0
    for w in words:
        split = split_clitic(w)
        if not split:
            continue
        out[w] = [(tag, split[1])] + [
            r for r in out.get(w, []) if r != (tag, split[1])
        ]
        n += 1
    return out, n
