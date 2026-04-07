/**
 * @file      mediaiotask.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiotask.h>

PROMEKI_NAMESPACE_BEGIN

MediaIOTask::~MediaIOTask() = default;

Error MediaIOTask::executeCmd(MediaIOCommandOpen &cmd) {
        return Error::NotImplemented;
}

Error MediaIOTask::executeCmd(MediaIOCommandClose &cmd) {
        return Error::Ok;
}

Error MediaIOTask::executeCmd(MediaIOCommandRead &cmd) {
        return Error::NotSupported;
}

Error MediaIOTask::executeCmd(MediaIOCommandWrite &cmd) {
        return Error::NotSupported;
}

Error MediaIOTask::executeCmd(MediaIOCommandSeek &cmd) {
        return Error::IllegalSeek;
}

Error MediaIOTask::executeCmd(MediaIOCommandParams &cmd) {
        return Error::NotSupported;
}

Error MediaIOTask::executeCmd(MediaIOCommandStats &cmd) {
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
