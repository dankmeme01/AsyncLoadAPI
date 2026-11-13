from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from .build.geobuild.prelude import *

def main(build: Build):
    config = build.config
    debug = build.add_option("ASYNCLOAD_DEBUG", False, "Enable debug mode for AsyncLoad")
    install = build.add_option("ASYNCLOAD_INSTALL", False, "Install mod after building for Android")
    caching = build.add_option("ASYNCLOAD_ENABLE_CACHE", False, "Enable PBO/buffer caching")

    build.add_include_dir("src")
    build.add_include_dir("include")
    build.add_source_dir("src/*.cpp", recursive=False)
    build.add_source_dir("src/nodes/*.cpp")
    build.add_source_dir("src/tasks/*.cpp")
    build.add_source_dir("src/util/*.cpp")
    build.add_source_dir(f"src/platform/{config.platform.platform_str(False)}/")

    if config.platform.is_apple():
        build.add_source_dir("src/platform/shared_apple")
    if not config.platform.is_windows():
        build.add_source_dir("src/platform/shared_unix")

    build.enable_mod_json_generation("mod.template.json")
    assert build.mod_json

    build.add_geode_dep("prevter.imageplus", {
        "version": ">=v1.1.0",
        "required": False,
    })
    build.relax_geode_requirement()
    build.add_api_headers("include/**/*.hpp")
    build.mod_json["api"]["cmake"] = [
        "cmake/DependencyMain.cmake"
    ]


    if not config.platform.is_apple():
        build.add_cpm_dep("zeux/pugixml", "v1.15", link_name="pugixml-static", options={"PUGIXML_NO_EXCEPTIONS": "ON"})

    build.add_cpm_dep("greg7mdp/gtl", "v1.2.0", link_name="gtl", options={})


    build.add_definition("GEODE_DEFINE_EVENT_EXPORTS")

    # link to opengl stuff & AAssetManager
    if config.platform.is_android():
        build.link_libraries("EGL", "GLESv2", "android")

    if debug:
        build.add_definition("AL_DEBUG")
    if caching:
        build.add_definition("ENABLE_CACHE")

    if install:
        build.enable_hooks()

def post_build(ctx: HookContext):
    if ctx.platform.is_android() and adb.is_device_connected():
        adb.install(ctx.build_dir / "dankmeme.async-load-api.geode")
        adb.launch_game()