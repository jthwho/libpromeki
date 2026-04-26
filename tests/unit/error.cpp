/**
 * @file      error.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cerrno>
#include <system_error>
#include <doctest/doctest.h>
#include <promeki/error.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("Error: default construction is Ok") {
        Error e;
        CHECK(e.isOk());
        CHECK_FALSE(e.isError());
        CHECK(e.code() == Error::Ok);
}

TEST_CASE("Error: construction with error code") {
        Error e(Error::Invalid);
        CHECK(e.isError());
        CHECK_FALSE(e.isOk());
        CHECK(e.code() == Error::Invalid);
}

TEST_CASE("Error: equality comparison") {
        Error a(Error::Ok);
        Error b(Error::Ok);
        Error c(Error::IOError);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Error: ordering comparison") {
        Error a(Error::Ok);
        Error b(Error::Invalid);
        CHECK(a < b);
        CHECK(a <= b);
        CHECK(b > a);
        CHECK(b >= a);
        CHECK(a <= a);
        CHECK(a >= a);
}

TEST_CASE("Error: name returns non-empty string") {
        Error e(Error::Invalid);
        CHECK_FALSE(e.name().isEmpty());
}

TEST_CASE("Error: desc returns non-empty string") {
        Error e(Error::Invalid);
        CHECK_FALSE(e.desc().isEmpty());
}

TEST_CASE("Error: Ok name") {
        Error e(Error::Ok);
        CHECK_FALSE(e.name().isEmpty());
}

TEST_CASE("Error: all codes are distinct") {
        Error ok(Error::Ok);
        Error inv(Error::Invalid);
        Error io(Error::IOError);
        Error oom(Error::NoMem);
        CHECK(ok != inv);
        CHECK(inv != io);
        CHECK(io != oom);
}

TEST_CASE("Error: copy construction") {
        Error a(Error::Timeout);
        Error b(a);
        CHECK(a == b);
        CHECK(b.code() == Error::Timeout);
}

TEST_CASE("Error: assignment") {
        Error a(Error::Timeout);
        Error b;
        b = a;
        CHECK(b.code() == Error::Timeout);
}

TEST_CASE("Error: domain-specific codes have names") {
        Error a(Error::InvalidArgument);
        CHECK_FALSE(a.name().isEmpty());
        CHECK(a.isError());
        Error b(Error::InvalidDimension);
        CHECK_FALSE(b.name().isEmpty());
        CHECK(b.isError());
}

TEST_CASE("Error: BufferTooSmall has name and desc") {
        Error e(Error::BufferTooSmall);
        CHECK(e.isError());
        CHECK_FALSE(e.name().isEmpty());
        CHECK_FALSE(e.desc().isEmpty());
}

TEST_CASE("Error: NotHostAccessible has name and desc") {
        Error e(Error::NotHostAccessible);
        CHECK(e.isError());
        CHECK_FALSE(e.name().isEmpty());
        CHECK_FALSE(e.desc().isEmpty());
}

TEST_CASE("Error: NotAdjacent has name and desc") {
        Error e(Error::NotAdjacent);
        CHECK(e.isError());
        CHECK_FALSE(e.name().isEmpty());
        CHECK_FALSE(e.desc().isEmpty());
}

TEST_CASE("Error: PipelineBuildFailed has name and desc") {
        Error e(Error::PipelineBuildFailed);
        CHECK(e.isError());
        CHECK_FALSE(e.name().isEmpty());
        CHECK_FALSE(e.desc().isEmpty());
}

TEST_CASE("Error: PipelineRuntimeError has name and desc") {
        Error e(Error::PipelineRuntimeError);
        CHECK(e.isError());
        CHECK_FALSE(e.name().isEmpty());
        CHECK_FALSE(e.desc().isEmpty());
}

TEST_CASE("Error: InspectorDiscontinuityDetected has name and desc") {
        Error e(Error::InspectorDiscontinuityDetected);
        CHECK(e.isError());
        CHECK_FALSE(e.name().isEmpty());
        CHECK_FALSE(e.desc().isEmpty());
}

TEST_CASE("Error: pipeline and inspector codes are distinct") {
        // Ensures the new registry entries don't accidentally collide
        // with each other or other pipeline-adjacent codes.
        Error pbf(Error::PipelineBuildFailed);
        Error pre(Error::PipelineRuntimeError);
        Error idd(Error::InspectorDiscontinuityDetected);
        Error na(Error::NotAdjacent);
        CHECK(pbf != pre);
        CHECK(pbf != idd);
        CHECK(pre != idd);
        CHECK(pbf != na);
        CHECK(pre != na);
        CHECK(idd != na);
        CHECK(pbf.name() != pre.name());
        CHECK(pre.name() != idd.name());
}

TEST_CASE("Error: systemError returns errno for mapped codes") {
        Error e(Error::Invalid);
        CHECK(e.systemError() == EINVAL);

        Error e2(Error::IOError);
        CHECK(e2.systemError() == EIO);

        Error e3(Error::PermissionDenied);
        CHECK(e3.systemError() == EACCES);
}

TEST_CASE("Error: systemError returns -1 for unmapped codes") {
        Error e(Error::BufferTooSmall);
        CHECK(e.systemError() == -1);

        Error e2(Error::InvalidArgument);
        CHECK(e2.systemError() == -1);
}

TEST_CASE("Error: systemErrorName for mapped codes") {
        Error e(Error::Invalid);
        CHECK_FALSE(e.systemErrorName().isEmpty());

        Error e2(Error::IOError);
        CHECK_FALSE(e2.systemErrorName().isEmpty());
}

TEST_CASE("Error: syserr(int) maps POSIX errno to correct code") {
        CHECK(Error::syserr(EINVAL).code() == Error::Invalid);
        CHECK(Error::syserr(EIO).code() == Error::IOError);
        CHECK(Error::syserr(EACCES).code() == Error::PermissionDenied);
        CHECK(Error::syserr(ENOENT).code() == Error::NotExist);
        CHECK(Error::syserr(ENOMEM).code() == Error::NoMem);
        CHECK(Error::syserr(EBADF).code() == Error::BadFileDesc);
        CHECK(Error::syserr(EEXIST).code() == Error::Exists);
        CHECK(Error::syserr(ENOSPC).code() == Error::NoSpace);
        CHECK(Error::syserr(EROFS).code() == Error::ReadOnly);
        CHECK(Error::syserr(ESPIPE).code() == Error::IllegalSeek);
        CHECK(Error::syserr(0).code() == Error::Ok);
}

TEST_CASE("Error: syserr(int) returns UnsupportedSystemError for unknown errno") {
        // Use a very large errno that is unlikely to be mapped
        CHECK(Error::syserr(99999).code() == Error::UnsupportedSystemError);
}

TEST_CASE("Error: syserr(std::error_code) with no error returns Ok") {
        std::error_code ec;
        CHECK(Error::syserr(ec).isOk());
}

TEST_CASE("Error: syserr(std::error_code) with generic_category") {
        std::error_code ec = std::make_error_code(std::errc::invalid_argument);
        Error           e = Error::syserr(ec);
        CHECK(e.code() == Error::Invalid);

        std::error_code ec2 = std::make_error_code(std::errc::no_such_file_or_directory);
        Error           e2 = Error::syserr(ec2);
        CHECK(e2.code() == Error::NotExist);

        std::error_code ec3 = std::make_error_code(std::errc::permission_denied);
        Error           e3 = Error::syserr(ec3);
        CHECK(e3.code() == Error::PermissionDenied);
}

TEST_CASE("Error: syserr(std::error_code) with system_category") {
        std::error_code ec(EINVAL, std::system_category());
        Error           e = Error::syserr(ec);
        CHECK(e.code() == Error::Invalid);

        std::error_code ec2(EIO, std::system_category());
        Error           e2 = Error::syserr(ec2);
        CHECK(e2.code() == Error::IOError);
}
