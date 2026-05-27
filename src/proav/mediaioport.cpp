/**
 * @file      mediaioport.cpp
 * @copyright Jason Howard. All rights reserved.
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

MediaIOAllocator::Ptr MediaIOPort::allocator() const {
        // Always non-null — MediaIO::allocator() resolves to the
        // process-wide default when no override has been installed.
        // Defensive null check on _mediaIO so a port being torn down
        // mid-shutdown doesn't crash on cleanup paths.
        if (_mediaIO == nullptr) return MediaIOAllocator::defaultAllocator();
        return _mediaIO->allocator();
}

PROMEKI_NAMESPACE_END
