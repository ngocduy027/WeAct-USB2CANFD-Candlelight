/* No console/file descriptors are exposed by this firmware. Keep the newlib ABI
 * explicit instead of defining incompatible weak functions in a shared header. */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

int _close(int fd)
{
    (void)fd;
    errno = EBADF;
    return -1;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    (void)fd; (void)offset; (void)whence;
    errno = EBADF;
    return (off_t)-1;
}

ssize_t _read(int fd, void *buffer, size_t length)
{
    (void)fd; (void)buffer; (void)length;
    errno = EBADF;
    return -1;
}

ssize_t _write(int fd, const void *buffer, size_t length)
{
    (void)fd; (void)buffer; (void)length;
    errno = EBADF;
    return -1;
}
