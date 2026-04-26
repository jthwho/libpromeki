/**
 * @file      benchmarkrunner.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/benchmarkrunner.h>
#include <promeki/json.h>
#include <promeki/variant.h>
#include <promeki/regex.h>
#include <promeki/datetime.h>
#include <promeki/buildinfo.h>
#include <promeki/logger.h>
#include <promeki/units.h>
#include <cstdio>
#include <cmath>
#include <exception>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Formatting helpers
// ============================================================================

namespace {

        /** @brief Formats a uint64 iteration count with thousands-group commas. */
        String formatIterCount(uint64_t n) {
                String raw = String::number(n);
                if (raw.size() <= 3) return raw;
                String out;
                size_t first = raw.size() % 3;
                if (first > 0) {
                        out += raw.left(first);
                        if (raw.size() > first) out += ",";
                }
                for (size_t i = first; i < raw.size(); i += 3) {
                        out += raw.mid(i, 3);
                        if (i + 3 < raw.size()) out += ",";
                }
                return out;
        }

        /** @brief Right-pads a string with spaces to reach @p width bytes. */
        String padRight(const String &s, size_t width) {
                if (s.size() >= width) return s;
                return s + String(width - s.size(), ' ');
        }

        /** @brief Left-pads a string with spaces to reach @p width bytes. */
        String padLeft(const String &s, size_t width) {
                if (s.size() >= width) return s;
                return String(width - s.size(), ' ') + s;
        }

        /** @brief Column alignment used by the tiny ASCII table formatter below. */
        enum class Align {
                Left,
                Right
        };

        /**
 * @brief A single ASCII table column with header, alignment, and cell strings.
 *
 * @par Usage
 * Fill out the header and cells for every column, then call `renderTable()`
 * to produce the complete text block (header row + separator + rows).
 */
        struct Column {
                        String     header;              ///< @brief Column title shown in the header row.
                        Align      align = Align::Left; ///< @brief Alignment for header and cells.
                        size_t     minWidth = 0;        ///< @brief Floor on the computed column width.
                        StringList cells;               ///< @brief Per-row cell strings, in row order.
        };

        /**
 * @brief Renders a set of Columns into a formatted text table.
 *
 * Widths are computed from the widest cell (or header) in each column.
 * A two-space gutter separates columns.  A dash-separator row of the
 * correct total width follows the header.
 *
 * @param cols Columns to render.
 * @return The complete table as a single multi-line String.
 */
        String renderTable(const List<Column> &cols) {
                if (cols.isEmpty()) return String();

                // Compute each column's display width (header + widest cell + minWidth).
                List<size_t> widths;
                size_t       rowCount = 0;
                for (const auto &c : cols) {
                        size_t w = c.header.size();
                        if (c.minWidth > w) w = c.minWidth;
                        for (const auto &cell : c.cells) {
                                if (cell.size() > w) w = cell.size();
                        }
                        widths.pushToBack(w);
                        if (c.cells.size() > rowCount) rowCount = c.cells.size();
                }

                auto format = [&](size_t col, const String &value) -> String {
                        if (cols[col].align == Align::Right) {
                                return padLeft(value, widths[col]);
                        }
                        return padRight(value, widths[col]);
                };

                String out;

                // Header row.
                for (int i = 0; i < static_cast<int>(cols.size()); i++) {
                        if (i > 0) out += "  ";
                        out += format(i, cols[i].header);
                }
                out += "\n";

                // Separator row: dashes matching the header row width exactly,
                // including the two-space gutters between columns.
                size_t total = 0;
                for (size_t i = 0; i < widths.size(); i++) {
                        if (i > 0) total += 2;
                        total += widths[i];
                }
                out += String(total, '-') + "\n";

                // Data rows.
                for (size_t r = 0; r < rowCount; r++) {
                        for (int i = 0; i < static_cast<int>(cols.size()); i++) {
                                if (i > 0) out += "  ";
                                String cell = (r < cols[i].cells.size()) ? cols[i].cells[r] : String();
                                out += format(i, cell);
                        }
                        out += "\n";
                }
                return out;
        }

} // namespace

// ============================================================================
// Case registry
// ============================================================================

static List<BenchmarkCase> &caseRegistry() {
        static List<BenchmarkCase> reg;
        return reg;
}

int BenchmarkRunner::registerCase(const BenchmarkCase &theCase) {
        List<BenchmarkCase> &reg = caseRegistry();
        int                  idx = static_cast<int>(reg.size());
        reg.pushToBack(theCase);
        return idx;
}

const List<BenchmarkCase> &BenchmarkRunner::registeredCases() {
        return caseRegistry();
}

// ============================================================================
// BenchmarkResult JSON I/O
// ============================================================================

JsonObject BenchmarkResult::toJson() const {
        JsonObject obj;
        obj.set("suite", suite);
        obj.set("name", name);
        if (!label.isEmpty()) obj.set("label", label);
        if (!description.isEmpty()) obj.set("description", description);
        obj.set("iterations", iterations);
        obj.set("repeats", repeats);

        JsonObject ns;
        ns.set("avg", avgNsPerIter);
        ns.set("min", minNsPerIter);
        ns.set("max", maxNsPerIter);
        ns.set("stddev", stddevNsPerIter);
        obj.set("ns_per_iter", ns);

        if (itemsPerSecond > 0.0) obj.set("items_per_sec", itemsPerSecond);
        if (bytesPerSecond > 0.0) obj.set("bytes_per_sec", bytesPerSecond);

        if (!custom.isEmpty()) {
                JsonObject c;
                for (const auto &[k, v] : custom) c.set(k, v);
                obj.set("custom", c);
        }

        obj.set("succeeded", succeeded);
        if (!errorMessage.isEmpty()) obj.set("error", errorMessage);
        return obj;
}

BenchmarkResult BenchmarkResult::fromJson(const JsonObject &obj, Error *err) {
        BenchmarkResult r;
        if (err) *err = Error::Ok;

        r.suite = obj.getString("suite");
        r.name = obj.getString("name");
        r.label = obj.contains("label") ? obj.getString("label") : String();
        r.description = obj.contains("description") ? obj.getString("description") : String();
        r.iterations = obj.getUInt("iterations");
        r.repeats = obj.getUInt("repeats");

        if (obj.valueIsObject("ns_per_iter")) {
                JsonObject ns = obj.getObject("ns_per_iter");
                r.avgNsPerIter = ns.getDouble("avg");
                r.minNsPerIter = ns.getDouble("min");
                r.maxNsPerIter = ns.getDouble("max");
                r.stddevNsPerIter = ns.getDouble("stddev");
        }

        if (obj.contains("items_per_sec")) r.itemsPerSecond = obj.getDouble("items_per_sec");
        if (obj.contains("bytes_per_sec")) r.bytesPerSecond = obj.getDouble("bytes_per_sec");

        if (obj.valueIsObject("custom")) {
                JsonObject c = obj.getObject("custom");
                c.forEach([&](const String &k, const Variant &v) { r.custom.insert(k, v.get<double>()); });
        }

        if (obj.contains("succeeded")) r.succeeded = obj.getBool("succeeded");
        if (obj.contains("error")) r.errorMessage = obj.getString("error");
        return r;
}

// ============================================================================
// BenchmarkRunner
// ============================================================================

BenchmarkRunner::BenchmarkRunner() = default;

bool BenchmarkRunner::matchesFilter(const BenchmarkCase &theCase) const {
        if (_filterPattern.isEmpty()) return true;
        RegEx filter(_filterPattern);
        return filter.search(theCase.fullName());
}

int BenchmarkRunner::filteredCaseCount() const {
        const List<BenchmarkCase> &cases = registeredCases();
        if (_filterPattern.isEmpty()) return static_cast<int>(cases.size());

        // Build the regex once rather than reconstructing it per case.
        RegEx filter(_filterPattern);
        int   count = 0;
        for (const auto &c : cases) {
                if (filter.search(c.fullName())) count++;
        }
        return count;
}

int64_t BenchmarkRunner::estimatedDurationMs() const {
        // Ballpark per-case cost: one warmup-sized calibration run
        // plus `repeats` measurement runs at the target window.  This
        // overestimates trivial cases (calibration resolves in a few
        // ms) and underestimates cases that take longer than one
        // iteration to hit the time budget, but it lands within a
        // factor of two for the typical case — accurate enough to
        // warn the user whether they're in for seconds or hours.
        int64_t perCase =
                static_cast<int64_t>(_warmupMs) + static_cast<int64_t>(_repeats) * static_cast<int64_t>(_minTimeMs);
        return static_cast<int64_t>(filteredCaseCount()) * perCase;
}

Error BenchmarkRunner::runAll() {
        const List<BenchmarkCase> &cases = registeredCases();

        // Precompute a fixed width for the progress "running" column so
        // each case's result time lines up vertically on screen.  The
        // width is the longest filtered case fullName — using the
        // filtered set (instead of every registered case) keeps the
        // column from being dragged out by cases that will never
        // actually run in this sweep.
        size_t width = 0;
        for (const auto &c : cases) {
                if (!matchesFilter(c)) continue;
                size_t n = c.fullName().size();
                if (n > width) width = n;
        }
        _progressNameWidth = width;

        // Aggregate error stays Ok even when a case fails — per-case status
        // is captured inside BenchmarkResult::succeeded so callers can
        // inspect individual failures without losing otherwise-good data.
        for (const auto &c : cases) {
                if (!matchesFilter(c)) continue;
                runCase(c);
        }
        _progressNameWidth = 0;
        return Error::Ok;
}

Error BenchmarkRunner::runCaseByName(const String &fullName) {
        const List<BenchmarkCase> &cases = registeredCases();
        for (const auto &c : cases) {
                if (c.fullName() == fullName) {
                        runCase(c);
                        return Error::Ok;
                }
        }
        return Error::NotExist;
}

BenchmarkResult BenchmarkRunner::runCase(const BenchmarkCase &theCase) {
        BenchmarkResult r = measureCase(theCase);
        _results.pushToBack(r);
        return r;
}

void BenchmarkRunner::runOne(const BenchmarkCase &theCase, uint64_t iterations, BenchmarkState &state) {
        (void)iterations;
        try {
                theCase.invoke(state);
        } catch (const std::exception &) {
                // Swallow — measureCase() inspects state.completed() to
                // decide whether the case ran successfully.
        } catch (...) {
                // Same as above.
        }
        state.captureIfUnfinished();
        return;
}

uint64_t BenchmarkRunner::calibrateIterations(const BenchmarkCase &theCase) {
        uint64_t iter = 1;
        double   warmupNs = static_cast<double>(_warmupMs) * 1.0e6;

        for (;;) {
                BenchmarkState state(iter);
                runOne(theCase, iter, state);

                if (!state.completed()) {
                        // Case never iterated — fall back to minIterations.
                        return _minIterations;
                }

                // Calibration uses WALL time, not effective time.
                // A case that excludes most of its body via pauseTiming()
                // still takes real wall clock to run, and that wall
                // clock is what our `minTimeMs` measurement window
                // actually spends.  If we calibrated on effective time,
                // a pauseTiming-heavy case would spin the iter counter
                // up by orders of magnitude every pass looking for
                // "elapsed" that never grows — the classic runaway
                // calibrator bug.
                double elapsed = static_cast<double>(state.wallNs());
                if (elapsed >= warmupNs || iter >= _maxIterations) {
                        // Scale to the target measurement window, guarding
                        // against degenerate elapsed values.
                        double targetNs = static_cast<double>(_minTimeMs) * 1.0e6;
                        double scale = (elapsed > 0.0) ? (targetNs / elapsed) : 1.0;
                        double scaled = static_cast<double>(iter) * scale;

                        if (scaled < static_cast<double>(_minIterations)) scaled = static_cast<double>(_minIterations);
                        if (scaled > static_cast<double>(_maxIterations)) scaled = static_cast<double>(_maxIterations);
                        if (!std::isfinite(scaled) || scaled < 1.0) scaled = 1.0;
                        return static_cast<uint64_t>(scaled);
                }

                // Grow fast when work is tiny, slower once we're in the ballpark.
                if (elapsed < warmupNs * 0.01) {
                        iter *= 100;
                } else if (elapsed < warmupNs * 0.1) {
                        iter *= 10;
                } else {
                        iter *= 2;
                }
                if (iter > _maxIterations) iter = _maxIterations;
        }
}

BenchmarkResult BenchmarkRunner::measureCase(const BenchmarkCase &theCase) {
        BenchmarkResult r;
        r.suite = theCase.suite();
        r.name = theCase.name();
        r.description = theCase.description();
        r.repeats = _repeats;

        if (_verbose) {
                // Print the case id without a trailing newline so the
                // result can flow onto the same line once the case
                // finishes running.  _progressNameWidth — set by
                // runAll() to the longest filtered case name — pads
                // short names out to a fixed column so every result
                // lines up vertically on screen.  When a caller runs a
                // case outside runAll() (e.g. runCaseByName) the width
                // is zero and the name prints at natural width.
                String fullName = theCase.fullName();
                size_t pad = 0;
                if (_progressNameWidth > fullName.size()) {
                        pad = _progressNameWidth - fullName.size();
                }
                String padding;
                for (size_t i = 0; i < pad; i++) padding += ' ';
                std::printf("  %s%s ... ", fullName.cstr(), padding.cstr());
                std::fflush(stdout);
        }

        uint64_t iter = calibrateIterations(theCase);
        r.iterations = iter;

        StatsAccumulator    perIter;
        uint64_t            lastItems = 0;
        uint64_t            lastBytes = 0;
        String              lastLabel;
        Map<String, double> lastCounters;

        for (unsigned int rep = 0; rep < _repeats; rep++) {
                BenchmarkState state(iter);
                runOne(theCase, iter, state);
                if (!state.completed()) {
                        r.succeeded = false;
                        r.errorMessage = String("Case returned without iterating");
                        if (_verbose) std::printf("FAIL: did not iterate\n");
                        return r;
                }

                double nsPerIter = static_cast<double>(state.effectiveNs()) / static_cast<double>(iter > 0 ? iter : 1);
                perIter.add(nsPerIter);

                lastItems = state.itemsProcessed();
                lastBytes = state.bytesProcessed();
                lastLabel = state.label();
                lastCounters = state.counters();
        }

        r.avgNsPerIter = perIter.mean();
        r.minNsPerIter = perIter.min();
        r.maxNsPerIter = perIter.max();
        r.stddevNsPerIter = perIter.stddev();
        r.label = lastLabel;
        r.custom = lastCounters;

        // Throughput counters use the aggregate mean: items per iteration is
        // fixed by the case, and dividing the total items processed across
        // one repeat by the mean time per repeat yields items/sec.
        if (lastItems > 0 && r.avgNsPerIter > 0.0) {
                double totalItemsPerRun = static_cast<double>(lastItems);
                double secondsPerRun = (r.avgNsPerIter * static_cast<double>(iter)) / 1.0e9;
                if (secondsPerRun > 0.0) r.itemsPerSecond = totalItemsPerRun / secondsPerRun;
        }
        if (lastBytes > 0 && r.avgNsPerIter > 0.0) {
                double totalBytesPerRun = static_cast<double>(lastBytes);
                double secondsPerRun = (r.avgNsPerIter * static_cast<double>(iter)) / 1.0e9;
                if (secondsPerRun > 0.0) r.bytesPerSecond = totalBytesPerRun / secondsPerRun;
        }

        if (_verbose) {
                // Use the same unit-scaled formatters as the final
                // table so the progress line reads in ms/us/ns rather
                // than raw nanosecond counts.  The iter count is
                // grouped with commas so 3,718 is easier to scan than
                // 3718.
                String timeStr = Units::fromDurationNs(r.avgNsPerIter);
                String iterStr = formatIterCount(iter);
                std::printf("%s  (%s iter, %u %s)\n", timeStr.cstr(), iterStr.cstr(), _repeats,
                            _repeats == 1 ? "repeat" : "repeats");
        }
        return r;
}

// ============================================================================
// JSON output
// ============================================================================

JsonObject BenchmarkRunner::toJson() const {
        JsonObject root;
        root.set("version", 2);
        root.set("date", DateTime::now().toString());

        const BuildInfo *bi = getBuildInfo();
        if (bi && bi->repoident) root.set("build", String(bi->repoident));

        JsonObject config;
        config.set("min_time_ms", static_cast<int64_t>(_minTimeMs));
        config.set("warmup_ms", static_cast<int64_t>(_warmupMs));
        config.set("repeats", static_cast<int64_t>(_repeats));
        if (!_filterPattern.isEmpty()) config.set("filter", _filterPattern);
        root.set("config", config);

        JsonArray arr;
        for (const auto &r : _results) arr.add(r.toJson());
        root.set("cases", arr);
        return root;
}

Error BenchmarkRunner::writeJson(const String &path) const {
        JsonObject root = toJson();
        String     text = root.toString(2);

        FILE *fp = std::fopen(path.cstr(), "w");
        if (!fp) return Error::OpenFailed;

        if (std::fputs(text.cstr(), fp) == EOF) {
                std::fclose(fp);
                return Error::IOError;
        }
        std::fputc('\n', fp);
        std::fclose(fp);
        return Error::Ok;
}

List<BenchmarkResult> BenchmarkRunner::loadBaseline(const String &path, Error *err) {
        List<BenchmarkResult> out;
        if (err) *err = Error::Ok;

        FILE *fp = std::fopen(path.cstr(), "r");
        if (!fp) {
                if (err) *err = Error::NotExist;
                return out;
        }
        std::fseek(fp, 0, SEEK_END);
        long len = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        String text;
        if (len > 0) {
                List<char> buf;
                buf.resize(static_cast<size_t>(len) + 1);
                size_t rd = std::fread(buf.data(), 1, static_cast<size_t>(len), fp);
                buf[rd] = '\0';
                text = String(buf.data());
        }
        std::fclose(fp);

        Error      parseErr;
        JsonObject root = JsonObject::parse(text, &parseErr);
        if (parseErr.isError()) {
                if (err) *err = Error::Invalid;
                return out;
        }

        if (!root.valueIsArray("cases")) return out;
        JsonArray arr = root.getArray("cases");
        for (int i = 0; i < arr.size(); i++) {
                JsonObject obj = arr.getObject(i);
                out.pushToBack(BenchmarkResult::fromJson(obj));
        }
        return out;
}

// ============================================================================
// Human-readable output
// ============================================================================

String BenchmarkRunner::formatTable() const {
        if (_results.isEmpty()) return String("(no results)\n");

        // Build column definitions.  The Items/s and Throughput columns
        // render `-` when the case did not set the corresponding counter,
        // so they stay in the layout even for suites that do not report
        // throughput — consistent column count is easier to scan.
        Column suiteCol;
        suiteCol.header = "Suite";
        suiteCol.align = Align::Left;
        Column nameCol;
        nameCol.header = "Case";
        nameCol.align = Align::Left;
        Column iterCol;
        iterCol.header = "Iter";
        iterCol.align = Align::Right;
        Column timeCol;
        timeCol.header = "Time";
        timeCol.align = Align::Right;
        Column stddevCol;
        stddevCol.header = "Stddev";
        stddevCol.align = Align::Right;
        Column itemsCol;
        itemsCol.header = "Items/s";
        itemsCol.align = Align::Right;
        Column rateCol;
        rateCol.header = "Throughput";
        rateCol.align = Align::Right;
        Column statusCol;
        statusCol.header = "";
        statusCol.align = Align::Left;

        bool anyStatus = false;

        for (const auto &r : _results) {
                suiteCol.cells.pushToBack(r.suite);
                nameCol.cells.pushToBack(r.name);
                iterCol.cells.pushToBack(formatIterCount(r.iterations));
                timeCol.cells.pushToBack(Units::fromDurationNs(r.avgNsPerIter));
                stddevCol.cells.pushToBack(Units::fromDurationNs(r.stddevNsPerIter));
                itemsCol.cells.pushToBack(Units::fromItemsPerSec(r.itemsPerSecond));
                rateCol.cells.pushToBack(Units::fromBytesPerSec(r.bytesPerSecond));
                if (r.succeeded) {
                        statusCol.cells.pushToBack(String());
                } else {
                        statusCol.cells.pushToBack(String("FAIL: ") + r.errorMessage);
                        anyStatus = true;
                }
        }

        List<Column> cols;
        cols.pushToBack(suiteCol);
        cols.pushToBack(nameCol);
        cols.pushToBack(iterCol);
        cols.pushToBack(timeCol);
        cols.pushToBack(stddevCol);
        cols.pushToBack(itemsCol);
        cols.pushToBack(rateCol);
        if (anyStatus) cols.pushToBack(statusCol);

        return renderTable(cols);
}

String BenchmarkRunner::formatComparison(const List<BenchmarkResult> &baseline) const {
        if (_results.isEmpty()) return String("(no current results)\n");

        // Build baseline lookup by full name so delta % can reference
        // the matching baseline entry in O(1).
        Map<String, BenchmarkResult> baseMap;
        for (const auto &b : baseline) baseMap.insert(b.suite + "." + b.name, b);

        Column suiteCol;
        suiteCol.header = "Suite";
        suiteCol.align = Align::Left;
        Column nameCol;
        nameCol.header = "Case";
        nameCol.align = Align::Left;
        Column currentCol;
        currentCol.header = "Current";
        currentCol.align = Align::Right;
        Column baseCol;
        baseCol.header = "Baseline";
        baseCol.align = Align::Right;
        Column deltaCol;
        deltaCol.header = "Delta";
        deltaCol.align = Align::Right;

        for (const auto &r : _results) {
                suiteCol.cells.pushToBack(r.suite);
                nameCol.cells.pushToBack(r.name);
                currentCol.cells.pushToBack(Units::fromDurationNs(r.avgNsPerIter));

                String fullName = r.suite + "." + r.name;
                if (baseMap.contains(fullName)) {
                        const BenchmarkResult &b = baseMap[fullName];
                        baseCol.cells.pushToBack(Units::fromDurationNs(b.avgNsPerIter));
                        if (b.avgNsPerIter > 0.0) {
                                double delta = 100.0 * (r.avgNsPerIter - b.avgNsPerIter) / b.avgNsPerIter;
                                // Prefix positive deltas with `+` so the
                                // direction is obvious at a glance.
                                String sign = delta >= 0.0 ? "+" : "";
                                deltaCol.cells.pushToBack(sign + String::number(delta, 1) + "%");
                        } else {
                                deltaCol.cells.pushToBack(String("-"));
                        }
                } else {
                        baseCol.cells.pushToBack(String("(new)"));
                        deltaCol.cells.pushToBack(String("-"));
                }
        }

        List<Column> cols;
        cols.pushToBack(suiteCol);
        cols.pushToBack(nameCol);
        cols.pushToBack(currentCol);
        cols.pushToBack(baseCol);
        cols.pushToBack(deltaCol);
        return renderTable(cols);
}

String BenchmarkRunner::formatRegisteredCases() {
        const List<BenchmarkCase> &cases = registeredCases();
        if (cases.isEmpty()) return String("(no cases registered)\n");

        Column suiteCol;
        suiteCol.header = "Suite";
        suiteCol.align = Align::Left;
        Column nameCol;
        nameCol.header = "Case";
        nameCol.align = Align::Left;
        Column descCol;
        descCol.header = "Description";
        descCol.align = Align::Left;

        for (const auto &c : cases) {
                suiteCol.cells.pushToBack(c.suite());
                nameCol.cells.pushToBack(c.name());
                descCol.cells.pushToBack(c.description());
        }

        List<Column> cols;
        cols.pushToBack(suiteCol);
        cols.pushToBack(nameCol);
        cols.pushToBack(descCol);
        return renderTable(cols);
}

PROMEKI_NAMESPACE_END
