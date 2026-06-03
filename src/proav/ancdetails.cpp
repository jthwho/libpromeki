/**
 * @file      ancdetails.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Implements the rendering helpers for @ref AncDetails — the verbose,
 * fully-decoded analysis form produced by @ref AncTranslator::details.
 */

#include <promeki/ancdetails.h>

PROMEKI_NAMESPACE_BEGIN

size_t AncDetails::issueCount(const AncDetailSeverity &severity) const {
        size_t n = 0;
        for (const Issue &i : _issues) {
                if (i.severity == severity) ++n;
        }
        return n;
}

String AncDetails::toString() const {
        StringList rows;
        for (const String &line : _lines) {
                rows.pushToBack(line);
        }
        for (const Issue &i : _issues) {
                rows.pushToBack(String("[") + i.severity.valueName() + "] " + i.message);
        }
        return rows.join("\n");
}

JsonObject AncDetails::toJson() const {
        JsonObject obj;

        JsonArray lines;
        for (const String &line : _lines) {
                lines.add(line);
        }
        obj.set("lines", lines);

        JsonArray issues;
        for (const Issue &i : _issues) {
                JsonObject o;
                o.set("severity", i.severity.valueName());
                o.set("message", i.message);
                issues.add(o);
        }
        obj.set("issues", issues);

        return obj;
}

PROMEKI_NAMESPACE_END
