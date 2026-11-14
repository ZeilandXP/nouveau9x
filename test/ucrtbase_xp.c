#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <sys/stat.h>

FILE *__acrt_iob_func(unsigned i)
{
    return &__iob_func()[i];
}

int __stdio_common_vsprintf(unsigned long long options, char *str, size_t len, const char *fmt, void *locale, va_list ap)
{
    (void)options; (void)locale;
    if (len == (size_t)-1) return vsprintf(str, fmt, ap);
    return vsnprintf(str, len, fmt, ap);
}

int __stdio_common_vfprintf(unsigned long long options, FILE *f, const char *fmt, void *locale, va_list ap)
{
    (void)options; (void)locale;
    return vfprintf(f, fmt, ap);
}

int __stdio_common_vsscanf(unsigned long long options, const char *str, size_t len, const char *fmt, void *locale, va_list ap)
{
    (void)options; (void)len; (void)locale;
    return vsscanf(str, fmt, ap);
}

short _dclass(double x)
{
    return (short)_fpclass(x);
}

short _fdclass(float x)
{
    return (short)_fpclass((double)x);
}

int _dsign(double x)
{
    return signbit(x) ? 1 : 0;
}

int _fdsign(float x)
{
    return signbit(x) ? 1 : 0;
}

void _assert(const char *msg, const char *file, unsigned line)
{
    fprintf(stderr, "Assertion failed: %s, file %s, line %u\n", msg, file, line);
    abort();
}

size_t strnlen(const char *s, size_t maxlen)
{
    size_t i = 0;
    while (i < maxlen && s[i]) i++;
    return i;
}

int _fstat64i32(int fd, struct _stat64i32 *buf)
{
    return _fstat(fd, (struct _stat *)buf);
}

/*
 * WineD3D / Vista compatibility shims integrated into ucrtbase.dll
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <limits.h>

typedef struct nvx_condition_state {
    CRITICAL_SECTION guard;
    HANDLE semaphore;
    LONG waiters;
    LONG wakeups;
} nvx_condition_state;

void WINAPI InitializeConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    nvx_condition_state *state = (nvx_condition_state *)calloc(1, sizeof(*state));
    if (!state) return;
    InitializeCriticalSection(&state->guard);
    state->semaphore = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    if (!state->semaphore) {
        DeleteCriticalSection(&state->guard);
        free(state);
        return;
    }
    ConditionVariable->Ptr = state;
}

void WINAPI WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    nvx_condition_state *state = (nvx_condition_state *)ConditionVariable->Ptr;
    LONG release_count = 0;
    if (!state) return;
    EnterCriticalSection(&state->guard);
    if (state->waiters > state->wakeups)
        release_count = 1;
    if (release_count > 0 && ReleaseSemaphore(state->semaphore, release_count, NULL))
        state->wakeups += release_count;
    LeaveCriticalSection(&state->guard);
}

void WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    nvx_condition_state *state = (nvx_condition_state *)ConditionVariable->Ptr;
    LONG release_count = 0;
    if (!state) return;
    EnterCriticalSection(&state->guard);
    release_count = state->waiters - state->wakeups;
    if (release_count > 0 && ReleaseSemaphore(state->semaphore, release_count, NULL))
        state->wakeups += release_count;
    LeaveCriticalSection(&state->guard);
}

BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE ConditionVariable,
                                     PCRITICAL_SECTION CriticalSection,
                                     DWORD dwMilliseconds)
{
    nvx_condition_state *state = (nvx_condition_state *)ConditionVariable->Ptr;
    DWORD result;
    if (!state) return FALSE;
    EnterCriticalSection(&state->guard);
    state->waiters++;
    LeaveCriticalSection(CriticalSection);
    LeaveCriticalSection(&state->guard);

    result = WaitForSingleObject(state->semaphore, dwMilliseconds);
    EnterCriticalSection(&state->guard);
    if (result == WAIT_OBJECT_0) {
        if (state->wakeups > 0)
            state->wakeups--;
        state->waiters--;
    } else {
        state->waiters--;
        if (state->wakeups > 0 &&
            WaitForSingleObject(state->semaphore, 0) == WAIT_OBJECT_0) {
            if (state->waiters > 0)
                ReleaseSemaphore(state->semaphore, 1, NULL);
            else
                state->wakeups--;
        }
    }
    LeaveCriticalSection(&state->guard);
    EnterCriticalSection(CriticalSection);
    return result == WAIT_OBJECT_0;
}

BOOL WINAPI InitOnceExecuteOnce(PINIT_ONCE InitOnce, PINIT_ONCE_FN InitFn,
                                PVOID Parameter, LPVOID *Context)
{
    (void)Context;
    for (;;) {
        LONG state = InterlockedCompareExchange((volatile LONG *)&InitOnce->Ptr, 0, 0);
        if (state == 2) return TRUE;
        if (state == 0 &&
            InterlockedCompareExchange((volatile LONG *)&InitOnce->Ptr, 1, 0) == 0) {
            BOOL ok = InitFn(InitOnce, Parameter, Context);
            InterlockedExchange((volatile LONG *)&InitOnce->Ptr, ok ? 2 : 0);
            return ok;
        }
        Sleep(0);
    }
}

HRESULT WINAPI SetThreadDescription(HANDLE hThread, PCWSTR lpThreadDescription)
{
    (void)hThread;
    (void)lpThreadDescription;
    return S_OK;
}

#define STATUS_NOT_SUPPORTED ((LONG)0xC00000BBL)

LONG WINAPI D3DKMTCloseAdapter(const void *pData)
{
    (void)pData;
    return STATUS_NOT_SUPPORTED;
}

LONG WINAPI D3DKMTCreateDCFromMemory(void *pData)
{
    (void)pData;
    return STATUS_NOT_SUPPORTED;
}

LONG WINAPI D3DKMTCreateDevice(void *pData)
{
    (void)pData;
    return STATUS_NOT_SUPPORTED;
}

LONG WINAPI D3DKMTDestroyDCFromMemory(const void *pData)
{
    (void)pData;
    return STATUS_NOT_SUPPORTED;
}

LONG WINAPI D3DKMTDestroyDevice(const void *pData)
{
    (void)pData;
    return STATUS_NOT_SUPPORTED;
}

LONG WINAPI D3DKMTOpenAdapterFromGdiDisplayName(void *pData)
{
    (void)pData;
    return STATUS_NOT_SUPPORTED;
}

LONG WINAPI D3DKMTOpenAdapterFromLuid(void *pData)
{
    (void)pData;
    return STATUS_NOT_SUPPORTED;
}

LONG WINAPI D3DKMTQueryVideoMemoryInfo(void *pData)
{
    (void)pData;
    return STATUS_NOT_SUPPORTED;
}

LONG WINAPI D3DKMTSetVidPnSourceOwner(const void *pData)
{
    (void)pData;
    return STATUS_NOT_SUPPORTED;
}
