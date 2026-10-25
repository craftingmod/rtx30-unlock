/* Minimal errno constants for building the EFI application without a Windows
 * SDK / MSVC CRT. The values match the stable Linux errno values that
 * src/unlock/compat/compat.h documents (identical macro definitions are
 * harmless). Used via CPATH, see docs/build-without-msvc.md. */
#pragma once

#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define ENODEV 19
#define EINVAL 22
#define ENOSPC 28
#define ERANGE 34
#define EPROTO 71
#define EBADMSG 74
#define EOVERFLOW 75
#define EOPNOTSUPP 95
#define ETIMEDOUT 110
#define EALREADY 114
