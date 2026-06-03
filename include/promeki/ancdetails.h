/**
 * @file      ancdetails.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/enums_anc.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Fully-decoded, human-readable analysis of one @ref AncPacket.
 * @ingroup proav
 *
 * The output of the @ref AncTranslator "Details" decode path
 * (@ref AncTranslator::details).  Where @ref AncTranslator::parse yields
 * a machine-typed @ref Variant and @ref AncTranslator::describe yields a
 * one-line summary, an @c AncDetails is the verbose, inspector-oriented
 * form: a list of human-readable lines that fully spell out every wire
 * field — enumerated values rendered as their names rather than raw
 * hex (e.g. @c "Payload = VITC1" instead of @c "Payload = 0x1") — plus
 * a list of warnings and errors describing anything suspect about the
 * packet.
 *
 * Unlike the parse / build path, a Details decode @em always produces a
 * value: a packet that fails to decode still yields an @c AncDetails
 * carrying whatever header fields could be recovered plus an
 * @ref AncDetailSeverity::Error issue explaining the failure.  This makes
 * it the right tool for a packet inspector that must render @em something
 * for every packet it is handed.
 *
 * @par Structure
 *
 *  - @ref lines — ordered, fully-decoded field lines.  Conventionally
 *    @c "Name = Value" pairs (use @ref addField), but free-form lines
 *    (section headers, raw hex dumps) are allowed via @ref addLine.
 *  - @ref issues — zero or more @ref Issue records, each a
 *    @ref AncDetailSeverity plus a message.  An inspector can filter or
 *    colour-code on severity without parsing the message text.
 *
 * The class is plain-value: copies are independent and there is no
 * internal shared pointer.
 *
 * @par Example
 * @code
 * AncTranslator t;
 * AncDetails d = t.details(pkt);
 * for (const String &line : d.lines()) print(line);
 * if (d.hasErrors()) print("packet did not fully decode");
 * @endcode
 *
 * @par Thread Safety
 * Plain value type.  Distinct instances may be used concurrently;
 * concurrent mutation of a single instance is not synchronised.
 *
 * @see AncTranslator::details, AncDetailSeverity
 */
class AncDetails {
        public:
                /**
                 * @brief One diagnostic note — a severity plus a
                 *        human-readable message.
                 *
                 * Raised by a detailer when it wants to flag something
                 * about the packet: a standards-conformance divergence
                 * (@ref AncDetailSeverity::Warning), a decode failure
                 * (@ref AncDetailSeverity::Error), or a non-defect
                 * observation worth surfacing (@ref AncDetailSeverity::Info).
                 */
                struct Issue {
                                /** @brief How serious the note is. */
                                AncDetailSeverity severity;

                                /** @brief Human-readable description of the note. */
                                String message;

                                /** @brief Default-constructs an Info issue with an empty message. */
                                Issue() : severity(AncDetailSeverity::Info) {}

                                /** @brief Constructs from a severity and message. */
                                Issue(const AncDetailSeverity &sev, const String &msg)
                                        : severity(sev), message(msg) {}

                                /** @brief Field-wise equality. */
                                bool operator==(const Issue &o) const {
                                        return severity == o.severity && message == o.message;
                                }

                                /** @brief Inequality. */
                                bool operator!=(const Issue &o) const { return !(*this == o); }
                };

                /** @brief List of @ref Issue records. */
                using IssueList = List<Issue>;

                /** @brief Default-constructs an empty AncDetails (no lines, no issues). */
                AncDetails() = default;

                // -- Decoded lines ----------------------------------------

                /** @brief Returns the ordered list of fully-decoded field lines. */
                const StringList &lines() const { return _lines; }

                /** @brief Appends one free-form decoded line verbatim. */
                void addLine(const String &line) { _lines.pushToBack(line); }

                /**
                 * @brief Appends a @c "name = value" decoded line.
                 *
                 * The canonical way a detailer spells out one wire field.
                 * @p value should already be in its human-readable form —
                 * enumerated values rendered as their names, not raw hex.
                 *
                 * @param name  The field label (e.g. @c "Payload").
                 * @param value The decoded value (e.g. @c "VITC1").
                 */
                void addField(const String &name, const String &value) {
                        _lines.pushToBack(name + " = " + value);
                }

                // -- Issues -----------------------------------------------

                /** @brief Returns the list of diagnostic issues. */
                const IssueList &issues() const { return _issues; }

                /** @brief Appends an issue with the given severity and message. */
                void addIssue(const AncDetailSeverity &severity, const String &message) {
                        _issues.pushToBack(Issue(severity, message));
                }

                /** @brief Appends an @ref AncDetailSeverity::Info issue. */
                void addInfo(const String &message) { addIssue(AncDetailSeverity::Info, message); }

                /** @brief Appends an @ref AncDetailSeverity::Warning issue. */
                void addWarning(const String &message) { addIssue(AncDetailSeverity::Warning, message); }

                /** @brief Appends an @ref AncDetailSeverity::Error issue. */
                void addError(const String &message) { addIssue(AncDetailSeverity::Error, message); }

                /** @brief Returns the number of issues at exactly @p severity. */
                size_t issueCount(const AncDetailSeverity &severity) const;

                /** @brief Returns @c true when at least one @ref AncDetailSeverity::Warning issue is present. */
                bool hasWarnings() const { return issueCount(AncDetailSeverity::Warning) > 0; }

                /** @brief Returns @c true when at least one @ref AncDetailSeverity::Error issue is present. */
                bool hasErrors() const { return issueCount(AncDetailSeverity::Error) > 0; }

                /** @brief Returns @c true when there are no lines and no issues. */
                bool isEmpty() const { return _lines.isEmpty() && _issues.isEmpty(); }

                // -- Comparison -------------------------------------------

                /** @brief Field-wise equality (lines and issues both compared in order). */
                bool operator==(const AncDetails &o) const {
                        return _lines == o._lines && _issues == o._issues;
                }

                /** @brief Inequality. */
                bool operator!=(const AncDetails &o) const { return !(*this == o); }

                // -- Rendering --------------------------------------------

                /**
                 * @brief Renders the whole analysis as a multi-line String.
                 *
                 * Each decoded line on its own row, followed by each issue
                 * prefixed with its severity (e.g.
                 * @c "[Warning] ST 2016-3 mandates DC=8; received DC=4").
                 * Intended for CLI dumps and logs.
                 */
                String toString() const;

                /** @brief Returns a structured JSON representation (@c lines array + @c issues array). */
                JsonObject toJson() const;

        private:
                StringList _lines;
                IssueList  _issues;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
