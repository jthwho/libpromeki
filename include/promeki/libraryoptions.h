/**
 * @file      libraryoptions.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/variantdatabase.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Library-wide option database populated from environment variables.
 * @ingroup util
 *
 * Provides a central registry of configuration knobs that affect the
 * behaviour of the promeki library itself.  Each option is declared with
 * a @ref VariantSpec that captures its type, default, and description.
 *
 * On startup the @ref Application constructor calls @ref loadFromEnvironment
 * which scans the process environment for variables matching
 * @c PROMEKI_OPT_<Name> and merges them — type-coerced via
 * @ref VariantSpec::parseString — into the singleton @ref instance().
 *
 * Options can also be set programmatically at any time:
 *
 * @par Thread Safety
 * Inherits @ref VariantDatabase: distinct instances may be used
 * concurrently.  The singleton @ref instance is intended to be
 * configured once at startup (typically in @c main before worker
 * threads start) and read thereafter from any thread.  Concurrent
 * mutation requires external synchronization.
 *
 * @par Example
 * @code
 * // Override an option before Application construction
 * LibraryOptions::instance().set(LibraryOptions::CoreDumps, true);
 *
 * // Or via the environment:
 * //   export PROMEKI_OPT_CoreDumps=true
 * @endcode
 */
class LibraryOptions : public VariantDatabase<"LibraryOptions"> {
        public:
                /** @brief Base class alias. */
                using Base = VariantDatabase<"LibraryOptions">;

                using Base::Base;

                // ============================================================
                // Crash handling
                // ============================================================

                /// @brief bool — install crash signal handlers (default true).
                PROMEKI_DECLARE_ID(CrashHandler,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Install crash signal handlers."));

                /// @brief bool — raise RLIMIT_CORE for core dumps (default false).
                PROMEKI_DECLARE_ID(CoreDumps,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable core dumps via RLIMIT_CORE."));

                /// @brief String — crash log directory (empty = Dir::temp()).
                PROMEKI_DECLARE_ID(CrashLogDir,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Crash log directory (empty = system temp)."));

                /// @brief bool — include the process environment in crash/trace
                /// reports (default true).  Disable if the environment may
                /// contain secrets you don't want written to disk.
                PROMEKI_DECLARE_ID(CaptureEnvironment,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Include environment variables in crash reports."));

                // ============================================================
                // Filesystem
                // ============================================================

                /// @brief String — override for the path returned by
                /// @ref Dir::temp (empty = use the OS default, which
                /// is @c std::filesystem::temp_directory_path — typically
                /// @c /tmp on Linux, @c %TEMP% on Windows).
                ///
                /// Set this option to pin every @ref Dir::temp call —
                /// and every consumer downstream of it (crash logs,
                /// scratch JSON, ad-hoc test output, etc.) — to a
                /// dedicated directory.  The common use case is moving
                /// temp traffic off a tmpfs-backed @c /tmp onto a
                /// disk-backed mount so large scratch files don't
                /// consume RAM.  Set via code:
                ///
                /// @code
                /// LibraryOptions::instance().set(
                ///     LibraryOptions::TempDir,
                ///     String("/mnt/data/tmp/promeki"));
                /// @endcode
                ///
                /// ...or via the environment:
                ///
                /// @code
                /// export PROMEKI_OPT_TempDir=/mnt/data/tmp/promeki
                /// @endcode
                ///
                /// The override is returned verbatim — the directory
                /// is not auto-created.  Callers that require the
                /// directory to exist should @ref Dir::mkpath the
                /// result themselves.
                PROMEKI_DECLARE_ID(TempDir,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription(
                                        "Override for Dir::temp() (empty = OS default)."));

                /// @brief String — override for the path returned by
                /// @ref Dir::ipc (empty = platform default, which is
                /// @c /dev/shm/promeki on Linux — a tmpfs location well
                /// suited to both shared-memory objects and named
                /// @c AF_UNIX sockets — and @ref Dir::temp elsewhere).
                ///
                /// The IPC directory is where cross-process primitives
                /// (shared memory regions, local-socket files, lock
                /// files) live.  Set this option to pin the path for
                /// every IPC-consuming feature in the library in one
                /// place.  Common use cases:
                ///
                ///  - Move IPC traffic off a read-only or restricted
                ///    @c /dev/shm onto a writable partition.
                ///  - Isolate test runs by pointing at a per-test
                ///    directory.
                ///  - Align with a site's /run layout for cross-user
                ///    IPC: create a group-owned directory at
                ///    deployment time and point every process at it.
                ///
                /// Set via code:
                ///
                /// @code
                /// LibraryOptions::instance().set(
                ///     LibraryOptions::IpcDir,
                ///     String("/run/promeki/ipc"));
                /// @endcode
                ///
                /// ...or via the environment:
                ///
                /// @code
                /// export PROMEKI_OPT_IpcDir=/run/promeki/ipc
                /// @endcode
                ///
                /// The override is returned verbatim — the directory
                /// is not auto-created.  Callers that require the
                /// directory to exist should @ref Dir::mkpath the
                /// result themselves.
                PROMEKI_DECLARE_ID(IpcDir,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription(
                                        "Override for Dir::ipc() (empty = platform default)."));

                // ============================================================
                // Termination signal handling
                // ============================================================

                /// @brief bool — install termination signal handlers
                /// (SIGINT/SIGTERM/SIGHUP/SIGQUIT on POSIX,
                /// CTRL_C_EVENT/CTRL_BREAK_EVENT/... on Windows).
                /// Default @c true.  When enabled, the @ref Application
                /// constructor wires a dedicated signal-waiting thread
                /// that turns those signals into @ref Application::quit
                /// calls plus a wake-up posted to the main
                /// @ref EventLoop.
                PROMEKI_DECLARE_ID(TerminationSignalHandler,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Install termination signal handlers (Ctrl-C/kill)."));

                /// @brief bool — force-exit on a second termination signal
                /// delivery (default @c true).  When true, the first
                /// Ctrl-C asks the application to quit cleanly and a
                /// second Ctrl-C escalates to @c std::_Exit.  Set to
                /// @c false for command-line tools that want Ctrl-C
                /// to take effect on the very first delivery, or for
                /// applications that want to handle multiple signals
                /// themselves.
                PROMEKI_DECLARE_ID(SignalDoubleTapExit,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Force-exit on second termination signal delivery."));

                /**
                 * @brief Returns the global LibraryOptions singleton.
                 *
                 * The instance is default-constructed on first access, with
                 * every declared ID set to its spec default.
                 *
                 * @return A reference to the singleton.
                 */
                static LibraryOptions &instance();

                /**
                 * @brief Populates options from @c PROMEKI_OPT_* environment variables.
                 *
                 * Scans the process environment for variables whose name begins
                 * with @c PROMEKI_OPT_, strips the prefix, and attempts to
                 * resolve the remainder as a registered option name.  The value
                 * string is type-coerced via the option's @ref VariantSpec and
                 * merged into this database.  Unrecognised names are logged as
                 * warnings and skipped.
                 */
                void loadFromEnvironment();
};

PROMEKI_NAMESPACE_END
