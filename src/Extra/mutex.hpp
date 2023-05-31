// mutex standard header

// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once
#ifndef _MUTEX_
#define _MUTEX_
#include <yvals_core.h>
#if _STL_COMPILER_PREPROCESSOR

#ifdef _M_CEE_PURE
#error <mutex> is not supported when compiling with /clr:pure.
#endif // _M_CEE_PURE

#include <__msvc_chrono.hpp>
#include <cstdlib>
#include <system_error>
#include <thread>
#include <utility>
#include <xcall_once.h>

#pragma pack(push, _CRT_PACKING)
#pragma warning(push, _STL_WARNING_LEVEL)
#pragma warning(disable : _STL_DISABLED_WARNINGS)
_STL_DISABLE_CLANG_WARNINGS
#pragma push_macro("new")
#undef new

_STD_BEGIN
_EXPORT_STD class condition_variable;
_EXPORT_STD class condition_variable_any;

struct _Mtx_internal_imp_mirror {
#ifdef _CRT_WINDOWS
#ifdef _WIN64
    static constexpr size_t _Critical_section_size = 8;
#else // _WIN64
    static constexpr size_t _Critical_section_size = 4;
#endif // _WIN64
#else // _CRT_WINDOWS
#ifdef _WIN64
    static constexpr size_t _Critical_section_size = 56;
#else // _WIN64
    static constexpr size_t _Critical_section_size = 32;
#endif // _WIN64
#endif // _CRT_WINDOWS

    int _Type;
    const void* _Vptr;
    union {
        void* _Srw_lock_placeholder;
        unsigned char _Padding[_Critical_section_size];
    };
    long _Thread_id;
    int _Count;
};

static_assert(sizeof(_Mtx_internal_imp_mirror) == _Mtx_internal_imp_size, "inconsistent size for mutex");
static_assert(alignof(_Mtx_internal_imp_mirror) == _Mtx_internal_imp_alignment, "inconsistent alignment for mutex");

class _Mutex_base { // base class for all mutex types
public:
    _Mutex_base(int _Flags = 0) noexcept {
        _Mtx_init_in_situ(_Mymtx(), _Flags | _Mtx_try);
    }

    ~_Mutex_base() noexcept {
        _Mtx_destroy_in_situ(_Mymtx());
    }

    _Mutex_base(const _Mutex_base&)            = delete;
    _Mutex_base& operator=(const _Mutex_base&) = delete;

    void lock() {
        if (_Mtx_lock(_Mymtx()) != _Thrd_success) {
            // undefined behavior, only occurs for plain mutexes (N4928 [thread.mutex.requirements.mutex.general]/6)
            _STD _Throw_Cpp_error(_RESOURCE_DEADLOCK_WOULD_OCCUR);
        }

        if (!_Verify_ownership_levels()) {
            // only occurs for recursive mutexes (N4928 [thread.mutex.recursive]/3)
            // POSIX specifies EAGAIN in the corresponding situation:
            // https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_mutex_lock.html
            _STD _Throw_Cpp_error(_RESOURCE_UNAVAILABLE_TRY_AGAIN);
        }
    }

    _NODISCARD_TRY_CHANGE_STATE bool try_lock() noexcept /* strengthened */ {
        // false may be from undefined behavior for plain mutexes (N4928 [thread.mutex.requirements.mutex.general]/6)
        return _Mtx_trylock(_Mymtx()) == _Thrd_success;
    }

    void unlock() noexcept /* strengthened */ {
        _Mtx_unlock(_Mymtx());
    }

    using native_handle_type = void*;

    _NODISCARD native_handle_type native_handle() noexcept /* strengthened */ {
        return _Mtx_getconcrtcs(_Mymtx());
    }

protected:
    _NODISCARD_TRY_CHANGE_STATE bool _Verify_ownership_levels() noexcept {
        if (_Mtx_storage_mirror._Count == INT_MAX) {
            // only occurs for recursive mutexes (N4928 [thread.mutex.recursive]/3)
            --_Mtx_storage_mirror._Count;
            return false;
        }

        return true;
    }

private:
    friend condition_variable;
    friend condition_variable_any;

    union {
        _Aligned_storage_t<_Mtx_internal_imp_size, _Mtx_internal_imp_alignment> _Mtx_storage;
        _Mtx_internal_imp_mirror _Mtx_storage_mirror;
    };

    _Mtx_t _Mymtx() noexcept { // get pointer to _Mtx_internal_imp_t inside _Mtx_storage
        return reinterpret_cast<_Mtx_t>(&_Mtx_storage);
    }
};

static_assert(sizeof(_Mutex_base) == _Mtx_internal_imp_size, "inconsistent size for mutex");
static_assert(alignof(_Mutex_base) == _Mtx_internal_imp_alignment, "inconsistent alignment for mutex");

_EXPORT_STD class mutex : public _Mutex_base { // class for mutual exclusion
public:
    /* constexpr */ mutex() noexcept // TRANSITION, ABI
        : _Mutex_base() {}

    mutex(const mutex&)            = delete;
    mutex& operator=(const mutex&) = delete;
};

_EXPORT_STD class recursive_mutex : public _Mutex_base { // class for recursive mutual exclusion
public:
    recursive_mutex() noexcept // strengthened
        : _Mutex_base(_Mtx_recursive) {}

    _NODISCARD_TRY_CHANGE_STATE bool try_lock() noexcept {
        return _Mutex_base::try_lock() && _Verify_ownership_levels();
    }

    recursive_mutex(const recursive_mutex&)            = delete;
    recursive_mutex& operator=(const recursive_mutex&) = delete;
};

_EXPORT_STD struct adopt_lock_t { // indicates adopt lock
    explicit adopt_lock_t() = default;
};

_EXPORT_STD struct defer_lock_t { // indicates defer lock
    explicit defer_lock_t() = default;
};

_EXPORT_STD struct try_to_lock_t { // indicates try to lock
    explicit try_to_lock_t() = default;
};

_EXPORT_STD _INLINE_VAR constexpr adopt_lock_t adopt_lock{};
_EXPORT_STD _INLINE_VAR constexpr defer_lock_t defer_lock{};
_EXPORT_STD _INLINE_VAR constexpr try_to_lock_t try_to_lock{};

_EXPORT_STD template <class _Mutex>
class unique_lock { // whizzy class with destructor that unlocks mutex
public:
    using mutex_type = _Mutex;

    unique_lock() noexcept = default;

    _NODISCARD_CTOR_LOCK explicit unique_lock(_Mutex& _Mtx)
        : _Pmtx(_STD addressof(_Mtx)), _Owns(false) { // construct and lock
        _Pmtx->lock();
        _Owns = true;
    }

    _NODISCARD_CTOR_LOCK unique_lock(_Mutex& _Mtx, adopt_lock_t) noexcept // strengthened
        : _Pmtx(_STD addressof(_Mtx)), _Owns(true) {} // construct and assume already locked

    unique_lock(_Mutex& _Mtx, defer_lock_t) noexcept
        : _Pmtx(_STD addressof(_Mtx)), _Owns(false) {} // construct but don't lock

    _NODISCARD_CTOR_LOCK unique_lock(_Mutex& _Mtx, try_to_lock_t)
        : _Pmtx(_STD addressof(_Mtx)), _Owns(_Pmtx->try_lock()) {} // construct and try to lock

    template <class _Rep, class _Period>
    _NODISCARD_CTOR_LOCK unique_lock(_Mutex& _Mtx, const chrono::duration<_Rep, _Period>& _Rel_time)
        : _Pmtx(_STD addressof(_Mtx)), _Owns(_Pmtx->try_lock_for(_Rel_time)) {} // construct and lock with timeout

    template <class _Clock, class _Duration>
    _NODISCARD_CTOR_LOCK unique_lock(_Mutex& _Mtx, const chrono::time_point<_Clock, _Duration>& _Abs_time)
        : _Pmtx(_STD addressof(_Mtx)), _Owns(_Pmtx->try_lock_until(_Abs_time)) {
        // construct and lock with timeout
#if _HAS_CXX20
        static_assert(chrono::is_clock_v<_Clock>, "Clock type required");
#endif // _HAS_CXX20
    }

    _NODISCARD_CTOR_LOCK unique_lock(_Mutex& _Mtx, const xtime* _Abs_time)
        : _Pmtx(_STD addressof(_Mtx)), _Owns(false) { // try to lock until _Abs_time
        _Owns = _Pmtx->try_lock_until(_Abs_time);
    }

    _NODISCARD_CTOR_LOCK unique_lock(unique_lock&& _Other) noexcept : _Pmtx(_Other._Pmtx), _Owns(_Other._Owns) {
        _Other._Pmtx = nullptr;
        _Other._Owns = false;
    }

    unique_lock& operator=(unique_lock&& _Other) noexcept /* strengthened */ {
        if (this != _STD addressof(_Other)) {
            if (_Owns) {
                _Pmtx->unlock();
            }

            _Pmtx        = _Other._Pmtx;
            _Owns        = _Other._Owns;
            _Other._Pmtx = nullptr;
            _Other._Owns = false;
        }
        return *this;
    }

    ~unique_lock() noexcept {
        if (_Owns) {
            _Pmtx->unlock();
        }
    }

    unique_lock(const unique_lock&)            = delete;
    unique_lock& operator=(const unique_lock&) = delete;

    void lock() { // lock the mutex
        _Validate();
        _Pmtx->lock();
        _Owns = true;
    }

    _NODISCARD_TRY_CHANGE_STATE bool try_lock() {
        _Validate();
        _Owns = _Pmtx->try_lock();
        return _Owns;
    }

    template <class _Rep, class _Period>
    _NODISCARD_TRY_CHANGE_STATE bool try_lock_for(const chrono::duration<_Rep, _Period>& _Rel_time) {
        _Validate();
        _Owns = _Pmtx->try_lock_for(_Rel_time);
        return _Owns;
    }

    template <class _Clock, class _Duration>
    _NODISCARD_TRY_CHANGE_STATE bool try_lock_until(const chrono::time_point<_Clock, _Duration>& _Abs_time) {
#if _HAS_CXX20
        static_assert(chrono::is_clock_v<_Clock>, "Clock type required");
#endif // _HAS_CXX20
        _Validate();
        _Owns = _Pmtx->try_lock_until(_Abs_time);
        return _Owns;
    }

    _NODISCARD_TRY_CHANGE_STATE bool try_lock_until(const xtime* _Abs_time) {
        _Validate();
        _Owns = _Pmtx->try_lock_until(_Abs_time);
        return _Owns;
    }

    void unlock() {
        if (!_Pmtx || !_Owns) {
            _Throw_system_error(errc::operation_not_permitted);
        }

        _Pmtx->unlock();
        _Owns = false;
    }

    void swap(unique_lock& _Other) noexcept {
        _STD swap(_Pmtx, _Other._Pmtx);
        _STD swap(_Owns, _Other._Owns);
    }

    _Mutex* release() noexcept {
        _Mutex* _Res = _Pmtx;
        _Pmtx        = nullptr;
        _Owns        = false;
        return _Res;
    }

    _NODISCARD bool owns_lock() const noexcept {
        return _Owns;
    }

    explicit operator bool() const noexcept {
        return _Owns;
    }

    _NODISCARD _Mutex* mutex() const noexcept {
        return _Pmtx;
    }

private:
    _Mutex* _Pmtx = nullptr;
    bool _Owns    = false;

    void _Validate() const { // check if the mutex can be locked
        if (!_Pmtx) {
            _Throw_system_error(errc::operation_not_permitted);
        }

        if (_Owns) {
            _Throw_system_error(errc::resource_deadlock_would_occur);
        }
    }
};

_EXPORT_STD template <class _Mutex>
void swap(unique_lock<_Mutex>& _Left, unique_lock<_Mutex>& _Right) noexcept {
    _Left.swap(_Right);
}

template <size_t... _Indices, class... _LockN>
void _Lock_from_locks(const int _Target, index_sequence<_Indices...>, _LockN&... _LkN) { // lock _LkN[_Target]
    int _Ignored[] = {((static_cast<int>(_Indices) == _Target ? (void) _LkN.lock() : void()), 0)...};
    (void) _Ignored;
}

template <size_t... _Indices, class... _LockN>
bool _Try_lock_from_locks(
    const int _Target, index_sequence<_Indices...>, _LockN&... _LkN) { // try to lock _LkN[_Target]
    bool _Result{};
    int _Ignored[] = {((static_cast<int>(_Indices) == _Target ? (void) (_Result = _LkN.try_lock()) : void()), 0)...};
    (void) _Ignored;
    return _Result;
}

template <size_t... _Indices, class... _LockN>
void _Unlock_locks(const int _First, const int _Last, index_sequence<_Indices...>, _LockN&... _LkN) noexcept
/* terminates */ {
    // unlock locks in _LkN[_First, _Last)
    int _Ignored[] = {
        ((_First <= static_cast<int>(_Indices) && static_cast<int>(_Indices) < _Last ? (void) _LkN.unlock() : void()),
            0)...};
    (void) _Ignored;
}

template <class _Fn>
struct _NODISCARD _Unlock_call_guard {
    static_assert(
        is_trivially_copyable_v<_Fn>, "This scope guard is only used for trivially copyable function objects.");

    explicit _Unlock_call_guard(const _Fn& _Fx) noexcept : _Func(_Fx) {}

    ~_Unlock_call_guard() noexcept {
        if (_Valid) {
            _Func();
        }
    }

    _Unlock_call_guard(const _Unlock_call_guard&)            = delete;
    _Unlock_call_guard& operator=(const _Unlock_call_guard&) = delete;

    _Fn _Func;
    bool _Valid = true;
};

template <class _Lock>
struct _NODISCARD _Unlock_one_guard {
    explicit _Unlock_one_guard(_Lock& _Lk) noexcept : _Lk_ptr(_STD addressof(_Lk)) {}

    ~_Unlock_one_guard() noexcept {
        if (_Lk_ptr) {
            _Lk_ptr->unlock();
        }
    }

    _Unlock_one_guard(const _Unlock_one_guard&)            = delete;
    _Unlock_one_guard& operator=(const _Unlock_one_guard&) = delete;

    _Lock* _Lk_ptr;
};

template <class... _LockN>
int _Try_lock_range(const int _First, const int _Last, _LockN&... _LkN) {
    using _Indices = index_sequence_for<_LockN...>;
    int _Next      = _First;

    auto _Unlocker = [_First, &_Next, &_LkN...]() noexcept { _STD _Unlock_locks(_First, _Next, _Indices{}, _LkN...); };
    _Unlock_call_guard<decltype(_Unlocker)> _Guard{_Unlocker};

    for (; _Next != _Last; ++_Next) {
        if (!_STD _Try_lock_from_locks(_Next, _Indices{}, _LkN...)) { // try_lock failed, backout
            return _Next;
        }
    }

    _Guard._Valid = false;
    return -1;
}

template <class _Lock0, class _Lock1, class _Lock2, class... _LockN>
int _Try_lock1(_Lock0& _Lk0, _Lock1& _Lk1, _Lock2& _Lk2, _LockN&... _LkN) { // try to lock 3 or more locks
    return _Try_lock_range(0, sizeof...(_LockN) + 3, _Lk0, _Lk1, _Lk2, _LkN...);
}

template <class _Lock0, class _Lock1>
int _Try_lock1(_Lock0& _Lk0, _Lock1& _Lk1) {
    // try to lock 2 locks, special case for better codegen and reduced metaprogramming for common case
    if (!_Lk0.try_lock()) {
        return 0;
    }

    _Unlock_one_guard<_Lock0> _Guard{_Lk0};
    if (!_Lk1.try_lock()) {
        return 1;
    }

    _Guard._Lk_ptr = nullptr;
    return -1;
}

_EXPORT_STD template <class _Lock0, class _Lock1, class... _LockN>
_NODISCARD_TRY_CHANGE_STATE int try_lock(_Lock0& _Lk0, _Lock1& _Lk1, _LockN&... _LkN) { // try to lock multiple locks
    return _Try_lock1(_Lk0, _Lk1, _LkN...);
}

template <class... _LockN>
int _Lock_attempt(const int _Hard_lock, _LockN&... _LkN) {
    // attempt to lock 3 or more locks, starting by locking _LkN[_Hard_lock] and trying to lock the rest
    using _Indices = index_sequence_for<_LockN...>;
    _Lock_from_locks(_Hard_lock, _Indices{}, _LkN...);
    int _Failed        = -1;
    int _Backout_start = _Hard_lock; // that is, unlock _Hard_lock

    {
        auto _Unlocker = [&_Backout_start, _Hard_lock, &_LkN...]() noexcept {
            _STD _Unlock_locks(_Backout_start, _Hard_lock + 1, _Indices{}, _LkN...);
        };
        _Unlock_call_guard<decltype(_Unlocker)> _Guard{_Unlocker};

        _Failed = _STD _Try_lock_range(0, _Hard_lock, _LkN...);
        if (_Failed == -1) {
            _Backout_start = 0; // that is, unlock [0, _Hard_lock] if the next throws
            _Failed        = _STD _Try_lock_range(_Hard_lock + 1, sizeof...(_LockN), _LkN...);
            if (_Failed == -1) { // we got all the locks
                _Guard._Valid = false;
                return -1;
            }
        }
        // we didn't get all the locks, backout with the scope guard
    }

    _STD this_thread::yield();
    return _Failed;
}

template <class _Lock0, class _Lock1, class _Lock2, class... _LockN>
void _Lock_nonmember1(_Lock0& _Lk0, _Lock1& _Lk1, _Lock2& _Lk2, _LockN&... _LkN) {
    // lock 3 or more locks, without deadlock
    int _Hard_lock = 0;
    while (_Hard_lock != -1) {
        _Hard_lock = _Lock_attempt(_Hard_lock, _Lk0, _Lk1, _Lk2, _LkN...);
    }
}

template <class _Lock0, class _Lock1>
bool _Lock_attempt_small(_Lock0& _Lk0, _Lock1& _Lk1) {
    // attempt to lock 2 locks, by first locking _Lk0, and then trying to lock _Lk1 returns whether to try again
    _Lk0.lock();
    {
        _Unlock_one_guard<_Lock0> _Guard{_Lk0};
        if (_Lk1.try_lock()) {
            _Guard._Lk_ptr = nullptr;
            return false;
        }
    }

    _STD this_thread::yield();
    return true;
}

template <class _Lock0, class _Lock1>
void _Lock_nonmember1(_Lock0& _Lk0, _Lock1& _Lk1) {
    // lock 2 locks, without deadlock, special case for better codegen and reduced metaprogramming for common case
    while (_Lock_attempt_small(_Lk0, _Lk1) && _Lock_attempt_small(_Lk1, _Lk0)) { // keep trying
    }
}

_EXPORT_STD template <class _Lock0, class _Lock1, class... _LockN>
void lock(_Lock0& _Lk0, _Lock1& _Lk1, _LockN&... _LkN) { // lock multiple locks, without deadlock
    _Lock_nonmember1(_Lk0, _Lk1, _LkN...);
}

_EXPORT_STD template <class _Mutex>
class _NODISCARD_LOCK lock_guard { // class with destructor that unlocks a mutex
public:
    using mutex_type = _Mutex;

    explicit lock_guard(_Mutex& _Mtx) : _MyMutex(_Mtx) { // construct and lock
        _MyMutex.lock();
    }

    lock_guard(_Mutex& _Mtx, adopt_lock_t) noexcept // strengthened
        : _MyMutex(_Mtx) {} // construct but don't lock

    ~lock_guard() noexcept {
        _MyMutex.unlock();
    }

    lock_guard(const lock_guard&)            = delete;
    lock_guard& operator=(const lock_guard&) = delete;

private:
    _Mutex& _MyMutex;
};

#if _HAS_CXX17
_EXPORT_STD template <class... _Mutexes>
class _NODISCARD_LOCK scoped_lock { // class with destructor that unlocks mutexes
public:
    explicit scoped_lock(_Mutexes&... _Mtxes) : _MyMutexes(_Mtxes...) { // construct and lock
        _STD lock(_Mtxes...);
    }

    explicit scoped_lock(adopt_lock_t, _Mutexes&... _Mtxes) noexcept // strengthened
        : _MyMutexes(_Mtxes...) {} // construct but don't lock

    ~scoped_lock() noexcept {
        _STD apply([](_Mutexes&... _Mtxes) { (..., (void) _Mtxes.unlock()); }, _MyMutexes);
    }

    scoped_lock(const scoped_lock&)            = delete;
    scoped_lock& operator=(const scoped_lock&) = delete;

private:
    tuple<_Mutexes&...> _MyMutexes;
};

template <class _Mutex>
class _NODISCARD_LOCK scoped_lock<_Mutex> {
public:
    using mutex_type = _Mutex;

    explicit scoped_lock(_Mutex& _Mtx) : _MyMutex(_Mtx) { // construct and lock
        _MyMutex.lock();
    }

    explicit scoped_lock(adopt_lock_t, _Mutex& _Mtx) noexcept // strengthened
        : _MyMutex(_Mtx) {} // construct but don't lock

    ~scoped_lock() noexcept {
        _MyMutex.unlock();
    }

    scoped_lock(const scoped_lock&)            = delete;
    scoped_lock& operator=(const scoped_lock&) = delete;

private:
    _Mutex& _MyMutex;
};

template <>
class scoped_lock<> {
public:
    explicit scoped_lock() = default;
    explicit scoped_lock(adopt_lock_t) noexcept /* strengthened */ {}

    scoped_lock(const scoped_lock&)            = delete;
    scoped_lock& operator=(const scoped_lock&) = delete;
};
#endif // _HAS_CXX17

#if defined(_M_CEE) || defined(_M_ARM64EC) || defined(_M_HYBRID) \
    || defined(__clang__) // TRANSITION, Clang doesn't recognize /ALTERNATENAME, not yet reported
#define _WINDOWS_API              __stdcall
#define _RENAME_WINDOWS_API(_Api) _Api##_clr
#else // ^^^ use forwarders / use /ALTERNATENAME vvv
#define _WINDOWS_API              __declspec(dllimport) __stdcall
#define _RENAME_WINDOWS_API(_Api) _Api
#endif // ^^^ use /ALTERNATENAME ^^^

// WINBASEAPI
// BOOL
// WINAPI
// InitOnceBeginInitialize(
//     _Inout_ LPINIT_ONCE lpInitOnce,
//     _In_ DWORD dwFlags,
//     _Out_ PBOOL fPending,
//     _Outptr_opt_result_maybenull_ LPVOID* lpContext
//     );
extern "C" _NODISCARD int _WINDOWS_API _RENAME_WINDOWS_API(__std_init_once_begin_initialize)(
    void** _LpInitOnce, unsigned long _DwFlags, int* _FPending, void** _LpContext) noexcept;

// WINBASEAPI
// BOOL
// WINAPI
// InitOnceComplete(
//     _Inout_ LPINIT_ONCE lpInitOnce,
//     _In_ DWORD dwFlags,
//     _In_opt_ LPVOID lpContext
//     );
extern "C" _NODISCARD int _WINDOWS_API _RENAME_WINDOWS_API(__std_init_once_complete)(
    void** _LpInitOnce, unsigned long _DwFlags, void* _LpContext) noexcept;

extern "C" [[noreturn]] void __stdcall __std_init_once_link_alternate_names_and_abort() noexcept;

// #define RTL_RUN_ONCE_INIT_FAILED    0x00000004UL
// #define INIT_ONCE_INIT_FAILED       RTL_RUN_ONCE_INIT_FAILED
_INLINE_VAR constexpr unsigned long _Init_once_init_failed = 0x4UL;

struct _Init_once_completer {
    once_flag& _Once;
    unsigned long _DwFlags;
    ~_Init_once_completer() {
        if (!_RENAME_WINDOWS_API(__std_init_once_complete)(&_Once._Opaque, _DwFlags, nullptr)) {
            __std_init_once_link_alternate_names_and_abort();
        }
    }
};

_EXPORT_STD template <class _Fn, class... _Args>
void(call_once)(once_flag& _Once, _Fn&& _Fx, _Args&&... _Ax) noexcept(
    noexcept(_STD invoke(_STD forward<_Fn>(_Fx), _STD forward<_Args>(_Ax)...))) /* strengthened */ {
    // call _Fx(_Ax...) once
    // parentheses against common "#define call_once(flag,func) pthread_once(flag,func)"
    int _Pending;
    if (!_RENAME_WINDOWS_API(__std_init_once_begin_initialize)(&_Once._Opaque, 0, &_Pending, nullptr)) {
        _CSTD abort();
    }

    if (_Pending != 0) {
        _Init_once_completer _Op{_Once, _Init_once_init_failed};
        _STD invoke(_STD forward<_Fn>(_Fx), _STD forward<_Args>(_Ax)...);
        _Op._DwFlags = 0;
    }
}

#undef _WINDOWS_API
#undef _RENAME_WINDOWS_API

_EXPORT_STD enum class cv_status { // names for wait returns
    no_timeout,
    timeout
};

_EXPORT_STD class condition_variable { // class for waiting for conditions
public:
    using native_handle_type = _Cnd_t;

    condition_variable() noexcept /* strengthened */ {
        _Cnd_init_in_situ(_Mycnd());
    }

    ~condition_variable() noexcept {
        _Cnd_destroy_in_situ(_Mycnd());
    }

    condition_variable(const condition_variable&)            = delete;
    condition_variable& operator=(const condition_variable&) = delete;

    void notify_one() noexcept { // wake up one waiter
        _Cnd_signal(_Mycnd());
    }

    void notify_all() noexcept { // wake up all waiters
        _Cnd_broadcast(_Mycnd());
    }

    void wait(unique_lock<mutex>& _Lck) noexcept /* strengthened */ { // wait for signal
        // Nothing to do to comply with LWG-2135 because std::mutex lock/unlock are nothrow
        _Cnd_wait(_Mycnd(), _Lck.mutex()->_Mymtx());
    }

    template <class _Predicate>
    void wait(unique_lock<mutex>& _Lck, _Predicate _Pred) { // wait for signal and test predicate
        while (!_Pred()) {
            wait(_Lck);
        }
    }

    template <class _Rep, class _Period>
    cv_status wait_for(unique_lock<mutex>& _Lck, const chrono::duration<_Rep, _Period>& _Rel_time) {
        // wait for duration
        if (_Rel_time <= chrono::duration<_Rep, _Period>::zero()) {
            return cv_status::timeout;
        }

        // TRANSITION, ABI: The standard says that we should use a steady clock,
        // but unfortunately our ABI speaks struct xtime, which is relative to the system clock.
        _CSTD xtime _Tgt;
        const bool _Clamped     = _To_xtime_10_day_clamped(_Tgt, _Rel_time);
        const cv_status _Result = wait_until(_Lck, &_Tgt);
        if (_Clamped) {
            return cv_status::no_timeout;
        }

        return _Result;
    }

    template <class _Rep, class _Period, class _Predicate>
    bool wait_for(unique_lock<mutex>& _Lck, const chrono::duration<_Rep, _Period>& _Rel_time, _Predicate _Pred) {
        // wait for signal with timeout and check predicate
        return _Wait_until1(_Lck, _To_absolute_time(_Rel_time), _Pred);
    }

    template <class _Clock, class _Duration>
    cv_status wait_until(unique_lock<mutex>& _Lck, const chrono::time_point<_Clock, _Duration>& _Abs_time) {
        // wait until time point
#if _HAS_CXX20
        static_assert(chrono::is_clock_v<_Clock>, "Clock type required");
#endif // _HAS_CXX20
        for (;;) {
            const auto _Now = _Clock::now();
            if (_Abs_time <= _Now) {
                return cv_status::timeout;
            }

            _CSTD xtime _Tgt;
            (void) _To_xtime_10_day_clamped(_Tgt, _Abs_time - _Now);
            const cv_status _Result = wait_until(_Lck, &_Tgt);
            if (_Result == cv_status::no_timeout) {
                return cv_status::no_timeout;
            }
        }
    }

    template <class _Clock, class _Duration, class _Predicate>
    bool wait_until(
        unique_lock<mutex>& _Lck, const chrono::time_point<_Clock, _Duration>& _Abs_time, _Predicate _Pred) {
        // wait for signal with timeout and check predicate
#if _HAS_CXX20
        static_assert(chrono::is_clock_v<_Clock>, "Clock type required");
#endif // _HAS_CXX20
        return _Wait_until1(_Lck, _Abs_time, _Pred);
    }

    cv_status wait_until(unique_lock<mutex>& _Lck, const xtime* _Abs_time) {
        // wait for signal with timeout
        if (!_Mtx_current_owns(_Lck.mutex()->_Mymtx())) {
            _Throw_Cpp_error(_OPERATION_NOT_PERMITTED);
        }

        // Nothing to do to comply with LWG-2135 because std::mutex lock/unlock are nothrow
        const int _Res = _Cnd_timedwait(_Mycnd(), _Lck.mutex()->_Mymtx(), _Abs_time);

        if (_Res == _Thrd_success) {
            return cv_status::no_timeout;
        } else {
            return cv_status::timeout;
        }
    }

    template <class _Predicate>
    bool wait_until(unique_lock<mutex>& _Lck, const xtime* _Abs_time, _Predicate _Pred) {
        // wait for signal with timeout and check predicate
        return _Wait_until1(_Lck, _Abs_time, _Pred);
    }

    _NODISCARD native_handle_type native_handle() noexcept /* strengthened */ {
        return _Mycnd();
    }

    void _Register(unique_lock<mutex>& _Lck, int* _Ready) noexcept { // register this object for release at thread exit
        _Cnd_register_at_thread_exit(_Mycnd(), _Lck.release()->_Mymtx(), _Ready);
    }

    void _Unregister(mutex& _Mtx) noexcept { // unregister this object for release at thread exit
        _Cnd_unregister_at_thread_exit(_Mtx._Mymtx());
    }

private:
    _Aligned_storage_t<_Cnd_internal_imp_size, _Cnd_internal_imp_alignment> _Cnd_storage;

    _Cnd_t _Mycnd() noexcept { // get pointer to _Cnd_internal_imp_t inside _Cnd_storage
        return reinterpret_cast<_Cnd_t>(&_Cnd_storage);
    }

    template <class _Predicate>
    bool _Wait_until1(unique_lock<mutex>& _Lck, const xtime* _Abs_time, _Predicate& _Pred) {
        // wait for signal with timeout and check predicate
        while (!_Pred()) {
            if (wait_until(_Lck, _Abs_time) == cv_status::timeout) {
                return _Pred();
            }
        }

        return true;
    }

    template <class _Clock, class _Duration, class _Predicate>
    bool _Wait_until1(
        unique_lock<mutex>& _Lck, const chrono::time_point<_Clock, _Duration>& _Abs_time, _Predicate& _Pred) {
        while (!_Pred()) {
            const auto _Now = _Clock::now();
            if (_Abs_time <= _Now) {
                return false;
            }

            _CSTD xtime _Tgt;
            const bool _Clamped = _To_xtime_10_day_clamped(_Tgt, _Abs_time - _Now);
            if (wait_until(_Lck, &_Tgt) == cv_status::timeout && !_Clamped) {
                return _Pred();
            }
        }

        return true;
    }
};

struct _UInt_is_zero {
    const unsigned int& _UInt;

    _NODISCARD bool operator()() const noexcept {
        return _UInt == 0;
    }
};

_EXPORT_STD class timed_mutex { // class for timed mutual exclusion
public:
    timed_mutex() = default;

    timed_mutex(const timed_mutex&)            = delete;
    timed_mutex& operator=(const timed_mutex&) = delete;

    void lock() { // lock the mutex
        unique_lock<mutex> _Lock(_My_mutex);
        while (_My_locked != 0) {
            _My_cond.wait(_Lock);
        }

        _My_locked = UINT_MAX;
    }

    _NODISCARD_TRY_CHANGE_STATE bool try_lock() noexcept /* strengthened */ { // try to lock the mutex
        lock_guard<mutex> _Lock(_My_mutex);
        if (_My_locked != 0) {
            return false;
        } else {
            _My_locked = UINT_MAX;
            return true;
        }
    }

    void unlock() { // unlock the mutex
        {
            // The lock here is necessary
            lock_guard<mutex> _Lock(_My_mutex);
            _My_locked = 0;
        }
        _My_cond.notify_one();
    }

    template <class _Rep, class _Period>
    _NODISCARD_TRY_CHANGE_STATE bool try_lock_for(
        const chrono::duration<_Rep, _Period>& _Rel_time) { // try to lock for duration
        return try_lock_until(_To_absolute_time(_Rel_time));
    }

    template <class _Time>
    bool _Try_lock_until(_Time _Abs_time) { // try to lock the mutex with timeout
        unique_lock<mutex> _Lock(_My_mutex);
        if (!_My_cond.wait_until(_Lock, _Abs_time, _UInt_is_zero{_My_locked})) {
            return false;
        }

        _My_locked = UINT_MAX;
        return true;
    }

    template <class _Clock, class _Duration>
    _NODISCARD_TRY_CHANGE_STATE bool try_lock_until(const chrono::time_point<_Clock, _Duration>& _Abs_time) {
        // try to lock the mutex with timeout
#if _HAS_CXX20
        static_assert(chrono::is_clock_v<_Clock>, "Clock type required");
#endif // _HAS_CXX20
        return _Try_lock_until(_Abs_time);
    }

    _NODISCARD_TRY_CHANGE_STATE bool try_lock_until(const xtime* _Abs_time) { // try to lock the mutex with timeout
        return _Try_lock_until(_Abs_time);
    }

private:
    mutex _My_mutex;
    condition_variable _My_cond;
    unsigned int _My_locked = 0;
};

_EXPORT_STD class recursive_timed_mutex { // class for recursive timed mutual exclusion
public:
    recursive_timed_mutex() = default;

    recursive_timed_mutex(const recursive_timed_mutex&)            = delete;
    recursive_timed_mutex& operator=(const recursive_timed_mutex&) = delete;

    void lock() { // lock the mutex
        const thread::id _Tid = this_thread::get_id();

        unique_lock<mutex> _Lock(_My_mutex);

        if (_Tid == _My_owner) {
            if (_My_locked < UINT_MAX) {
                ++_My_locked;
            } else {
                // POSIX specifies EAGAIN in the corresponding situation:
                // https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_mutex_lock.html
                _STD _Throw_system_error(errc::resource_unavailable_try_again);
            }
        } else {
            while (_My_locked != 0) {
                _My_cond.wait(_Lock);
            }

            _My_locked = 1;
            _My_owner  = _Tid;
        }
    }

    _NODISCARD_TRY_CHANGE_STATE bool try_lock() noexcept { // try to lock the mutex
        const thread::id _Tid = this_thread::get_id();

        lock_guard<mutex> _Lock(_My_mutex);

        if (_Tid == _My_owner) {
            if (_My_locked < UINT_MAX) {
                ++_My_locked;
            } else {
                return false;
            }
        } else {
            if (_My_locked != 0) {
                return false;
            } else {
                _My_locked = 1;
                _My_owner  = _Tid;
            }
        }
        return true;
    }

    void unlock() { // unlock the mutex
        bool _Do_notify = false;

        {
            lock_guard<mutex> _Lock(_My_mutex);
            --_My_locked;
            if (_My_locked == 0) {
                _Do_notify = true;
                _My_owner  = thread::id();
            }
        }

        if (_Do_notify) {
            _My_cond.notify_one();
        }
    }

    template <class _Rep, class _Period>
    _NODISCARD_TRY_CHANGE_STATE bool try_lock_for(
        const chrono::duration<_Rep, _Period>& _Rel_time) { // try to lock for duration
        return try_lock_until(_To_absolute_time(_Rel_time));
    }

    template <class _Time>
    bool _Try_lock_until(_Time _Abs_time) { // try to lock the mutex with timeout
        const thread::id _Tid = this_thread::get_id();

        unique_lock<mutex> _Lock(_My_mutex);

        if (_Tid == _My_owner) {
            if (_My_locked < UINT_MAX) {
                ++_My_locked;
            } else {
                return false;
            }
        } else {
            if (!_My_cond.wait_until(_Lock, _Abs_time, _UInt_is_zero{_My_locked})) {
                return false;
            }

            _My_locked = 1;
            _My_owner  = _Tid;
        }
        return true;
    }

    template <class _Clock, class _Duration>
    _NODISCARD_TRY_CHANGE_STATE bool try_lock_until(const chrono::time_point<_Clock, _Duration>& _Abs_time) {
        // try to lock the mutex with timeout
#if _HAS_CXX20
        static_assert(chrono::is_clock_v<_Clock>, "Clock type required");
#endif // _HAS_CXX20
        return _Try_lock_until(_Abs_time);
    }

    _NODISCARD_TRY_CHANGE_STATE bool try_lock_until(const xtime* _Abs_time) { // try to lock the mutex with timeout
        return _Try_lock_until(_Abs_time);
    }

private:
    mutex _My_mutex;
    condition_variable _My_cond;
    unsigned int _My_locked = 0;
    thread::id _My_owner;
};
_STD_END
#pragma pop_macro("new")
_STL_RESTORE_CLANG_WARNINGS
#pragma warning(pop)
#pragma pack(pop)
#endif // _STL_COMPILER_PREPROCESSOR
#endif // _MUTEX_
