# Matched-vocabulary comparison

Twelve tokenizers: {none, analyzer} pre-tokenization, {Unigram, BPE} subword model, {8k, 16k, 32k} vocabulary. Vocabulary size is matched by construction, so the only variable inside a (model, size) pair is whether the analyzer ran in front.

Fertility is pieces per alphabetic word token and bytes/piece is over the whole unit stream, both on the BabyLM dev split. BPC is bits per character of the pretokenized dev text, the same denominator for every row.

| condition | model | nominal | actual | fertility | bytes/piece | MorphoLex F1 | infl F1 | deriv F1 | compound R | BPC |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| none | unigram | 8000 | 8000 | 1.2029 | 2.784 | 0.5937 | 0.2817 | 0.3564 | 0.8172 | n/a |
| analyzer | unigram | 8000 | 8000 | 1.2579 | 2.690 | 0.6375 | 0.3866 | 0.3702 | 0.8289 | n/a |
| none | unigram | 16000 | 16000 | 1.1386 | 2.907 | 0.6281 | 0.3031 | 0.3934 | 0.7507 | n/a |
| analyzer | unigram | 16000 | 16000 | 1.2089 | 2.777 | 0.6920 | 0.4274 | 0.4181 | 0.7798 | n/a |
| none | unigram | 32000 | 32000 | 1.1061 | 2.973 | 0.6308 | 0.3347 | 0.4327 | 0.6366 | n/a |
| analyzer | unigram | 32000 | 32000 | 1.1867 | 2.818 | 0.7302 | 0.4666 | 0.4726 | 0.7109 | n/a |
| none | bpe | 8000 | 8000 | 1.1896 | 2.811 | 0.4955 | 0.0986 | 0.2648 | 0.7136 | n/a |
| analyzer | bpe | 8000 | 8000 | 1.2369 | 2.728 | 0.5953 | 0.2870 | 0.3007 | 0.7235 | n/a |
| none | bpe | 16000 | 16000 | 1.1083 | 2.972 | 0.5040 | 0.0921 | 0.2721 | 0.6761 | n/a |
| analyzer | bpe | 16000 | 16000 | 1.1754 | 2.842 | 0.6328 | 0.2806 | 0.3262 | 0.6752 | n/a |
| none | bpe | 32000 | 32000 | 1.0587 | 3.080 | 0.4801 | 0.0791 | 0.2771 | 0.5895 | n/a |
| analyzer | bpe | 32000 | 32000 | 1.1422 | 2.908 | 0.6464 | 0.2737 | 0.3415 | 0.5686 | n/a |

## Analyzer minus none

A positive difference is the analyzer winning on the four gold columns. On fertility and on BPC it is the other way round: fewer pieces per word is better, and fewer bits per character is better.

| model | size | MorphoLex F1 | infl F1 | deriv F1 | compound R | fertility | BPC |
|---|---:|---:|---:|---:|---:|---:|---:|
| unigram | 8k | +0.0438 | +0.1049 | +0.0137 | +0.0117 | +0.0550 | n/a |
| unigram | 16k | +0.0639 | +0.1243 | +0.0247 | +0.0291 | +0.0704 | n/a |
| unigram | 32k | +0.0994 | +0.1319 | +0.0399 | +0.0744 | +0.0806 | n/a |
| bpe | 8k | +0.0998 | +0.1884 | +0.0359 | +0.0099 | +0.0473 | n/a |
| bpe | 16k | +0.1289 | +0.1885 | +0.0542 | -0.0009 | +0.0671 | n/a |
| bpe | 32k | +0.1663 | +0.1946 | +0.0645 | -0.0209 | +0.0835 | n/a |

## Does it help?

At 8k, unigram: MorphoLex F1 0.5937 to 0.6375 (+0.0438); bpe: MorphoLex F1 0.4955 to 0.5953 (+0.0998).

At 16k, unigram: MorphoLex F1 0.6281 to 0.6920 (+0.0639); bpe: MorphoLex F1 0.5040 to 0.6328 (+0.1289).

At 32k, unigram: MorphoLex F1 0.6308 to 0.7302 (+0.0994); bpe: MorphoLex F1 0.4801 to 0.6464 (+0.1663).

## Morphological alignment in full

**MorphoLex, independent, 12,828 forms.**

| system | P | R | F1 | exact |
|---|--:|--:|---:|------:|
| none-unigram-8k | 0.479 | 0.780 | 0.594 | 0.318 |
| analyzer-unigram-8k | 0.512 | 0.844 | 0.638 | 0.380 |
| none-unigram-16k | 0.561 | 0.714 | 0.628 | 0.402 |
| analyzer-unigram-16k | 0.607 | 0.805 | 0.692 | 0.496 |
| none-unigram-32k | 0.649 | 0.614 | 0.631 | 0.426 |
| analyzer-unigram-32k | 0.700 | 0.763 | 0.730 | 0.563 |
| none-bpe-8k | 0.404 | 0.641 | 0.496 | 0.228 |
| analyzer-bpe-8k | 0.487 | 0.765 | 0.595 | 0.322 |
| none-bpe-16k | 0.446 | 0.579 | 0.504 | 0.296 |
| analyzer-bpe-16k | 0.564 | 0.721 | 0.633 | 0.435 |
| none-bpe-32k | 0.479 | 0.481 | 0.480 | 0.308 |
| analyzer-bpe-32k | 0.642 | 0.651 | 0.646 | 0.483 |

**MorphyNet inflection, in distribution, 186,947 forms.**

| system | P | R | F1 | exact |
|---|--:|--:|---:|------:|
| none-unigram-8k | 0.182 | 0.629 | 0.282 | 0.033 |
| analyzer-unigram-8k | 0.245 | 0.923 | 0.387 | 0.037 |
| none-unigram-16k | 0.201 | 0.612 | 0.303 | 0.055 |
| analyzer-unigram-16k | 0.277 | 0.932 | 0.427 | 0.057 |
| none-unigram-32k | 0.229 | 0.622 | 0.335 | 0.083 |
| analyzer-unigram-32k | 0.312 | 0.929 | 0.467 | 0.077 |
| none-bpe-8k | 0.066 | 0.194 | 0.099 | 0.015 |
| analyzer-bpe-8k | 0.186 | 0.625 | 0.287 | 0.032 |
| none-bpe-16k | 0.064 | 0.165 | 0.092 | 0.021 |
| analyzer-bpe-16k | 0.188 | 0.551 | 0.281 | 0.055 |
| none-bpe-32k | 0.057 | 0.129 | 0.079 | 0.026 |
| analyzer-bpe-32k | 0.191 | 0.484 | 0.274 | 0.083 |

**MorphyNet derivation, in distribution, 194,771 forms.**

| system | P | R | F1 | exact |
|---|--:|--:|---:|------:|
| none-unigram-8k | 0.231 | 0.782 | 0.356 | 0.108 |
| analyzer-unigram-8k | 0.239 | 0.823 | 0.370 | 0.114 |
| none-unigram-16k | 0.264 | 0.770 | 0.393 | 0.169 |
| analyzer-unigram-16k | 0.280 | 0.824 | 0.418 | 0.173 |
| none-unigram-32k | 0.302 | 0.762 | 0.433 | 0.234 |
| analyzer-unigram-32k | 0.330 | 0.830 | 0.473 | 0.231 |
| none-bpe-8k | 0.174 | 0.549 | 0.265 | 0.067 |
| analyzer-bpe-8k | 0.198 | 0.627 | 0.301 | 0.089 |
| none-bpe-16k | 0.185 | 0.510 | 0.272 | 0.105 |
| analyzer-bpe-16k | 0.223 | 0.609 | 0.326 | 0.143 |
| none-bpe-32k | 0.197 | 0.467 | 0.277 | 0.144 |
| analyzer-bpe-32k | 0.243 | 0.574 | 0.342 | 0.198 |

