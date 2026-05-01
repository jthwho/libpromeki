/**
 * @file      mediaioport.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaioport.h>
#include <promeki/mediaio.h>
#include <promeki/mediaioportgroup.h>

PROMEKI_NAMESPACE_BEGIN

MediaIOPort::MediaIOPort(MediaIOPortGroup *group, const String &name, int index)
        : ObjectBase(group),
          _mediaIO(group->mediaIO()),
          _group(group),
          _name(name),
          _index(index) {}

MediaIOPort::~MediaIOPort() = default;

PROMEKI_NAMESPACE_END
