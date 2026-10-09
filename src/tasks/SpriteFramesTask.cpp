#include "SpriteFramesTask.hpp"
#include <AsyncLoad/FileUtils.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

SpriteFramesTask::SpriteFramesTask(SpriteFramesLoadParams&& params, std::shared_ptr<Control> ctl) : CrtpTask(std::move(ctl)) {
    m_ignoreFrames = std::move(params.ignoreFrames);

    if (!params.data.empty()) {
        // user provided raw plist data, no need to read any files
        m_state = State::PlistRead;
        m_data = std::move(params.data);
        return;
    }

    // user provided a path, prepare to read from it
    m_state = State::PrePlistRead;
    m_path = asp::BoxedString{params.path};
    m_pathIsFull = params.isFullPath;
}

TaskAdvanceResult SpriteFramesTask::advance(bool mainThread) {
    if (!this->shouldRun()) return TaskAdvanceResult::Finished;

    switch (m_state) {
        case State::PrePlistRead: {
            auto res = getFileData(m_path.c_str(), m_pathIsFull);
            if (!res) {
                this->fail(fmt::format("Failed to read plist file: {}", res.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }
            m_data = std::move(*res);
            m_state = State::PlistRead;
        } break;

        case State::PlistRead: {
            auto result = parseSpriteFrames(m_data.data(), m_data.size(), {
                .ignoreFrames = std::move(m_ignoreFrames)
            });
            if (!result) {
                this->fail(fmt::format("Failed to parse sprite frames (path: {}): {}", m_path, result.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }

            m_state = State::SpriteFramesReady;
            this->complete(Ok(std::move(*result)));
            return TaskAdvanceResult::Finished;
        } break;

        default: {
            AL_ASSERT(false && "Invalid state for SpriteFramesTask");
        } break;
    }

    return TaskAdvanceResult::Pending;
}

void SpriteFramesTask::fail(std::string message) {
    m_state = State::Failed;
    this->complete(Err(std::move(message)));
}

}
