#include "SceTypes.hpp"
#include "prx/libkernel/Pthread/include/Pthread.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

extern "C" {
int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry, void* arg, const char* name);
int APS5_VABI scePthreadJoin(Pthread thread, void** retval);
int APS5_VABI scePthreadMutexInit(PthreadMutex* mutex, const PthreadMutexattr* attr, const char* name);
int APS5_VABI scePthreadMutexDestroy(PthreadMutex* mutex);
int APS5_VABI scePthreadMutexLock(PthreadMutex* mutex);
int APS5_VABI scePthreadMutexUnlock(PthreadMutex* mutex);
int APS5_VABI scePthreadMutexTrylock(PthreadMutex* mutex);
int APS5_VABI scePthreadCondInit(PthreadCond* cond, const PthreadCondattr* attr, const char* name);
int APS5_VABI scePthreadCondDestroy(PthreadCond* cond);
int APS5_VABI scePthreadCondTimedwait(PthreadCond* cond, PthreadMutex* mutex, unsigned int usec);
int APS5_VABI scePthreadCondSignal(PthreadCond* cond);
int APS5_VABI pthread_cond_reltimedwait_np_nid_postfix(PthreadCond* cond, PthreadMutex* mutex, unsigned int usec);
int APS5_VABI pthread_condattr_init_nid_postfix(PthreadCondattr* attr);
int APS5_VABI pthread_condattr_destroy_nid_postfix(PthreadCondattr* attr);
int APS5_VABI pthread_condattr_setclock_nid_postfix(PthreadCondattr* attr, KernelClockid clockId);
int APS5_VABI pthread_cond_timedwait_nid_postfix(PthreadCond* cond, PthreadMutex* mutex, const KernelTimespec* abstime);
int* APS5_VABI __error_nid_postfix();
int APS5_VABI __cxa_atexit_nid_postfix(void (APS5_VABI *)(void*), void*, void*);
void APS5_VABI __pthread_cxa_finalize_nid_postfix(void*);
unsigned int APS5_VABI sceKernelSleep(unsigned int seconds);
}

static int cleanupAt = -1;
static void APS5_VABI Cleanup(void*) { cleanupAt = 0; }

static constexpr int SCE_OK = 0;
static constexpr int SCE_KERNEL_ERROR_ETIMEDOUT = static_cast<int>(0x8002003C);
static constexpr int SCE_KERNEL_ERROR_EBUSY = static_cast<int>(0x80020010);
static constexpr int POSIX_EPERM = 1;
static constexpr int POSIX_EINVAL = 22;
static constexpr int POSIX_ETIMEDOUT = 60;

static void Require(bool value) { if (!value) std::abort(); }

struct Context {
    PthreadMutex mutex = nullptr;
    std::atomic<bool> acquired{false};
};

static void* APS5_VABI Contender(void* arg) {
    auto& context = *static_cast<Context*>(arg);
    Require(scePthreadMutexLock(&context.mutex) == SCE_OK);
    context.acquired.store(true);
    Require(scePthreadMutexUnlock(&context.mutex) == SCE_OK);
    return nullptr;
}

struct Signalled {
    PthreadMutex mutex = nullptr;
    PthreadCond cond = nullptr;
    bool ready = false;
    int trylock = SCE_OK;
};

static void* APS5_VABI Trylocker(void* arg) {
    auto& signalled = *static_cast<Signalled*>(arg);
    signalled.trylock = scePthreadMutexTrylock(&signalled.mutex);
    return nullptr;
}

static void* APS5_VABI Signaller(void* arg) {
    auto& signalled = *static_cast<Signalled*>(arg);
    Require(scePthreadMutexLock(&signalled.mutex) == SCE_OK);
    signalled.ready = true;
    Require(scePthreadCondSignal(&signalled.cond) == SCE_OK);
    Require(scePthreadMutexUnlock(&signalled.mutex) == SCE_OK);
    return nullptr;
}

static void RequireHeldByCaller(Signalled& signalled) {
    Pthread thread = nullptr;
    Require(scePthreadCreate(&thread, nullptr, Trylocker, &signalled, nullptr) == SCE_OK);
    Require(scePthreadJoin(thread, nullptr) == SCE_OK);
    Require(signalled.trylock == SCE_KERNEL_ERROR_EBUSY);
}

static void RelativeWait() {
    Signalled signalled;
    Require(scePthreadMutexInit(&signalled.mutex, nullptr, nullptr) == SCE_OK);
    Require(scePthreadCondInit(&signalled.cond, nullptr, nullptr) == SCE_OK);

    Require(pthread_cond_reltimedwait_np_nid_postfix(&signalled.cond, &signalled.mutex, 1000) == POSIX_EPERM);

    Require(scePthreadMutexLock(&signalled.mutex) == SCE_OK);
    const auto start = std::chrono::steady_clock::now();
    Require(pthread_cond_reltimedwait_np_nid_postfix(&signalled.cond, &signalled.mutex, 50000) == POSIX_ETIMEDOUT);
    Require(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(40));
    RequireHeldByCaller(signalled);
    Require(pthread_cond_reltimedwait_np_nid_postfix(&signalled.cond, &signalled.mutex, 0) == POSIX_ETIMEDOUT);
    RequireHeldByCaller(signalled);

    Pthread thread = nullptr;
    Require(scePthreadCreate(&thread, nullptr, Signaller, &signalled, nullptr) == SCE_OK);
    const auto waitStart = std::chrono::steady_clock::now();
    while (!signalled.ready)
        Require(pthread_cond_reltimedwait_np_nid_postfix(&signalled.cond, &signalled.mutex, 10000000) == 0);
    Require(std::chrono::steady_clock::now() - waitStart < std::chrono::seconds(5));
    RequireHeldByCaller(signalled);
    Require(scePthreadMutexUnlock(&signalled.mutex) == SCE_OK);
    Require(scePthreadJoin(thread, nullptr) == SCE_OK);

    Require(scePthreadCondDestroy(&signalled.cond) == SCE_OK);
    Require(scePthreadMutexDestroy(&signalled.mutex) == SCE_OK);
}

static void AttributeClockValidation() {
    PthreadCondattr empty = nullptr;
    Require(pthread_condattr_setclock_nid_postfix(nullptr, 0) == POSIX_EINVAL);
    Require(pthread_condattr_setclock_nid_postfix(&empty, 0) == POSIX_EINVAL);
    for (const KernelClockid accepted : {0, 1, 2, 4}) {
        PthreadCondattr attr = nullptr;
        Require(pthread_condattr_init_nid_postfix(&attr) == 0);
        *__error_nid_postfix() = 12345;
        Require(pthread_condattr_setclock_nid_postfix(&attr, accepted) == 0);
        Require(*__error_nid_postfix() == 12345);
        for (const KernelClockid rejected : {std::numeric_limits<KernelClockid>::min(), -1, 3, 5, 6, 7, 8,
            9, 10, 11, 12, 13, 14, 15, std::numeric_limits<KernelClockid>::max()}) {
            const int result = pthread_condattr_setclock_nid_postfix(&attr, rejected);
            if (result != POSIX_EINVAL) {
                std::fprintf(stderr, "pthread_condattr_setclock accepted unsupported clock %d: result %d\n", rejected, result);
                std::abort();
            }
            Require(*__error_nid_postfix() == 12345);
            Require(attr->_clockid == accepted);
        }
        Signalled signalled;
        Require(scePthreadCondInit(&signalled.cond, &attr, nullptr) == 0);
        Require(pthread_condattr_destroy_nid_postfix(&attr) == 0 && attr == nullptr);
        Require(scePthreadMutexInit(&signalled.mutex, nullptr, nullptr) == 0);
        Require(scePthreadMutexLock(&signalled.mutex) == 0);
        const KernelTimespec expired{0, 0};
        Require(pthread_cond_timedwait_nid_postfix(&signalled.cond, &signalled.mutex, &expired) == POSIX_ETIMEDOUT);
        RequireHeldByCaller(signalled);
        Require(scePthreadMutexUnlock(&signalled.mutex) == 0);
        Require(scePthreadMutexDestroy(&signalled.mutex) == 0);
        Require(scePthreadCondDestroy(&signalled.cond) == 0);
    }
}

int main() {
    AttributeClockValidation();
    RelativeWait();

    Context context;
    PthreadCond cond = nullptr;
    Require(scePthreadMutexInit(&context.mutex, nullptr, nullptr) == SCE_OK);
    Require(scePthreadCondInit(&cond, nullptr, nullptr) == SCE_OK);

    Require(scePthreadMutexLock(&context.mutex) == SCE_OK);
    Pthread thread = nullptr;
    Require(scePthreadCreate(&thread, nullptr, Contender, &context, nullptr) == SCE_OK);
    while (!context.acquired.load())
        Require(scePthreadCondTimedwait(&cond, &context.mutex, 1000) == SCE_KERNEL_ERROR_ETIMEDOUT);
    Require(scePthreadMutexUnlock(&context.mutex) == SCE_OK);
    Require(scePthreadJoin(thread, nullptr) == SCE_OK);

    Require(scePthreadCondDestroy(&cond) == SCE_OK);
    Require(scePthreadMutexDestroy(&context.mutex) == SCE_OK);

    Require(sceKernelSleep(0) == SCE_OK);
    void* finalizeHandle = reinterpret_cast<void*>(7);
    Require(__cxa_atexit_nid_postfix(Cleanup, nullptr, finalizeHandle) == SCE_OK);
    __pthread_cxa_finalize_nid_postfix(finalizeHandle);
    Require(cleanupAt == 0);
}
