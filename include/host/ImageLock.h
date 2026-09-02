/**
 * @file ImageLock.h
 * @brief Advisory lock on a host disk image, so the build cannot rewrite an
 *        image a running machine is using.
 *
 * The hazard this exists for: `ninja disk` rewrites disk.img in place with
 * std::ios::trunc, and BlockDevice re-opens the image by path on every sector
 * access, so a rebuild while a machine is up is adopted instantly with nothing
 * to signal it. DOS carries live per-file state across that swap (allocated
 * against the old FAT), so a save in flight stamps a directory entry pointing
 * at a chain the new image does not agree with.
 *
 * The contract is deliberately asymmetric:
 *
 *   - A running machine takes a SHARED lock for its session and never blocks
 *     anyone. Failure to take it is ignored -- the machine must boot even on a
 *     filesystem with no working advisory locks (some network mounts), and an
 *     image that does not exist yet is a legitimate all-zero disk.
 *   - The build takes an EXCLUSIVE lock before writing and refuses if it cannot
 *     get one, because that means a machine has the image.
 *
 * Advisory, not mandatory: anything that does not participate is unaffected, so
 * this cannot break hand-editing an image or reading one with another tool.
 * Locks are released when the holding process exits however it exits, so there
 * is no stale lock file to explain away -- which is the main reason this is a
 * lock rather than a PID file.
 */

#ifndef HOST_IMAGELOCK_H
#define HOST_IMAGELOCK_H

#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Host
{
    /// Which kind of claim to stake on the image.
    enum class LockMode
    {
        Shared,   ///< a running machine: coexists with other readers
        Exclusive ///< the build: fails if any machine holds the image
    };

    /**
     * @class ImageLock
     * @brief RAII advisory lock on one image file. Move-only; releases on destruction.
     */
    class ImageLock
    {
    public:
        ImageLock() = default;
        ~ImageLock() { release(); }

        ImageLock(const ImageLock &) = delete;
        ImageLock &operator=(const ImageLock &) = delete;

        ImageLock(ImageLock &&other) noexcept : handle_(other.handle_)
        {
            other.handle_ = kNoHandle;
        }
        ImageLock &operator=(ImageLock &&other) noexcept
        {
            if (this != &other)
            {
                release();
                handle_ = other.handle_;
                other.handle_ = kNoHandle;
            }
            return *this;
        }

        /**
         * @brief Try to claim @p path without blocking.
         * @return true if the claim succeeded, or if there is nothing to claim.
         *
         * A missing file returns true for both modes and leaves nothing held: no
         * machine can be using an image that does not exist, so there is no
         * conflict to report. The caller cannot distinguish "locked" from
         * "nothing to lock", and does not need to -- both mean "go ahead".
         */
        bool tryAcquire(const std::string &path, LockMode mode)
        {
            release();

#if defined(_WIN32)
            const HANDLE h = CreateFileA(path.c_str(), GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE)
            {
                const DWORD err = GetLastError();
                return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
            }

            DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
            if (mode == LockMode::Exclusive) flags |= LOCKFILE_EXCLUSIVE_LOCK;
            OVERLAPPED ov{};
            if (!LockFileEx(h, flags, 0, MAXDWORD, MAXDWORD, &ov))
            {
                CloseHandle(h);
                return false;
            }
            handle_ = h;
            return true;
#else
            const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) return errno == ENOENT;

            // flock attaches to the open file description, not the process, so two
            // opens of one path conflict even from the same program -- which is what
            // makes this testable in a single process.
            const int op = (mode == LockMode::Exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB;
            if (::flock(fd, op) != 0)
            {
                ::close(fd);
                return false;
            }
            handle_ = fd;
            return true;
#endif
        }

        /// Drop the claim, if any. Safe to call when nothing is held.
        void release()
        {
            if (handle_ == kNoHandle) return;
#if defined(_WIN32)
            CloseHandle(handle_);
#else
            ::close(handle_); // closing releases the flock
#endif
            handle_ = kNoHandle;
        }

        /// Whether a real lock is being held (false when the file was absent).
        [[nodiscard]] bool held() const { return handle_ != kNoHandle; }

    private:
#if defined(_WIN32)
        using Handle = HANDLE;
        static inline const Handle kNoHandle = INVALID_HANDLE_VALUE;
#else
        using Handle = int;
        static constexpr Handle kNoHandle = -1;
#endif
        Handle handle_ = kNoHandle;
    };
} // namespace Host

#endif // HOST_IMAGELOCK_H
