#include "SpritesheetTask.hpp"
#include <AsyncLoad/util/Setting.hpp>
#include <AsyncLoad/util/String.hpp>
#include <AsyncLoad/FileUtils.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

struct MergeCandidate {
    size_t index;
    SpriteFrameData data;
    std::string pngPath;
    Ref<CCTexture2D> pngTexture;
};

struct PendingUnloadedMergeFrame {
    SpriteFrame frame;
    size_t textureIdx;
};

struct PendingLoadedMergeFrame {
    SpriteFrame frame;
    CCTexture2D* texture;
};

struct PendingTextureLoad {
    std::string path;
    Ref<CCTexture2D> texture;
};

struct SpritesheetState {
    /// Bare name, without extension
    asp::BoxedString m_name;
    asp::BoxedString m_primaryPlistPath;
    TaskHandle m_handle;
    std::atomic<size_t> m_pendingTasks{0};
    std::optional<asp::BoxedString> m_error;
    TextureQuality m_textureQuality;

    Ref<CCTexture2D> m_texture;
    std::unordered_set<std::string> m_loadedFrameNames;
    std::vector<SpriteFrame> m_spriteFrames;
    std::vector<PendingUnloadedMergeFrame> m_pendingUnloadedMergeFrames;
    std::vector<PendingLoadedMergeFrame> m_pendingMergeFrames;
    std::vector<MergeCandidate> m_mergeCandidates;
    std::vector<PendingTextureLoad> m_pendingTextureLoads;

    size_t insertPendingTextureLoad(std::string_view path) {
        auto idx = asp::iter::from(m_pendingTextureLoads)
            .enumerate()
            .find([&](auto pair) { return pair.second.path == path; })
            .transform([&](auto pair) { return pair.first; });

        if (idx) {
            return *idx;
        }

        m_pendingTextureLoads.emplace_back(PendingTextureLoad {
            .path = std::string(path),
        });
        return m_pendingTextureLoads.size() - 1;
    }
};

static bool shouldMerge(SpritesheetMergeBehavior behavior) {
    switch (behavior) {
        case SpritesheetMergeBehavior::Never: return false;
        case SpritesheetMergeBehavior::Always: return true;
        case SpritesheetMergeBehavior::Default:
            return getSettingFast<"merge-plists", bool>();
    }
}

template <typename T, typename E>
static void handleInitialSubResult(SpritesheetState& state, std::string_view what, Result<T, E> result) {
    if (result.isErr()) {
        auto msg = fmt::format("{} failed at step \"{}\": {}", state.m_handle.name(), what, result.unwrapErr());
        log::warn("{}", msg);
        if (!state.m_error) {
            state.m_error = std::move(msg);
        }
    }

    auto nowPending = state.m_pendingTasks.fetch_sub(1, std::memory_order::relaxed) - 1;

    if (nowPending == 0) {
        ALManager::get().enqueueSuspendedTask(state.m_handle, true);
    }
}

static std::string_view parent(std::string_view path, bool keepSlash = false) {
    auto slash = path.find_last_of("/\\");
    if (slash == std::string_view::npos) {
        return {};
    }

    return path.substr(0, keepSlash ? slash + 1 : slash);
}

static void handleCandidatePlistResult(SpritesheetState& state, std::string_view path, Result<SpriteFrameData> result, size_t index) {
    if (!result) {
        log::warn("Failed to load spritesheet candidate at {}: {}", path, result.unwrapErr());
        // not considered a hard error, just skip this candidate
    } else {
        // get the path to png via textureFileName in the plist
        auto data = std::move(result.unwrap());
        auto png = fmt::format("{}{}", parent(path, true), data.getMetadata().textureFileName);

        // since we have the full png path (which is a cctexturecache key), we can check if the texture is cached right away,
        // because this function runs on main thread
        auto tc = CCTextureCache::get();
        Ref pngTexture = (CCTexture2D*)tc->m_pTextures->objectForKey(string::convert(png));

        state.m_mergeCandidates.emplace_back(MergeCandidate {
            .index = index,
            .data = std::move(data),
            .pngPath = std::move(png),
            .pngTexture = std::move(pngTexture),
        });
    }

    auto nowPending = state.m_pendingTasks.fetch_sub(1, std::memory_order::relaxed) - 1;

    if (nowPending == 0) {
        ALManager::get().enqueueSuspendedTask(state.m_handle, false);
    }
}

static void handleCandidatePngResult(SpritesheetState& state, size_t idx, Result<Ref<CCTexture2D>> result) {
    if (result) {
        auto& load = state.m_pendingTextureLoads[idx];
        load.texture = std::move(*result);

        // add to cache because we are using submitTextureLoad
        CCTextureCache::get()->m_pTextures->setObject(load.texture, load.path);
    } else {
        log::warn("Failed to load '{}' for spritesheet merge: {}", state.m_pendingTextureLoads[idx].path, result.unwrapErr());
    }

    auto nowPending = state.m_pendingTasks.fetch_sub(1, std::memory_order::relaxed) - 1;

    if (nowPending == 0) {
        ALManager::get().enqueueSuspendedTask(state.m_handle, true);
    }
}

SpritesheetTask::SpritesheetTask(std::string_view name, SpritesheetMergeBehavior behavior, std::shared_ptr<Control> ctl) : CrtpTask(std::move(ctl)) {
    auto sstate = std::make_shared<SpritesheetState>();
    sstate->m_handle = this->handle();
    sstate->m_name = name;

    m_mergeSheets = shouldMerge(behavior);

    auto plistPath = fmt::format("{}.plist", name);

    // cocos in CCSpriteFrameCache uses the raw .plist filename as the key in m_pLoadedFileNames,
    // without running fullPathForFilename. we will replicate this and also use it as a unique key.
    auto plistKey = gd::string{plistPath};
    auto sfc = CCSpriteFrameCache::get();
    if (sfc->m_pLoadedFileNames->contains(plistKey)) {
        // already loaded!
        m_state = State::Finished;
        this->complete(Ok());
        return;
    }

    m_spritesheetState = std::move(sstate);
    m_state = State::Start;
}

TaskAdvanceResult SpritesheetTask::advance(bool mainThread) {
    if (!this->shouldRun()) return TaskAdvanceResult::Finished;

    switch (m_state) {
        // Stage 1: spawn initial texture and plist load tasks
        case State::Start: {
            AL_DEBUG_ASSERT(mainThread);

            m_spritesheetState->m_textureQuality = getTextureQuality();

            auto& am = ALManager::get();
            auto pngPath = fmt::format("{}.png", m_spritesheetState->m_name);
            auto plistPath = fmt::format("{}.plist", m_spritesheetState->m_name);
            auto fullPlistPath = fullPathForFilename(plistPath);
            m_spritesheetState->m_primaryPlistPath = fullPlistPath;

            // submit 2 tasks, for png and for plist
            m_spritesheetState->m_pendingTasks.fetch_add(2, std::memory_order::relaxed);

            am.loadTextureEager(pngPath, [pngPath, sstate = m_spritesheetState](Result<Ref<CCTexture2D>> result) {
                if (result) {
                    sstate->m_texture = std::move(*result);
                }
                handleInitialSubResult(*sstate, fmt::format("texture load for {}", pngPath), result);
            });

            am.submitSpriteFramesLoad({
                .path = fullPlistPath,
                .isFullPath = true,
                .callback = [plistPath, sstate = m_spritesheetState](Result<SpriteFrameData> result) {
                    if (result) {
                        auto frames = std::move(result.unwrap()).getFrames();
                        for (auto& frame : frames) {
                            sstate->m_loadedFrameNames.insert(frame.name);
                            sstate->m_spriteFrames.emplace_back(std::move(frame));
                        }
                    }
                    handleInitialSubResult(*sstate, fmt::format("sprite frames load for {}", plistPath), std::move(result));
                },
            });

            // not truly "Done" yet, but we should not be unsuspended until we are
            m_state = State::InitialPassDone;

            return TaskAdvanceResult::Suspend;
        } break;

        // Stage 2: initial texture and sprite frames tasks have finished, next steps are more complicated
        case State::InitialPassDone: {
            AL_DEBUG_ASSERT(mainThread);

            // if anything failed, propagate the error
            if (m_spritesheetState->m_error) {
                this->fail(std::string(*m_spritesheetState->m_error));
                return TaskAdvanceResult::Finished;
            }

            AL_ASSERT(m_spritesheetState->m_texture);
            auto& sf = m_spritesheetState->m_spriteFrames;

            // if merging is disabled, just add frames and we are done!
            if (!m_mergeSheets) {
                this->addAndComplete();
                return TaskAdvanceResult::Finished;
            }

            m_state = State::LoadingCandidates;
            return TaskAdvanceResult::Pending; // run on worker thread
        } break;

        // State 3: we have to load different sheet files
        case State::LoadingCandidates: {
            this->startLoadingCandidates();

            // nothing to merge? skip to next step!
            if (m_spritesheetState->m_pendingTasks.load(std::memory_order::relaxed) == 0) {
                m_state = State::MergingComplete;
                return TaskAdvanceResult::RequiresMainThread;
            }

            // otherwise, suspend and wait for all candidate plists to finish parsing
            m_state = State::MergingCandidates;
            return TaskAdvanceResult::Suspend;
        } break;

        // Stage 4: candidate plists finished loading, we need to merge sprite frames
        case State::MergingCandidates: {
            bool complete = this->doMerge();
            return complete ? TaskAdvanceResult::RequiresMainThread : TaskAdvanceResult::Suspend;
        } break;

        // Step 5: we loaded everything we need, it's only a matter of adding to sprite frame cache
        case State::MergingComplete: {
            AL_DEBUG_ASSERT(mainThread);
            this->addAndComplete();
            return TaskAdvanceResult::Finished;
        } break;

        default: {
            AL_ASSERT(false && "Invalid state for SpritesheetTask");
        } break;
    }

    return TaskAdvanceResult::Pending;
}

void SpritesheetTask::startLoadingCandidates() {
    auto paths = AsyncLoad::getSearchPaths();

    std::vector<std::string> candidates;
    for (auto& spath : *paths) {
        int tq = (int)m_spritesheetState->m_textureQuality;

        // go through current quality and all below
        do {
            auto plistName = fmt::format("{}{}.plist", m_spritesheetState->m_name, getQualitySuffix((TextureQuality)tq));
            // now we have e.g. "GJ_GameSheet01-uhd.plist" and we can look for it in the search paths

            auto fp = getPathForFilename(plistName, spath);
            if (fp.empty()) {
                tq--;
                continue;
            }

            // skip the primary
            if (fp == m_spritesheetState->m_primaryPlistPath) {
                break;
            }

            candidates.emplace_back(string::convert(std::move(fp)));
            break;
        } while (tq >= (int)TextureQuality::Low);
    }

    if (candidates.empty()) {
        return;
    }

    // parse them all
    auto& am = ALManager::get();

    m_spritesheetState->m_pendingTasks.fetch_add(candidates.size(), std::memory_order::relaxed);
    for (auto& [idx, path] : asp::iter::from(candidates).enumerate()) {
        am.submitSpriteFramesLoad({
            .path = path,
            .isFullPath = true,
            .ignoreFrames = m_spritesheetState->m_loadedFrameNames,
            .callback = [path, idx, sstate = m_spritesheetState](Result<SpriteFrameData> result) {
                handleCandidatePlistResult(*sstate, path, std::move(result), idx);
            },
        });
    }
}

bool SpritesheetTask::doMerge() {
    auto& state = *m_spritesheetState;
    std::ranges::sort(state.m_mergeCandidates, std::less{}, &MergeCandidate::index);

    for (auto& mc : state.m_mergeCandidates) {
        std::optional<size_t> textureIdx;

        for (auto& f : mc.data.getFrames()) {
            if (!state.m_loadedFrameNames.insert(f.name).second) {
                continue;
            }

            // this frame exists in this candidate spritesheet but NOT yet loaded by any other candidate, load it

            // first enqueue the png texture to be loaded if needed
            if (!textureIdx && !mc.pngTexture) {
                textureIdx = state.insertPendingTextureLoad(mc.pngPath);
            }

            if (textureIdx) {
                state.m_pendingUnloadedMergeFrames.emplace_back(PendingUnloadedMergeFrame {
                    .frame = std::move(f),
                    .textureIdx = *textureIdx,
                });
            } else {
                state.m_pendingMergeFrames.emplace_back(PendingLoadedMergeFrame {
                    .frame = std::move(f),
                    .texture = mc.pngTexture,
                });
            }
        }
    }

    // next step will depend on whether we have any frames referencing a texture that has not yet been loaded
    if (state.m_pendingUnloadedMergeFrames.empty()) {
        // no pngs to load, we can proceed straight to the final step
        m_state = State::MergingComplete;
        return true;
    }

    // we have to enqueue the pngs
    state.m_pendingTasks.fetch_add(state.m_pendingTextureLoads.size(), std::memory_order::relaxed);
    auto& am = ALManager::get();
    for (auto& [idx, png] : asp::iter::from(state.m_pendingTextureLoads).enumerate()) {
        am.submitTextureLoad({
            .path = png.path,
            .isFullPath = true,
            .callback = [idx, sstate = m_spritesheetState](Result<Ref<CCTexture2D>> result) {
                handleCandidatePngResult(*sstate, idx, result);
            },
        });
    }

    m_state = State::MergingComplete; // suspend and resume again at MergingComplete
    return false;
}

void SpritesheetTask::addAndComplete() {
    auto& state = *m_spritesheetState;

    log::debug(
        "Spritesheet load complete for '{}': {} frames ({} base, {} unloaded, {} loaded), {} candidate plists",
        state.m_name,
        state.m_loadedFrameNames.size(), state.m_spriteFrames.size(), state.m_pendingUnloadedMergeFrames.size(), state.m_pendingMergeFrames.size(),
        state.m_mergeCandidates.size()
    );
#ifdef AL_DEBUG
    if (!state.m_mergeCandidates.empty()) {
        log::debug("Considered candidates:");
        for (auto& mc : state.m_mergeCandidates) {
            log::debug("  {} ({} frames)", mc.pngPath, mc.data.getFrames().size());
        }
    }
#endif

    // base frames are simple
    auto plistKey = fmt::format("{}.plist", state.m_name);
    addSpriteFrames(state.m_spriteFrames, state.m_texture, plistKey);

    // for other frames we need to group by texture
    std::unordered_map<CCTexture2D*, std::vector<SpriteFrame>> framesByTexture;

    for (auto& unl : state.m_pendingUnloadedMergeFrames) {
        auto& load = state.m_pendingTextureLoads[unl.textureIdx];
        auto tex = load.texture.data();
        if (!tex) {
            log::warn("Frame '{}' will be skipped due to failed texture load for '{}'", unl.frame.name, load.path);
            continue;
        };

        framesByTexture[tex].push_back(unl.frame);
    }

    for (auto& fr : state.m_pendingMergeFrames) {
        framesByTexture[fr.texture].push_back(fr.frame);
    }

    for (auto& [tex, frames] : framesByTexture) {
        AL_TRACE("For texture {} adding {} frames", tex, frames.size());
        addSpriteFrames(frames, tex, "");
    }

    m_state = State::Finished;
    this->complete(Ok());
}

void SpritesheetTask::fail(std::string message) {
    m_state = State::Failed;
    this->complete(Err(std::move(message)));
}

}
