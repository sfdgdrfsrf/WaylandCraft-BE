#include "util/MemFd.h"

#include "util/Log.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#include <sys/ioctl.h>
#include <sys/syscall.h>
// ashmem pin/unpin ioctls
#ifndef ASHMEM_GET_SIZE
#define ASHMEM_GET_SIZE _IO(0x77, 4)
#endif
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#endif

namespace wlc {

int makeMemFd(size_t size) {
    if (size == 0) return -1;

#if defined(__linux__) || defined(__ANDROID__)
#if defined(SYS_memfd_create)
    // 1) Raw syscall memfd_create — works on Android 9+ even though bionic
    //    only exports the wrapper from API 30.
    for (int attempt = 0; attempt < 2; ++attempt) {
        unsigned flags = (attempt == 0) ? MFD_CLOEXEC : 0;
        int fd = static_cast<int>(syscall(SYS_memfd_create, "waylandcraft-shm", flags));
        if (fd >= 0) {
            if (ftruncate(fd, static_cast<off_t>(size)) == 0) return fd;
            ::close(fd);
        }
    }
#endif

#if defined(__ANDROID__)
    // 2) Legacy ashmem.
    int fd = ::open("/dev/ashmem", O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        if (ioctl(fd, ASHMEM_GET_SIZE, 0) >= 0) {
            // Name is optional; size is set with ASHMEM_SET_SIZE. Use the
            // "unpin by resize" trick: ashmem requires the named-set ioctl.
#ifndef ASHMEM_SET_SIZE
#define ASHMEM_SET_SIZE _IOW(0x77, 3, size_t)
#endif
            if (ioctl(fd, ASHMEM_SET_SIZE, size) == 0 &&
                ftruncate(fd, static_cast<off_t>(size)) == 0) {
                return fd;
            }
        }
        ::close(fd);
    }
#endif

    // 3) Unlinked file in a writable tmp dir.
    const char* dirs[] = {getenv("TMPDIR"), "/data/local/tmp", "/tmp", nullptr};
    for (const char** d = dirs; *d; ++d) {
        if (!*d) continue;
        std::string path = strf("%s/wlc-shm-XXXXXX", *d);
        std::vector<char> buf(path.begin(), path.end());
        buf.push_back('\0');
        int fd = ::mkstemp(buf.data());
        if (fd >= 0) {
            ::unlink(buf.data());
            if (ftruncate(fd, static_cast<off_t>(size)) == 0) return fd;
            ::close(fd);
        }
    }
#endif

    // 4) POSIX shm. Not available in bionic (no /dev/shm on Android); the
    //    memfd/ashmem/tmpfile backends above cover every Android device.
#if !defined(__ANDROID__)
    std::string name = strf("/wlc-shm-%d-%d", static_cast<int>(getpid()), rand());
    int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
        shm_unlink(name.c_str());
        if (ftruncate(fd, static_cast<off_t>(size)) == 0) return fd;
        ::close(fd);
    }
#endif

    Log::error("makeMemFd: all backends failed");
    return -1;
}

size_t memFdSize(int fd, size_t fallback) {
    struct stat st{};
    if (fd >= 0 && fstat(fd, &st) == 0 && st.st_size > 0) {
        return static_cast<size_t>(st.st_size);
    }
    return fallback;
}

} // namespace wlc
