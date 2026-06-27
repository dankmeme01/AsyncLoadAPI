#include <AsyncLoad/nodes/AsyncSprite.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

bool AsyncSprite::init() {
    return CCSprite::init();
}

bool AsyncSprite::initWithSpriteFrameName(ZStringView frameName) {
    // cannot really significantly speed this up
    return CCSprite::initWithSpriteFrameName(frameName.c_str());
}

bool AsyncSprite::initWithFile(ZStringView filename, CCRect rect) {
    this->initWithPlaceholder();

    m_handle = ALManager::get().loadTexture(filename, [this](auto result) {
        this->onLoaded(std::move(result));
    });

    return true;
}

bool AsyncSprite::initWithPlaceholder() {
    CCSprite::init();
    return true; // TODO
}

void AsyncSprite::onLoaded(Result<Ref<CCTexture2D>> result) {
    AL_TRACE("AsyncSprite::onLoaded (this = {}): {}", this, result);
    bool ok = result.isOk();
    if (ok) {
        this->initWithTexture(result.unwrap());
        if (m_callback) m_callback(Ok());
    } else {
        if (m_callback) m_callback(Err(std::move(result).unwrapErr()));
    }

    this->setPlaceholderShown(!ok);
}

void AsyncSprite::setPlaceholderShown(bool shown) {
    // TODO
}

AsyncSprite* AsyncSprite::create() {
    auto ret = new AsyncSprite();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

AsyncSprite* AsyncSprite::create(geode::ZStringView filename, cocos2d::CCRect rect) {
    auto ret = new AsyncSprite();
    if (ret->initWithFile(filename, rect)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

AsyncSprite* AsyncSprite::createWithTexture(cocos2d::CCTexture2D* texture, cocos2d::CCRect rect) {
    auto ret = new AsyncSprite();
    if (ret->initWithTexture(texture, rect)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

AsyncSprite* AsyncSprite::createWithSpriteFrame(cocos2d::CCSpriteFrame* frame) {
    auto ret = new AsyncSprite();
    if (ret->initWithSpriteFrame(frame)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

AsyncSprite* AsyncSprite::createWithSpriteFrameName(geode::ZStringView frameName) {
    auto ret = new AsyncSprite();
    if (ret->initWithSpriteFrameName(frameName)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}


}
