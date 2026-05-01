/**
 * @file      mediaiostatscollector.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaio.h>
#include <promeki/mediaiostatscollector.h>
#include <promeki/objectbase.tpp>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

MediaIOStatsCollector::MediaIOStatsCollector(ObjectBase *parent) : ObjectBase(parent) {}

MediaIOStatsCollector::MediaIOStatsCollector(MediaIO *target, ObjectBase *parent) : ObjectBase(parent) {
        setTarget(target);
}

MediaIOStatsCollector::~MediaIOStatsCollector() {
        setTarget(nullptr);
}

void MediaIOStatsCollector::setTarget(MediaIO *io) {
        MediaIO *prev = _target.data();
        if (prev != nullptr) {
                prev->commandCompletedSignal.disconnectFromObject(this);
        }
        _target = io;
        _windows.clear();
        if (io != nullptr) {
                io->commandCompletedSignal.connect(
                        [this](MediaIOCommand::Ptr cmd) { onCommandCompleted(std::move(cmd)); }, this);
        }
}

void MediaIOStatsCollector::setWindowSize(int n) {
        if (n < 0) n = 0;
        if (_windowSize == n) return;
        _windowSize = n;
        if (n == 0) {
                _windows.clear();
                return;
        }
        for (auto it = _windows.begin(); it != _windows.end(); ++it) {
                it->second.setCapacity(n);
        }
}

WindowedStat MediaIOStatsCollector::window(const Key &key) const {
        auto it = _windows.find(key);
        if (it == _windows.end()) return WindowedStat();
        return it->second;
}

void MediaIOStatsCollector::clear() {
        _windows.clear();
}

void MediaIOStatsCollector::onCommandCompleted(MediaIOCommand::Ptr cmd) {
        if (cmd.isNull()) return;
        if (_windowSize <= 0) return;
        const MediaIOCommand::Kind kind = cmd->kind();
        cmd->stats.forEach([this, kind](MediaIOStats::ID id, const Variant &val) {
                Key  key{kind, id};
                auto it = _windows.find(key);
                if (it == _windows.end()) {
                        WindowedStat ws(_windowSize);
                        if (ws.push(val)) {
                                _windows.insert(key, std::move(ws));
                        }
                        return;
                }
                if (it->second.capacity() != _windowSize) {
                        it->second.setCapacity(_windowSize);
                }
                it->second.push(val);
        });
}

PROMEKI_NAMESPACE_END
