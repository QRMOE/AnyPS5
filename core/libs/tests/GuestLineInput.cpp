#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/FileStream.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>

extern "C" {
std::int64_t APS5_VABI getdelim_nid_postfix(char**, std::size_t*, int, FileStream*);
std::int64_t APS5_VABI getline_nid_postfix(char**, std::size_t*, FileStream*);
int APS5_VABI feof_nid_postfix(FileStream*);
int APS5_VABI ferror_nid_postfix(FileStream*);
int* APS5_VABI __error_nid_postfix();
FileStream* APS5_VABI fopen_nid_postfix(const char*, const char*);
int APS5_VABI fclose_nid_postfix(FileStream*);
}

static void Require(bool condition, int line) {
    if (!condition) {
        std::fprintf(stderr, "Line input check failed at line %d\n", line);
        std::abort();
    }
}
#define Check(value) Require((value), __LINE__)

namespace {
bool allocationFails;
unsigned reallocations;
void* APS5_VABI Allocate(std::size_t size) { return std::malloc(size); }
void APS5_VABI Free(void* pointer) { std::free(pointer); }
void* APS5_VABI Calloc(std::size_t count, std::size_t size) { return std::calloc(count, size); }
void* APS5_VABI Reallocate(void* pointer, std::size_t size) {
    ++reallocations;
    return allocationFails ? nullptr : std::realloc(pointer, size);
}
void* APS5_VABI Align(std::size_t, std::size_t) { return nullptr; }
void* APS5_VABI Realign(void*, std::size_t, std::size_t) { return nullptr; }
int APS5_VABI PosixAlign(void**, std::size_t, std::size_t) { return 12; }

class Input {
public:
    FileStream stream;
    explicit Input(const std::string& data) : stream(std::tmpfile()) {
        Check(std::fwrite(data.data(), 1, data.size(), stream.GetHandle()) == data.size());
        std::rewind(stream.GetHandle());
    }
    ~Input() { stream.Close(); }
};
}

static void AllocationFailure() {
    std::array<void*, 10> api{reinterpret_cast<void*>(&Allocate), reinterpret_cast<void*>(&Free),
        reinterpret_cast<void*>(&Calloc), reinterpret_cast<void*>(&Reallocate), reinterpret_cast<void*>(&Align),
        reinterpret_cast<void*>(&Realign), reinterpret_cast<void*>(&PosixAlign)};
    ApplicationHeapRegister_nid_no_patch(api.data());
    Input input("abc\n");
    char* line = nullptr;
    std::size_t capacity = 100;
    allocationFails = true;
    Check(getline_nid_postfix(&line, &capacity, &input.stream) == -1);
    Check(*__error_nid_postfix() == 12 && line == nullptr && capacity == 0);
    Check(ferror_nid_postfix(&input.stream) != 0);
    input.stream.ClearError();
    line = static_cast<char*>(std::malloc(1));
    Check(line != nullptr);
    line[0] = '\0';
    capacity = 1;
    auto* original = line;
    Check(getline_nid_postfix(&line, &capacity, &input.stream) == -1);
    Check(*__error_nid_postfix() == 12 && line == original && capacity == 1);
    input.stream.ClearError();
    allocationFails = false;
    Check(getline_nid_postfix(&line, &capacity, &input.stream) == 4);
    Check(std::strcmp(line, "abc\n") == 0 && reallocations >= 3);
    ApplicationHeapFree_nid_no_patch(line);
}

int main(int argc, char**) {
    if (argc > 1) { AllocationFailure(); return 0; }
    std::array<void*, 10> api{};
    ApplicationHeapRegister_nid_no_patch(api.data());
    char* line = nullptr;
    std::size_t capacity = 777;
    {
        Input input(std::string("\nA\0B\nlast", 9));
        *__error_nid_postfix() = 71;
        Check(getline_nid_postfix(&line, &capacity, &input.stream) == 1);
        Check(line[0] == '\n' && line[1] == '\0' && capacity >= 2 && *__error_nid_postfix() == 71);
        Check(getline_nid_postfix(&line, &capacity, &input.stream) == 4);
        Check(std::memcmp(line, "A\0B\n", 5) == 0 && capacity >= 5);
        auto* previous = line;
        Check(getline_nid_postfix(&line, &capacity, &input.stream) == 4);
        Check(std::strcmp(line, "last") == 0 && line == previous);
        Check(feof_nid_postfix(&input.stream) != 0 && ferror_nid_postfix(&input.stream) == 0);
        Check(getline_nid_postfix(&line, &capacity, &input.stream) == -1 && line[0] == '\0');
    }
    {
        Input input(std::string("a\0b\0", 4));
        Check(getdelim_nid_postfix(&line, &capacity, 0, &input.stream) == 2);
        Check(line[0] == 'a' && line[1] == 0 && line[2] == 0);
        Check(getdelim_nid_postfix(&line, &capacity, 256, &input.stream) == 2);
        Check(line[0] == 'b' && line[1] == 0 && line[2] == 0);
        Check(getline_nid_postfix(nullptr, &capacity, &input.stream) == -1 && *__error_nid_postfix() == 22);
        Check(ferror_nid_postfix(&input.stream) != 0);
        input.stream.ClearError();
        Check(ferror_nid_postfix(&input.stream) == 0);
        Check(getline_nid_postfix(&line, nullptr, &input.stream) == -1 && *__error_nid_postfix() == 22);
    }
    Check(getline_nid_postfix(&line, &capacity, nullptr) == -1 && *__error_nid_postfix() == 22);
    ApplicationHeapFree_nid_no_patch(line);
    line = nullptr;
    capacity = 100;
    {
        Input empty("");
        Check(getline_nid_postfix(&line, &capacity, &empty.stream) == -1);
        Check(line != nullptr && line[0] == 0 && capacity >= 1);
    }
    {
        const std::string data(131072, 'x');
        Input input(data + "\n");
        Check(getline_nid_postfix(&line, &capacity, &input.stream) == static_cast<std::int64_t>(data.size() + 1));
        Check(std::memcmp(line, data.data(), data.size()) == 0);
        Check(line[data.size()] == '\n' && line[data.size() + 1] == '\0');
    }
    ApplicationHeapFree_nid_no_patch(line);
    line = nullptr;
    capacity = 0;
    {
        const auto name = "anyps5-line-write-only-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        auto* writeOnly = fopen_nid_postfix(name.c_str(), "wb");
        Check(writeOnly != nullptr);
        *__error_nid_postfix() = 71;
        Check(getline_nid_postfix(&line, &capacity, writeOnly) == -1);
        Check(*__error_nid_postfix() == 9 && ferror_nid_postfix(writeOnly) != 0);
        Check(line == nullptr && capacity == 0);
        Check(fclose_nid_postfix(writeOnly) == 0);
        Check(std::remove(name.c_str()) == 0);
    }
    std::string records;
    std::set<std::string> expected;
    for (unsigned index = 0; index < 300; ++index) {
        const auto record = "record-" + std::to_string(index) + "\n";
        records += record;
        expected.insert(record);
    }
    Input shared(records);
    std::mutex mutex;
    std::set<std::string> received;
    const auto consume = [&] {
        char* buffer = nullptr;
        std::size_t available = 0;
        for (;;) {
            const auto length = getline_nid_postfix(&buffer, &available, &shared.stream);
            if (length < 0) break;
            const std::string record(buffer, static_cast<std::size_t>(length));
            std::lock_guard lock(mutex);
            Check(received.insert(record).second);
        }
        ApplicationHeapFree_nid_no_patch(buffer);
    };
    std::thread first(consume), second(consume);
    first.join();
    second.join();
    Check(received == expected);
}
