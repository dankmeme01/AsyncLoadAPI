#ifndef __APPLE__

#include <AsyncLoad/SpriteFrames.hpp>
#include <AsyncLoad/util/hash.hpp>
#include <AsyncLoad/util/Parse.hpp>
#include <pugixml.hpp>

using namespace geode::prelude;

// This is to ensure that Geode's pugixml header is not included
static_assert(std::is_trivially_destructible_v<pugi::xml_node>);

namespace AsyncLoad {

struct SpriteFrameData::Impl {
    SpriteFrameMetadata metadata;
    std::vector<SpriteFrame> frames;
};

SpriteFrameData::SpriteFrameData(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
SpriteFrameData::SpriteFrameData(SpriteFrameData&&) noexcept = default;
SpriteFrameData& SpriteFrameData::operator=(SpriteFrameData&&) noexcept = default;
SpriteFrameData::~SpriteFrameData() = default;

const std::vector<SpriteFrame>& SpriteFrameData::getFrames() const {
    return m_impl->frames;
}

std::vector<SpriteFrame>& SpriteFrameData::getFrames() & {
    return m_impl->frames;
}

std::vector<SpriteFrame> SpriteFrameData::getFrames() && {
    return std::move(m_impl->frames);
}

const SpriteFrameMetadata& SpriteFrameData::getMetadata() const {
    return m_impl->metadata;
}

template <typename T>
std::optional<T> parseNode(pugi::xml_node node) {
    auto str = node.child_value();

    if constexpr (std::is_same_v<T, float>) {
        return geode::utils::numFromString<float>(str).ok();
    } else if constexpr (std::is_same_v<T, int>) {
        return geode::utils::numFromString<int>(str).ok();
    } else if constexpr (std::is_same_v<T, bool>) {
        return std::string_view{node.name()} == "true";
    } else if constexpr (std::is_same_v<T, CCPoint> || std::is_same_v<T, CCSize>) {
        return parsePoint(str);
    } else if constexpr(std::is_same_v<T, CCRect>) {
        return parseRect(str);
    } else {
        static_assert(std::is_void_v<T>, "unsupported type");
    }
}

# define assign_or_bail(var, val) if (auto _res = (val)) { var = std::move(_res).value(); } else { return false; }

bool parseSpriteFrameV0(pugi::xml_node node, SpriteFrame& sframe) {
    sframe.textureRotated = false;

    for (pugi::xml_node keyNode = node.child("key"); keyNode; keyNode = keyNode.next_sibling("key")) {
        auto keyName = keyNode.child_value();

        // the corresponding value node
        auto valueNode = keyNode.next_sibling();

        if (!valueNode) {
            continue;
        }

        auto keyHash = hashStringRuntime(keyName);
        switch (keyHash) {
            case AL_STRING_HASH("x"): {
                assign_or_bail(sframe.textureRect.origin.x, parseNode<float>(valueNode));
            } break;

            case AL_STRING_HASH("y"): {
                assign_or_bail(sframe.textureRect.origin.y, parseNode<float>(valueNode));
            } break;

            case AL_STRING_HASH("width"): {
                assign_or_bail(sframe.textureRect.size.width, parseNode<float>(valueNode));
            } break;

            case AL_STRING_HASH("height"): {
                assign_or_bail(sframe.textureRect.size.height, parseNode<float>(valueNode));
            } break;

            case AL_STRING_HASH("offsetX"): {
                assign_or_bail(sframe.offset.x, parseNode<float>(valueNode));
            } break;

            case AL_STRING_HASH("offsetY"): {
                assign_or_bail(sframe.offset.y, parseNode<float>(valueNode));
            } break;

            case AL_STRING_HASH("originalWidth"): {
                assign_or_bail(sframe.sourceSize.width, (parseNode<int>(valueNode)));
                sframe.sourceSize.width = std::abs(sframe.sourceSize.width);
            } break;

            case AL_STRING_HASH("originalHeight"): {
                assign_or_bail(sframe.sourceSize.height, (parseNode<int>(valueNode)));
                sframe.sourceSize.height = std::abs(sframe.sourceSize.height);
            } break;

            default: break;
        }
    }

    return true;
}

bool parseSpriteFrameV1_2(pugi::xml_node node, SpriteFrame& sframe, int format) {
    for (pugi::xml_node keyNode = node.child("key"); keyNode; keyNode = keyNode.next_sibling("key")) {
        auto keyName = keyNode.child_value();

        // the corresponding value node
        auto valueNode = keyNode.next_sibling();

        if (!valueNode) {
            continue;
        }

        auto keyHash = hashStringRuntime(keyName);
        switch (keyHash) {
            case AL_STRING_HASH("frame"): {
                assign_or_bail(sframe.textureRect, parseNode<cocos2d::CCRect>(valueNode));
            } break;

            case AL_STRING_HASH("rotated"): {
                if (format == 2) {
                    assign_or_bail(sframe.textureRotated, parseNode<bool>(valueNode));
                }
            } break;

            case AL_STRING_HASH("offset"): {
                assign_or_bail(sframe.offset, parseNode<cocos2d::CCPoint>(valueNode));
            } break;

            case AL_STRING_HASH("sourceSize"): {
                assign_or_bail(sframe.sourceSize, parseNode<cocos2d::CCSize>(valueNode));
            } break;

            default: break;
        }
    }

    return true;
}

bool parseSpriteFrameV3(pugi::xml_node node, SpriteFrame& sframe) {
    for (pugi::xml_node keyNode = node.child("key"); keyNode; keyNode = keyNode.next_sibling("key")) {
        auto keyName = keyNode.child_value();

        // the corresponding value node
        auto valueNode = keyNode.next_sibling();

        if (!valueNode) {
            continue;
        }

        auto keyHash = hashStringRuntime(keyName);

        switch (keyHash) {
            case AL_STRING_HASH("spriteOffset"): {
                assign_or_bail(sframe.offset, parseNode<cocos2d::CCPoint>(valueNode));
            } break;

            case AL_STRING_HASH("spriteSize"): {
                assign_or_bail(sframe.textureRect.size, parseNode<cocos2d::CCSize>(valueNode));
            } break;

            case AL_STRING_HASH("spriteSourceSize"): {
                assign_or_bail(sframe.sourceSize, parseNode<cocos2d::CCSize>(valueNode));
            } break;

            case AL_STRING_HASH("textureRect"): {
                if (auto tr = parseNode<cocos2d::CCRect>(valueNode)) {
                    sframe.textureRect.origin = tr->origin;
                } else {
                    return false;
                }
            } break;

            case AL_STRING_HASH("textureRotated"): {
                assign_or_bail(sframe.textureRotated, parseNode<bool>(valueNode));
            } break;

            case AL_STRING_HASH("aliases"): {
                // It seems like aliases are never used by GD, so I cannot test this code, but in theory it should work..
                for (pugi::xml_node aliasNode = valueNode.child("string"); aliasNode; aliasNode = aliasNode.next_sibling("string")) {
                    sframe.aliases.push_back(aliasNode.child_value());
                }
            } break;

            default: break;
        }
    }

    return true;
}

bool parseSpriteFrame(pugi::xml_node node, SpriteFrame& sframe, int format) {
    switch (format) {
        case 0: return parseSpriteFrameV0(node, sframe);
        case 1: [[fallthrough]];
        case 2: return parseSpriteFrameV1_2(node, sframe, format);
        case 3: return parseSpriteFrameV3(node, sframe);
        default: std::unreachable(); // this is already handled by parseSpriteFrames
    }
}

Result<SpriteFrameData> parseSpriteFrames(void* data, size_t size, const ParseSpriteFramesOptions& options) {
    auto sfdata = std::make_unique<SpriteFrameData::Impl>();

    pugi::xml_parse_result result;
    pugi::xml_document doc;

    if (options.passBufferOwnership) {
        result = doc.load_buffer_inplace_own(data, size);
    } else {
        result = doc.load_buffer_inplace(data, size);
    }

    if (!result) {
        return Err("Failed to parse XML: {}", result.description());
    }

    pugi::xml_node plist = doc.child("plist");
    if (!plist) {
        return Err("Failed to find root <plist> node");
    }

    pugi::xml_node rootDict = plist.child("dict");
    if (!rootDict) {
        return Err("Failed to find root <dict> node");
    }

    pugi::xml_node frames, metadata;

    for (pugi::xml_node keyNode = rootDict.child("key"); keyNode; keyNode = keyNode.next_sibling("key")) {
        auto keyName = keyNode.child_value();

        // the corresponding value node
        auto valueNode = keyNode.next_sibling();

        if (!valueNode) {
            continue;
        }

        if (strcmp(keyName, "frames") == 0) {
            frames = valueNode;
        } else if (strcmp(keyName, "metadata") == 0) {
            metadata = valueNode;
        }
    }

    if (!frames) {
        return Err("Failed to find 'frames' node");
    }

    if (!metadata) {
        return Err("Failed to find 'metadata' node");
    }

    // Iterate over the metadata
    for (pugi::xml_node keyNode = metadata.child("key"); keyNode; keyNode = keyNode.next_sibling("key")) {
        auto keyName = keyNode.child_value();

        // the corresponding value node
        auto valueNode = keyNode.next_sibling();

        if (!valueNode) {
            continue;
        }

        auto keyHash = hashStringRuntime(keyName);

        switch (keyHash) {
            case AL_STRING_HASH("format"): {
                sfdata->metadata.format = parseNode<int>(valueNode).value_or(-1);
            } break;

            case AL_STRING_HASH("textureFileName"): {
                sfdata->metadata.textureFileName = valueNode.child_value();
            } break;

            default: break;
        }
    }

    if (sfdata->metadata.format < 0 || sfdata->metadata.format > 3) {
        return Err("Unsupported format version: {}", sfdata->metadata.format);
    }

    // Note: one could try and optimize this by counting the amount of children and reserving space in the `sfData->frames`.
    // In my tests this proved to be ever so slightly slower, although it could depend on the platform and device.
    // Therefore this optimization was not done here.

    // Iterate over the frames
    for (pugi::xml_node keyNode = frames.child("key"); keyNode; keyNode = keyNode.next_sibling("key")) {
        std::string name = keyNode.child_value();
        if (options.ignoreFrames.contains(name)) {
            continue;
        }

        SpriteFrame frame;
        frame.name = std::move(name);

        // the corresponding value node
        auto frameDict = keyNode.next_sibling();
        if (!frameDict) {
            continue;
        }

        if (!parseSpriteFrame(frameDict, frame, sfdata->metadata.format)) {
            log::warn("Failed to parse frame '{}', skipping!", frame.name);
            continue;
        }

        sfdata->frames.push_back(std::move(frame));
    }

    return Ok(SpriteFrameData{std::move(sfdata)});
}

}

#endif
