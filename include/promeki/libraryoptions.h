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
 * @brief Phantom tag for the library-wide options @ref VariantDatabase.
 * @ingroup util
 */
struct LibraryOptionsTag {};

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
 * @par Example
 * @code
 * // Override an option before Application construction
 * LibraryOptions::instance().set(LibraryOptions::CoreDumps, true);
 *
 * // Or via the environment:
 * //   export PROMEKI_OPT_CoreDumps=true
 * @endcode
 */
class LibraryOptions : public VariantDatabase<LibraryOptionsTag> {
        public:
                /** @brief Base class alias. */
                using Base = VariantDatabase<LibraryOptionsTag>;

                using Base::Base;

                // ============================================================
                // Crash handling
                // ============================================================

                /// @brief bool — install crash signal handlers (default true).
                static inline const ID CrashHandler = declareID("CrashHandler",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Install crash signal handlers."));

                /// @brief bool — raise RLIMIT_CORE for core dumps (default false).
                static inline const ID CoreDumps = declareID("CoreDumps",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable core dumps via RLIMIT_CORE."));

                /// @brief String — crash log directory (empty = Dir::temp()).
                static inline const ID CrashLogDir = declareID("CrashLogDir",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Crash log directory (empty = system temp)."));

                /// @brief bool — include the process environment in crash/trace
                /// reports (default true).  Disable if the environment may
                /// contain secrets you don't want written to disk.
                static inline const ID CaptureEnvironment = declareID("CaptureEnvironment",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Include environment variables in crash reports."));

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
                static inline const ID TerminationSignalHandler = declareID("TerminationSignalHandler",
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
                static inline const ID SignalDoubleTapExit = declareID("SignalDoubleTapExit",
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
