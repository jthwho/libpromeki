/**
 * @file      process.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/process.h>

using namespace promeki;

TEST_CASE("Process") {

        SUBCASE("Default construction") {
                Process p;
                CHECK(p.program().isEmpty());
                CHECK(p.arguments().isEmpty());
                CHECK(p.workingDirectory().isEmpty());
                CHECK(p.environment().isEmpty());
                CHECK(p.state() == Process::NotRunning);
                CHECK_FALSE(p.isRunning());
                CHECK(p.exitCode() == -1);
                CHECK(p.pid() == -1);
        }

        SUBCASE("Setters and getters") {
                Process p;
                p.setProgram("/usr/bin/echo");
                CHECK(p.program() == "/usr/bin/echo");

                List<String> args = {"hello", "world"};
                p.setArguments(args);
                CHECK(p.arguments().size() == 2);
                CHECK(p.arguments()[0] == "hello");
                CHECK(p.arguments()[1] == "world");

                p.setWorkingDirectory("/tmp");
                CHECK(p.workingDirectory().toString() == "/tmp");

                Map<String, String> env;
                env.insert("FOO", "bar");
                p.setEnvironment(env);
                CHECK(p.environment().contains("FOO"));
        }

        SUBCASE("Start with empty program returns error") {
                Process p;
                Error   err = p.start();
                CHECK(err == Error::Invalid);
                CHECK(p.state() == Process::NotRunning);
        }

        SUBCASE("Start already-running process returns error") {
                Process p;
                Error   err = p.start("/bin/sleep", {"10"});
                REQUIRE(err.isOk());
                CHECK(p.isRunning());

                Error err2 = p.start();
                CHECK(err2 == Error::AlreadyOpen);

                p.kill();
                p.waitForFinished(5000);
        }

        SUBCASE("Run echo and read stdout") {
                Process p;
                Error   err = p.start("/bin/echo", {"-n", "Hello, promeki!"});
                REQUIRE(err.isOk());
                CHECK(p.isRunning());
                CHECK(p.pid() > 0);

                err = p.waitForFinished();
                REQUIRE(err.isOk());
                CHECK(p.exitCode() == 0);
                CHECK_FALSE(p.isRunning());

                Buffer out = p.readAllStdout();
                REQUIRE(out.isValid());
                CHECK(out.size() == 15);
                CHECK(std::memcmp(out.data(), "Hello, promeki!", 15) == 0);
        }

        SUBCASE("Convenience start(program, args)") {
                Process p;
                Error   err = p.start("/bin/true", {});
                REQUIRE(err.isOk());

                err = p.waitForFinished();
                CHECK(err.isOk());
                CHECK(p.exitCode() == 0);
        }

        SUBCASE("Non-zero exit code") {
                Process p;
                Error   err = p.start("/bin/false", {});
                REQUIRE(err.isOk());

                err = p.waitForFinished();
                CHECK(err.isOk());
                CHECK(p.exitCode() != 0);
        }

        SUBCASE("Program not found returns error") {
                Process p;
                Error   err = p.start("/nonexistent/program", {});
                // With exec-notify pipe, exec failure is reported directly
                CHECK(err == Error::NotExist);
                CHECK_FALSE(p.isRunning());
        }

        SUBCASE("Write to stdin and read back via cat") {
                Process p;
                Error   err = p.start("/bin/cat", {});
                REQUIRE(err.isOk());

                const char *input = "stdin round-trip test";
                size_t      len = std::strlen(input);
                ssize_t     written = p.writeToStdin(input, len);
                CHECK(written == static_cast<ssize_t>(len));
                p.closeWriteChannel();

                err = p.waitForFinished();
                REQUIRE(err.isOk());
                CHECK(p.exitCode() == 0);

                Buffer out = p.readAllStdout();
                REQUIRE(out.isValid());
                CHECK(out.size() == len);
                CHECK(std::memcmp(out.data(), input, len) == 0);
        }

        SUBCASE("Read stderr") {
                Process p;
                Error   err = p.start("/bin/sh", {"-c", "echo -n error_output >&2"});
                REQUIRE(err.isOk());

                err = p.waitForFinished();
                REQUIRE(err.isOk());
                CHECK(p.exitCode() == 0);

                Buffer errBuf = p.readAllStderr();
                REQUIRE(errBuf.isValid());
                CHECK(errBuf.size() == 12);
                CHECK(std::memcmp(errBuf.data(), "error_output", 12) == 0);
        }

        SUBCASE("Working directory") {
                Process p;
                p.setWorkingDirectory("/tmp");
                Error err = p.start("/bin/pwd", {});
                REQUIRE(err.isOk());

                err = p.waitForFinished();
                REQUIRE(err.isOk());
                CHECK(p.exitCode() == 0);

                Buffer out = p.readAllStdout();
                REQUIRE(out.isValid());
                // pwd output should start with /tmp
                String outStr(static_cast<const char *>(out.data()), out.size());
                CHECK(outStr.trim() == "/tmp");
        }

        SUBCASE("Custom environment") {
                Process             p;
                Map<String, String> env;
                env.insert("PROMEKI_TEST_VAR", "hello123");
                env.insert("PATH", "/usr/bin:/bin");
                p.setEnvironment(env);

                Error err = p.start("/bin/sh", {"-c", "echo -n $PROMEKI_TEST_VAR"});
                REQUIRE(err.isOk());

                err = p.waitForFinished();
                REQUIRE(err.isOk());
                CHECK(p.exitCode() == 0);

                Buffer out = p.readAllStdout();
                REQUIRE(out.isValid());
                CHECK(out.size() == 8);
                CHECK(std::memcmp(out.data(), "hello123", 8) == 0);
        }

        SUBCASE("Kill running process") {
                Process p;
                Error   err = p.start("/bin/sleep", {"60"});
                REQUIRE(err.isOk());
                CHECK(p.isRunning());

                p.kill();
                err = p.waitForFinished(5000);
                CHECK(err.isOk());
                CHECK_FALSE(p.isRunning());
                // Killed by SIGKILL, exit code should be negative
                CHECK(p.exitCode() < 0);
        }

        SUBCASE("Terminate running process") {
                Process p;
                Error   err = p.start("/bin/sleep", {"60"});
                REQUIRE(err.isOk());
                CHECK(p.isRunning());

                p.terminate();
                err = p.waitForFinished(10000);
                CHECK(err.isOk());
                CHECK_FALSE(p.isRunning());
                CHECK(p.exitCode() < 0);
        }

        SUBCASE("waitForFinished on non-running process") {
                Process p;
                Error   err = p.waitForFinished();
                CHECK(err == Error::NotOpen);
        }

        SUBCASE("waitForStarted on non-running process") {
                Process p;
                Error   err = p.waitForStarted();
                CHECK(err == Error::NotOpen);
        }

        SUBCASE("writeToStdin on non-running process returns -1") {
                Process     p;
                const char *data = "test";
                ssize_t     written = p.writeToStdin(data, 4);
                CHECK(written == -1);
        }

        SUBCASE("readAllStdout on non-running process returns empty buffer") {
                Process p;
                Buffer  buf = p.readAllStdout();
                CHECK_FALSE(buf.isValid());
        }

        SUBCASE("readAllStderr on non-running process returns empty buffer") {
                Process p;
                Buffer  buf = p.readAllStderr();
                CHECK_FALSE(buf.isValid());
        }

        SUBCASE("closeWriteChannel on non-running process is safe") {
                Process p;
                p.closeWriteChannel();
        }

        SUBCASE("kill on non-running process is safe") {
                Process p;
                p.kill();
        }

        SUBCASE("terminate on non-running process is safe") {
                Process p;
                p.terminate();
        }

        SUBCASE("Signals") {
                Process p;
                bool    startedEmitted = false;
                bool    finishedEmitted = false;
                int     finishedCode = -999;

                p.startedSignal.connect([&]() { startedEmitted = true; });

                p.finishedSignal.connect([&](int code) {
                        finishedEmitted = true;
                        finishedCode = code;
                });

                Error err = p.start("/bin/true", {});
                REQUIRE(err.isOk());
                CHECK(startedEmitted);

                err = p.waitForFinished();
                REQUIRE(err.isOk());
                CHECK(finishedEmitted);
                CHECK(finishedCode == 0);
        }

        SUBCASE("Large stdout output does not deadlock") {
                Process p;
                // Generate 100KB of output — exceeds pipe buffer (~64KB)
                Error err = p.start("/bin/sh", {"-c", "dd if=/dev/zero bs=1024 count=100 2>/dev/null | tr '\\0' 'A'"});
                REQUIRE(err.isOk());

                err = p.waitForFinished(10000);
                REQUIRE(err.isOk());
                CHECK(p.exitCode() == 0);

                Buffer out = p.readAllStdout();
                REQUIRE(out.isValid());
                CHECK(out.size() == 102400);
        }
}
