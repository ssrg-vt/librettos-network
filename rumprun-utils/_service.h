#ifndef _RUMPRUN_COMMON_SERVICE_H
#define _RUMPRUN_COMMON_SERVICE_H 1

#define RUMPRUN_IOCTL_MAGIC					0x82
#define RUMPRUN_SERVICE_IOCTL_CLEANUP		_IO(RUMPRUN_IOCTL_MAGIC, 0)
#define RUMPRUN_SERVICE_IOCTL_BIND		_IO(RUMPRUN_IOCTL_MAGIC, 1)
#define RUMPRUN_SERVICE_IOCTL_SWITCH		_IO(RUMPRUN_IOCTL_MAGIC, 2)

#endif /* !_RUMPRUN_COMMON_SERVICE_H */
