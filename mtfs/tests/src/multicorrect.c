/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#include <memory.h>
#include <mtfs_random.h>
#include <debug.h>
#include <multithread.h>
#include <debug.h>
#include <memory.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <inttypes.h>

typedef struct block {
	char *data;
	size_t block_size;
} block_t;

#define MAX_BUFF_LENGTH 104857600
#define MAX_LSEEK_OFFSET MAX_BUFF_LENGTH
pthread_rwlock_t rwlock;
#define MAX_BNUM 16
int bnum = 0;
char *branch_path[MAX_BNUM];
int branch_fd[MAX_BNUM];
char *path;
int fd;

static inline block_t *alloc_block(size_t block_size)
{
	block_t *block = NULL;
	
	MASSERT(block_size > 0);
	
	MTFS_ALLOC_PTR(block);
	if (block == NULL) {
		goto out;
	}
	
	MTFS_ALLOC(block->data, block_size);
	if (block->data == NULL) {
		goto free_block;
	}

	block->block_size = block_size;
	goto out;

free_block:
	MTFS_FREE_PTR(block);
out:
	return block;
}

static inline int free_block(block_t *block)
{
	MTFS_FREE(block->data, block->block_size);
	MTFS_FREE_PTR(block);
	return 0;
}

int random_fill_block(block_t *block)
{
	static int fill_char = 0;
	static int fill_max = 0;
	int fill_time = 0;
	int char_number = block->block_size;
	int left_char = 0;
	int i = 0;
	int j = 0;
	int data = 0;
	int position = 0;
	
	if (fill_char == 0) {
		if (RAND_MAX >= 0xFFFFFF) {
			fill_char = 3;
			fill_max = 0xFFFFFF;
		} else if (RAND_MAX >= 0xFFFF) {
			fill_char = 2;
			fill_max = 0xFFFF;
		} else if (RAND_MAX >= 0xFF) {
			fill_char = 1;
			fill_max = 0xFF;
		}
		//MASSERT(fill_char);
	}
	
	fill_time = char_number / fill_char;
	left_char = char_number - fill_time * fill_char;

	for(i = 0, position = 0; i < fill_time; i++) {
		data = mtfs_random() & fill_max;
		for(j = 0; j < fill_char; j++) {
			block->data[position] = data;
			data >>= 8;
			position++;
		}
	}

	for(; position < char_number; position ++) {
		block->data[position] = mtfs_random() & 0xFF;
	}
	return 0;
}

int dump_block(block_t *block)
{
	int i = 0;

	MPRINT("block_size: %zd\n", block->block_size);
	for(i = 0; i < block->block_size; i++) {
		MPRINT("%02X", (uint8_t)(block->data[i]));
	}
	MPRINT("\n");
	return 0;
}

int cmp_block(block_t *s1, block_t *s2)
{
	MASSERT(s1->block_size == s2->block_size);

	return memcmp(s1->data, s2->data, s1->block_size);
}

off_t random_get_offset()
{
	MASSERT(MAX_LSEEK_OFFSET >= 1);
	return mtfs_rand_range(0, MAX_LSEEK_OFFSET);
}

#define MAX_USLEEP_TIME 1000
void sleep_random()
{
	useconds_t useconds = mtfs_rand_range(1, MAX_USLEEP_TIME);
	usleep(useconds);
}

int begin = 0;
void *write_little_proc(struct thread_info *thread_info)
{
	size_t block_size = 1;
	size_t count = 0;
	block_t *block = NULL;
	off_t offset = 0;
	ssize_t writed = 0;
	int ret = 0;

	ret = prctl(PR_SET_NAME, thread_info->identifier, 0, 0, 0);
	if (ret) {
		MERROR("failed to set thead name\n");
		goto out;
	}	

	block = alloc_block(block_size);
	if (block == NULL) {
		MERROR("Not enough memory\n");
		goto out;
	}

	random_fill_block(block);
	while (1) {
		pthread_rwlock_rdlock(&rwlock);
		{
			count = block_size;
			offset = random_get_offset();
			MPRINT("writing %zd at %lu\n", count, offset);
			writed = pwrite(fd, block->data, count, offset);
			if (writed != count) {
				MERROR("failed to write, "
				       "expected %zd, got %zd\n", 
				       count, writed);
				exit(0);
			}
			MPRINT("writed %zd at %lu\n", writed, offset);
		}
		pthread_rwlock_unlock(&rwlock);
		sleep_random();
	}

	free_block(block);	
out:
	return NULL;
}

void *write_proc(struct thread_info *thread_info)
{
	size_t block_size = MAX_BUFF_LENGTH;
	size_t count = 0;
	block_t *block = NULL;
	off_t offset = 0;
	off_t writed = 0;
	int ret = 0;

	ret = prctl(PR_SET_NAME, thread_info->identifier, 0, 0, 0);
	if (ret) {
		MERROR("failed to set thead name\n");
		goto out;
	}	

	block = alloc_block(block_size);
	if (block == NULL) {
		MERROR("Not enough memory\n");
		goto out;
	}

	random_fill_block(block);
	while (1) {
		pthread_rwlock_rdlock(&rwlock);
		{
			count = block_size;
			offset = 0;
			MPRINT("writing %zd at %lu\n", count, offset);
			writed = pwrite(fd, block->data, count, offset);
			if (writed != count) {
				MERROR("failed to write, "
				       "expected %zd, got %lu\n", 
				       count, writed);
				exit(0);
			}
			MPRINT("writed %lu at %lu\n", writed, offset);
			begin = 0;
		}
		pthread_rwlock_unlock(&rwlock);
		sleep_random();
	}

	free_block(block);	
out:
	return NULL;
}

void *check_proc(struct thread_info *thread_info)
{
	char *buf = NULL;
	size_t count = MAX_BUFF_LENGTH;
	off_t read_count = 0;
	off_t first_read_count = 0;
	char *first_buf = NULL;
	int done = 0;
	struct stat *stat_buf = NULL;
	struct stat *first_stat_buf = NULL;
	int ret = 0;
	int i = 0;

	ret = prctl(PR_SET_NAME, thread_info->identifier, 0, 0, 0);
	if (ret) {
		MERROR("failed to set thead name\n");
		goto out;
	}	

	MTFS_ALLOC(buf, count);
	if (buf == NULL) {
		MERROR("not enough memory\n");
		goto out;
	}

	MTFS_ALLOC(first_buf, count);
	if (first_buf == NULL) {
		MERROR("not enough memory\n");
		goto out;
	}

	MTFS_ALLOC_PTR(stat_buf);
	if (stat_buf == NULL) {
		MERROR("not enough memory\n");
		goto out;
	}

	MTFS_ALLOC_PTR(first_stat_buf);
	if (first_stat_buf == NULL) {
		MERROR("not enough memory\n");
		goto out;
	}

	while (1) {
		pthread_rwlock_wrlock(&rwlock);
		{
			MPRINT("checking\n");
			ret = stat(branch_path[0], first_stat_buf);
			if (ret) {
				MERROR("failed to stat\n");
				goto out;
			}

			ret = lseek(branch_fd[0], 0, SEEK_SET);
			if (ret) {
				MERROR("failed to lseek\n");
				goto out;
			}

			for (i = 1; i < bnum; i++) {
				ret = stat(branch_path[i], stat_buf);
				if (ret) {
					MERROR("failed to stat\n");
					goto out;
				}

				if (stat_buf->st_size != first_stat_buf->st_size) {
					MERROR("size differ\n");
					goto out;
				}

				ret = lseek(branch_fd[i], 0, SEEK_SET);
				if (ret) {
					MERROR("failed to lseek\n");
					goto out;
				}
			}

			done = 0;
			do {
				first_read_count = read(branch_fd[0], first_buf, count);
				if (first_read_count < 0) {
					MERROR("failed to read\n");
					goto out;
				}

				for (i = 1; i < bnum; i++) {
					read_count = read(branch_fd[i], buf, count);
					if (read_count < 0) {
						MERROR("failed to read\n");
						goto out;
					}

					if (read_count != first_read_count) {
						MERROR("read count differ\n");
						goto out;
					}

					if (memcmp(first_buf, buf, read_count) != 0) {
						MERROR("buff differ\n");
						goto out;
					}
				}
			} while (first_read_count != 0);
			MPRINT("checked\n");
		}
		pthread_rwlock_unlock(&rwlock);
		sleep(10);
	}
out:
	if (first_buf != NULL) {
		MTFS_FREE(first_buf, count);
	}
	if (buf != NULL) {
		MTFS_FREE(buf, count);
	}
	if (stat_buf != NULL) {
		MTFS_FREE_PTR(stat_buf);
	}	
	if (first_stat_buf != NULL) {
		MTFS_FREE_PTR(first_stat_buf);
	}
	exit(1);
	return NULL;
}

struct thread_group thread_groups[] = {
	{1, "write_little", (void *(*)(struct thread_info *))write_little_proc, NULL},
	{1, "write", (void *(*)(struct thread_info *))write_proc, NULL},
	{1, "check", (void *(*)(struct thread_info *))check_proc, NULL},
	{0, NULL, NULL, NULL}, /* for end detection */
};

void usage(const char *progname)
{
	fprintf(stderr, "usage: %s [-s seconds] [-c] file bfile0...\n"
		"\t-s: seconds to test\n"
		"\t-c: run check thread\n", progname);
}

	
int main(int argc, char *argv[])
{
	int ret = 0;
	int seconds = 60;
	int i = 0;
	char *end = NULL;
	int check = 0;
	int c;

	while ((c = getopt(argc, argv, "s:c")) != EOF) {
		switch(c) {
		case 's':
			i = strtoul(optarg, &end, 0);
			if (i && end != NULL && *end == '\0') {
				seconds = i;
			} else {
				MERROR("bad seconds: '%s'\n", optarg);
				usage(argv[0]);
				return 1;
			}
			break;
		case 'c':
			check++;
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}
	
	if (argc < optind + 2) {
		usage(argv[0]);
		return 1;
	}

	bnum = argc - optind - 1;
	path = argv[optind];
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
	if (fd < 0) {
		MERROR("failed to open %s: %s\n", path, strerror(errno));
		ret = errno;
		goto out;
	}
	printf("file = %s\n", path);
	optind++;


	for (i = 0; i < bnum; i++, optind++) {
		branch_path[i] = argv[optind];
		printf("bfile[%d] = %s\n", i, branch_path[i]);
		branch_fd[i] = open(branch_path[i], O_RDONLY);
		if (branch_fd[i] < 0) {
			MERROR("failed to open %s: %s\n", branch_path[i], strerror(errno));
			ret = errno;
			goto out;
		}
	}

	ret = pthread_rwlock_init(&rwlock, NULL);
	if (ret) {
		goto out;
	}

	ret = create_thread_group(&thread_groups[0], NULL);
	if (ret == -1) {
		goto out;
	}

	ret = create_thread_group(&thread_groups[1], NULL);
	if (ret == -1) {
		goto out;
	}

	if (check) {
		ret = create_thread_group(&thread_groups[2], NULL);
		if (ret == -1) {
			goto out;
		}
	}

	for (i = 0; i < seconds; i+=10) {
		sleep(10);
		MPRINT(".");
		MFLUSH();
	}
	pthread_rwlock_destroy(&rwlock);
out:
	return ret;
}
