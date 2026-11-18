"""Lenition under a closed list of leniting prefixes. Off by default.

Scalpel's mutation FST only ever looks at the first consonant of the string
it is handed, which is correct for what it is: ``mutation_fst.h`` commits it
to being a pure application layer with "no triggers, no lexicon, no
heuristics", and its semantics are pinned to a Python reference and a shared
attested-form test file. A list of prefixes that trigger lenition is exactly
a trigger inventory, so it does not belong inside that module.

This is the smallest extension that reaches non-initial lenition without
touching the contract: a closed list locates one morpheme boundary, and the
*existing* lenition rule is then applied at that offset instead of at index
0. The mutation logic is unchanged; only where it is applied moves. Nothing
here is enabled unless a caller asks for it.

Scope. The list is prefixes, not compounding in general. Irish forms
noun-noun compounds freely and the first element lenites the second
('ceannlitir'), which is open-class and cannot be a closed list. Those sites
stay out of reach and are counted as such in the write-up.

Precision caveat. MoirfEolas does not annotate lenition, so there is no gold
to score these firings against. Short prefixes are the risk: 'do', 'fo',
'in', 'so' are real prefixes that also spuriously match the opening letters
of monomorphemic words whose stem happens to contain a C+h digraph
('dochar', 'rotha'). Two guards below reduce that, and what remains is
measured against a hand-checked sample rather than claimed.

One ambiguity the guards do not resolve: 'so-' is both the Irish prefix
meaning easily-done ('so-chreidte') and the opening of borrowed 'soch-'
from Greek socio- ('socheolaíocht'), where the gold annotates the prefix as
'soch' and the ch belongs to the stem. Both of the firings that disagree
with the gold layout at the default setting are this case.
"""

# Consonants the FST's is_lenitable() accepts, and the s-condition from
# is_s_follower(): s lenites only before a vowel, l, n or r. Kept identical
# to mutation_fst.cpp so a site this module calls valid is a site the FST
# would also accept if it were handed the stem directly.
LENITABLE = set("bcdfgmpt")
S_FOLLOWERS = set("aeiouáéíóúlnr")
VOWELS = set("aeiouáéíóú")

# Leniting prefixes attested in Jane's prefix inventory
# (data/MoirfEolas/Affixes/final_prefixes.txt). Grammar decides membership,
# not corpus frequency: a prefix that usually appears before a vowel still
# lenites when it meets a consonant, and the rate at which the gold string
# happens to be followed by C+h is a property of the word list, not of the
# prefix. Sorted longest-first at use so 'príomh' wins over 'prí'.
LENITING_PREFIXES = frozenset({
    # intensifiers, quality and negation
    "an", "ath", "dea", "droch", "fíor", "mí", "neamh", "sár", "ró", "ro",
    # size, degree, position
    "ard", "bun", "cúl", "gearr", "iar", "idir", "lár", "leath", "meán",
    "mion", "mór", "oll", "réamh", "uas", "barr",
    # relational and derivational
    "comh", "dí", "do", "fo", "for", "frith", "il", "im", "in", "leas",
    # 'ain' is deliberately absent: as a prefix string it splits the common
    # 'ainmhí' family as ain + mhí, and the attestation guard cannot catch it
    # because 'mí' and 'mithe' are both real words. It cost six false
    # positives and bought almost no recall.
    "so", "tras", "tré", "ion", "as", "ais", "ur",
    # lexical first elements that behave as prefixes in the inventory
    "aer", "bith", "dé", "féin", "gar", "gnáth", "grian", "nua", "príomh",
    "ríomh", "sain", "sean", "síor", "sprioc", "uile", "íos", "buan",
    "céad", "aon", "fíor",
})

# A stem this short after the prefix is almost always a bad cut: 'rotha'
# would give prefix 'ro' + stem 'ta'. Real compounds have a real second
# element.
MIN_STEM = 3

# Prefixes at or below this length only fire when the delenited stem is
# attested elsewhere, because they are the ones that spuriously match the
# opening of a monomorphemic word. Set from the sweep in
# docs/irish/lenition_depth.md: 2 and 3 both hold precision at 99.9% against
# the reliably annotated subset, and 2 recovers four more points of recall,
# so there is no reason to pay for 3.
SHORT_PREFIX = 2


def lenition_site(rest):
    """Is ``rest`` a lenited consonant plus its stem? Mirrors lenite_cp."""
    if len(rest) < 2 or rest[1] not in "hH":
        return False
    c = rest[0].lower()
    if c == "s":
        return len(rest) > 2 and rest[2].lower() in S_FOLLOWERS
    return c in LENITABLE


def delenite(rest):
    """Take the séimhiú back out: 'bhean' -> 'bean'."""
    return rest[:1] + rest[2:]


def _boundary(word, plen):
    """Where the stem starts after a prefix of ``plen`` characters.

    'an-mhaith' hyphenates straight after the prefix. 'seana-mháthair'
    inserts a linking vowel first. Both are one boundary.
    """
    if word[plen:plen + 1] == "-":
        return plen + 1
    probe = plen
    while probe < len(word) and probe - plen < 2 and word[probe].lower() in VOWELS:
        probe += 1
    if word[probe:probe + 1] == "-":
        return probe + 1
    return plen


def prefix_lenition(word, attested=None, prefixes=LENITING_PREFIXES,
                    short_prefix=SHORT_PREFIX):
    """``(prefix, stem)`` for a lenition site after a leniting prefix.

    ``prefix`` is the surface material up to and including any hyphen, so
    ``prefix + lenited_stem`` rebuilds the word. ``stem`` is delenited.
    Returns None when nothing fires.

    ``attested`` is a set of word forms used only to vet short prefixes; it
    is required for them and ignored for long ones. Longest prefix first, so
    'príomh' is preferred over any shorter string that also matches.
    """
    low = word.lower()
    for p in sorted(prefixes, key=len, reverse=True):
        if not low.startswith(p):
            continue
        # Irish writes an initial mutation in front of a retained capital:
        # 'nUachtarán', 'tSeáin', 'hÉireann'. So an interior capital inside
        # the matched region means the match has run across a mutation
        # boundary, not over a prefix: 'nua' must not match 'nUachtarán'
        # and hand back 'nUa' + 'ctarán'. An all-caps word is a rendering
        # choice rather than a boundary, and the FST accepts those
        # ('BHFUIL'), so it is exempt. MoirfEolas has none, but running text
        # does.
        if not word.isupper() and any(c.isupper() for c in word[1:len(p)]):
            continue
        start = _boundary(word, len(p))
        if start >= len(word):
            continue
        rest = word[start:]
        if len(rest) < MIN_STEM or not lenition_site(rest):
            continue
        stem = delenite(rest)
        # A hyphen is an explicit boundary the writer put there, so it
        # vouches for the cut on its own and the attestation test is
        # skipped. Without one, a short prefix has to earn it.
        hyphenated = word[start - 1:start] == "-"
        if len(p) <= short_prefix and not hyphenated:
            if attested is None or stem.lower() not in attested:
                continue
        return word[:start], stem
    return None


# Tags whose material simply prepends, so stripping one leaves a string the
# prefix pass can read. Lenition is excluded: it is not a prefix.
FRONTING_TAGS = ("Ecl", "HPref", "TPref")


def prefix_lenition_composed(word, readings, attested=None, **kw):
    """Reach lenition that sits behind an urú or a clitic as well as a prefix.

    'gcomhpháirtithe' is g + comh + pháirtithe and 'hinfheistíocht' is
    h + in + fheistíocht. The FST already identifies the fronting mutation,
    so the prefix pass only has to run on the base it returns rather than on
    the surface. Nothing new is analysed; two existing passes compose.

    ``readings`` is the FST's own ``[(tag, base), ...]`` for the word.
    Returns ``(fronting_material, prefix, stem)`` or None. Concatenating the
    three with the séimhiú put back rebuilds the surface.
    """
    for tag, base in readings:
        if tag not in FRONTING_TAGS or not base or base == word:
            continue
        if not word.endswith(base):
            # Only a genuine prepending reading keeps the base as a suffix
            # of the surface; anything else is not a boundary we can trust.
            continue
        front = word[:len(word) - len(base)]
        hit = prefix_lenition(base, attested, **kw)
        if hit:
            return front, hit[0], hit[1]
    return None
