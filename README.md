# Async Load

Geometry Dash API mod for fast and asynchronous loading of textures, spritesheets, files, and more.

## Why?

Most of the routines for loading things in GD are terribly inefficient. On top of that, they tend to not be parallelizable - you can only load 1 texture at a time, and you must freeze the main thread while doing so. For most purposes, this ends up being *ok* in practice, but not ideal. The primary issues with the approaches used in cocos2d-x are:

* Poor code quality: inefficient algorithms, lots of allocations
* Use of global state, meaning functions must only be called from main thread
* No work offloading, so loading a texture or resolving a path mid-attempt can cause lag spikes
* No streaming, entire files being read into RAM at once instead of chunk-by-chunk
* Old & naive APIs that work everywhere but aren't efficient

Async Load API is created for mods that handle a significant volume of textures (but you can use it even if not!) and offers an API that fixes all the issues mentioned above. This mod originally began as Globed icon preloading, extracted to [Blaze](https://github.com/dankmeme01/blaze) and improved, and then extracted into a separate, general-purpose API mod.

## Design

When it comes to loading textures and spritesheets, the mod takes a fully async approach. You tell the mod to load a specific texture and a callback is called once it's done. As little work as possible is done on caller (and main) thread, lag spikes are minimized:

```cpp
AsyncLoad::loadTexture(
    "my-texture.png"_spr,
    [](Result<Ref<CCTexture2D>> result) {
        if (texture) {
            // texture is loaded, you can for example create a CCSprite now:
            CCSprite::createWithTexture(result.unwrap());

            // or via name, since `loadTexture` adds it to CCTextureCache this will work instantly:
            CCSprite::create("my-texture.png"_spr);
        } else {
            // descriptive error as to why loading failed
            log::warn("Failed to load: {}", result.unwrapErr());
        }
    }
);
```

This code *just works*! There's nothing fancy you need to do, this is all it takes to efficiently load a texture.

Additionally, the API takes a Task-based approach, allowing you to do cancel texture loads or joining a group of them:
```cpp
AsyncLoad::TaskGroup group;

auto handle = AsyncLoad::loadTexture("my-texture-1.png"_spr, ...);
group.add(handle);

group.add(AsyncLoad::loadTexture("my-texture-2.png"_spr, ...));
group.add(AsyncLoad::loadTexture("my-texture-3.png"_spr, ...));

// Block main thread until all 3 textures are finished
group.wait();

// Or, alternatively, close the group and let it complete asynchronously
group.close([](AsyncLoad::TaskGroupResult result) {
    // ...
});
```

The example above is where this mod shines the most, and allows you to do something impossible in vanilla: instead of sequentially calling `CCTextureCache::addImage`, you enqueue 3 textures to load *at the same time*. Because your computer likely has at least 3 threads, most of the work can be done fully in parallel, and the work ends up finishing ~2-3x faster thanks to parallelizing alone.

## Features

This is a list of what Async Load does, and what cocos2d-x doesn't do or does much worse:

* Memory-mapped IO for lazy reading of files on all platforms
* Incremental PNG decoder via `libspng` (not on iOS)
* Pool of worker threads that handle tasks
* Thread-safety: many lower-level calls like `getFileData`, `fullPathForFilename`, `submitTextureLoad` are fully thread-safe
* Use of OpenGL PBOs (and other modern features) for asynchronous GL texture creation (on supported devices)
* Use of SIMD where applicable, for performance increase on modern CPUs
* Fast parser of `.plist` spritesheets using pugixml (~9x faster, non-Apple)
* Blazingly fast of binary `.plist` spritesheets using `NSPropertyListSerialization` (Apple)
* Texture Loader-style merging of other spritesheets with same name

## Roadmap

* Finish AsyncSprite, maybe parity with LazySprite
* Async SimplePlayer & PlayerObject
