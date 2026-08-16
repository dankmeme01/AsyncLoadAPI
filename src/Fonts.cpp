#include <AsyncLoad/Fonts.hpp>
#include <AsyncLoad/FileUtils.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

static Ref<CCBMFontConfiguration> convert(BitmapFont& font) {
    auto conf = new CCBMFontConfiguration();
    auto padding = font.getPadding();
    conf->m_tPadding = {padding.left, padding.right, padding.top, padding.bottom};
    conf->m_nCommonHeight = font.getCommonHeight();
    conf->m_sAtlasName = font.getAtlasName();
    conf->m_pCharacterSet = new gd::set<unsigned int>();

    auto& chars = font.getCharDefs();
    for (auto& [c, def] : chars) {
        auto element = new tCCFontDefHashElement{};
        element->fontDef = {
            .charID = c,
            .rect = def.rect,
            .xOffset = (short)def.xOffset,
            .yOffset = (short)def.yOffset,
            .xAdvance = (short)def.xAdvance,
        };
        element->key = c;

        HASH_ADD_INT(conf->m_pFontDefDictionary, key, element);
        conf->m_pCharacterSet->insert(c);
    }

    for (auto& [pair, offset] : font.getKernings()) {
        auto element = new tCCKerningHashElement{};
        element->key = (pair.first << 16) | (pair.second & 0xffff);
        element->amount = offset.amount;
        HASH_ADD_INT(conf->m_pKerningDictionary, key, element);
    }

    return Ref<CCBMFontConfiguration>::adopt(conf);
}

// semi-copied from prevter :D
Ref<CCBMFontConfiguration> loadFont(std::span<const uint8_t> data) {
    BitmapFont font;
    if (!font.initWithContents(std::string_view{reinterpret_cast<const char*>(data.data()), data.size()})) {
        return nullptr;
    }

    return convert(font);
}

Ref<CCBMFontConfiguration> loadFont(ZStringView path, bool useCache) {
    if (useCache) {
        auto font = BitmapFont::load(path);
        return font ? convert(*font) : nullptr;
    }

    // no geode cache, read and load manually
    auto data = getFileData(path);
    if (!data) {
        log::warn("loadFont failed: {}", data.unwrapErr());
        return nullptr;
    }

    return loadFont(data.unwrap().span());
}

}
