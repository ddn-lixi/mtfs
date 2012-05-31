/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#if MTFS_IS_LUSTRE
#include <liblustre.h>
#include <lnet/lnetctl.h>
#include <obd.h>
#include <lustre_lib.h>
#include <obd_lov.h>
#include <lustre/liblustreapi.h>
#else /* MTFS_IS_LUSTRE */
#include <string.h>
#define GOTO(label, rc)   do { rc; goto label; } while (0)
#endif /* MTFS_IS_LUSTRE */

#define ACT_NONE        0
#define ACT_READ        1
#define ACT_WRITE       2
#define ACT_SEEK        4
#define ACT_READHOLE    8
#define ACT_VERIFY      16

void usage()
{
	printf("usage: rwv -f filename <-r|-w> [-a] [-z] [-d] [-v]"
                "[-s offset] -n iovcnt SIZE1 SIZE2 SIZE3...\n");
        printf("-a  append IO (O_APPEND)\n");
        printf("-r  file read (O_RDONLY)\n");
        printf("-w  file write (O_WRONLY)\n");
        printf("-s  set the start pos of the read/write test\n");
        printf("-z  test for read hitting hole\n");
        printf("-d  create flags (O_LOV_DELAY_CREATE)\n");
        printf("-v  verify the data content of read\n");
}

int data_verify(struct iovec *iov, int iovcnt, char c)
{
        int i;

        for (i = 0; i < iovcnt; i++) {
                size_t count = iov[i].iov_len;
                char *s = iov[i].iov_base;

                for (; count > 0; ++s, count--) {
                        if (*s != c) {
                                printf("Data mismatch %x: %x\n", *s, c);
                                return 1;
                        }
                }
        }
        return 0;
}

int main(int argc, char** argv)
{
        int c;
        int fd;
        int rc = 0;
        int flags = 0;
        int iovcnt = 0;
        int act = ACT_NONE;
        char pad = 0xba;
        char *end;
        char *fname = "FILE";
        unsigned long len = 0;
        struct iovec *iov;
        off64_t offset = 0;

        while ((c = getopt(argc, argv, "f:n:s:rwahvdz")) != -1) {
                switch (c) {
                case 'f':
                        fname = optarg;
                        break;
                case 'n':
                        iovcnt = strtoul(optarg, &end, 0);
                        if (*end) {
                                printf("Bad iov count: %s\n", optarg);
                                return 1;
                        }
                        if (iovcnt > UIO_MAXIOV || iovcnt <= 0) {
                                printf("Wrong iov count\n");
                                return 1;
                        }
                        break;
                case 's':
                        act |= ACT_SEEK;
                        offset = strtoull(optarg, &end, 0);
                        if (*end) {
                                printf("Bad seek offset: %s\n", optarg);
                                return 1;
                        }
                        break;
                case 'w':
                        act |= ACT_WRITE;
                        break;
                case 'r':
                        act |= ACT_READ;
                        break;
                case 'a':
                        flags |= O_APPEND;
                        break;
#if MTFS_IS_LUSTRE
                case 'd':
                        flags |= O_LOV_DELAY_CREATE;
                        break;
#endif /* MTFS_IS_LUSTRE */
                case 'z':
                        pad = 0;
                        act |= ACT_READHOLE;
                        break;
                case 'v':
                        act |= ACT_VERIFY;
                        break;
                case 'h':
                        usage();
                        break;
                }
        }

        if (act == ACT_NONE) {
                usage();
                return 1;
        }

        if ((act & ACT_READ) &&  (act & ACT_WRITE)) {
                printf("Read and write test should be exclusive\n");
                return 1;
        }

        if (argc - optind < iovcnt) {
                printf("Not enough parameters for iov size\n");
                return 1;
        }

        iov = (struct iovec *)malloc(iovcnt * sizeof(struct iovec));
        if (iov == NULL) {
                printf("No memory %s\n", strerror(errno));
                return 1;
        }

        for (c = 0; c < iovcnt; c++) {
                struct iovec *iv = &iov[c];

                iv->iov_len = strtoul(argv[optind++], &end, 0);
                if (*end) {
                        printf("Error iov size\n");
                        GOTO(out, rc = 1);
                }
                iv->iov_base = mmap(NULL, iv->iov_len, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANON, 0, 0);
                if (iv->iov_base == MAP_FAILED) {
                        printf("No memory %s\n", strerror(errno));
                        GOTO(out, rc = 1);
                }
                if (act & ACT_WRITE)
                        memset(iv->iov_base, pad, iv->iov_len);
                len += iv->iov_len;
        }

        fd = open(fname, O_LARGEFILE | O_RDWR | O_CREAT | flags, 0644);
        if (fd == -1) {
                printf("Cannot open %s:%s\n", fname, strerror(errno));
                return 1;
        }

        if ((act & ACT_SEEK) && (lseek64(fd, offset, SEEK_SET) < 0)) {
                printf("Cannot seek %s\n", strerror(errno));
                GOTO(out, rc = 1);
        }

        if (act & ACT_WRITE) {
                rc = writev(fd, iov, iovcnt);
                if (rc != len) {
                        printf("Write error: %s (rc = %d, len = %ld)\n",
                                strerror(errno), rc, len);
                        GOTO(out, rc = 1);
                }
        } else if (act & ACT_READ) {
                rc = readv(fd, iov, iovcnt);
                if (rc != len) {
                        printf("Read error: %s rc = %d\n", strerror(errno), rc);
                        GOTO(out, rc = 1);
                }

                /* It should return zeroed buf if the read hits hole.*/
                if (((act & ACT_READHOLE) || (act & ACT_VERIFY)) &&
                    data_verify(iov, iovcnt, pad))
                        GOTO(out, rc = 1);
        }

        rc = 0;
out:
        if (iov)
                free(iov);
	return rc;
}
