/**
 * @file      testmedia.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "testmedia.h"

#include <promeki/buffer.h>
#include <promeki/dir.h>
#include <promeki/error.h>
#include <promeki/file.h>
#include <promeki/iodevice.h>
#include <promeki/json.h>
#include <promeki/logger.h>
#include <promeki/result.h>

#include <cstdlib>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                // Reads @p path entirely into a String, returning an
                // empty String + diagnostic on any I/O error.  The
                // testmedia index is a single small JSON blob (tens
                // of kB), so a slurp-then-parse path is fine — no
                // streaming needed.
                String readWholeFile(const String &path, String *outErr) {
                        File f(path);
                        if (f.open(IODevice::ReadOnly).isError()) {
                                if (outErr) *outErr = String("cannot open '") + path + String("'");
                                return String();
                        }
                        Result<int64_t> szR = f.size();
                        if (szR.second().isError()) {
                                f.close();
                                if (outErr) *outErr = String("cannot stat '") + path + String("'");
                                return String();
                        }
                        const int64_t sz = szR.first();
                        if (sz <= 0) {
                                f.close();
                                return String();
                        }
                        Buffer buf(static_cast<size_t>(sz));
                        buf.setSize(static_cast<size_t>(sz));
                        f.read(buf.data(), sz);
                        f.close();
                        return String::fromUtf8(static_cast<const char *>(buf.data()), static_cast<size_t>(sz));
                }

                // True when @p root looks like a populated testmedia
                // tree: it has an @c index.json sitting at its root.
                // Cheaper than fully parsing the index — we only need a
                // yes/no for the discovery search.
                bool isTestMediaRoot(const FilePath &root) {
                        if (root.toString().isEmpty()) return false;
                        FilePath idx = root / String("index.json");
                        return idx.exists();
                }

                StringList stringListFromJsonArray(const JsonArray &arr) {
                        StringList out;
                        for (int i = 0; i < arr.size(); ++i) {
                                out.pushToBack(arr.getString(i));
                        }
                        return out;
                }

        } // namespace

        FilePath resolveTestMediaRoot(const String &cliOverride) {
                // 1. Explicit --testmedia / -m wins outright.
                if (!cliOverride.isEmpty()) {
                        FilePath p(cliOverride);
                        if (isTestMediaRoot(p)) return p;
                        // Caller asked for a specific path; honour it even
                        // if it doesn't look like a corpus so the per-case
                        // load failure surfaces with a useful diagnostic.
                        return p;
                }
                // 2. PROMEKI_TESTMEDIA environment override.
                const char *env = std::getenv("PROMEKI_TESTMEDIA");
                if (env != nullptr && env[0] != '\0') {
                        FilePath p((String(env)));
                        if (isTestMediaRoot(p)) return p;
                        return p;
                }
                // 3. The in-tree symlink that the libpromeki checkout
                //    ships with.  PROMEKI_SOURCE_DIR is the compile-
                //    definition the captions suite already relies on; we
                //    piggyback on it so the default path stays in sync
                //    with the source tree.
                FilePath def = FilePath(String(PROMEKI_SOURCE_DIR)) / String("testmedia");
                if (isTestMediaRoot(def)) return def;
                return FilePath();
        }

        bool loadTestMediaIndex(const FilePath &root, List<TestMediaEntry> &outEntries, String *outErrMsg) {
                outEntries.clear();
                if (root.toString().isEmpty()) {
                        if (outErrMsg) *outErrMsg = String("testmedia root is empty");
                        return false;
                }
                FilePath idxPath = root / String("index.json");
                if (!idxPath.exists()) {
                        if (outErrMsg)
                                *outErrMsg = String("no index.json under '") + root.toString() + String("'");
                        return false;
                }

                String ioErr;
                String text = readWholeFile(idxPath.toString(), &ioErr);
                if (text.isEmpty()) {
                        if (outErrMsg) *outErrMsg = ioErr.isEmpty() ? String("empty index.json") : ioErr;
                        return false;
                }

                Error      pErr;
                JsonObject indexObj = JsonObject::parse(text, &pErr);
                if (pErr.isError()) {
                        if (outErrMsg)
                                *outErrMsg = String("index.json parse failed: ") + pErr.desc();
                        return false;
                }

                JsonArray files = indexObj.getArray(String("files"));
                if (files.size() == 0) {
                        // Not strictly an error — an empty corpus is
                        // valid, the runner just won't register any
                        // data-driven cases.  We still log it so a
                        // mis-populated tree shows up in the runner's
                        // log even if no test ever fires.
                        promekiInfo("testmedia: index has zero files (path='%s')",
                                    idxPath.toString().cstr());
                        return true;
                }

                for (int i = 0; i < files.size(); ++i) {
                        JsonObject row = files.getObject(i);
                        if (row.isEmpty()) continue;

                        TestMediaEntry e;
                        const String relPath = row.getString(String("path"));
                        if (relPath.isEmpty()) continue;
                        e.relPath = FilePath(relPath);
                        e.path = root / relPath;
                        e.mediaType = row.getString(String("mediaType"));
                        e.useCases = stringListFromJsonArray(row.getArray(String("useCases")));
                        e.title = row.getString(String("title"));
                        e.description = row.getString(String("description"));
                        e.tags = stringListFromJsonArray(row.getArray(String("tags")));
                        if (row.contains(String("inLfs"))) {
                                e.inLfs = row.getBool(String("inLfs"));
                        }
                        if (row.contains(String("bytes"))) {
                                e.bytes = row.getInt(String("bytes"));
                        }

                        JsonArray expects = row.getArray(String("expectedOutputs"));
                        for (int j = 0; j < expects.size(); ++j) {
                                JsonObject xrow = expects.getObject(j);
                                if (xrow.isEmpty()) continue;
                                TestMediaExpectedOutput x;
                                x.type = xrow.getString(String("type"));
                                String xrel = xrow.getString(String("path"));
                                if (!xrel.isEmpty()) x.path = root / xrel;
                                x.description = xrow.getString(String("description"));
                                x.tool = xrow.getString(String("tool"));
                                e.expectedOutputs.pushToBack(x);
                        }

                        outEntries.pushToBack(e);
                }

                return true;
        }

        List<TestMediaEntry> filterByUseCase(const List<TestMediaEntry> &all, const String &useCase) {
                List<TestMediaEntry> out;
                for (size_t i = 0; i < all.size(); ++i) {
                        const StringList &uc = all[i].useCases;
                        for (size_t j = 0; j < uc.size(); ++j) {
                                if (uc[j] == useCase) {
                                        out.pushToBack(all[i]);
                                        break;
                                }
                        }
                }
                return out;
        }

        TestMediaExpectedOutput findExpectedOutput(const TestMediaEntry &entry, const String &type) {
                const String wantLc = type.toLower();
                for (size_t i = 0; i < entry.expectedOutputs.size(); ++i) {
                        if (entry.expectedOutputs[i].type.toLower() == wantLc) {
                                return entry.expectedOutputs[i];
                        }
                }
                return TestMediaExpectedOutput();
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
