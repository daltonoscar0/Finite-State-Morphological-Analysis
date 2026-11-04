// Bridge: one word per line on stdin, one TSV line per word on stdout:
//   word \t tag:base \t tag:base ...
// Candidates come from ga::demutate (identity reading excluded).
#include <iostream>
#include <string>

#include "mutation_fst.h"

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        std::cout << line;
        for (const auto& [tag, base] : ga::demutate(line))
            std::cout << '\t' << tag << ':' << base;
        std::cout << '\n';
    }
    return 0;
}
