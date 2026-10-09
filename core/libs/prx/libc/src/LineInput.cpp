#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/FileStream.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cerrno>
#include <cstdint>
#include <limits>
#include <new>

extern "C" void APS5_VABI _Lockfilelock_nid_postfix(FileStream*);
extern "C" void APS5_VABI _Unlockfilelock_nid_postfix(FileStream*);
extern "C" int* APS5_VABI __error_nid_postfix();

namespace {
class StreamLock {
    FileStream* stream;
public:
    explicit StreamLock(FileStream* value) : stream(value) { _Lockfilelock_nid_postfix(stream); }
    ~StreamLock() { _Unlockfilelock_nid_postfix(stream); }
    StreamLock(const StreamLock&) = delete;
    StreamLock& operator=(const StreamLock&) = delete;
};

bool Expand(char** buffer, std::size_t* capacity, std::size_t required) {
    constexpr auto maximum = static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()) + 1;
    if (required > maximum) { *__error_nid_postfix() = 84; return false; }
    if (required <= *capacity) return true;
    std::size_t grown = 1;
    while (grown < required) grown *= 2;
    try {
        auto* replacement = static_cast<char*>(ApplicationHeapReallocate_nid_no_patch(*buffer, grown));
        *buffer = replacement;
        *capacity = grown;
        return true;
    } catch (const std::bad_alloc&) {
        *__error_nid_postfix() = 12;
        return false;
    }
}

std::int64_t Failure(FileStream* stream, int error) {
    stream->SetError();
    *__error_nid_postfix() = error;
    return -1;
}
}

extern "C" std::int64_t APS5_VABI getdelim_nid_postfix(char** buffer, std::size_t* capacity,
    int delimiter, FileStream* stream) {
    if (!stream) { *__error_nid_postfix() = 22; return -1; }
    StreamLock lock(stream);
    if (!buffer || !capacity) return Failure(stream, 22);
    if (!*buffer) *capacity = 0;
    if (!stream->CanRead()) return Failure(stream, 9);
    auto* native = GetNativeStream(stream);
    const int saved = *__error_nid_postfix();
    std::size_t length = 0;
    for (;;) {
        errno = 0;
        const int value = std::fgetc(native);
        if (value == EOF) {
            const int error = errno;
            if (std::ferror(native)) {
                if (error == EAGAIN) {
                    while (length != 0) {
                        if (std::ungetc(static_cast<unsigned char>((*buffer)[--length]), native) == EOF) break;
                    }
                    if (length == 0 && *buffer) (*buffer)[0] = '\0';
                }
                return Failure(stream, error == EAGAIN ? 35 : error == 0 ? 5 : error);
            }
            if (!Expand(buffer, capacity, length + 1)) return Failure(stream, *__error_nid_postfix());
            (*buffer)[length] = '\0';
            stream->SyncStatus();
            *__error_nid_postfix() = saved;
            return length == 0 ? -1 : static_cast<std::int64_t>(length);
        }
        if (length == static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
            std::ungetc(value, native);
            return Failure(stream, 84);
        }
        if (!Expand(buffer, capacity, length + 2)) {
            const int error = *__error_nid_postfix();
            std::ungetc(value, native);
            return Failure(stream, error);
        }
        (*buffer)[length++] = static_cast<char>(value);
        if (value == static_cast<unsigned char>(delimiter)) {
            (*buffer)[length] = '\0';
            stream->SyncStatus();
            *__error_nid_postfix() = saved;
            return static_cast<std::int64_t>(length);
        }
    }
}

extern "C" std::int64_t APS5_VABI getline_nid_postfix(char** buffer, std::size_t* capacity, FileStream* stream) {
    return getdelim_nid_postfix(buffer, capacity, '\n', stream);
}
