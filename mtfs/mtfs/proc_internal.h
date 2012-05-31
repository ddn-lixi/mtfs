/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#ifndef __MTFS_PROC_INTERNAL_H__
#define __MTFS_PROC_INTERNAL_H__
int mtfs_insert_proc(void);
void mtfs_remove_proc(void);
void mtfs_proc_remove(struct proc_dir_entry **rooth);

#ifdef HAVE_SYSCTL_UNNUMBERED
#define CTL_MTFS          CTL_UNNUMBERED
#define MTFS_PROC_DEBUG   CTL_UNNUMBERED
#define MTFS_PROC_MEMUSED CTL_UNNUMBERED
#else /* !define(HAVE_SYSCTL_UNNUMBERED) */
#define CTL_MTFS         (0x100)
enum {
	MTFS_PROC_DEBUG = 1, /* control debugging */
	MTFS_PROC_MEMUSED,   /* bytes currently allocated */
};
#endif /* !define(HAVE_SYSCTL_UNNUMBERED) */

struct mtfs_proc_vars {
        const char   *name;
        read_proc_t *read_fptr;
        write_proc_t *write_fptr;
        void *data;
        struct file_operations *fops;
        /**
         * /proc file mode.
         */
        mode_t proc_mode;
};

struct proc_dir_entry *mtfs_proc_register(const char *name,
                                            struct proc_dir_entry *parent,
                                            struct mtfs_proc_vars *list, void *data);
extern struct proc_dir_entry *mtfs_proc_root;
extern struct proc_dir_entry *mtfs_proc_device;
#endif /* __MTFS_PROC_INTERNAL_H__ */
