#pragma once
#include "TaskImpl.hpp"
#include <AsyncLoad/Manager.hpp>

namespace AsyncLoad {

/// Orchestrator task around TextureTask and SpriteFramesTask, handles candidate merges

struct SpritesheetState;

struct SpritesheetTask final : CrtpTask<SpritesheetTask, void> {
    enum class State : uint8_t {
        /// Nothing done yet
        Start,
        /// Initial plist and png files have been loaded, pre-merge
        InitialPassDone,

        /// Merging
        LoadingCandidates,
        MergingCandidates,

        MergingComplete,

        /// Everything done
        Finished,

        Failed,
    };

    std::shared_ptr<SpritesheetState> m_spritesheetState;

    State m_state;
    bool m_mergeSheets;

    SpritesheetTask(std::string_view name, SpritesheetMergeBehavior behavior, std::shared_ptr<Control> ctl);

    TaskAdvanceResult advance(bool mainThread = false) override;

    void fail(std::string message);
    void startLoadingCandidates();
    bool doMerge();
    void addAndComplete();

    bool wantsInitialMainThread() const override {
        return true;
    }
};

}
