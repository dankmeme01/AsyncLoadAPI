#pragma once

#include <Geode/Geode.hpp>
#include <AsyncLoad/Manager.hpp>
#include "../Util.hpp"

namespace AsyncLoad {

class AL_DLL AsyncSprite : public cocos2d::CCSprite {
public:
    using Callback = geode::Function<void(geode::Result<>)>;

    static AsyncSprite* create();
    static AsyncSprite* create(geode::ZStringView filename, cocos2d::CCRect = {});
    static AsyncSprite* createWithTexture(cocos2d::CCTexture2D* texture, cocos2d::CCRect rect = {});
    static AsyncSprite* createWithSpriteFrame(cocos2d::CCSpriteFrame* frame);
    static AsyncSprite* createWithSpriteFrameName(geode::ZStringView frameName);

    void setCallback(Callback callback) {
        m_callback = std::move(callback);
    }

protected:
    TaskHandle m_handle;
    Callback m_callback;

    bool init() override;
    bool initWithSpriteFrameName(geode::ZStringView frameName);
    bool initWithFile(geode::ZStringView filename, cocos2d::CCRect rect = {});
    bool initWithPlaceholder();

    void onLoaded(geode::Result<geode::Ref<cocos2d::CCTexture2D>> result);
    void setPlaceholderShown(bool shown);
};

}
