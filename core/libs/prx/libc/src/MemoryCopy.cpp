#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstddef>
#include <cstring>

extern "C" void APS5_VABI bcopy_nid_postfix(const void* source, void* destination, std::size_t length) {
    if (length != 0) std::memmove(destination, source, length);
}
