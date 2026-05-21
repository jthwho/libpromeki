/**
 * @file      csccontext.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/csccontext.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

CSCContext::CSCContext(size_t maxWidth) : _maxWidth(maxWidth) {
        // Allocate aligned float buffers, each holding maxWidth floats
        size_t bytes = maxWidth * sizeof(float);
        for (int i = 0; i < BufferCount; i++) {
                _buffers[i] = Buffer(bytes, BufferAlign);
                if (!_buffers[i].isValid()) {
                        promekiWarn("CSCContext: failed to allocate buffer %d of %d (%zu bytes)",
                                    i, (int)BufferCount, bytes);
                        _maxWidth = 0;
                        return;
                }
                _buffers[i].fill(0);
        }
        return;
}

float *CSCContext::buffer(int index) {
        if (index < 0 || index >= BufferCount || !_buffers[index].isValid()) {
                promekiWarnThrottled(5000, "CSCContext::buffer: invalid request index=%d valid=%d",
                                     index,
                                     (index >= 0 && index < BufferCount) ? (_buffers[index].isValid() ? 1 : 0) : -1);
                return nullptr;
        }
        return static_cast<float *>(_buffers[index].data());
}

PROMEKI_NAMESPACE_END
