from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from .build.geobuild.prelude import *

def main(build: Build):
    config = build.config
    debug = build.add_option("ASYNCLOAD_DEBUG", False, "Enable debug mode for AsyncLoad")

    build.add_include_dir("src")
    build.add_include_dir("include")
    build.add_source_dir("src/*.cpp", recursive=False)
    build.add_source_dir("src/nodes/*.cpp")
    build.add_source_dir(f"src/platform/{config.platform.platform_str(False)}/")

    build.enable_mod_json_generation("mod.template.json")
    build.add_geode_dep("prevter.imageplus", {
        "version": ">=v1.1.0",
        "required": False,
    })

    if config.platform.is_apple():
        build.add_source_dir("src/platform/shared_apple")

    if not config.platform.is_apple():
        build.add_cpm_dep("zeux/pugixml", "v1.15", link_name="pugixml-static", options={"PUGIXML_NO_EXCEPTIONS": "ON"})

    if debug:
        build.add_definition("AL_DEBUG")

# file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
# if (APPLE)
#     list(APPEND SOURCES src/SpriteFramesApple.mm)
# endif()
