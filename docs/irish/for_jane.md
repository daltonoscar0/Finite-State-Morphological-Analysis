Hi Jane,

Four things since the call, and three questions at the end.

**Lenition depth: Scalpel only ever looks at the first consonant of a word.**
I bucketed every lenition site in MoirfEolas by how many morphemes sit in
front of it, using your prefix and urú columns to read the morpheme layout.
Word-initially it finds 6,801 of 6,802 sites, so 100.0%. One morpheme in
(seanbhean, an-mhaith, príomhchathair) it finds 0 of 2,636. Two or more in
(gcomhpháirtithe, hinfheistíocht) it finds 0 of 452. So 3,088 sites, just over
31% of all of them, are invisible to it. That is a design consequence rather
than a bug: the FST is a pure application layer and only inspects the first
consonant of whatever string it is handed.

I built a closed list of 56 leniting prefixes that locates the morpheme
boundary and then applies the existing lenition rule at that offset instead
of at position 0. It takes depth 1 from 0% to 75.2% and depth 2+ to 68.4%.
Precision is 99.83% measured against your morpheme layout, on the subset
where that layout is reliable: 2,286 firings, 2,282 landing on a site your
annotation marks, one genuine error. It is off by default and changes no
CríochScore number, so nothing you have seen moves.

**Tokenizer generality: the gain is not a Unigram effect, and Unigram 8k is
the weakest case.** I trained Unigram, BPE and WordPiece at 8k, 16k, 32k and
64k, with and without the pre-tokenizer. The overall CríochScore gain:

                 8k      16k     32k     64k
    Unigram     +2.01   +2.63   +3.27   +4.28
    BPE         +3.91   +4.36   +5.01   +5.98
    WordPiece   +5.40   +6.07   +6.43   +6.99

It helps every cell, it grows with vocabulary size, and it is smallest for
Unigram. Your own six shipped families at 8k say the same: BPE 35.85 to
40.98, Byte-BPE 38.19 to 44.00, SentencePieceBPE 38.05 to 43.86,
SentencePieceULM 42.84 to 47.58, UnigramLM 43.32 to 45.98, WordPiece 35.23 to
41.69. Your absorption script explains why: WordPiece buries 72.4% of all urú
material inside a token and Unigram only 27.7%, and the pre-tokenizer takes
all of them to about zero. Fertility cost is 0.01 to 0.03 tokens per word.

Two negative results worth having. Pre-tokenizing the corpus before training,
rather than only at inference, loses in 10 of 12 cells, so I am not adopting
it. And suffix_only drops slightly under the pre-tokenizer in 9 of 12 cells,
by 0.01 to 0.11 points. It is small but it is always in the same direction,
so it is real, not noise. It is the only category that ever gets worse.

**The d' forms are handled now.** 670 MoirfEolas rows have an apostrophe and
Scalpel was returning no analysis at all for any of them. It now splits the
clitic into its own segment and still identifies the lenition underneath,
which is the 172 d'fh- forms where the two stack. On Unigram 8k, your
protocol: uru_only 77.78 to 100.00, uru+prefix 66.67 to 73.81, uru+suffix
63.00 to 66.05, all_affixes 50.17 to 52.95, overall 45.46 to 45.98. Nothing
falls. Both your reference rows still reproduce exactly before the change.

**The MT pipeline is ready for your data.** One command tokenizes a file for
any of 84 configurations, with a detokenize path. Round trip is exact on all
12,000 lines of my test set for every tokenizer family, output is
byte-identical across runs, and it runs at about 10,000 lines a second, so a
million lines is under two minutes. Send the parallel data whenever it is
clean and I can turn it round the same day.

Three questions.

1. What is "Mingram"? It is in my notes from the call and I cannot place it.
   It does not appear anywhere in tknzn4Irish, not in the working tree, not
   in any file, and not in the git history, so I have left the slot empty
   rather than guessing at something.

2. The urú and prefix columns look like they were filled by a case-sensitive
   pass. They are populated for 0.4% of the 6,969 capitalised words, against
   18.7% and 34.7% of the 28,548 lowercase ones, while the suffix column is
   populated for 100% of the capitalised ones. The practical effect is that
   Príomhfhostaithe, Comhdhálacha and Ard-Chúirt have no prefix recorded, so
   roughly 310 real prefix-plus-lenition sites cannot be expressed, and any
   urú or prefix category score is effectively over lowercase words only. Is
   that known, and do you want me to send the list of affected forms?

3. The urú column records d' and D' but leaves m', b' and B' empty, 13 words.
   They look like the same phenomenon to me, so I suspect that is a gap
   rather than a decision. Worth a line in the paper either way.

On the lenition_candidates.csv I sent: the flags should line up with what you
are annotating, and the one thing I would check first is the stem_unattested
column, since 2,296 rows carry it and most of those are proper nouns taking a
real séimhiú rather than bad analyses.

Oscar
