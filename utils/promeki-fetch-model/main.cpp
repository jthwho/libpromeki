/**
 * @file      main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * promeki-fetch-model — downloads ML model files into Dir::models()
 * with optional SHA-256 verification.  Currently knows the canonical
 * Hugging Face URLs for the whisper.cpp GGML weights served at
 * https://huggingface.co/ggerganov/whisper.cpp/tree/main; the
 * @c WhisperTranscriptionEngine backend looks for them under
 * @c Dir::models()/whisper/ggml-<name>.bin.
 *
 * Usage:
 *
 *   promeki-fetch-model <name>            Fetch the named model.
 *   promeki-fetch-model --list            Print the model catalog.
 *   promeki-fetch-model <name> -d <dir>   Override destination directory.
 *   promeki-fetch-model <name> --force    Re-download even if the file
 *                                         already exists at the
 *                                         expected size.
 *   promeki-fetch-model <name> --no-verify  Skip the SHA-256 check
 *                                         when the catalog has a hash.
 *
 * The download streams directly to the destination file via
 * @ref HttpRequest::setBodySink, so multi-GB weights do not buffer
 * in memory and the SHA-256 is computed incrementally as bytes flow
 * past.  Pressing Ctrl-C cancels the in-flight transfer cleanly
 * (the progress callback returns false on @ref Application::shouldQuit,
 * which trips an @ref Error::Cancelled finish on the HttpClient) and
 * removes the partial on-disk file.
 */

#include <cstdio>
#include <cstdlib>

#include <promeki/application.h>
#include <promeki/buffer.h>
#include <promeki/cmdlineparser.h>
#include <promeki/dir.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <promeki/future.h>
#include <promeki/httpclient.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/sha2.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/timestamp.h>

using namespace promeki;

namespace {

// SubDir under Dir::models() that the engine looks for.  Keep this in
// sync with WhisperTranscriptionEngine::resolveModelPath.
constexpr const char *kWhisperSubDir = "whisper";

// A single entry in the model catalog.  `name` is the short identifier
// the user passes on the command line ("small", "large-v3-q5_0", etc.)
// and the engine accepts as MediaConfig::TranscriptionModelHint.
// `filename` is the actual filename on disk (always "ggml-<name>.bin")
// — kept explicit so future non-whisper models can use a different
// convention without touching the resolver.  `sha256Hex` is optional;
// empty means "no hash check available, trust HTTPS transport
// integrity".
struct CatalogEntry {
                const char *name;
                const char *filename;
                const char *url;
                const char *sha256Hex;
};

// Subset of the whisper.cpp model zoo that's broadly useful.  Add more
// rows as needed; the engine resolves any bare hint passed via
// TranscriptionModelHint against `Dir::models()/whisper/ggml-<hint>.bin`
// so users can pre-stage a model not listed here without touching the
// catalog.  SHA-256 hashes are left empty on the first cut — we rely on
// HTTPS transport integrity until we have a way to mirror the upstream
// hashes (whisper.cpp's own download script doesn't carry them either).
const CatalogEntry kCatalog[] = {
        // SHA-256 hashes captured from a verified download via this
        // tool against ggerganov/whisper.cpp@main on 2026-05-25.
        // Entries with an empty hash skip verification — fill in as
        // the upstream model lands and we verify it.
        {"tiny",
         "ggml-tiny.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin",
         "be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21"},
        {"tiny.en",
         "ggml-tiny.en.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin",
         ""},
        {"base",
         "ggml-base.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin",
         ""},
        {"base.en",
         "ggml-base.en.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin",
         ""},
        {"small",
         "ggml-small.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin",
         ""},
        {"small.en",
         "ggml-small.en.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin",
         ""},
        {"medium",
         "ggml-medium.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin",
         ""},
        {"medium.en",
         "ggml-medium.en.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.en.bin",
         ""},
        {"large-v3",
         "ggml-large-v3.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3.bin",
         ""},
        {"large-v3-q5_0",
         "ggml-large-v3-q5_0.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-q5_0.bin",
         ""},
        {"large-v3-turbo",
         "ggml-large-v3-turbo.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo.bin",
         ""},
        {"large-v3-turbo-q5_0",
         "ggml-large-v3-turbo-q5_0.bin",
         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin",
         ""},
};

const CatalogEntry *findModel(const String &name) {
        for (const CatalogEntry &e : kCatalog) {
                if (name == e.name) return &e;
        }
        return nullptr;
}

void printUsage() {
        std::printf(
                "Usage: promeki-fetch-model <name> [-d <dir>] [--force] [--no-verify]\n"
                "       promeki-fetch-model --list\n"
                "\n"
                "  -d, --dest <dir>   Override destination directory.  Default is\n"
                "                     Dir::models()/whisper/ — set\n"
                "                     LibraryOptions::ModelsDir (or the\n"
                "                     PROMEKI_OPT_ModelsDir env var) to change\n"
                "                     where Dir::models() points.\n"
                "      --force        Re-download even if the target file already\n"
                "                     exists.\n"
                "      --no-verify    Skip SHA-256 verification when the catalog\n"
                "                     entry carries a hash.\n"
                "      --list         Print the model catalog and exit.\n"
                "  -h, --help         Print this help.\n"
                "\n"
                "Models are fetched from huggingface.co/ggerganov/whisper.cpp.\n");
}

void printCatalog() {
        std::printf("Available models (download dest: %s/%s):\n",
                    Dir::models().path().toString().cstr(),
                    kWhisperSubDir);
        for (const CatalogEntry &e : kCatalog) {
                std::printf("  %-22s %s\n", e.name, e.filename);
        }
}

// Formats a byte count as a human-readable string, e.g. "2.34 GiB".
// Kept local for now — if a second tool needs the same thing we'll
// promote it into String::humanBytes() or similar.
String formatBytes(int64_t bytes) {
        if (bytes < 0) return String("?");
        const double v = static_cast<double>(bytes);
        if (v >= 1024.0 * 1024.0 * 1024.0)
                return String::sprintf("%.2f GiB", v / (1024.0 * 1024.0 * 1024.0));
        if (v >= 1024.0 * 1024.0) return String::sprintf("%.2f MiB", v / (1024.0 * 1024.0));
        if (v >= 1024.0) return String::sprintf("%.2f KiB", v / 1024.0);
        return String::sprintf("%lld B", static_cast<long long>(bytes));
}

// Renders the live progress line in place via a carriage return.
// The trailing spaces overwrite the previous (potentially longer)
// line — without them you get cruft from "100.0%" overlapping a
// shorter "  4.1%" earlier in the run.
void renderProgress(int64_t received, int64_t total, double bytesPerSec) {
        const String got = formatBytes(received);
        const String tot = (total > 0) ? formatBytes(total) : String("?");
        const String rate = String::sprintf("%s/s", formatBytes(static_cast<int64_t>(bytesPerSec)).cstr());
        if (total > 0) {
                const double pct = 100.0 * static_cast<double>(received) / static_cast<double>(total);
                std::fprintf(stderr, "\r  %5.1f%%   %s / %s   %s            ", pct,
                             got.cstr(), tot.cstr(), rate.cstr());
        } else {
                std::fprintf(stderr, "\r  %s   %s            ", got.cstr(), rate.cstr());
        }
        std::fflush(stderr);
}

// Pumps the EventLoop until `fut` is ready.  HttpClient::send returns
// a Future whose backing Promise is fulfilled from a callback posted
// to the EventLoop, so simply calling Future::result without pumping
// would deadlock forever.  A short tick keeps the loop responsive to
// the SIGINT-driven QuitItem even when the download itself is idle.
Result<HttpResponse> waitForResponse(Future<HttpResponse> fut, EventLoop *loop) {
        while (!fut.isReady()) {
                loop->processEvents(EventLoop::WaitForMore, 100);
        }
        return fut.result();
}

// Synchronously downloads `displayName`'s contents from `initialUrl`,
// streaming bytes straight into `dest` while updating a SHA-256 hasher
// and a TTY progress line.  HttpClient follows the HF→CDN signed-URL
// redirect chain on its own (DefaultMaxRedirects), so the sink only
// sees the final 2xx bytes.  Cancels cleanly on SIGINT (the progress
// callback returns false once Application::shouldQuit observes the
// quit request) and removes the partial file when the transfer
// doesn't complete.
Error downloadTo(const String &displayName, const String &initialUrl, const FilePath &dest,
                 const String &expectedSha256Hex, bool skipVerify, EventLoop *loop) {
        // Open the destination file up-front so we fail fast on
        // permission / disk-full errors before the network request.
        // Truncate is important — a prior failed run may have left
        // a partial file at this path.
        File file(dest);
        Error fileErr = file.open(File::WriteOnly, File::Create | File::Truncate);
        if (fileErr.isError()) {
                std::fprintf(stderr, "  failed to open %s for writing: %s\n",
                             dest.toString().cstr(), fileErr.name().cstr());
                return fileErr;
        }

        Sha256 hasher;
        bool   hashNeeded = !skipVerify && !expectedSha256Hex.isEmpty();

        // Rate-limit the progress redraw so the printf stream doesn't
        // become its own bottleneck on fast downloads — 10 Hz is
        // smooth enough to read and rare enough not to matter.
        const TimeStamp  startTime = TimeStamp::now();
        TimeStamp        lastDraw = startTime;
        const Duration   drawInterval = Duration::fromMilliseconds(100);

        // Captured-by-reference state for the streaming callbacks.
        int64_t totalReceived = 0;
        Error   writeErr;

        HttpClient client;
        client.setSslContext(SslContext()); // default = system CA bundle
        client.setMaxBodyBytes(static_cast<int64_t>(8) * 1024 * 1024 * 1024);
        client.setTimeoutMs(0); // no timeout — large downloads can take a while
        // Hugging Face's CDN (xethub) returns 401 for requests with
        // no User-Agent.  Set one early so every redirect inherits it.
        client.setDefaultHeader("User-Agent", "promeki-fetch-model/1.0");

        std::fprintf(stderr, "Fetching %s ...\n", displayName.cstr());

        HttpRequest req;
        req.setMethod(HttpMethod::Get);
        req.setUrl(Url::fromString(initialUrl).first());

        // Streaming sink: write directly to disk + feed the SHA-256
        // hasher.  Returning a non-Ok Error here trips an early
        // Pending::finish that surfaces the same code on the Future,
        // so the user sees the disk error (NoSpace, etc.) instead of
        // a generic "connection closed unexpectedly".
        req.setBodySink([&](const void *data, size_t len) -> Error {
                int64_t wrote = file.write(data, static_cast<int64_t>(len));
                if (wrote != static_cast<int64_t>(len)) {
                        writeErr = Error::IOError;
                        return Error::IOError;
                }
                if (hashNeeded) hasher.update(data, len);
                return Error::Ok;
        });

        // Progress callback: redraws the live status line and honors
        // Application::shouldQuit (which the SignalHandler sets on
        // SIGINT/SIGTERM).  Returning false cancels the request with
        // Error::Cancelled; the partial file is removed below.
        req.setProgressCallback([&](int64_t received, int64_t total) -> bool {
                totalReceived = received;
                if (Application::shouldQuit()) return false;
                const TimeStamp now = TimeStamp::now();
                if (received == 0 || (now - lastDraw) >= drawInterval ||
                    (total > 0 && received >= total)) {
                        const Duration elapsed = now - startTime;
                        const double   secs = elapsed.toSecondsDouble();
                        const double   rate = secs > 0.0 ? static_cast<double>(received) / secs : 0.0;
                        renderProgress(received, total, rate);
                        lastDraw = now;
                }
                return true;
        });

        Future<HttpResponse> fut = client.send(req);
        Result<HttpResponse> res = waitForResponse(std::move(fut), loop);

        // Move past the in-place progress line before printing trailer.
        std::fprintf(stderr, "\n");

        if (error(res).isError()) {
                std::fprintf(stderr, "  HTTP request failed: %s\n", error(res).name().cstr());
                Error closeErr = file.close();
                (void)closeErr;
                Dir(dest).remove();
                return error(res);
        }
        const HttpResponse &resp = value(res);
        if (!resp.status().isSuccess()) {
                std::fprintf(stderr, "  HTTP status %d (expected 2xx)\n", resp.status().value());
                Error closeErr = file.close();
                (void)closeErr;
                Dir(dest).remove();
                return Error::ProtocolError;
        }

        Error closeErr = file.close();
        if (writeErr.isError()) {
                std::fprintf(stderr, "  write to %s failed: %s\n", dest.toString().cstr(),
                             writeErr.name().cstr());
                Dir(dest).remove();
                return writeErr;
        }
        if (closeErr.isError()) {
                std::fprintf(stderr, "  close of %s failed: %s\n", dest.toString().cstr(),
                             closeErr.name().cstr());
                Dir(dest).remove();
                return closeErr;
        }

        std::fprintf(stderr, "  downloaded %s (%lld bytes)\n",
                     formatBytes(totalReceived).cstr(), static_cast<long long>(totalReceived));

        if (hashNeeded) {
                SHA256Digest digest = hasher.finalize();
                String       hex = digest.toHexString();
                if (hex != expectedSha256Hex) {
                        std::fprintf(stderr,
                                     "  SHA-256 mismatch:\n    expected: %s\n    got:      %s\n",
                                     expectedSha256Hex.cstr(), hex.cstr());
                        Dir(dest).remove();
                        return Error::CorruptData;
                }
                std::fprintf(stderr, "  SHA-256 verified.\n");
        } else if (expectedSha256Hex.isEmpty()) {
                std::fprintf(stderr, "  (no catalog hash to verify against; trusting HTTPS)\n");
        }

        std::fprintf(stderr, "  wrote %s\n", dest.toString().cstr());
        return Error::Ok;
}

} // namespace

int main(int argc, char *argv[]) {
        // HttpClient needs an EventLoop on the current thread to drive
        // async I/O; Application adopts the main thread + sets up that
        // loop in its constructor, and also installs the SignalHandler
        // that turns SIGINT/SIGTERM into Application::quit().
        Application app(argc, argv);

        bool   wantList = false;
        bool   force = false;
        bool   skipVerify = false;
        bool   wantHelp = false;
        String destOverride;

        CmdLineParser parser;
        parser.registerOptions({
                {'h', "help", "Print usage and exit",
                 CmdLineParser::OptionCallback([&]() { wantHelp = true; return 0; })},
                {0, "list", "Print model catalog and exit",
                 CmdLineParser::OptionCallback([&]() { wantList = true; return 0; })},
                {'d', "dest", "Override destination directory",
                 CmdLineParser::OptionStringCallback(
                         [&](const String &s) { destOverride = s; return 0; })},
                {0, "force", "Re-download even if target file exists",
                 CmdLineParser::OptionCallback([&]() { force = true; return 0; })},
                {0, "no-verify", "Skip SHA-256 verification",
                 CmdLineParser::OptionCallback([&]() { skipVerify = true; return 0; })},
        });
        int rc = parser.parseMain(argc, argv);
        if (rc != 0) {
                printUsage();
                return rc;
        }
        if (wantHelp) {
                printUsage();
                return 0;
        }
        if (wantList) {
                printCatalog();
                return 0;
        }
        if (parser.argCount() < 1) {
                std::fprintf(stderr, "promeki-fetch-model: missing <name> argument\n\n");
                printUsage();
                return 2;
        }
        const String name = parser.arg(0);
        const CatalogEntry *entry = findModel(name);
        if (entry == nullptr) {
                std::fprintf(stderr, "promeki-fetch-model: no model named '%s'.  Available:\n",
                             name.cstr());
                for (const CatalogEntry &e : kCatalog) {
                        std::fprintf(stderr, "  %s\n", e.name);
                }
                return 2;
        }

        // Resolve destination directory: explicit -d overrides the
        // library convention.  Either way, mkpath ensures the
        // intermediate directories exist.
        Dir destDir;
        if (!destOverride.isEmpty()) {
                destDir = Dir(FilePath(destOverride));
        } else {
                destDir = Dir(Dir::models().path() / kWhisperSubDir);
        }
        Error mkerr = destDir.mkpath();
        if (mkerr.isError()) {
                std::fprintf(stderr, "promeki-fetch-model: failed to create %s: %s\n",
                             destDir.path().toString().cstr(), mkerr.name().cstr());
                return 1;
        }
        FilePath destFile = destDir.path() / entry->filename;

        if (destFile.exists() && !force) {
                std::printf("promeki-fetch-model: %s already exists (use --force to redownload).\n",
                            destFile.toString().cstr());
                return 0;
        }

        std::fprintf(stderr, "Model:       %s\n", entry->name);
        std::fprintf(stderr, "File:        %s\n", entry->filename);
        std::fprintf(stderr, "Destination: %s\n", destFile.toString().cstr());

        Error err = downloadTo(String(entry->name), entry->url, destFile,
                               String(entry->sha256Hex), skipVerify, Application::mainEventLoop());
        if (err == Error::Cancelled) {
                std::fprintf(stderr, "promeki-fetch-model: cancelled.\n");
                return 130; // 128 + SIGINT, conventional shell exit code
        }
        if (err.isError()) {
                std::fprintf(stderr, "promeki-fetch-model: download failed: %s\n", err.name().cstr());
                return 1;
        }
        std::fprintf(stderr, "promeki-fetch-model: done.\n");
        return 0;
}
