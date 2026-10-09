#include "ImageTask.hpp"
#include <AsyncLoad/FileUtils.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

ImageTask::ImageTask(ImageLoadParams&& params, std::shared_ptr<Control> ctl) : CrtpTask(std::move(ctl)) {
    if (!params.data.empty()) {
        // user provided raw image data, no need to read any files
        m_state = State::ImageRead;
        m_imageData = std::move(params.data);
        return;
    }

    // user provided a path, prepare to read from it
    m_state = State::PreImageRead;
    m_path = asp::BoxedString{params.path};
    m_pathIsFull = params.isFullPath;
}

TaskAdvanceResult ImageTask::advance(bool mainThread) {
    if (!this->shouldRun()) return TaskAdvanceResult::Finished;

    switch (m_state) {
        case State::PreImageRead: {
            auto res = getFileData(m_path.c_str(), m_pathIsFull);
            if (!res) {
                this->fail(fmt::format("failed to read image file: {}", res.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }
            m_imageData = std::move(*res);
            m_state = State::ImageRead;
        } break;

        case State::ImageRead: {
            auto res = RawImage::create(m_imageData.span());
            m_imageData = {};
            if (!res) {
                this->fail(fmt::format("failed to decode image: {}", res.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }
            auto image = std::move(*res);
            image.premultiply();

            m_state = State::ImageReady;
            this->complete(Ok(std::move(image)));
            return TaskAdvanceResult::Finished;
        } break;

        default: {
            AL_ASSERT(false && "Invalid state for ImageTask");
        } break;
    }

    return TaskAdvanceResult::Pending;
}

void ImageTask::fail(std::string message) {
    m_state = State::Failed;
    this->complete(Err(std::move(message)));
}

}
