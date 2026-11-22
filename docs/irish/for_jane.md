Hi Jane,

Quick update since the call, with one correction and four questions.

Correction: the lenition_candidates.csv I sent Wednesday only covers
word-initial lenition. Scalpel only reads the first consonant of a word:

  word-initial        6,802 sites   6,801 found
  one morpheme in     2,636 sites       0 found
  two or more in        452 sites       0 found

So it missed about 31% of sites (an-mhaith, seanbhean, gcomhpháirtithe).
The attached lenition_sites_v2.csv has all 9,890. Please crosscheck against
this one. These sites come from my script reading your prefix and urú
columns, not from gold, so treat the counts as provisional until your
lenition annotation is in.

d' forms: Scalpel now splits d', m' and b' (659 forms). With Unigram 8k,
overall goes from 45.46 to 45.98 and no category drops. uru_only goes from
77.78 to 100.00, but that is only 27 words.

Other tokenizers: the gain is not a Unigram effect. Change in overall
CríochScore from adding Scalpel:

               8k      16k     32k     64k
  Unigram    +2.01   +2.63   +3.27   +4.28
  BPE        +3.91   +4.36   +5.01   +5.98
  WordPiece  +5.40   +6.07   +6.43   +6.99

suffix_only dips slightly (at most 0.11) in nine of the twelve.

MT: I have a one-command, lossless tokenizer ready for your data.

Questions:
1. My call notes say "Mingram" next to BPE as a tokenizer to test. Do you
   remember what that was?
2. Prefix and urú are almost empty for capitalised words (about 0.4% filled),
   while suffix is filled for all of them. Is that intended?
3. d' is in the urú column but m' and b' are not. Also intended?
4. For MT, should Scalpel run only at inference, or also when training the
   tokenizer? Inference-only scores better, but about half the markers (d',
   h-, n-, t-) are missing from those vocabularies. I lean towards training
   on split text.

Best,
Oscar
