/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

/*
 * For both kernel and userspace use
 * DO NOT use anything special that opposes this purpose
 */
#include <parse_option.h>
#include <parser.h>

enum {
	opt_debug, /* Set debug_level */
	opt_dirs, /* Set dirs */
	opt_err /* Error */
};

static match_table_t tokens = {
	{ opt_debug, "debug=%d" },
	{ opt_dirs, "device=%s" },
	{ opt_err, NULL }
};

int parse_dir_option(char *dir_option, mount_option_t *mount_option)
{
	int ret = 0;
	char *start = dir_option;
	char *end = NULL;
	char *tmp = NULL;
	int branch_number = 0;
	int tmp_branch_number = 0;

	HASSERT(dir_option);
	HASSERT(mount_option);
	
	/* We don't want to go off the end of our dir arguments later on. */
	for (end = start; *end; end++) ;

	/* colon at the beginning */
	if (*start == ':') {
		start++;
	}
	
	/* colon at the end */
	if (*(end-1) == ':') {
		end--;
		*end = '\0';
	} 

	if (end <= start || *start == '\0') {
		HERROR("dir not given\n");
		ret = -EINVAL;
		goto out;
	}
	
	/* counting the number of intermediate colons */
	for(tmp = start; *tmp; tmp++) {
		if(*tmp == ':') {
			branch_number++;
		}
	}
	/* increment by 1 more because actual number of branches is 1 more,  b1:b2:b3 */
	branch_number++;
	mount_option_init(mount_option, branch_number);

	while(start < end && *start != '\0') {
		tmp = start;
		while (*tmp && *tmp != ':' && tmp < end) {
			tmp++;
		}
		*tmp = '\0';

		mount_option->branch[tmp_branch_number].length = strlen(start) + 1;
		MTFS_ALLOC(mount_option->branch[tmp_branch_number].path, mount_option->branch[tmp_branch_number].length);
		if (mount_option->branch[tmp_branch_number].path == NULL) {
			HERROR("unable to strdup dir name [%s] when parsing [%d]th dir", start, tmp_branch_number);
			tmp_branch_number--;
			goto free_array;
		}
		memcpy(mount_option->branch[tmp_branch_number].path, start, mount_option->branch[tmp_branch_number].length - 1);
		
		tmp_branch_number++;
		start = tmp + 1;
	}
	HASSERT(tmp_branch_number == branch_number);
	goto out;
free_array:
	HASSERT(mount_option->branch);
	for(; tmp_branch_number >= 0; tmp_branch_number++) {
		HASSERT(mount_option->branch[tmp_branch_number].path);
		MTFS_FREE(mount_option->branch[tmp_branch_number].path, mount_option->branch[tmp_branch_number].length);
	}
	MTFS_FREE(mount_option->branch, sizeof(*mount_option->branch) * mount_option->bnum);
out:
	return ret;
}

/* Parse options from mount. Returns 0 on success */
int mtfs_parse_options(char *input, mount_option_t *mount_option)
{
	int ret = 0;
	char *p = NULL;
	int token = 0;
	substring_t args[MAX_OPT_ARGS];
	int dirs_setted = 0;
	int tmp = 0;
	
	HASSERT(mount_option);
	if (!input) {
		HERROR("Hidden dirs not seted\n");
		ret = -EINVAL;
		goto out;
	}
	
	while ((p = strsep(&input, ",")) != NULL) {
		if (!*p) {
			continue;
		}
		token = match_token(p, tokens, args);
		switch (token) {
		case opt_debug:
			if (match_int(&args[0], &tmp)) {
				HERROR("debug level requires an argument\n");
				ret = -EINVAL;
				goto error;
			}
			
			/* check debug_level */
			if (tmp < 0 || tmp > 100) {
				HERROR("debug level should be between %d and %d\n", 0, 100);
				ret = -EINVAL;
				goto error;
			}
			
			HDEBUG("set debug_level to %d\n", tmp);
			mount_option->debug_level = tmp;
			break;
		case opt_dirs:
			if (dirs_setted) {
				HERROR("unexpected multiple dir options\n");
				ret = -EINVAL;
				goto error;
			}
			p = match_strdup(&args[0]);
			if (p == NULL) {
				HERROR("not enough memory\n");
				ret = -ENOMEM;
				goto error;
			}
			ret = parse_dir_option(p, mount_option);
#if defined (__linux__) && defined(__KERNEL__)
			kfree(p);
#else
			free(p);
#endif
			if (ret) {
				goto error;
			}
			/* Finally , everything is OK */
			dirs_setted = 1;
			break;
		default:
			HERROR("unexpected option\n");
			ret = -EINVAL;
			goto error;
		}
	}
	
	if (!dirs_setted) {
		HERROR("Hidden dirs not seted\n");
		ret = -EINVAL;
	}
	goto out;
error:
	if (dirs_setted) {
		mount_option_finit(mount_option);
	}	
out:
	return ret;	
}