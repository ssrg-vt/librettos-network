SRCS+=	xen/gnttab.c
SRCS+=	xen/gntmap.c
SRCS+=	xen/events.c
SRCS+=	xen/hypervisor.c

ifdef BACKEND
SRCS+=  xen/backend.c
endif

ifdef FRONTEND
SRCS+=  xen/frontend.c
endif

.PHONY: xendir
xendir:
	mkdir -p ${RROBJ}/platform/xen
