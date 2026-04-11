/**
 * @file      crashhandler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/crashhandler.h>
#include <promeki/config.h>
#include <promeki/libraryoptions.h>
#include <promeki/application.h>
#include <promeki/buildinfo.h>
#include <promeki/dir.h>
#include <promeki/logger.h>
#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
extern "C" char **environ;
#if PROMEKI_HAVE_CXA_DEMANGLE
#include <cxxabi.h>
#endif
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <execinfo.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#if defined(PROMEKI_PLATFORM_LINUX)
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/ucontext.h>
#endif
#endif

#if defined(PROMEKI_PLATFORM_LINUX)
// Raw dirent64 layout for getdents64 syscall — avoids kernel header
// dependency while staying signal-safe.
struct promeki_dirent64 {
        uint64_t        d_ino;
        int64_t         d_off;
        unsigned short  d_reclen;
        unsigned char   d_type;
        char            d_name[];
};
#endif

PROMEKI_NAMESPACE_BEGIN

#if defined(PROMEKI_PLATFORM_POSIX)

// ============================================================================
// Signal-safe helpers — no heap allocation, no stdio, no C++ I/O
// ============================================================================

namespace {

// Pre-built crash log path.  Filled at install() time so the signal
// handler never needs to allocate.
constexpr size_t MaxPathLen   = 512;
constexpr size_t MaxHostLen   = 256;
constexpr size_t MaxCmdLen    = 2048;
constexpr size_t MaxOsLen     = 256;
constexpr size_t MaxEnvLen    = 65536;   ///< 64 KiB upper bound on env snapshot.
constexpr size_t MaxLibOptLen = 1024;    ///< Library options snapshot buffer.

char g_crashLogPath[MaxPathLen] = {};
char g_hostname[MaxHostLen]     = {};
char g_workingDir[MaxPathLen]   = {};
char g_cmdLine[MaxCmdLen]       = {};
char g_osSysname[MaxOsLen]      = {}; ///< uname.sysname (e.g. Linux, Darwin)
char g_osRelease[MaxOsLen]      = {}; ///< uname.release (kernel release)
char g_osVersion[MaxOsLen]      = {}; ///< uname.version (kernel version string)
char g_osMachine[MaxOsLen]      = {}; ///< uname.machine (arch: x86_64, arm64, ...)
char g_envSnapshot[MaxEnvLen]   = {}; ///< Environment snapshot (KEY=VALUE\n entries).
size_t g_envSnapshotLen         = 0;
bool g_envTruncated             = false;
char g_libOptsSnapshot[MaxLibOptLen] = {}; ///< LibraryOptions snapshot.
time_t g_startTime              = 0;  ///< Process start time (epoch seconds).
bool g_installed = false;

// Signals we handle.
constexpr int CrashSignals[] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };
constexpr int NumCrashSignals = sizeof(CrashSignals) / sizeof(CrashSignals[0]);

/// Signal-safe write of a C string to a file descriptor.
void safeWrite(int fd, const char *s) {
        if(s == nullptr) return;
        size_t len = 0;
        while(s[len] != '\0') ++len;
        while(len > 0) {
                ssize_t n = ::write(fd, s, len);
                if(n <= 0) break;
                s += n;
                len -= static_cast<size_t>(n);
        }
}

/// Signal-safe write to two file descriptors at once.
void safeWrite2(int fd1, int fd2, const char *s) {
        safeWrite(fd1, s);
        if(fd2 >= 0) safeWrite(fd2, s);
}

/// Signal-safe unsigned integer to decimal string.
/// Returns pointer into the provided buffer (writes from the end).
char *utoa(uint64_t val, char *buf, size_t bufLen) {
        char *p = buf + bufLen - 1;
        *p = '\0';
        if(val == 0) {
                --p;
                *p = '0';
                return p;
        }
        while(val > 0 && p > buf) {
                --p;
                *p = '0' + static_cast<char>(val % 10);
                val /= 10;
        }
        return p;
}

/// Signal-safe pointer/unsigned to lowercase hex string with 0x prefix.
/// Returns a pointer into the provided buffer.
char *utox(uint64_t val, char *buf, size_t bufLen) {
        static const char hex[] = "0123456789abcdef";
        char *p = buf + bufLen - 1;
        *p = '\0';
        if(val == 0) {
                --p; *p = '0';
        } else {
                while(val > 0 && p > buf + 2) {
                        --p;
                        *p = hex[val & 0xF];
                        val >>= 4;
                }
        }
        if(p > buf + 1) { --p; *p = 'x'; }
        if(p > buf)     { --p; *p = '0'; }
        return p;
}

/// Signal-safe formatter: writes a unix-epoch seconds value into
/// @p buf as an ISO 8601 UTC timestamp @c "YYYY-MM-DDTHH:MM:SSZ".
/// The buffer must be at least 21 bytes.  Returns @p buf for chaining.
///
/// Implemented by hand to avoid any dependency on @c gmtime_r or
/// @c strftime, which are not on the POSIX async-signal-safe list.
char *formatIsoUtc(time_t epoch, char *buf, size_t bufLen) {
        if(bufLen < 21) { buf[0] = '\0'; return buf; }
        int64_t days = static_cast<int64_t>(epoch) / 86400;
        int64_t rem  = static_cast<int64_t>(epoch) % 86400;
        if(rem < 0) { rem += 86400; --days; }

        int hour   = static_cast<int>(rem / 3600); rem %= 3600;
        int minute = static_cast<int>(rem / 60);
        int second = static_cast<int>(rem % 60);

        int year = 1970;
        for(;;) {
                bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
                int yearDays = leap ? 366 : 365;
                if(days < yearDays) break;
                days -= yearDays;
                ++year;
        }
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        static const int monthDays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
        int month = 0;
        while(month < 12) {
                int md = monthDays[month];
                if(month == 1 && leap) md = 29;
                if(days < md) break;
                days -= md;
                ++month;
        }
        int day = static_cast<int>(days) + 1;
        ++month;

        auto two = [](int v, char *dst) {
                dst[0] = '0' + static_cast<char>((v / 10) % 10);
                dst[1] = '0' + static_cast<char>(v % 10);
        };
        buf[0] = '0' + static_cast<char>((year / 1000) % 10);
        buf[1] = '0' + static_cast<char>((year / 100)  % 10);
        buf[2] = '0' + static_cast<char>((year / 10)   % 10);
        buf[3] = '0' + static_cast<char>( year         % 10);
        buf[4] = '-';
        two(month,  buf + 5);
        buf[7] = '-';
        two(day,    buf + 8);
        buf[10] = 'T';
        two(hour,   buf + 11);
        buf[13] = ':';
        two(minute, buf + 14);
        buf[16] = ':';
        two(second, buf + 17);
        buf[19] = 'Z';
        buf[20] = '\0';
        return buf;
}

/// Signal-safe signed integer to decimal string.
char *itoa(int64_t val, char *buf, size_t bufLen) {
        if(val < 0) {
                buf[0] = '-';
                char *p = utoa(static_cast<uint64_t>(-val), buf + 1, bufLen - 1);
                if(p == buf + 1) return buf;
                // Shift the digits so the '-' is adjacent.
                if(p > buf + 1) {
                        size_t len = 0;
                        while(p[len] != '\0') ++len;
                        for(size_t i = 0; i <= len; ++i) buf[1 + i] = p[i];
                }
                return buf;
        }
        return utoa(static_cast<uint64_t>(val), buf, bufLen);
}

/// Returns the signal name as a C string literal.
const char *signalName(int signo) {
        switch(signo) {
                case SIGSEGV: return "SIGSEGV";
                case SIGABRT: return "SIGABRT";
                case SIGBUS:  return "SIGBUS";
                case SIGFPE:  return "SIGFPE";
                case SIGILL:  return "SIGILL";
                default:      return "UNKNOWN";
        }
}

/// Signal-safe: writes a single @c rlimit field to two file descriptors.
/// Handles @c RLIM_INFINITY by writing "unlimited" instead of a number.
void writeRlimitField(int fd1, int fd2, rlim_t val) {
        if(val == RLIM_INFINITY) {
                safeWrite2(fd1, fd2, "unlimited");
                return;
        }
        char buf[32];
        safeWrite2(fd1, fd2, utoa(static_cast<uint64_t>(val), buf, sizeof(buf)));
}

/// Signal-safe: writes a single getrlimit() result as "NAME: cur / max".
void writeOneRlimit(int fd1, int fd2, const char *label, int which) {
        struct rlimit rl;
        if(getrlimit(which, &rl) != 0) return;
        safeWrite2(fd1, fd2, label);
        writeRlimitField(fd1, fd2, rl.rlim_cur);
        safeWrite2(fd1, fd2, " / ");
        writeRlimitField(fd1, fd2, rl.rlim_max);
        safeWrite2(fd1, fd2, "  (soft/hard)\n");
}

/// Signal-safe: copies the contents of @p path to @p fd using raw
/// open/read/write syscalls.  Silently no-ops if the file can't be
/// opened.  Intended for dumping small-ish /proc files into the
/// crash log.
void dumpFileToFd(const char *path, int fd) {
        if(fd < 0) return;
        int src = ::open(path, O_RDONLY);
        if(src < 0) return;
        char buf[1024];
        for(;;) {
                ssize_t n = ::read(src, buf, sizeof(buf));
                if(n <= 0) break;
                ssize_t off = 0;
                while(off < n) {
                        ssize_t w = ::write(fd, buf + off, n - off);
                        if(w <= 0) { off = n; break; }
                        off += w;
                }
        }
        ::close(src);
}

/// Returns a human-readable description of @c siginfo_t::si_code
/// for the most common fault signals.  Returns an empty string for
/// unknown codes.
const char *siCodeString(int signo, int code) {
        switch(signo) {
                case SIGSEGV:
                        switch(code) {
                                case SEGV_MAPERR: return "address not mapped";
                                case SEGV_ACCERR: return "invalid permissions";
                                default: return "";
                        }
                case SIGBUS:
                        switch(code) {
                                case BUS_ADRALN: return "invalid address alignment";
                                case BUS_ADRERR: return "non-existent physical address";
                                case BUS_OBJERR: return "object-specific hardware error";
                                default: return "";
                        }
                case SIGFPE:
                        switch(code) {
                                case FPE_INTDIV: return "integer divide by zero";
                                case FPE_INTOVF: return "integer overflow";
                                case FPE_FLTDIV: return "floating-point divide by zero";
                                case FPE_FLTOVF: return "floating-point overflow";
                                case FPE_FLTUND: return "floating-point underflow";
                                case FPE_FLTRES: return "floating-point inexact result";
                                case FPE_FLTINV: return "invalid floating-point operation";
                                case FPE_FLTSUB: return "subscript out of range";
                                default: return "";
                        }
                case SIGILL:
                        switch(code) {
                                case ILL_ILLOPC: return "illegal opcode";
                                case ILL_ILLOPN: return "illegal operand";
                                case ILL_ILLADR: return "illegal addressing mode";
                                case ILL_ILLTRP: return "illegal trap";
                                case ILL_PRVOPC: return "privileged opcode";
                                case ILL_PRVREG: return "privileged register";
                                case ILL_COPROC: return "coprocessor error";
                                case ILL_BADSTK: return "internal stack error";
                                default: return "";
                        }
                default: return "";
        }
}

/// Returns a native identifier for the calling thread.  The value
/// returned here is whatever the kernel uses as a thread identifier
/// on this platform:
///
/// - Linux:   kernel TID via @c gettid() (@c SYS_gettid syscall)
/// - macOS:   64-bit pthread thread ID via @c pthread_threadid_np
/// - FreeBSD: @c pthread_getthreadid_np (thread ID)
/// - Others:  falls back to @c getpid()
///
/// All of the above are async-signal-safe in practice.
uint64_t gettid_safe() {
#if defined(PROMEKI_PLATFORM_LINUX)
        return static_cast<uint64_t>(syscall(SYS_gettid));
#elif defined(PROMEKI_PLATFORM_APPLE)
        uint64_t tid = 0;
        pthread_threadid_np(nullptr, &tid);
        return tid;
#elif defined(PROMEKI_PLATFORM_FREEBSD)
        return static_cast<uint64_t>(pthread_getthreadid_np());
#else
        return static_cast<uint64_t>(getpid());
#endif
}

/// Writes the name of the thread identified by @p tid into @p buf.
/// Returns the number of bytes written (excluding the terminating
/// NUL), or 0 on failure.
///
/// Platform notes:
/// - Linux:   reads @c /proc/self/task/<tid>/comm via raw syscalls.
/// - macOS:   there is no signal-safe way to fetch a thread's name
///            for an arbitrary TID, so this returns 0 on non-Linux.
///            The caller can still fetch the current thread's name
///            via @c pthread_getname_np in @ref readCurrentThreadName.
int readThreadName(uint64_t tid, char *buf, size_t bufLen) {
#if defined(PROMEKI_PLATFORM_LINUX)
        char path[64];
        char *p = path;
        const char *prefix = "/proc/self/task/";
        while(*prefix) *p++ = *prefix++;
        char numBuf[24];
        char *num = utoa(tid, numBuf, sizeof(numBuf));
        while(*num) *p++ = *num++;
        const char *suffix = "/comm";
        while(*suffix) *p++ = *suffix++;
        *p = '\0';

        int fd = ::open(path, O_RDONLY);
        if(fd < 0) return 0;
        ssize_t n = ::read(fd, buf, bufLen - 1);
        ::close(fd);
        if(n <= 0) return 0;
        // Strip trailing newline.
        if(n > 0 && buf[n - 1] == '\n') --n;
        buf[n] = '\0';
        return static_cast<int>(n);
#else
        (void)tid;
        (void)buf;
        (void)bufLen;
        return 0;
#endif
}

/// Writes the calling thread's name into @p buf.  Works on every
/// POSIX platform that implements @c pthread_getname_np.
int readCurrentThreadName(char *buf, size_t bufLen) {
        buf[0] = '\0';
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE) || \
    defined(PROMEKI_PLATFORM_BSD)
        if(pthread_getname_np(pthread_self(), buf, bufLen) == 0) {
                return static_cast<int>(std::strlen(buf));
        }
#else
        (void)buf;
        (void)bufLen;
#endif
        return 0;
}

/// Writes the list of running threads (TID + name) to @p fd1 and
/// @p fd2, marking the crashing thread.
///
/// Platform notes:
/// - Linux:   enumerates @c /proc/self/task/ via @c getdents64, reads
///            each thread's name from @c /comm — all signal-safe.
/// - macOS:   full thread enumeration would require Mach APIs
///            (@c task_threads, etc.) which are not signal-safe and
///            take locks internally.  We report just the crashing
///            thread — still useful, and always safe.
/// - Others:  same fallback as macOS.
void writeThreadList(int fd1, int fd2, uint64_t markedTid, const char *marker) {
#if defined(PROMEKI_PLATFORM_LINUX)
        int dirfd = ::open("/proc/self/task", O_RDONLY | O_DIRECTORY);
        if(dirfd < 0) {
                safeWrite2(fd1, fd2, "  (could not enumerate threads)\n");
                return;
        }

        // Use getdents64 directly — async-signal-safe raw syscall.
        char dirBuf[2048];
        for(;;) {
                long nread = syscall(SYS_getdents64, dirfd, dirBuf, sizeof(dirBuf));
                if(nread <= 0) break;
                long off = 0;
                while(off < nread) {
                        struct promeki_dirent64 *ent =
                                reinterpret_cast<struct promeki_dirent64 *>(dirBuf + off);
                        off += ent->d_reclen;

                        // Skip . and ..
                        if(ent->d_name[0] == '.') continue;

                        // Parse TID from directory name.
                        uint64_t tid = 0;
                        for(const char *c = ent->d_name; *c; ++c) {
                                if(*c < '0' || *c > '9') { tid = 0; break; }
                                tid = tid * 10 + static_cast<uint64_t>(*c - '0');
                        }
                        if(tid == 0) continue;

                        char numBuf[24];
                        char nameBuf[32];

                        safeWrite2(fd1, fd2, "  TID: ");
                        safeWrite2(fd1, fd2, utoa(tid, numBuf, sizeof(numBuf)));

                        int nameLen = readThreadName(tid, nameBuf, sizeof(nameBuf));
                        if(nameLen > 0) {
                                safeWrite2(fd1, fd2, "  Name: ");
                                safeWrite2(fd1, fd2, nameBuf);
                        }

                        if(tid == markedTid) {
                                safeWrite2(fd1, fd2, "  ");
                                safeWrite2(fd1, fd2, marker);
                        }
                        safeWrite2(fd1, fd2, "\n");
                }
        }
        ::close(dirfd);
#else
        // Fallback: report just the marked thread (via
        // pthread_getname_np, available on macOS and BSDs).
        char numBuf[24];
        char nameBuf[64];
        safeWrite2(fd1, fd2, "  TID: ");
        safeWrite2(fd1, fd2, utoa(markedTid, numBuf, sizeof(numBuf)));
        if(readCurrentThreadName(nameBuf, sizeof(nameBuf)) > 0) {
                safeWrite2(fd1, fd2, "  Name: ");
                safeWrite2(fd1, fd2, nameBuf);
        }
        safeWrite2(fd1, fd2, "  ");
        safeWrite2(fd1, fd2, marker);
        safeWrite2(fd1, fd2, "\n");
        safeWrite2(fd1, fd2, "  (full thread enumeration requires Mach APIs, "
                             "not signal-safe)\n");
#endif
}

/// Writes a single backtrace frame to @p fd1 (and optionally @p fd2),
/// demangling the C++ symbol name via @c abi::__cxa_demangle when
/// available.  Format matches @c backtrace_symbols except the mangled
/// name is replaced with its demangled form.
///
/// The input @p sym is a string from @c backtrace_symbols and is
/// temporarily modified in place while parsing — the caller owns the
/// backing memory and will free it.
///
/// @note This helper is not strictly async-signal-safe: it calls
///       @c abi::__cxa_demangle which allocates.  It is only invoked
///       after the crash header and thread list have already been
///       written via signal-safe primitives, so at worst a failure
///       here costs us a prettier stack trace.
void writeDemangledFrame(int fd1, int fd2, char *sym) {
#if PROMEKI_HAVE_CXA_DEMANGLE
        char *lparen = std::strchr(sym, '(');
        if(lparen != nullptr) {
                char *plus   = std::strchr(lparen, '+');
                char *rparen = std::strchr(lparen, ')');
                if(plus != nullptr && rparen != nullptr &&
                   plus > lparen + 1 && plus < rparen) {
                        // Terminate at '+' so the mangled name is a
                        // standalone C string for __cxa_demangle.
                        *plus = '\0';
                        int status = 0;
                        char *demangled = abi::__cxa_demangle(lparen + 1,
                                                              nullptr,
                                                              nullptr,
                                                              &status);
                        *plus = '+';

                        if(status == 0 && demangled != nullptr) {
                                // Output: <filename>( <demangled> <suffix>
                                // Split sym at '(' so we can reuse
                                // safeWrite2 for each piece.
                                *lparen = '\0';
                                safeWrite2(fd1, fd2, sym);
                                safeWrite2(fd1, fd2, "(");
                                safeWrite2(fd1, fd2, demangled);
                                safeWrite2(fd1, fd2, plus);
                                safeWrite2(fd1, fd2, "\n");
                                *lparen = '(';
                                std::free(demangled);
                                return;
                        }
                        if(demangled != nullptr) std::free(demangled);
                }
        }
#endif
        // Fallback: write the raw symbol unchanged.  Used when the
        // frame doesn't have a mangled-name segment, when demangling
        // fails, or when abi::__cxa_demangle isn't available at build
        // time.
        safeWrite2(fd1, fd2, sym);
        safeWrite2(fd1, fd2, "\n");
}

/// Writes a full backtrace to @p fd1 and @p fd2 with demangled C++
/// symbol names where possible.  Falls back to the signal-safe
/// @c backtrace_symbols_fd path if @c backtrace_symbols fails to
/// allocate.
void writeStackTrace(int fd1, int fd2, void **frames, int frameCt) {
        char **symbols = backtrace_symbols(frames, frameCt);
        if(symbols == nullptr) {
                // Fallback to the signal-safe path.
                if(fd1 >= 0) backtrace_symbols_fd(frames, frameCt, fd1);
                if(fd2 >= 0) backtrace_symbols_fd(frames, frameCt, fd2);
                return;
        }
        for(int i = 0; i < frameCt; ++i) {
                writeDemangledFrame(fd1, fd2, symbols[i]);
        }
        std::free(symbols);
}

/// Snapshots the current process environment into @ref g_envSnapshot.
/// Iterating @c environ is not safe to do from a signal handler
/// because @c setenv / @c putenv can realloc the underlying array,
/// so we capture it up front and copy the strings into a bounded
/// static buffer the handler can read verbatim.  If the buffer fills,
/// @ref g_envTruncated is set and the remaining entries are dropped.
void snapshotEnvironment() {
        g_envSnapshotLen = 0;
        g_envTruncated = false;
        if(environ == nullptr) {
                g_envSnapshot[0] = '\0';
                return;
        }
        for(char **ep = environ; *ep != nullptr; ++ep) {
                const char *entry = *ep;
                size_t entryLen = std::strlen(entry);
                // Need room for entry + newline + final NUL.
                if(g_envSnapshotLen + entryLen + 2 > MaxEnvLen) {
                        g_envTruncated = true;
                        break;
                }
                std::memcpy(g_envSnapshot + g_envSnapshotLen, entry, entryLen);
                g_envSnapshotLen += entryLen;
                g_envSnapshot[g_envSnapshotLen++] = '\n';
        }
        g_envSnapshot[g_envSnapshotLen] = '\0';
}

/// Signal-safe: dumps a single named 64-bit register value as
/// @c "NAME: 0xHEX  " (no newline).
void writeReg(int fd1, int fd2, const char *name, uint64_t val) {
        char numBuf[32];
        safeWrite2(fd1, fd2, name);
        safeWrite2(fd1, fd2, ": ");
        safeWrite2(fd1, fd2, utox(val, numBuf, sizeof(numBuf)));
        safeWrite2(fd1, fd2, "  ");
}

/// Signal-safe: dumps the CPU register state from the ucontext_t
/// passed to a SA_SIGINFO signal handler.  Architecture- and
/// OS-specific; falls through silently on combos we don't yet
/// cover.  These registers are the state at the instant the fault
/// was taken, which is often the most useful debugging info when
/// the stack is corrupt and the backtrace is garbage.
void writeCpuRegisters(int fd1, int fd2, void *ucontext) {
        if(ucontext == nullptr) return;

#if defined(PROMEKI_PLATFORM_LINUX) && defined(__x86_64__)
        const ucontext_t *uc = static_cast<const ucontext_t *>(ucontext);
        const greg_t *r = uc->uc_mcontext.gregs;
        safeWrite2(fd1, fd2, "\n--- CPU Registers (x86_64) ---\n");
        writeReg(fd1, fd2, "RIP", static_cast<uint64_t>(r[REG_RIP]));
        writeReg(fd1, fd2, "RSP", static_cast<uint64_t>(r[REG_RSP]));
        writeReg(fd1, fd2, "RBP", static_cast<uint64_t>(r[REG_RBP]));
        safeWrite2(fd1, fd2, "\n");
        writeReg(fd1, fd2, "RAX", static_cast<uint64_t>(r[REG_RAX]));
        writeReg(fd1, fd2, "RBX", static_cast<uint64_t>(r[REG_RBX]));
        writeReg(fd1, fd2, "RCX", static_cast<uint64_t>(r[REG_RCX]));
        writeReg(fd1, fd2, "RDX", static_cast<uint64_t>(r[REG_RDX]));
        safeWrite2(fd1, fd2, "\n");
        writeReg(fd1, fd2, "RSI", static_cast<uint64_t>(r[REG_RSI]));
        writeReg(fd1, fd2, "RDI", static_cast<uint64_t>(r[REG_RDI]));
        writeReg(fd1, fd2, "R8 ", static_cast<uint64_t>(r[REG_R8]));
        writeReg(fd1, fd2, "R9 ", static_cast<uint64_t>(r[REG_R9]));
        safeWrite2(fd1, fd2, "\n");
        writeReg(fd1, fd2, "R10", static_cast<uint64_t>(r[REG_R10]));
        writeReg(fd1, fd2, "R11", static_cast<uint64_t>(r[REG_R11]));
        writeReg(fd1, fd2, "R12", static_cast<uint64_t>(r[REG_R12]));
        writeReg(fd1, fd2, "R13", static_cast<uint64_t>(r[REG_R13]));
        safeWrite2(fd1, fd2, "\n");
        writeReg(fd1, fd2, "R14", static_cast<uint64_t>(r[REG_R14]));
        writeReg(fd1, fd2, "R15", static_cast<uint64_t>(r[REG_R15]));
        writeReg(fd1, fd2, "EFL", static_cast<uint64_t>(r[REG_EFL]));
        safeWrite2(fd1, fd2, "\n");
#elif defined(PROMEKI_PLATFORM_LINUX) && defined(__aarch64__)
        const ucontext_t *uc = static_cast<const ucontext_t *>(ucontext);
        const struct mcontext_t *mc = &uc->uc_mcontext;
        safeWrite2(fd1, fd2, "\n--- CPU Registers (aarch64) ---\n");
        writeReg(fd1, fd2, "PC ", static_cast<uint64_t>(mc->pc));
        writeReg(fd1, fd2, "SP ", static_cast<uint64_t>(mc->sp));
        writeReg(fd1, fd2, "PST", static_cast<uint64_t>(mc->pstate));
        safeWrite2(fd1, fd2, "\n");
        // X0..X30 — print four per line, labelling LR and FP.
        char nameBuf[8];
        for(int i = 0; i <= 30; ++i) {
                if(i == 29) {
                        std::memcpy(nameBuf, "FP ", 4);
                } else if(i == 30) {
                        std::memcpy(nameBuf, "LR ", 4);
                } else {
                        nameBuf[0] = 'X';
                        if(i < 10) {
                                nameBuf[1] = '0' + i;
                                nameBuf[2] = ' ';
                                nameBuf[3] = '\0';
                        } else {
                                nameBuf[1] = '0' + (i / 10);
                                nameBuf[2] = '0' + (i % 10);
                                nameBuf[3] = '\0';
                        }
                }
                writeReg(fd1, fd2, nameBuf,
                         static_cast<uint64_t>(mc->regs[i]));
                if(i % 4 == 3) safeWrite2(fd1, fd2, "\n");
        }
        safeWrite2(fd1, fd2, "\n");
#else
        (void)fd1;
        (void)fd2;
        // No register dump for this platform/arch combo.
#endif
}

/// Raises RLIMIT_CORE to the hard limit.
void enableCoreDumps() {
        struct rlimit rl;
        if(getrlimit(RLIMIT_CORE, &rl) != 0) return;
        rl.rlim_cur = rl.rlim_max;
        if(setrlimit(RLIMIT_CORE, &rl) != 0) return;
        if(rl.rlim_cur == RLIM_INFINITY) {
                promekiInfo("CrashHandler: core dumps enabled (unlimited)");
        } else {
                promekiInfo("CrashHandler: core dumps enabled (max %llu bytes)",
                            static_cast<unsigned long long>(rl.rlim_cur));
        }
}

// ============================================================================
// Shared report body — written by both the signal handler (crash path)
// and CrashHandler::writeTrace (diagnostic path).  Writes everything
// from PID through the memory map.  The caller is responsible for
// the opening header and the trailing "saved to" line.
// ============================================================================

void writeReportBody(int logFd, bool isCrash) {
        char numBuf[32];
        char timeBuf[32];

        // Snapshot errno immediately — it's a thread-local that any
        // later call we make (file I/O, etc.) could overwrite.
        int savedErrno = errno;

        // Timestamp (time() is signal-safe, and the ISO formatter is
        // hand-rolled to be signal-safe too).
        time_t now = time(nullptr);
        safeWrite2(STDERR_FILENO, logFd, "Time: ");
        safeWrite2(STDERR_FILENO, logFd,
                   formatIsoUtc(now, timeBuf, sizeof(timeBuf)));
        safeWrite2(STDERR_FILENO, logFd, "  (epoch ");
        safeWrite2(STDERR_FILENO, logFd,
                   utoa(static_cast<uint64_t>(now), numBuf, sizeof(numBuf)));
        safeWrite2(STDERR_FILENO, logFd, ")\n");

        // Process uptime: now - startTime, signal-safe arithmetic.
        if(g_startTime != 0 && now >= g_startTime) {
                uint64_t uptime = static_cast<uint64_t>(now - g_startTime);
                safeWrite2(STDERR_FILENO, logFd, "Uptime: ");
                safeWrite2(STDERR_FILENO, logFd,
                           utoa(uptime, numBuf, sizeof(numBuf)));
                safeWrite2(STDERR_FILENO, logFd, " seconds\n");
        }

        // PID / PPID.
        safeWrite2(STDERR_FILENO, logFd, "PID: ");
        safeWrite2(STDERR_FILENO, logFd,
                   utoa(static_cast<uint64_t>(getpid()),
                        numBuf, sizeof(numBuf)));
        safeWrite2(STDERR_FILENO, logFd, "   Parent PID: ");
        safeWrite2(STDERR_FILENO, logFd,
                   utoa(static_cast<uint64_t>(getppid()),
                        numBuf, sizeof(numBuf)));
        safeWrite2(STDERR_FILENO, logFd, "\n");

        // User / group (real & effective).
        safeWrite2(STDERR_FILENO, logFd, "User: ");
        safeWrite2(STDERR_FILENO, logFd,
                   utoa(static_cast<uint64_t>(getuid()),
                        numBuf, sizeof(numBuf)));
        safeWrite2(STDERR_FILENO, logFd, "/");
        safeWrite2(STDERR_FILENO, logFd,
                   utoa(static_cast<uint64_t>(geteuid()),
                        numBuf, sizeof(numBuf)));
        safeWrite2(STDERR_FILENO, logFd, " (real/effective)   Group: ");
        safeWrite2(STDERR_FILENO, logFd,
                   utoa(static_cast<uint64_t>(getgid()),
                        numBuf, sizeof(numBuf)));
        safeWrite2(STDERR_FILENO, logFd, "/");
        safeWrite2(STDERR_FILENO, logFd,
                   utoa(static_cast<uint64_t>(getegid()),
                        numBuf, sizeof(numBuf)));
        safeWrite2(STDERR_FILENO, logFd, "\n");

        // errno at entry.  Usually stale garbage on a fault, but
        // occasionally meaningful when a crash originates from a
        // failing syscall wrapper.
        safeWrite2(STDERR_FILENO, logFd, "errno: ");
        safeWrite2(STDERR_FILENO, logFd,
                   itoa(savedErrno, numBuf, sizeof(numBuf)));
        safeWrite2(STDERR_FILENO, logFd, "\n");

        // Host
        if(g_hostname[0] != '\0') {
                safeWrite2(STDERR_FILENO, logFd, "Host: ");
                safeWrite2(STDERR_FILENO, logFd, g_hostname);
                safeWrite2(STDERR_FILENO, logFd, "\n");
        }

        // Working directory at install time.
        if(g_workingDir[0] != '\0') {
                safeWrite2(STDERR_FILENO, logFd, "Working Dir: ");
                safeWrite2(STDERR_FILENO, logFd, g_workingDir);
                safeWrite2(STDERR_FILENO, logFd, "\n");
        }

        // Command line.
        if(g_cmdLine[0] != '\0') {
                safeWrite2(STDERR_FILENO, logFd, "Command: ");
                safeWrite2(STDERR_FILENO, logFd, g_cmdLine);
                safeWrite2(STDERR_FILENO, logFd, "\n");
        }

        // --- Build info ---
        // All BuildInfo fields are static string literals embedded in
        // the binary, so reading them is fully signal-safe.
        const BuildInfo *bi = getBuildInfo();
        if(bi != nullptr) {
                safeWrite2(STDERR_FILENO, logFd, "\n--- Build Info ---\n");
                safeWrite2(STDERR_FILENO, logFd, "Name:     ");
                safeWrite2(STDERR_FILENO, logFd, bi->name);
                safeWrite2(STDERR_FILENO, logFd, "\n");
                safeWrite2(STDERR_FILENO, logFd, "Version:  ");
                safeWrite2(STDERR_FILENO, logFd, bi->version);
                safeWrite2(STDERR_FILENO, logFd, "\n");
                safeWrite2(STDERR_FILENO, logFd, "Type:     ");
                safeWrite2(STDERR_FILENO, logFd, bi->type);
                safeWrite2(STDERR_FILENO, logFd, "\n");
                safeWrite2(STDERR_FILENO, logFd, "Repo:     ");
                safeWrite2(STDERR_FILENO, logFd, bi->repoident);
                safeWrite2(STDERR_FILENO, logFd, "\n");
                safeWrite2(STDERR_FILENO, logFd, "Built:    ");
                safeWrite2(STDERR_FILENO, logFd, bi->date);
                safeWrite2(STDERR_FILENO, logFd, " ");
                safeWrite2(STDERR_FILENO, logFd, bi->time);
                safeWrite2(STDERR_FILENO, logFd, " on ");
                safeWrite2(STDERR_FILENO, logFd, bi->hostname);
                safeWrite2(STDERR_FILENO, logFd, "\n");
        }

        // --- Library Options ---
        // Shows the effective LibraryOptions configuration, snapshotted
        // at install() time.  Handy for "why did it do/not do X?"
        // debugging of the library's own knobs.
        if(g_libOptsSnapshot[0] != '\0') {
                safeWrite2(STDERR_FILENO, logFd, "\n--- Library Options ---\n");
                safeWrite2(STDERR_FILENO, logFd, g_libOptsSnapshot);
        }

        // --- OS Info ---
        // g_os* were snapshotted from uname() at install() time so
        // reading them here is signal-safe.
        safeWrite2(STDERR_FILENO, logFd, "\n--- OS Info ---\n");
        safeWrite2(STDERR_FILENO, logFd, "Built For: ");
        safeWrite2(STDERR_FILENO, logFd, PROMEKI_PLATFORM);
        safeWrite2(STDERR_FILENO, logFd, "\n");
        if(g_osSysname[0] != '\0') {
                safeWrite2(STDERR_FILENO, logFd, "System:    ");
                safeWrite2(STDERR_FILENO, logFd, g_osSysname);
                safeWrite2(STDERR_FILENO, logFd, " ");
                safeWrite2(STDERR_FILENO, logFd, g_osRelease);
                safeWrite2(STDERR_FILENO, logFd, "\n");
                safeWrite2(STDERR_FILENO, logFd, "Kernel:    ");
                safeWrite2(STDERR_FILENO, logFd, g_osVersion);
                safeWrite2(STDERR_FILENO, logFd, "\n");
                safeWrite2(STDERR_FILENO, logFd, "Arch:      ");
                safeWrite2(STDERR_FILENO, logFd, g_osMachine);
                safeWrite2(STDERR_FILENO, logFd, "\n");
        }

        // CPU count (sysconf is signal-safe in practice on every
        // platform we care about; glibc implements it as an inline
        // read of a cached value).
        long nCpu = sysconf(_SC_NPROCESSORS_ONLN);
        if(nCpu > 0) {
                safeWrite2(STDERR_FILENO, logFd, "CPUs:      ");
                safeWrite2(STDERR_FILENO, logFd,
                           utoa(static_cast<uint64_t>(nCpu),
                                numBuf, sizeof(numBuf)));
                safeWrite2(STDERR_FILENO, logFd, " online\n");
        }

#if defined(PROMEKI_PLATFORM_LINUX)
        // Linux exposes a handful of short, always-readable files
        // under /proc that give extra context: system uptime, load
        // average, and the kernel boot command line.
        if(logFd >= 0) {
                safeWrite(logFd, "Sys Uptime: ");
                dumpFileToFd("/proc/uptime", logFd);
                safeWrite(logFd, "Load Avg:   ");
                dumpFileToFd("/proc/loadavg", logFd);
                safeWrite(logFd, "Boot Cmd:   ");
                dumpFileToFd("/proc/cmdline", logFd);
        }
#endif

        // --- Memory ---
        // getrusage is in the async-signal-safe list as of POSIX.1-2024
        // and is a syscall wrapper on every platform we care about.
        safeWrite2(STDERR_FILENO, logFd, "\n--- Memory ---\n");
        struct rusage ru;
        if(getrusage(RUSAGE_SELF, &ru) == 0) {
                // ru_maxrss units differ by platform: KiB on Linux,
                // bytes on macOS/BSD.
                safeWrite2(STDERR_FILENO, logFd, "Peak RSS:          ");
                safeWrite2(STDERR_FILENO, logFd,
                           utoa(static_cast<uint64_t>(ru.ru_maxrss),
                                numBuf, sizeof(numBuf)));
#if defined(PROMEKI_PLATFORM_APPLE) || defined(PROMEKI_PLATFORM_BSD)
                safeWrite2(STDERR_FILENO, logFd, " bytes\n");
#else
                safeWrite2(STDERR_FILENO, logFd, " KiB\n");
#endif
                safeWrite2(STDERR_FILENO, logFd, "Page Faults:       ");
                safeWrite2(STDERR_FILENO, logFd,
                           utoa(static_cast<uint64_t>(ru.ru_minflt),
                                numBuf, sizeof(numBuf)));
                safeWrite2(STDERR_FILENO, logFd, " minor / ");
                safeWrite2(STDERR_FILENO, logFd,
                           utoa(static_cast<uint64_t>(ru.ru_majflt),
                                numBuf, sizeof(numBuf)));
                safeWrite2(STDERR_FILENO, logFd, " major\n");

                safeWrite2(STDERR_FILENO, logFd, "CPU Time (user):   ");
                safeWrite2(STDERR_FILENO, logFd,
                           utoa(static_cast<uint64_t>(ru.ru_utime.tv_sec),
                                numBuf, sizeof(numBuf)));
                safeWrite2(STDERR_FILENO, logFd, "s\n");
                safeWrite2(STDERR_FILENO, logFd, "CPU Time (system): ");
                safeWrite2(STDERR_FILENO, logFd,
                           utoa(static_cast<uint64_t>(ru.ru_stime.tv_sec),
                                numBuf, sizeof(numBuf)));
                safeWrite2(STDERR_FILENO, logFd, "s\n");

                safeWrite2(STDERR_FILENO, logFd, "Context Switches:  ");
                safeWrite2(STDERR_FILENO, logFd,
                           utoa(static_cast<uint64_t>(ru.ru_nvcsw),
                                numBuf, sizeof(numBuf)));
                safeWrite2(STDERR_FILENO, logFd, " voluntary / ");
                safeWrite2(STDERR_FILENO, logFd,
                           utoa(static_cast<uint64_t>(ru.ru_nivcsw),
                                numBuf, sizeof(numBuf)));
                safeWrite2(STDERR_FILENO, logFd, " involuntary\n");
        }

#if defined(PROMEKI_PLATFORM_LINUX)
        // System-wide memory via sysinfo(2).  Signal-safe — simple
        // syscall wrapper that fills a caller-provided struct.
        // Great for spotting OOM-adjacent crashes.
        struct sysinfo si;
        if(sysinfo(&si) == 0) {
                uint64_t unit = si.mem_unit ? si.mem_unit : 1;
                auto writeMb = [&](const char *label, uint64_t bytes) {
                        safeWrite2(STDERR_FILENO, logFd, label);
                        safeWrite2(STDERR_FILENO, logFd,
                                   utoa(bytes / (1024 * 1024),
                                        numBuf, sizeof(numBuf)));
                        safeWrite2(STDERR_FILENO, logFd, " MiB\n");
                };
                writeMb("System RAM Total:  ", si.totalram  * unit);
                writeMb("System RAM Free:   ", si.freeram   * unit);
                writeMb("System RAM Shared: ", si.sharedram * unit);
                writeMb("System Swap Total: ", si.totalswap * unit);
                writeMb("System Swap Free:  ", si.freeswap  * unit);
        }
#endif

#if defined(PROMEKI_PLATFORM_LINUX)
        // Dump /proc/self/status to the log file only — it contains
        // detailed Vm* fields (VmPeak, VmRSS, VmHWM, VmSwap, ...) plus
        // state/uid/gid/thread count info.  Too verbose for stderr.
        if(logFd >= 0) {
                safeWrite(logFd, "\n--- Process Status (/proc/self/status) ---\n");
                dumpFileToFd("/proc/self/status", logFd);
        }
#endif

        // --- Resource Limits ---
        // getrlimit is POSIX-standard and async-signal-safe (reads
        // kernel state into a user buffer).  Useful for diagnosing
        // stack overflow (STACK), too-many-open-files (NOFILE), and
        // OOM-adjacent (AS / DATA) crashes.
        safeWrite2(STDERR_FILENO, logFd, "\n--- Resource Limits ---\n");
        writeOneRlimit(STDERR_FILENO, logFd, "Stack:   ", RLIMIT_STACK);
        writeOneRlimit(STDERR_FILENO, logFd, "NOFILE:  ", RLIMIT_NOFILE);
        writeOneRlimit(STDERR_FILENO, logFd, "AS:      ", RLIMIT_AS);
        writeOneRlimit(STDERR_FILENO, logFd, "Data:    ", RLIMIT_DATA);
        writeOneRlimit(STDERR_FILENO, logFd, "Core:    ", RLIMIT_CORE);
#ifdef RLIMIT_NPROC
        writeOneRlimit(STDERR_FILENO, logFd, "NPROC:   ", RLIMIT_NPROC);
#endif

        // --- Thread ---
        uint64_t tid = gettid_safe();
        const char *threadLabel = isCrash ? "Crashed Thread" : "Current Thread";
        const char *threadMark  = isCrash ? "<-- crashed"    : "<-- current";
        safeWrite2(STDERR_FILENO, logFd, "\n--- ");
        safeWrite2(STDERR_FILENO, logFd, threadLabel);
        safeWrite2(STDERR_FILENO, logFd, " ---\n");
        safeWrite2(STDERR_FILENO, logFd, "TID: ");
        safeWrite2(STDERR_FILENO, logFd, utoa(tid, numBuf, sizeof(numBuf)));
        char threadName[64];
        int nameLen = readThreadName(tid, threadName, sizeof(threadName));
        if(nameLen <= 0) {
                // Linux /proc path failed (or we're on another OS);
                // fall back to pthread_getname_np which works for the
                // current thread on every POSIX platform.
                nameLen = readCurrentThreadName(threadName, sizeof(threadName));
        }
        if(nameLen > 0) {
                safeWrite2(STDERR_FILENO, logFd, "  Name: ");
                safeWrite2(STDERR_FILENO, logFd, threadName);
        }
        safeWrite2(STDERR_FILENO, logFd, "\n");

        // --- All threads ---
        safeWrite2(STDERR_FILENO, logFd, "\n--- All Threads ---\n");
        writeThreadList(STDERR_FILENO, logFd, tid, threadMark);

        // --- Stack trace ---
        safeWrite2(STDERR_FILENO, logFd, "\n--- Stack Trace ---\n");
        constexpr int MaxFrames = 100;
        void *frames[MaxFrames];
        int frameCt = backtrace(frames, MaxFrames);
        if(frameCt > 0) {
                writeStackTrace(STDERR_FILENO, logFd, frames, frameCt);
        }

#if defined(PROMEKI_PLATFORM_LINUX)
        // --- Memory map (log file only — too noisy for stderr).
        // Linux-only: FreeBSD has /proc/curproc/map (different format,
        // not always mounted).  macOS has no equivalent accessible
        // via the signal-safe file API.
        if(logFd >= 0) {
                safeWrite(logFd, "\n--- Memory Map (/proc/self/maps) ---\n");
                dumpFileToFd("/proc/self/maps", logFd);
        }
#endif

        // --- Environment (log file only — may contain sensitive
        // values, and is noisy in a terminal).  Controlled by the
        // LibraryOptions::CaptureEnvironment flag: the buffer is
        // empty when capture was disabled at install() time.
        if(logFd >= 0 && g_envSnapshotLen > 0) {
                safeWrite(logFd, "\n--- Environment ---\n");
                // One large write of the whole snapshot.  We use the
                // raw write() syscall (not safeWrite, which expects
                // a NUL-terminated string) so embedded NULs can't
                // happen in env entries anyway.
                size_t off = 0;
                while(off < g_envSnapshotLen) {
                        ssize_t n = ::write(logFd,
                                            g_envSnapshot + off,
                                            g_envSnapshotLen - off);
                        if(n <= 0) break;
                        off += static_cast<size_t>(n);
                }
                if(g_envTruncated) {
                        safeWrite(logFd, "(truncated — environment "
                                         "exceeds snapshot buffer)\n");
                }
        }

        // --- End marker ---
        // A missing trailer means the report was truncated by the
        // kernel (SIGKILL mid-write), disk-full, or similar.
        safeWrite2(STDERR_FILENO, logFd, "\n=== END OF REPORT ===\n");
}

// ============================================================================
// Signal handler — writes a CRASH header then delegates to writeReportBody.
// ============================================================================

void signalHandler(int signo, siginfo_t *info, void *ucontext) {
        char numBuf[32];

        // Open crash log file (pre-built path).
        int logFd = -1;
        if(g_crashLogPath[0] != '\0') {
                logFd = ::open(g_crashLogPath,
                               O_WRONLY | O_CREAT | O_TRUNC,
                               0644);
        }

        // --- Header ---
        safeWrite2(STDERR_FILENO, logFd, "\n=== CRASH: ");
        safeWrite2(STDERR_FILENO, logFd, signalName(signo));
        safeWrite2(STDERR_FILENO, logFd, " (signal ");
        safeWrite2(STDERR_FILENO, logFd, itoa(signo, numBuf, sizeof(numBuf)));
        safeWrite2(STDERR_FILENO, logFd, ") ===\n");

        // Fault details from siginfo_t.
        if(info != nullptr) {
                const char *codeStr = siCodeString(signo, info->si_code);
                if(codeStr[0] != '\0') {
                        safeWrite2(STDERR_FILENO, logFd, "Cause: ");
                        safeWrite2(STDERR_FILENO, logFd, codeStr);
                        safeWrite2(STDERR_FILENO, logFd, "\n");
                }
                // si_addr is the faulting address for SIGSEGV/SIGBUS/
                // SIGFPE/SIGILL.  Meaningless for SIGABRT.
                if(signo != SIGABRT) {
                        safeWrite2(STDERR_FILENO, logFd, "Fault Address: ");
                        safeWrite2(STDERR_FILENO, logFd,
                                   utox(reinterpret_cast<uint64_t>(info->si_addr),
                                        numBuf, sizeof(numBuf)));
                        safeWrite2(STDERR_FILENO, logFd, "\n");
                }
        }

        // CPU register state at the instant of the fault.  Only the
        // crash path has a valid ucontext; traces pass nullptr and
        // the helper no-ops on unsupported arch/OS combos.
        writeCpuRegisters(STDERR_FILENO, logFd, ucontext);

        writeReportBody(logFd, /*isCrash=*/true);

        if(logFd >= 0) {
                ::close(logFd);
                safeWrite(STDERR_FILENO, "\nCrash log saved to: ");
                safeWrite(STDERR_FILENO, g_crashLogPath);
                safeWrite(STDERR_FILENO, "\n");
        }

        // Best-effort logger flush — NOT signal-safe, but all critical
        // output has already been written via raw write() above.
        promekiLogSync();

        // Restore default handler and re-raise for core dump.
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(signo, &sa, nullptr);
        raise(signo);
}

} // namespace

// ============================================================================
// Public API
// ============================================================================

void CrashHandler::install() {
        // Build crash log path from Dir::temp(), Application::appName(), and PID.
        String appName = Application::appName();
        if(appName.isEmpty()) appName = "promeki";
        FilePath logDir;
        String cfgDir = LibraryOptions::instance().getAs<String>(LibraryOptions::CrashLogDir);
        if(!cfgDir.isEmpty()) {
                logDir = FilePath(cfgDir);
        } else {
                logDir = Dir::temp().path();
        }
        String logFile = String::sprintf("%s/promeki-crash-%s-%d.log",
                                         logDir.toString().cstr(),
                                         appName.cstr(),
                                         static_cast<int>(getpid()));

        // Copy into the static buffer for the signal handler.
        size_t len = logFile.length();
        if(len >= MaxPathLen) len = MaxPathLen - 1;
        std::memcpy(g_crashLogPath, logFile.cstr(), len);
        g_crashLogPath[len] = '\0';

        // Snapshot process start time for uptime calculation.
        g_startTime = time(nullptr);

        // Snapshot hostname (not signal-safe to fetch inside the handler).
        if(gethostname(g_hostname, MaxHostLen - 1) != 0) {
                g_hostname[0] = '\0';
        } else {
                g_hostname[MaxHostLen - 1] = '\0';
        }

        // Snapshot OS info via uname.  Safe to call here; the result
        // is copied into static buffers the signal handler can read.
        struct utsname un;
        if(uname(&un) == 0) {
                std::strncpy(g_osSysname, un.sysname, MaxOsLen - 1);
                std::strncpy(g_osRelease, un.release, MaxOsLen - 1);
                std::strncpy(g_osVersion, un.version, MaxOsLen - 1);
                std::strncpy(g_osMachine, un.machine, MaxOsLen - 1);
                g_osSysname[MaxOsLen - 1] = '\0';
                g_osRelease[MaxOsLen - 1] = '\0';
                g_osVersion[MaxOsLen - 1] = '\0';
                g_osMachine[MaxOsLen - 1] = '\0';
        }

        // Snapshot the current working directory.
        if(getcwd(g_workingDir, MaxPathLen) == nullptr) {
                g_workingDir[0] = '\0';
        }

        // Snapshot the command line, space-joined.
        g_cmdLine[0] = '\0';
        const StringList &args = Application::arguments();
        size_t pos = 0;
        for(size_t i = 0; i < args.size(); ++i) {
                const String &arg = args[i];
                const char *s = arg.cstr();
                size_t slen = arg.length();
                if(i > 0 && pos + 1 < MaxCmdLen) {
                        g_cmdLine[pos++] = ' ';
                }
                if(pos + slen >= MaxCmdLen) {
                        slen = (pos < MaxCmdLen - 1) ? (MaxCmdLen - 1 - pos) : 0;
                }
                std::memcpy(g_cmdLine + pos, s, slen);
                pos += slen;
                if(pos >= MaxCmdLen - 1) break;
        }
        g_cmdLine[pos] = '\0';

        // Capture environment if enabled (default true).  Done here
        // rather than lazily in the signal handler because walking
        // environ is not safe if another thread may be calling setenv.
        if(LibraryOptions::instance().getAs<bool>(LibraryOptions::CaptureEnvironment)) {
                snapshotEnvironment();
        } else {
                g_envSnapshotLen = 0;
                g_envTruncated = false;
                g_envSnapshot[0] = '\0';
        }

        // Snapshot the effective LibraryOptions so the handler can
        // show the library's current configuration without touching
        // the Variant database at crash time.  Iterates every set
        // ID via forEach so new options added to LibraryOptions
        // (or via PROMEKI_OPT_* env) appear automatically.
        {
                String dump;
                LibraryOptions::instance().forEach(
                        [&dump](LibraryOptions::ID id, const Variant &val) {
                                dump += String::sprintf("%-20s = %s\n",
                                        id.name().cstr(),
                                        val.get<String>().cstr());
                        });
                size_t dlen = dump.length();
                if(dlen >= MaxLibOptLen) dlen = MaxLibOptLen - 1;
                std::memcpy(g_libOptsSnapshot, dump.cstr(), dlen);
                g_libOptsSnapshot[dlen] = '\0';
        }

        // Enable core dumps if requested.
        if(LibraryOptions::instance().getAs<bool>(LibraryOptions::CoreDumps)) {
                enableCoreDumps();
        }

        // Install signal handlers via sigaction so we can receive the
        // siginfo_t describing the fault (si_code, si_addr).
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = signalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        for(int i = 0; i < NumCrashSignals; ++i) {
                sigaction(CrashSignals[i], &sa, nullptr);
        }
        g_installed = true;

        promekiInfo("CrashHandler: installed (crash log: %s)", g_crashLogPath);
}

void CrashHandler::uninstall() {
        if(!g_installed) return;
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        for(int i = 0; i < NumCrashSignals; ++i) {
                sigaction(CrashSignals[i], &sa, nullptr);
        }
        g_installed = false;
        g_crashLogPath[0] = '\0';
        g_hostname[0] = '\0';
        g_workingDir[0] = '\0';
        g_cmdLine[0] = '\0';
        g_osSysname[0] = '\0';
        g_osRelease[0] = '\0';
        g_osVersion[0] = '\0';
        g_osMachine[0] = '\0';
        g_envSnapshot[0] = '\0';
        g_envSnapshotLen = 0;
        g_envTruncated   = false;
        g_libOptsSnapshot[0] = '\0';
        g_startTime      = 0;
}

bool CrashHandler::isInstalled() {
        return g_installed;
}

void CrashHandler::writeTrace(const char *reason) {
        // Atomic sequence number makes each call's filename unique
        // within this process, so repeated traces don't overwrite
        // each other.
        static std::atomic<uint32_t> traceSeq{0};
        uint32_t seq = traceSeq.fetch_add(1, std::memory_order_relaxed) + 1;

        // Build the trace log path.
        String appName = Application::appName();
        if(appName.isEmpty()) appName = "promeki";
        FilePath logDir;
        String cfgDir = LibraryOptions::instance().getAs<String>(LibraryOptions::CrashLogDir);
        if(!cfgDir.isEmpty()) {
                logDir = FilePath(cfgDir);
        } else {
                logDir = Dir::temp().path();
        }
        String tracePath = String::sprintf("%s/promeki-trace-%s-%d-%04u.log",
                                           logDir.toString().cstr(),
                                           appName.cstr(),
                                           static_cast<int>(getpid()),
                                           static_cast<unsigned int>(seq));

        int logFd = ::open(tracePath.cstr(),
                           O_WRONLY | O_CREAT | O_TRUNC,
                           0644);

        // Header
        safeWrite2(STDERR_FILENO, logFd, "\n=== TRACE");
        if(reason != nullptr && reason[0] != '\0') {
                safeWrite2(STDERR_FILENO, logFd, ": ");
                safeWrite2(STDERR_FILENO, logFd, reason);
        }
        safeWrite2(STDERR_FILENO, logFd, " ===\n");

        writeReportBody(logFd, /*isCrash=*/false);

        if(logFd >= 0) {
                ::close(logFd);
                safeWrite(STDERR_FILENO, "\nTrace saved to: ");
                safeWrite(STDERR_FILENO, tracePath.cstr());
                safeWrite(STDERR_FILENO, "\n");
        }

        // Best-effort logger flush.
        promekiLogSync();
}

#else // non-POSIX platforms

void CrashHandler::install() {
        // Crash handling not supported on this platform.
}

void CrashHandler::uninstall() {
}

bool CrashHandler::isInstalled() {
        return false;
}

void CrashHandler::writeTrace(const char *reason) {
        (void)reason;
        // No-op on unsupported platforms.
}

#endif

PROMEKI_NAMESPACE_END
