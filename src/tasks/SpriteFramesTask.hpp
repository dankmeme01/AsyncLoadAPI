#pragma once
#include "TaskImpl.hpp"
#include <AsyncLoad/Manager.hpp>

namespace AsyncLoad {

struct SpriteFramesTask final : CrtpTask<SpriteFramesTask, SpriteFrameData> {
    enum class State : uint8_t {
        /// We have the path and we are about to read the file from the disk.
        PrePlistRead,
        /// We have the data of the plist file, we are yet to parse it
        PlistRead,
        /// The plist has been parsed and a SpriteFrameData is now available.
        SpriteFramesReady,

        Failed,
    };

    asp::BoxedString m_path;
    CachedBufferChunk m_data;
    std::unordered_set<std::string> m_ignoreFrames;
    State m_state;
    bool m_pathIsFull;

    SpriteFramesTask(SpriteFramesLoadParams&& params, std::shared_ptr<Control> ctl);

    // bool finished() const override;
    // void invokeCallback() override;
    TaskAdvanceResult advance(bool mainThread = false) override;

    void fail(std::string message);
};

}
