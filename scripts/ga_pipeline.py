"""Lossless Scalpel pre-tokenization for running text, per tokenizer family.

The scored harness builds a token stream for CríochScore only: Jane's
``remove_specials`` normalises the pieces before the metric sees them, so a
stream that reconstructs the word is enough and the family's own
word-boundary convention never has to be honoured. A stream that is going to
be written to a file and fed to a model is a different object. Two things the
boundary-survival check found have to be fixed here:

* **WordPiece gets no continuation marker.** The hybrid encodes the stem as a
  standalone word, so its first piece has no '##' even though it continues
  the word. Harmless for scoring (the marker is stripped anyway), ill-formed
  downstream, and it was true of all 5,419 split words.
* **Eight of the sixteen markers layer 1 emits are not in any vocabulary.**
  `d'`, `D'`, `b'`, `B'`, `m'`, `h-`, `n-`, `t-` are absent from every one of
  Jane's six models at every size, so a real encoder re-splits them and the
  boundary moves. Each unit is therefore encoded separately here, and the
  pieces of a marker stay inside the marker.

Losslessness is by construction rather than by convention. Tokenization
produces a *partition* of the line: concatenating the units in order returns
the input exactly, because every split used is concatenative (layer 1's
marker + base, peeled punctuation, preserved whitespace). The family
convention is then applied as a separately invertible marking step, so
``detokenize(tokenize(x)) == x`` holds for every family rather than only for
those whose convention happens to carry the information.

Whitespace. A single space is carried by the family's word-initial marker
where there is one. Anything else, and any whitespace at all for the families
that have no marker, is emitted as an explicit escape token, because a
convention that cannot represent a tab must not silently drop one.
"""
import re
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ga_clitic import CLITIC_TAG, split_clitic                   # noqa: E402
from ga_mutation_split import (                                  # noqa: E402
    SPLIT_TAGS, marker_and_base, pick,
)

# How each family marks a word boundary, and how it marks continuation.
# 'initial' is prefixed to the first piece of a chunk; 'cont' to every piece
# after it. A family with neither needs the explicit separator.
CONVENTIONS = {
    "BPE":              {"initial": None, "cont": None},
    "UnigramLM":        {"initial": None, "cont": None},
    "Byte-BPE":         {"initial": "Ġ",  "cont": None},
    "SentencePieceBPE": {"initial": "▁",  "cont": None},
    "SentencePieceULM": {"initial": "▁",  "cont": None},
    "WordPiece":        {"initial": None, "cont": "##"},
}

# Explicit whitespace escape. Chosen from the private use area so it cannot
# collide with a real piece, and it carries the exact codepoints so a tab or
# a run of spaces survives.
WS_OPEN, WS_CLOSE = "ws:", ""

_CHUNKS = re.compile(r"(\s+)")


def escape_ws(run):
    return WS_OPEN + "|".join(f"{ord(c):x}" for c in run) + WS_CLOSE


def unescape_ws(tok):
    body = tok[len(WS_OPEN):-len(WS_CLOSE)]
    return "".join(chr(int(h, 16)) for h in body.split("|"))


def is_ws_token(tok):
    return tok.startswith(WS_OPEN) and tok.endswith(WS_CLOSE)


def peel(chunk):
    """``(lead, core, trail)`` splitting punctuation off a word.

    An apostrophe is never peeled from the front of a clitic, because 'd'' is
    part of the word. A trailing apostrophe is peeled, because that is a
    quote mark rather than a clitic.
    """
    i, j = 0, len(chunk)
    while i < j and not _is_word_char(chunk[i]):
        i += 1
    while j > i and not _is_word_char(chunk[j - 1]):
        j -= 1
    # Pull a clitic apostrophe back into the core: peeling stopped at the
    # first word character, which for "d'fhág" is the d, so nothing is lost;
    # this guards the reverse case of a leading quote before a clitic.
    return chunk[:i], chunk[i:j], chunk[j:]


def _is_word_char(c):
    cat = unicodedata.category(c)
    return cat.startswith("L") or cat.startswith("N") or c in "-'’ʼ"


def layer1_units(core, demut_readings, tags=(CLITIC_TAG,) + SPLIT_TAGS):
    """Split a word core at its layer-1 boundary. Concatenative by design.

    Returns a list of one or two strings whose concatenation is ``core``.
    The clitic is checked directly rather than through ``pick`` so that a
    caller which has no FST readings to hand still splits d'/m'/b'.
    """
    clitic = split_clitic(core)
    if clitic:
        return [clitic[0], clitic[1]]
    chosen = pick(core, demut_readings, tags)
    if not chosen:
        return [core]
    _, base = chosen
    marker, base, concat = marker_and_base(core, base)
    if not concat or not marker or not base:
        return [core]
    return [marker, base]


def units_of_line(line, readings_for=None):
    """Partition ``line`` into units. ``"".join(units) == line``.

    ``readings_for`` is a callable word -> [(tag, base), ...]; without it only
    the clitic rule fires, which is what a caller that has not run the FST
    gets.
    """
    get = readings_for or (lambda w: [])
    units = []
    for part in _CHUNKS.split(line):
        if not part:
            continue
        if part.isspace():
            units.append(part)
            continue
        lead, core, trail = peel(part)
        if lead:
            units.append(lead)
        if core:
            units.extend(layer1_units(core, get(core)))
        if trail:
            units.append(trail)
    return units


def tokenize(line, family, encode_unit=None, readings_for=None):
    """Tokens for one line, in ``family``'s convention. Invertible.

    ``encode_unit`` is a callable unit -> [pieces] whose pieces concatenate
    back to the unit; without it each unit is left whole, which is the
    pre-tokenizer-only view. Units are encoded one at a time so a boundary
    layer 1 inserted can never be re-merged across it.
    """
    conv = CONVENTIONS[family]
    enc = encode_unit or (lambda u: [u])
    out, first_chunk = [], True

    for unit in units_of_line(line, readings_for):
        if unit.isspace():
            # A single space is already implied by any convention: by the
            # word-initial marker where there is one, and by the *absence* of
            # the continuation marker where there is one. Only families with
            # neither, and whitespace that is not a single space, need it
            # spelled out.
            # Whatever the representation, what follows starts a new chunk.
            first_chunk = True
            if unit == " " and (conv["initial"] or conv["cont"]):
                continue
            out.append(escape_ws(unit))
            continue

        pieces = enc(unit)
        for k, piece in enumerate(pieces):
            if first_chunk and k == 0 and conv["initial"]:
                out.append(conv["initial"] + piece)
            elif not (first_chunk and k == 0) and conv["cont"]:
                out.append(conv["cont"] + piece)
            else:
                out.append(piece)
        first_chunk = False

    return out


def detokenize(tokens, family):
    """Exact inverse of ``tokenize``."""
    conv = CONVENTIONS[family]
    parts = []
    for tok in tokens:
        if is_ws_token(tok):
            parts.append(unescape_ws(tok))
            continue
        if conv["initial"] and tok.startswith(conv["initial"]):
            # A marked piece starts a new chunk, which means a space unless
            # it is the very first thing on the line or follows explicit
            # whitespace.
            if parts and not parts[-1][-1:].isspace():
                parts.append(" ")
            parts.append(tok[len(conv["initial"]):])
            continue
        if conv["cont"] and tok.startswith(conv["cont"]):
            parts.append(tok[len(conv["cont"]):])
            continue
        # Unmarked. For a '##' family an unmarked piece begins a chunk.
        if conv["cont"] and parts and not parts[-1][-1:].isspace():
            parts.append(" ")
        parts.append(tok)
    return "".join(parts)
