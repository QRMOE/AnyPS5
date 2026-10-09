#include "prx/libc/include/general/VabiMacros.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>

extern "C" void APS5_VABI bcopy_nid_postfix(const void*, void*, std::size_t);
extern "C" int* APS5_VABI __error_nid_postfix();

static void Require(bool condition, int line) {
    if (!condition) {
        std::fprintf(stderr, "Bcopy check failed at line %d\n", line);
        std::abort();
    }
}
#define Check(value) Require((value), __LINE__)

int main() {
    *__error_nid_postfix() = 71;
    bcopy_nid_postfix(nullptr, nullptr, 0);
    Check(*__error_nid_postfix() == 71);
    std::array<unsigned char, 66> original{};
    for (std::size_t index = 0; index < original.size(); ++index) original[index] = static_cast<unsigned char>(index * 37);
    for (std::size_t source = 1; source <= 64; ++source) {
        for (std::size_t destination = 1; destination <= 64; ++destination) {
            for (std::size_t length = 0; source + length <= 65 && destination + length <= 65; ++length) {
                auto actual = original;
                auto expected = original;
                for (std::size_t index = 0; index < length; ++index) expected[destination + index] = original[source + index];
                *__error_nid_postfix() = 71;
                bcopy_nid_postfix(actual.data() + source, actual.data() + destination, length);
                Check(*__error_nid_postfix() == 71);
                Check(actual == expected);
            }
        }
    }
    std::array<unsigned char, 6> separate{};
    const unsigned char input[]{0xff, 0, 0x80, 0x55};
    bcopy_nid_postfix(input, separate.data() + 1, sizeof(input));
    Check(separate[0] == 0 && separate[5] == 0);
    for (std::size_t index = 0; index < sizeof(input); ++index) Check(separate[index + 1] == input[index]);
    bcopy_nid_postfix(nullptr, separate.data(), 0);
    bcopy_nid_postfix(separate.data(), nullptr, 0);
}
