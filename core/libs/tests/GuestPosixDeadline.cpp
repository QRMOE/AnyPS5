#include "prx/libkernel/Pthread/Posix/Common.hpp"
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

KernelTimespec clockValue{};
int clockCalls = 0;
int requestedClock = -1;

struct DeadlineCase {
    KernelTimespec now;
    KernelTimespec deadline;
    KernelUseconds expected;
};

}

extern "C" int APS5_VABI clock_gettime_nid_postfix(int clockId, KernelTimespec* output) {
    ++clockCalls;
    requestedClock = clockId;
    *output = clockValue;
    return 0;
}

int main() {
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    constexpr auto longest = std::numeric_limits<KernelUseconds>::max();
    constexpr DeadlineCase cases[] = {
        {{0, 0}, {0, 0}, 0},
        {{0, 0}, {0, 999}, 0},
        {{0, 0}, {0, 1000}, 1},
        {{100, 900000000}, {101, 100000000}, 200000},
        {{100, 900000000}, {100, 899999999}, 0},
        {{100, 0}, {99, 999999999}, 0},
        {{-1, 999999000}, {0, 0}, 1},
        {{-2, 500000000}, {-1, 499999999}, 999999},
        {{0, 0}, {4294, 967294999}, longest - 1},
        {{0, 0}, {4294, 967295000}, longest},
        {{0, 0}, {4294, 967295999}, longest},
        {{0, 0}, {4294, 967296000}, longest},
        {{0, 999999999}, {4295, 0}, 4294000000u},
        {{-4295, 500000000}, {0, 0}, 4294500000u},
        {{0, 0}, {9223372037, 0}, longest},
        {{0, 0}, {-9223372037, 0}, 0},
        {{0, 0}, {maximum, 999999999}, longest},
        {{0, 0}, {minimum, 0}, 0},
        {{minimum, 0}, {maximum, 999999999}, longest},
        {{maximum, 999999999}, {minimum, 0}, 0},
        {{minimum, 999999000}, {minimum + 1, 0}, 1},
        {{maximum - 1, 999999000}, {maximum, 0}, 1},
        {{minimum, 0}, {minimum, 1000}, 1},
        {{maximum, 0}, {maximum, 1000}, 1},
    };
    bool passed = true;
    for (const auto& test : cases) {
        clockValue = test.now;
        clockCalls = 0;
        requestedClock = -1;
        KernelUseconds actual = 77;
        const bool valid = PosixThread::RelativeMicroseconds(4, &test.deadline, &actual);
        if (!valid || actual != test.expected || clockCalls != 1 || requestedClock != 4) {
            std::fprintf(stderr, "deadline %lld.%09lld from %lld.%09lld: valid=%d, usec=%u, expected=%u, calls=%d, clock=%d\n",
                static_cast<long long>(test.deadline.tv_sec), static_cast<long long>(test.deadline.tv_nsec),
                static_cast<long long>(test.now.tv_sec), static_cast<long long>(test.now.tv_nsec),
                valid, actual, test.expected, clockCalls, requestedClock);
            passed = false;
        }
    }
    constexpr std::int64_t invalidNanoseconds[] = {-1, 1000000000, minimum, maximum};
    for (const auto nanoseconds : invalidNanoseconds) {
        const KernelTimespec invalid{0, nanoseconds};
        KernelUseconds actual = 77;
        clockCalls = 0;
        if (PosixThread::RelativeMicroseconds(0, &invalid, &actual) || actual != 77 || clockCalls != 0) {
            std::fprintf(stderr, "invalid nanoseconds %lld were not rejected before reading the clock\n", static_cast<long long>(nanoseconds));
            passed = false;
        }
    }
    KernelUseconds actual = 77;
    clockCalls = 0;
    if (PosixThread::RelativeMicroseconds(0, nullptr, &actual) || actual != 77 || clockCalls != 0) {
        std::fputs("null deadline was not rejected before reading the clock\n", stderr);
        passed = false;
    }
    return passed ? 0 : 1;
}
