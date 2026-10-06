// libc functions a PS5 title has no export for and the platform layer does not answer.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

// curl's wakeup pipe: pipe() plus the flags, as FreeBSD's pipe2 does
int pipe2(int fds[2], int flags)
{
	if (pipe(fds) != 0)
		return -1;

	for (int i = 0; i < 2; i++)
	{
		if ((flags & O_NONBLOCK) && fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK) == -1)
			goto fail;
		if ((flags & O_CLOEXEC) && fcntl(fds[i], F_SETFD, FD_CLOEXEC) == -1)
			goto fail;
	}

	return 0;

fail:
	close(fds[0]);
	close(fds[1]);
	return -1;
}

// Only LLVM's malloc-usage statistics ask; the heap is the platform layer's, not a break
void* sbrk(intptr_t increment)
{
	(void)increment;
	errno = ENOMEM;
	return (void*)-1;
}

// Only LLVM's path limits ask: FreeBSD's defaults
long pathconf(const char* path, int name)
{
	(void)path;
	switch (name)
	{
	case _PC_NAME_MAX: return 255;
	case _PC_PATH_MAX: return 1024;
	default: errno = EINVAL; return -1;
	}
}
