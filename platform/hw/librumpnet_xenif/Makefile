.include <bsd.own.mk>
.include "../../../netdom.mk"

LIB=	rumpnet_xenif

SRCS=	if_virt.c
SRCS+=	xenif_component.c

RUMPTOP= ${TOPRUMP}

IFBASE=		-DVIRTIF_BASE=xenif

CPPFLAGS+=	-I${RUMPTOP}/librump/rumpkern -I${RUMPTOP}/librump/rumpnet
CPPFLAGS+=	-I${.CURDIR}
CPPFLAGS+=	${IFBASE} ${NETDOM_FLAG}

RUMPCOMP_USER_SRCS=     xenif_user.c
RUMPCOMP_USER_CPPFLAGS+= -I${.CURDIR}/..
RUMPCOMP_USER_CPPFLAGS+= -I${.CURDIR}/../include
RUMPCOMP_USER_CPPFLAGS+= -I${.CURDIR}/../../xen/xen/include
RUMPCOMP_USER_CPPFLAGS+= -I${.CURDIR}/../../../include
RUMPCOMP_USER_CPPFLAGS+= -I../../include
RUMPCOMP_USER_CPPFLAGS+= ${IFBASE} ${NETDOM_FLAG}

# XXX
.undef RUMPKERN_ONLY

.include "${RUMPTOP}/Makefile.rump"
.include <bsd.lib.mk>
.include <bsd.klinks.mk>
