#pragma once
#include <AsyncLoad/Manager.hpp>
#include <asp/ptr/BoxedString.hpp>
#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

/// State machine representing the current state of this task
enum class TaskState : uint8_t {
    /// We have the path and we are about to read the file from the disk.
    PreImageRead,
    /// We have the data of the image file, and we are about to decode it from a format like PNG or WEBP into raw pixels.
    ImageRead,
    /// Image has been fully decoded and a raw image is available. A texture may now be created with this.
    ImageReady,

    /// A PBO has been mapped and we are ready to write into it from a thread
    AsyncPboReady,
    /// Image has either been decoded into a PBO or data has been memcpied from CCImage to the PBO.
    /// The PBO is now ready to be finalized into a CCTexture2D.
    AsyncPboDone,
    /// A CCTexture2D is now ready and available.
    TextureReady,

    /// We have the path and we are about to read the file from the disk.
    PrePlistRead,
    /// We have the data of the plist file, we are yet to parse it
    PlistRead,
    /// The plist has been parsed and a SpriteFrameData is now available.
    SpriteFramesReady,


    /// An operation failed.
    Failed,
    /// Invalid, uninitialized state
    Invalid,
};

std::string_view format_as(TaskState state);

/// The final goal, what the task should do before it is considered successfully complete.
enum class TaskGoal : uint8_t {
    /// Load a raw image into memory
    Image,
    /// Create an OpenGL texture
    Texture,
    /// Parse sprite frames
    SpriteFrames,
};

std::string_view format_as(TaskGoal goal);

/// The result of a single operation in a task, returned from advance()
enum class TaskAdvanceResult : uint8_t {
    /// Task is still pending, and advance() should be called again.
    Pending,
    /// Task is still pending, and work needs to be done strictly on main thread.
    /// In this state, advance() will do nothing when in a thread pool, and otherwise will advance the task.
    RequiresMainThread,
    /// Task has finished, either successfully or not.
    Finished,
};

struct Task {
    uint64_t m_id;
#ifdef AL_DEBUG
    asp::Instant m_startTime;
#endif
    Atomic<TaskState> m_state{TaskState::Invalid};
    Atomic<TaskGoal> m_goal;
    Atomic<bool> m_cancelled = false;
    std::string m_error;

    Task() : m_id(utils::random::nextU64()), m_startTime(asp::Instant::now()) {}

    TaskHandle handle() const {
        return TaskHandle(m_id);
    }

    TaskState state() const {
        return m_state.load(std::memory_order::acquire);
    }

    void setState(TaskState state) {
        m_state.store(state, std::memory_order::release);
    }

    TaskGoal goal() const {
        return m_goal.load(std::memory_order::acquire);
    }

    void setGoal(TaskGoal goal) {
        m_goal.store(goal, std::memory_order::release);
    }

    bool cancelled() const {
        return m_cancelled.load(std::memory_order::acquire);
    }

    void cancel() {
        m_cancelled.store(true, std::memory_order::release);
    }

    void fail(std::string message) {
        m_error = std::move(message);
        this->setState(TaskState::Failed);
    }

    bool failed() const {
        return m_state == TaskState::Failed;
    }

#ifdef AL_DEBUG
    asp::Duration elapsed() const {
        return m_startTime.elapsed();
    }
#endif

    /// Returns whether the operation finished: due to either a full success, an error or being cancelled.
    /// If this returns true, the task should immediately be discarded, and invokeCallback() should be called if cancelled() == false.
    virtual bool finished() const = 0;
    virtual void invokeCallback() = 0;

    /// Advances the task forward, can be safely called from a thread pool or from the main thread.
    virtual TaskAdvanceResult advance(bool mainThread = false) = 0;
};

struct ImageTask final : Task {
    ImageLoadParams::Callback m_callback;
    asp::BoxedString m_path;
    CachedBufferChunk m_imageData; // bytes in an arbitrary image format
    std::optional<RawImage> m_image;
    bool m_pathIsFull;

    ImageTask(ImageLoadParams&& params);

    bool finished() const override;
    void invokeCallback() override;
    TaskAdvanceResult advance(bool mainThread = false) override;
};

struct TextureTask final : Task {
    TextureLoadParams::Callback m_callback;
    asp::BoxedString m_path;
    CachedBufferChunk m_imageData; // bytes in an arbitrary image format
    std::optional<RawImage> m_image;
    Ref<CCTexture2D> m_texture = nullptr;
    GLuint m_glTex = 0;
    SmartPBO m_glPbo;
    void* m_mappedPboPtr = nullptr;
    bool m_pathIsFull;

    TextureTask(TextureLoadParams&& params);
    ~TextureTask();

    bool finished() const override;
    void invokeCallback() override;
    TaskAdvanceResult advance(bool mainThread = false) override;

    void preparePBO();
    TaskAdvanceResult startAsyncPBOLoad();
    TaskAdvanceResult doWriteIntoAsyncPBO();
    TaskAdvanceResult startPBOLoad();
    TaskAdvanceResult startNoPBOLoad();
    void doFinalizeAsyncPBO();

    Ref<CCTexture2D> finalizeTexture(GLuint tex);
};

struct SpriteFramesTask final : Task {
    SpriteFramesLoadParams::Callback m_callback;
    asp::BoxedString m_path;
    CachedBufferChunk m_data;
    std::optional<SpriteFrameData> m_spriteFrames;
    bool m_pathIsFull;

    SpriteFramesTask(SpriteFramesLoadParams&& params);

    bool finished() const override;
    void invokeCallback() override;
    TaskAdvanceResult advance(bool mainThread = false) override;
};

}
