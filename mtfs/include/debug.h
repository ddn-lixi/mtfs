/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#ifndef __MTFS_DEBUG_H__
#define __MTFS_DEBUG_H__
#include <libcfs/libcfs.h>
#ifdef LUSTRE_IS_NOT_2_0
#else
#include <libcfs/kp30.h>
#endif
#if defined (__linux__) && defined(__KERNEL__)

#define HASSERT LASSERT
#define HBUG LBUG

#if 1
#define HDEBUG(format, args...)     CDEBUG(D_INFO, "mtfs: "format, ##args)
#else
#define HDEBUG(format, args...)     CERROR("mtfs: "format, ##args)
#endif
#define HDEBUG_MEM(format, args...) CDEBUG(D_SUPER, format, ##args)
#define HERROR(format, args...)     CERROR("mtfs: "format, ##args)
#define HWARN(format, args...)      CERROR("mtfs: "format, ##args)
#define HPRINT(format, args...)     CERROR("mtfs: "format, ##args)

#else /* !(defined (__linux__) && defined(__KERNEL__)) */
#include <errno.h>
#include <stdio.h>
#include <assert.h>

#define HASSERT(e) assert(e)

#define HDEBUG(format, args...) fprintf(stderr, "DEBUG: %s(%d) %s(): " format, __FILE__,  __LINE__, __FUNCTION__, ##args)
#define HERROR(format, args...) fprintf(stderr, "ERROR: %s(%d) %s(): " format, __FILE__,  __LINE__, __FUNCTION__, ##args)
#define HWARN(format, args...)  fprintf(stderr, "WARN: %s(%d) %s(): " format, __FILE__,  __LINE__, __FUNCTION__, ##args)
#define HPRINT(format, args...) fprintf(stdout, format, ##args)
#define HFLUSH() fflush(stdout)

#define IS_ERR(a) ((unsigned long)(a) > (unsigned long)-1000L)
#define PTR_ERR(a) ((long)(a))
#define ERR_PTR(a) ((void*)((long)(a)))
#endif /* !(defined (__linux__) && defined(__KERNEL__)) */

#if defined(__GNUC__)
#define HENTRY()                                                        \
do {                                                                    \
        HDEBUG("entered\n");                                            \
} while (0)

#define _HRETURN()                                                      \
do {                                                                    \
        HDEBUG("leaving\n");                                            \
        return;                                                         \
} while (0)

#define HRETURN(ret)                                                    \
do {                                                                    \
        typeof(ret) RETURN__ret = (ret);                                \
        HDEBUG("leaving (ret = %lu:%ld:0x%lx)\n",                       \
               (long)RETURN__ret, (long)RETURN__ret, (long)RETURN__ret);\
        return RETURN__ret;                                             \
} while (0)
#else
# error "Unkown compiler"
#endif /* __GNUC__ */

#endif /* __MTFS_DEBUG_H__ */
