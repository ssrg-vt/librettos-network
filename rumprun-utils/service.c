/*
 * LibrettOS Network Server Tool
 *
 * Copyright (c) 2019 Mincheol Sung <mincheol@vt.edu>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "service.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

struct arg
{
	unsigned int remote_domain;
	unsigned int remote_port;
};

static int rumprun_ioctl(unsigned int cmd, void* arg)
{
	int fd, rc;

	fd = open("/dev/rumprun_service", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open /dev/rumprun_service: [%s]. %s\n", strerror(errno), errno == EACCES ? "Are you root?" : "Did you load frontend driver?");
		exit(1);
	}
	rc = ioctl(fd, cmd, arg);
	close(fd);
	return rc;
}

int main(int argc, char *argv[])
{
	int rc;
	struct arg args = {0,0};

	if (argc == 3) {
		args.remote_domain = atoi(argv[2]);
	} else if (argc != 2) {
		printf("wrong argument\n");
		return 0;
	}

	if (!strcmp(argv[1], "clean")) {
		rc = rumprun_ioctl(RUMPRUN_SERVICE_IOCTL_CLEANUP, NULL);
	} else if (!strcmp(argv[1], "bind")) {
		/* Always remote port 8 */
		args.remote_port = 8;
		rc = rumprun_ioctl(RUMPRUN_SERVICE_IOCTL_BIND, &args);
	} else if (!strcmp(argv[1], "switch")) {
		rc = rumprun_ioctl(RUMPRUN_SERVICE_IOCTL_SWITCH, &args);
	}

	if (rc < 0)
		printf("Rumrun service failed: %d\n",rc);

	return 0;
}
