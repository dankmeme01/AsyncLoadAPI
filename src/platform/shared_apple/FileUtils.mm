#pragma once
#import <Foundation/Foundation.h>
#include <AsyncLoad/FileUtils.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>

using namespace geode::prelude;

namespace AsyncLoad {

// CCFileUtils::isFileExist on MacOS will always return false if the path is a
// relative path that does not contain any slashes, so we reimplement it properly.
// Thanks cocos!
bool fileExists(ZStringView path) {
    if (path.empty()) return false;

    auto view = path.view();

    if (view[0] == '/') {
        // absolute path
        return [[NSFileManager defaultManager] fileExistsAtPath:[NSString stringWithUTF8String:path.c_str()]];
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

    return fullpath != nil;
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


Result<CachedBufferChunk> getFileDataImpl(ZStringView path) {
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

Result<OwnedBuffer> getFileDataOwnedImpl(ZStringView path) {
    NSString* nsp = [NSString stringWithUTF8String:path.c_str()];
    NSError* error = nil;
    NSData* data = [NSData dataWithContentsOfFile:nsp options:NSDataReadingMappedIfSafe error:&error];

    if (data) {
        auto length = [data length];
        auto buffer = std::make_unique_for_overwrite<uint8_t[]>(length);
        [data getBytes:buffer.data() length:length];
        return Ok(std::move(buffer));
    }

    return Err("Failed to read path '{}': {}", path, [[error localizedDescription] UTF8String]);
}

// mmap implementation

Result<FileMappedBuffer> getMappedFileImpl(ZStringView path) {
    if (!canMapFile(path, true)) {
        return Err("File '{}' cannot be memory mapped, it is not a file on disk", path);
    }

    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        return Err("Failed to open file '{}', errno: {}", path, errno);
    }

    return FileMappedBuffer::createWithFd(fd);
}

bool canMapFile(geode::ZStringView path, bool assumeFullPath) {
    return true;
}

Result<> FileMappedBuffer::_map() {
    struct stat s;
    if (fstat(m_fd, &s) == -1) {
        return Err("Failed to stat file '{}', errno: {}", m_fd, errno);
    }

    m_size = s.st_size;
    auto ptr = mmap(nullptr, m_size, PROT_READ, MAP_PRIVATE, m_fd, 0);
    if (ptr == MAP_FAILED) {
        return Err("Failed to mmap file '{}', errno: {}", m_fd, errno);
    }

    m_ptr = (uint8_t*)ptr;
    return Ok();
}

void FileMappedBuffer::_destroy() {
    if (m_ptr) {
        munmap(m_ptr, m_size);
    }

    if (m_fd != INVALID_FD) {
        close(m_fd);
    }
}

}
