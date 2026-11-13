#import <Foundation/Foundation.h>
#include <Geode/Geode.hpp>
#include <AsyncLoad/FileUtils.hpp>
#include <asp/fs.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

static bool isRegularFile(NSString* p) {
    BOOL isDir = NO;
    return [[NSFileManager defaultManager] fileExistsAtPath:p isDirectory:&isDir] && !isDir;
}

// CCFileUtils::isFileExist on MacOS will always return false if the path is a
// relative path that does not contain any slashes, so we reimplement it properly.
// Thanks cocos!
bool impl::fileExists(ZStringView path) {
    if (path.empty()) return false;

    auto view = path.view();

    if (view[0] == '/') {
        // absolute path
        return isRegularFile([NSString stringWithUTF8String:path.c_str()]);
    }

    StringBuffer<512> pathBuf;
    StringBuffer<512> fileBuf;
    size_t pos = view.find_last_of('/');
    if (pos != std::string::npos) {
        fileBuf.append(view.substr(pos + 1));
        pathBuf.append(view.substr(0, pos));
    } else {
        fileBuf.append(view);
    }

    NSString* nspath = pathBuf.size() == 0 ? nil : [NSString stringWithUTF8String:pathBuf.c_str()];
    NSString* nsfile = [NSString stringWithUTF8String:fileBuf.c_str()];
    NSString* fullpath = [[NSBundle mainBundle] pathForResource:nsfile ofType:nil inDirectory:nspath];

    return fullpath && isRegularFile(fullpath);
}

std::string getPathForDirAndFilenameImpl(ZStringView directory, ZStringView filename) {
    if (!directory.empty() && directory.view().front() == '/') {
        // absolute path
        StringBuffer<1024> fullBuf;
        fullBuf.append("{}{}", directory, filename);
        if ([[NSFileManager defaultManager] fileExistsAtPath:[NSString stringWithUTF8String:fullBuf.c_str()]]) {
            return fullBuf.str();
        }
    } else {
        // relative path
        NSString* fp = [[NSBundle mainBundle]
            pathForResource:[NSString stringWithUTF8String:filename.c_str()]
            ofType:nil
            inDirectory:[NSString stringWithUTF8String:directory.c_str()]
        ];
        if (fp) {
            return std::string([fp UTF8String]);
        }
    }
    return std::string{};
}


Result<CachedBufferChunk> impl::getFileData(ZStringView path) {
    NSString* nsp = [NSString stringWithUTF8String:path.c_str()];
    NSError* error = nil;
    NSData* data = [NSData dataWithContentsOfFile:nsp options:NSDataReadingMappedIfSafe error:&error];

    if (data) {
        auto buffer = BufferCache::get().getSized([data length]);
        [data getBytes:buffer.data() length:buffer.size()];
        return Ok(std::move(buffer));
    }

    return Err("Failed to read path '{}': {}", path, [[error localizedDescription] UTF8String]);
}

Result<OwnedBuffer> impl::getFileDataOwned(ZStringView path) {
    NSString* nsp = [NSString stringWithUTF8String:path.c_str()];
    NSError* error = nil;
    NSData* data = [NSData dataWithContentsOfFile:nsp options:NSDataReadingMappedIfSafe error:&error];

    if (data) {
        auto length = [data length];
        auto buffer = std::make_unique_for_overwrite<uint8_t[]>(length);
        [data getBytes:buffer.get() length:length];
        return Ok(OwnedBuffer {
            std::move(buffer),
            length
        });
    }

    return Err("Failed to read path '{}': {}", path, [[error localizedDescription] UTF8String]);
}

// mmap implementation

Result<FileMappedBuffer> impl::getMappedFile(ZStringView path) {
    auto file = GEODE_UNWRAP(asp::fs::File::open(path.view()));
    return FileMappedBuffer::createWithFile(std::move(file));
}

Result<FileMetadata> impl::getFileMetadata(geode::ZStringView path) {
    auto meta = GEODE_UNWRAP(asp::fs::PathView{path.view()}.metadata());
    return Ok(FileMetadata {
        .canMap = meta.isFile(),
        .size = meta.size
    });
}

}
