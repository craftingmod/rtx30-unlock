/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TRANSACTION_TEST_LINUX_ERRNO_H
#define TRANSACTION_TEST_LINUX_ERRNO_H
/* Keep Linux return values consistent across the real core and Windows harness. */
#include <errno.h>
#undef ENOENT
#undef EIO
#undef EAGAIN
#undef ENOMEM
#undef EBUSY
#undef ENODEV
#undef EINVAL
#undef ERANGE
#undef EPROTO
#undef EBADMSG
#undef EOVERFLOW
#undef EOPNOTSUPP
#undef ETIMEDOUT
#undef EALREADY
#define ENOENT 2
#define EIO 5
#define EAGAIN 11
#define ENOMEM 12
#define EBUSY 16
#define ENODEV 19
#define EINVAL 22
#define ERANGE 34
#define EPROTO 71
#define EBADMSG 74
#define EOVERFLOW 75
#define EOPNOTSUPP 95
#define ETIMEDOUT 110
#define EALREADY 114
#endif
