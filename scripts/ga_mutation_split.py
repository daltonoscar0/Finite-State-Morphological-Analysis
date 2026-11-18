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

def _run_bridge(words):
    proc = subprocess.run(
        [str(BRIDGE)], input="\n".join(words),
        capture_output=True, text=True, check=True,
    )
    out = {}
    for line in proc.stdout.splitlines():
        parts = line.split("\t")
        out[parts[0]] = [tuple(c.split(":", 1)) for c in parts[1:]]
    return out


def demutate_all(words, skipped=None, chunk=20000):
    """word -> [(tag, base), ...], in the analyser's own candidate order.

    The bridge aborts rather than returning on some inputs: a hyphenated
    mutation marker in front of an all-caps stem trips the self-check
    ``assert(inverse_ok_cp(...))`` at mutation_fst.cpp:201, because analysis
    accepts 'n-APP' but generation uppercases the inserted marker and loses
    the hyphen, so the round-trip test the analyser applies to its own output
    fails. Four forms in data/GA do it: n-APP, n-EOS, n-IOTA, t-AMHÁIN.

    One aborting word must not take a 272,000-word run with it, so the input
    is processed in chunks and a failing chunk is bisected until the
    individual offenders are isolated and set aside. Offenders are appended
    to ``skipped`` if given, so a caller can report them rather than silently
    scoring fewer words than it was handed. Fixing the analyser itself needs
    the Python reference and the shared attested-form file named in
    mutation_fst.h, neither of which is in this checkout.
    """
    out, bad = {}, [] if skipped is None else skipped

    def process(batch):
        if not batch:
            return
        try:
            out.update(_run_bridge(batch))
        except subprocess.CalledProcessError:
            if len(batch) == 1:
                bad.append(batch[0])
                return
            mid = len(batch) // 2
            process(batch[:mid])
            process(batch[mid:])

    words = list(words)
    for i in range(0, len(words), chunk):
        process(words[i:i + chunk])
    return out


# --------------------------------------------------------------------------
# Layer 1 providers. A provider is a callable taking the word list and
# returning {word: [(tag, base), ...]} in candidate order. Anything that can
# produce that shape can be scored, which is how a second analyser gets
# compared without its toolchain being installed here.
# --------------------------------------------------------------------------

KNOWN_TAGS = frozenset(SPLIT_TAGS_WITH_LENITION) | {DPRIME_TAG}


def read_tsv_readings(path, words=None, known_tags=KNOWN_TAGS):
    """Read ``word <tab> tag <tab> base`` rows from an external analyser.

    One row per reading, so a word with several candidates has several rows
    and their file order is the candidate order. Blank lines and lines
    starting with '#' are skipped, as is a header row whose first field is
    literally 'word'.

    Tags outside ``known_tags`` are dropped rather than guessed at, because
    the ranking in ``pick`` is by tag and an unrecognised tag has no defined
    position in it. The count of dropped rows is returned so the caller can
    report it instead of silently scoring less than it was given.
    """
    readings, dropped, seen_tags = {}, {}, set()
    wanted = set(words) if words is not None else None

    with open(path, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.rstrip("\n").rstrip("\r")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                raise ValueError(
                    f"{path}:{lineno}: expected 3 tab-separated fields "
                    f"(word, tag, base), got {len(parts)}")
            word, tag, base = parts[0], parts[1], parts[2]
            if lineno == 1 and word == "word" and tag == "tag":
                continue
            seen_tags.add(tag)
            if tag not in known_tags:
                dropped[tag] = dropped.get(tag, 0) + 1
                continue
            if wanted is not None and word not in wanted:
                continue
            readings.setdefault(word, []).append((tag, base))

    return readings, {"dropped_by_tag": dropped, "tags_seen": sorted(seen_tags)}


def provider_scalpel(words, **_):
    """Scalpel's mutation FST, through the ga_demutate bridge binary."""
    return demutate_all(words), {}


def provider_tsv(words, tsv_path=None, **_):
    """Any external analyser's batch output, as a three-column TSV."""
    if not tsv_path:
        raise SystemExit("--layer1 tsv needs --layer1-tsv PATH")
    return read_tsv_readings(tsv_path, words)


PROVIDERS = {
    "scalpel": provider_scalpel,
    "tsv": provider_tsv,
}


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


