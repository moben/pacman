/*
 *  util.c
 *
 *  Copyright (c) 2006-2011 Pacman Development Team <pacman-dev@archlinux.org>
 *  Copyright (c) 2002-2006 by Judd Vinet <jvinet@zeroflux.org>
 *  Copyright (c) 2005 by Aurelien Foret <orelien@chez.com>
 *  Copyright (c) 2005 by Christian Hamar <krics@linuxforum.hu>
 *  Copyright (c) 2006 by David Kimpe <dnaku@frugalware.org>
 *  Copyright (c) 2005, 2006 by Miklos Vajna <vmiklos@frugalware.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <syslog.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <locale.h> /* setlocale */

/* libarchive */
#include <archive.h>
#include <archive_entry.h>

#ifdef HAVE_LIBSSL
#include <openssl/md5.h>
#include <openssl/sha.h>
#else
#include "md5.h"
#include "sha2.h"
#endif

/* libalpm */
#include "util.h"
#include "log.h"
#include "alpm.h"
#include "alpm_list.h"
#include "handle.h"
#include "trans.h"

#ifndef HAVE_STRSEP
/* This is a replacement for strsep which is not portable (missing on Solaris).
 * Copyright (c) 2001 by François Gouget <fgouget_at_codeweavers.com> */
char* strsep(char** str, const char* delims)
{
	char* token;

	if(*str==NULL) {
		/* No more tokens */
		return NULL;
	}

	token=*str;
	while(**str!='\0') {
		if(strchr(delims,**str)!=NULL) {
			**str='\0';
			(*str)++;
			return token;
		}
		(*str)++;
	}
	/* There is no other token */
	*str=NULL;
	return token;
}
#endif

int _alpm_makepath(const char *path)
{
	return _alpm_makepath_mode(path, 0755);
}

/* does the same thing as 'mkdir -p' */
int _alpm_makepath_mode(const char *path, mode_t mode)
{
	/* A bit of pointer hell here. Descriptions:
	 * orig - a copy of path so we can safely butcher it with strsep
	 * str - the current position in the path string (after the delimiter)
	 * ptr - the original position of str after calling strsep
	 * incr - incrementally generated path for use in stat/mkdir call
	 */
	char *orig, *str, *ptr, *incr;
	mode_t oldmask = umask(0000);
	int ret = 0;

	orig = strdup(path);
	incr = calloc(strlen(orig) + 1, sizeof(char));
	str = orig;
	while((ptr = strsep(&str, "/"))) {
		if(strlen(ptr)) {
			/* we have another path component- append the newest component to
			 * existing string and create one more level of dir structure */
			strcat(incr, "/");
			strcat(incr, ptr);
			if(access(incr, F_OK)) {
				if(mkdir(incr, mode)) {
					ret = 1;
					break;
				}
			}
		}
	}
	free(orig);
	free(incr);
	umask(oldmask);
	return ret;
}

#define CPBUFSIZE 8 * 1024

int _alpm_copyfile(const char *src, const char *dest)
{
	FILE *in, *out;
	size_t len;
	char *buf;
	int ret = 0;

	in = fopen(src, "rb");
	if(in == NULL) {
		return 1;
	}
	out = fopen(dest, "wb");
	if(out == NULL) {
		fclose(in);
		return 1;
	}

	CALLOC(buf, (size_t)CPBUFSIZE, (size_t)1, ret = 1; goto cleanup;);

	/* do the actual file copy */
	while((len = fread(buf, 1, CPBUFSIZE, in))) {
		size_t nwritten = 0;
		nwritten = fwrite(buf, 1, len, out);
		if((nwritten != len) || ferror(out)) {
			ret = -1;
			goto cleanup;
		}
	}

	/* chmod dest to permissions of src, as long as it is not a symlink */
	struct stat statbuf;
	if(!stat(src, &statbuf)) {
		if(! S_ISLNK(statbuf.st_mode)) {
			fchmod(fileno(out), statbuf.st_mode);
		}
	} else {
		/* stat was unsuccessful */
		ret = 1;
	}

cleanup:
	fclose(in);
	fclose(out);
	FREE(buf);
	return ret;
}

/* Trim whitespace and newlines from a string
*/
char *_alpm_strtrim(char *str)
{
	char *pch = str;

	if(*str == '\0') {
		/* string is empty, so we're done. */
		return str;
	}

	while(isspace((unsigned char)*pch)) {
		pch++;
	}
	if(pch != str) {
		size_t len = strlen(pch);
		if(len) {
			memmove(str, pch, len + 1);
		} else {
			*str = '\0';
		}
	}

	/* check if there wasn't anything but whitespace in the string. */
	if(*str == '\0') {
		return str;
	}

	pch = (str + (strlen(str) - 1));
	while(isspace((unsigned char)*pch)) {
		pch--;
	}
	*++pch = '\0';

	return str;
}

/**
 * Trim trailing newline from a string (if one exists).
 * @param str a single line of text
 * @return the length of the trimmed string
 */
size_t _alpm_strip_newline(char *str)
{
	size_t len;
	if(str == '\0') {
		return 0;
	}
	len = strlen(str);
	while(len > 0 && str[len - 1] == '\n') {
		len--;
	}
	str[len] = '\0';

	return len;
}

/* Compression functions */

/**
 * @brief Unpack a specific file in an archive.
 *
 * @param handle the context handle
 * @param archive the archive to unpack
 * @param prefix where to extract the files
 * @param filename a file within the archive to unpack
 * @return 0 on success, 1 on failure
 */
int _alpm_unpack_single(alpm_handle_t *handle, const char *archive,
		const char *prefix, const char *filename)
{
	alpm_list_t *list = NULL;
	int ret = 0;
	if(filename == NULL) {
		return 1;
	}
	list = alpm_list_add(list, (void *)filename);
	ret = _alpm_unpack(handle, archive, prefix, list, 1);
	alpm_list_free(list);
	return ret;
}

/**
 * @brief Unpack a list of files in an archive.
 *
 * @param handle the context handle
 * @param archive the archive to unpack
 * @param prefix where to extract the files
 * @param list a list of files within the archive to unpack or NULL for all
 * @param breakfirst break after the first entry found
 *
 * @return 0 on success, 1 on failure
 */
int _alpm_unpack(alpm_handle_t *handle, const char *archive, const char *prefix,
		alpm_list_t *list, int breakfirst)
{
	int ret = 0;
	mode_t oldmask;
	struct archive *_archive;
	struct archive_entry *entry;
	int cwdfd;

	if((_archive = archive_read_new()) == NULL) {
		RET_ERR(handle, ALPM_ERR_LIBARCHIVE, 1);
	}

	archive_read_support_compression_all(_archive);
	archive_read_support_format_all(_archive);

	if(archive_read_open_filename(_archive, archive,
				ARCHIVE_DEFAULT_BYTES_PER_BLOCK) != ARCHIVE_OK) {
		_alpm_log(handle, ALPM_LOG_ERROR, _("could not open file %s: %s\n"), archive,
				archive_error_string(_archive));
		RET_ERR(handle, ALPM_ERR_PKG_OPEN, 1);
	}

	oldmask = umask(0022);

	/* save the cwd so we can restore it later */
	do {
		cwdfd = open(".", O_RDONLY);
	} while(cwdfd == -1 && errno == EINTR);
	if(cwdfd < 0) {
		_alpm_log(handle, ALPM_LOG_ERROR, _("could not get current working directory\n"));
	}

	/* just in case our cwd was removed in the upgrade operation */
	if(chdir(prefix) != 0) {
		_alpm_log(handle, ALPM_LOG_ERROR, _("could not change directory to %s (%s)\n"),
				prefix, strerror(errno));
		ret = 1;
		goto cleanup;
	}

	while(archive_read_next_header(_archive, &entry) == ARCHIVE_OK) {
		const struct stat *st;
		const char *entryname; /* the name of the file in the archive */

		st = archive_entry_stat(entry);
		entryname = archive_entry_pathname(entry);

		if(S_ISREG(st->st_mode)) {
			archive_entry_set_perm(entry, 0644);
		} else if(S_ISDIR(st->st_mode)) {
			archive_entry_set_perm(entry, 0755);
		}

		/* If specific files were requested, skip entries that don't match. */
		if(list) {
			char *entry_prefix = strdup(entryname);
			char *p = strstr(entry_prefix,"/");
			if(p) {
				*(p+1) = '\0';
			}
			char *found = alpm_list_find_str(list, entry_prefix);
			free(entry_prefix);
			if(!found) {
				if(archive_read_data_skip(_archive) != ARCHIVE_OK) {
					ret = 1;
					goto cleanup;
				}
				continue;
			} else {
				_alpm_log(handle, ALPM_LOG_DEBUG, "extracting: %s\n", entryname);
			}
		}

		/* Extract the archive entry. */
		int readret = archive_read_extract(_archive, entry, 0);
		if(readret == ARCHIVE_WARN) {
			/* operation succeeded but a non-critical error was encountered */
			_alpm_log(handle, ALPM_LOG_WARNING, _("warning given when extracting %s (%s)\n"),
					entryname, archive_error_string(_archive));
		} else if(readret != ARCHIVE_OK) {
			_alpm_log(handle, ALPM_LOG_ERROR, _("could not extract %s (%s)\n"),
					entryname, archive_error_string(_archive));
			ret = 1;
			goto cleanup;
		}

		if(breakfirst) {
			break;
		}
	}

cleanup:
	umask(oldmask);
	archive_read_finish(_archive);
	if(cwdfd >= 0) {
		if(fchdir(cwdfd) != 0) {
			_alpm_log(handle, ALPM_LOG_ERROR,
					_("could not restore working directory (%s)\n"), strerror(errno));
		}
		close(cwdfd);
	}

	return ret;
}

/* does the same thing as 'rm -rf' */
int _alpm_rmrf(const char *path)
{
	int errflag = 0;
	struct dirent *dp;
	DIR *dirp;
	char name[PATH_MAX];
	struct stat st;

	if(_alpm_lstat(path, &st) == 0) {
		if(!S_ISDIR(st.st_mode)) {
			if(!unlink(path)) {
				return 0;
			} else {
				if(errno == ENOENT) {
					return 0;
				} else {
					return 1;
				}
			}
		} else {
			dirp = opendir(path);
			if(!dirp) {
				return 1;
			}
			for(dp = readdir(dirp); dp != NULL; dp = readdir(dirp)) {
				if(dp->d_ino) {
					sprintf(name, "%s/%s", path, dp->d_name);
					if(strcmp(dp->d_name, "..") != 0 && strcmp(dp->d_name, ".") != 0) {
						errflag += _alpm_rmrf(name);
					}
				}
			}
			closedir(dirp);
			if(rmdir(path)) {
				errflag++;
			}
		}
		return errflag;
	}
	return 0;
}

/**
 * Determine if there are files in a directory
 * @param handle the context handle
 * @param path the full absolute directory path
 * @param full_count whether to return an exact count of files
 * @return a file count if full_count is != 0, else >0 if directory has
 * contents, 0 if no contents, and -1 on error
 */
ssize_t _alpm_files_in_directory(alpm_handle_t *handle, const char *path,
		int full_count)
{
	ssize_t files = 0;
	struct dirent *ent;
	DIR *dir = opendir(path);

	if(!dir) {
		if(errno == ENOTDIR) {
			_alpm_log(handle, ALPM_LOG_DEBUG, "%s was not a directory\n", path);
		} else {
			_alpm_log(handle, ALPM_LOG_DEBUG, "could not read directory %s\n",
					path);
		}
		return -1;
	}
	while((ent = readdir(dir)) != NULL) {
		const char *name = ent->d_name;

		if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
			continue;
		}

		files++;

		if(!full_count) {
			break;
		}
	}

	closedir(dir);
	return files;
}

int _alpm_logaction(alpm_handle_t *handle, const char *fmt, va_list args)
{
	int ret = 0;

	if(handle->usesyslog) {
		/* we can't use a va_list more than once, so we need to copy it
		 * so we can use the original when calling vfprintf below. */
		va_list args_syslog;
		va_copy(args_syslog, args);
		vsyslog(LOG_WARNING, fmt, args_syslog);
		va_end(args_syslog);
	}

	if(handle->logstream) {
		time_t t;
		struct tm *tm;

		t = time(NULL);
		tm = localtime(&t);

		/* Use ISO-8601 date format */
		fprintf(handle->logstream, "[%04d-%02d-%02d %02d:%02d] ",
						tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
						tm->tm_hour, tm->tm_min);
		ret = vfprintf(handle->logstream, fmt, args);
		fflush(handle->logstream);
	}

	return ret;
}

int _alpm_run_chroot(alpm_handle_t *handle, const char *path, char *const argv[])
{
	pid_t pid;
	int pipefd[2], cwdfd;
	int retval = 0;

	/* save the cwd so we can restore it later */
	do {
		cwdfd = open(".", O_RDONLY);
	} while(cwdfd == -1 && errno == EINTR);
	if(cwdfd < 0) {
		_alpm_log(handle, ALPM_LOG_ERROR, _("could not get current working directory\n"));
	}

	/* just in case our cwd was removed in the upgrade operation */
	if(chdir(handle->root) != 0) {
		_alpm_log(handle, ALPM_LOG_ERROR, _("could not change directory to %s (%s)\n"),
				handle->root, strerror(errno));
		goto cleanup;
	}

	_alpm_log(handle, ALPM_LOG_DEBUG, "executing \"%s\" under chroot \"%s\"\n",
			path, handle->root);

	/* Flush open fds before fork() to avoid cloning buffers */
	fflush(NULL);

	if(pipe(pipefd) == -1) {
		_alpm_log(handle, ALPM_LOG_ERROR, _("could not create pipe (%s)\n"), strerror(errno));
		retval = 1;
		goto cleanup;
	}

	/* fork- parent and child each have seperate code blocks below */
	pid = fork();
	if(pid == -1) {
		_alpm_log(handle, ALPM_LOG_ERROR, _("could not fork a new process (%s)\n"), strerror(errno));
		retval = 1;
		goto cleanup;
	}

	if(pid == 0) {
		/* this code runs for the child only (the actual chroot/exec) */
		close(1);
		close(2);
		while(dup2(pipefd[1], 1) == -1 && errno == EINTR);
		while(dup2(pipefd[1], 2) == -1 && errno == EINTR);
		close(pipefd[0]);
		close(pipefd[1]);

		/* use fprintf instead of _alpm_log to send output through the parent */
		if(chroot(handle->root) != 0) {
			fprintf(stderr, _("could not change the root directory (%s)\n"), strerror(errno));
			exit(1);
		}
		if(chdir("/") != 0) {
			fprintf(stderr, _("could not change directory to %s (%s)\n"),
					"/", strerror(errno));
			exit(1);
		}
		umask(0022);
		execv(path, argv);
		fprintf(stderr, _("call to execv failed (%s)\n"), strerror(errno));
		exit(1);
	} else {
		/* this code runs for the parent only (wait on the child) */
		int status;
		FILE *pipe_file;

		close(pipefd[1]);
		pipe_file = fdopen(pipefd[0], "r");
		if(pipe_file == NULL) {
			close(pipefd[0]);
			retval = 1;
		} else {
			while(!feof(pipe_file)) {
				char line[PATH_MAX];
				if(fgets(line, PATH_MAX, pipe_file) == NULL)
					break;
				alpm_logaction(handle, "%s", line);
				EVENT(handle, ALPM_EVENT_SCRIPTLET_INFO, line, NULL);
			}
			fclose(pipe_file);
		}

		while(waitpid(pid, &status, 0) == -1) {
			if(errno != EINTR) {
				_alpm_log(handle, ALPM_LOG_ERROR, _("call to waitpid failed (%s)\n"), strerror(errno));
				retval = 1;
				goto cleanup;
			}
		}

		/* report error from above after the child has exited */
		if(retval != 0) {
			_alpm_log(handle, ALPM_LOG_ERROR, _("could not open pipe (%s)\n"), strerror(errno));
			goto cleanup;
		}
		/* check the return status, make sure it is 0 (success) */
		if(WIFEXITED(status)) {
			_alpm_log(handle, ALPM_LOG_DEBUG, "call to waitpid succeeded\n");
			if(WEXITSTATUS(status) != 0) {
				_alpm_log(handle, ALPM_LOG_ERROR, _("command failed to execute correctly\n"));
				retval = 1;
			}
		}
	}

cleanup:
	if(cwdfd >= 0) {
		if(fchdir(cwdfd) != 0) {
			_alpm_log(handle, ALPM_LOG_ERROR,
					_("could not restore working directory (%s)\n"), strerror(errno));
		}
		close(cwdfd);
	}

	return retval;
}

int _alpm_ldconfig(alpm_handle_t *handle)
{
	char line[PATH_MAX];

	_alpm_log(handle, ALPM_LOG_DEBUG, "running ldconfig\n");

	snprintf(line, PATH_MAX, "%setc/ld.so.conf", handle->root);
	if(access(line, F_OK) == 0) {
		snprintf(line, PATH_MAX, "%ssbin/ldconfig", handle->root);
		if(access(line, X_OK) == 0) {
			char *argv[] = { "ldconfig", NULL };
			_alpm_run_chroot(handle, "/sbin/ldconfig", argv);
		}
	}

	return 0;
}

/* Helper function for comparing strings using the
 * alpm "compare func" signature */
int _alpm_str_cmp(const void *s1, const void *s2)
{
	return strcmp(s1, s2);
}

/** Find a filename in a registered alpm cachedir.
 * @param handle the context handle
 * @param filename name of file to find
 * @return malloced path of file, NULL if not found
 */
char *_alpm_filecache_find(alpm_handle_t *handle, const char *filename)
{
	char path[PATH_MAX];
	char *retpath;
	alpm_list_t *i;
	struct stat buf;

	/* Loop through the cache dirs until we find a matching file */
	for(i = handle->cachedirs; i; i = i->next) {
		snprintf(path, PATH_MAX, "%s%s", (char *)i->data,
				filename);
		if(stat(path, &buf) == 0 && S_ISREG(buf.st_mode)) {
			retpath = strdup(path);
			_alpm_log(handle, ALPM_LOG_DEBUG, "found cached pkg: %s\n", retpath);
			return retpath;
		}
	}
	/* package wasn't found in any cachedir */
	return NULL;
}

/** Check the alpm cachedirs for existance and find a writable one.
 * If no valid cache directory can be found, use /tmp.
 * @param handle the context handle
 * @return pointer to a writable cache directory.
 */
const char *_alpm_filecache_setup(alpm_handle_t *handle)
{
	struct stat buf;
	alpm_list_t *i;
	char *cachedir, *tmpdir;

	/* Loop through the cache dirs until we find a usable directory */
	for(i = handle->cachedirs; i; i = i->next) {
		cachedir = i->data;
		if(stat(cachedir, &buf) != 0) {
			/* cache directory does not exist.... try creating it */
			_alpm_log(handle, ALPM_LOG_WARNING, _("no %s cache exists, creating...\n"),
					cachedir);
			if(_alpm_makepath(cachedir) == 0) {
				_alpm_log(handle, ALPM_LOG_DEBUG, "using cachedir: %s\n", cachedir);
				return cachedir;
			}
		} else if(!S_ISDIR(buf.st_mode)) {
			_alpm_log(handle, ALPM_LOG_DEBUG,
					"skipping cachedir, not a directory: %s\n", cachedir);
		} else if(access(cachedir, W_OK) != 0) {
			_alpm_log(handle, ALPM_LOG_DEBUG,
					"skipping cachedir, not writable: %s\n", cachedir);
		} else if(!(buf.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))) {
			_alpm_log(handle, ALPM_LOG_DEBUG,
					"skipping cachedir, no write bits set: %s\n", cachedir);
		} else {
			_alpm_log(handle, ALPM_LOG_DEBUG, "using cachedir: %s\n", cachedir);
			return cachedir;
		}
	}

	/* we didn't find a valid cache directory. use TMPDIR or /tmp. */
	if((tmpdir = getenv("TMPDIR")) && stat(tmpdir, &buf) && S_ISDIR(buf.st_mode)) {
		/* TMPDIR was good, we can use it */
	} else {
		tmpdir = "/tmp";
	}
	alpm_option_add_cachedir(handle, tmpdir);
	cachedir = handle->cachedirs->prev->data;
	_alpm_log(handle, ALPM_LOG_DEBUG, "using cachedir: %s\n", cachedir);
	_alpm_log(handle, ALPM_LOG_WARNING,
			_("couldn't find or create package cache, using %s instead\n"), cachedir);
	return cachedir;
}

/** lstat wrapper that treats /path/dirsymlink/ the same as /path/dirsymlink.
 * Linux lstat follows POSIX semantics and still performs a dereference on
 * the first, and for uses of lstat in libalpm this is not what we want.
 * @param path path to file to lstat
 * @param buf structure to fill with stat information
 * @return the return code from lstat
 */
int _alpm_lstat(const char *path, struct stat *buf)
{
	int ret;
	size_t len = strlen(path);

	/* strip the trailing slash if one exists */
	if(len != 0 && path[len - 1] == '/') {
		char *newpath = strdup(path);
		newpath[len - 1] = '\0';
		ret = lstat(newpath, buf);
		free(newpath);
	} else {
		ret = lstat(path, buf);
	}

	return ret;
}

#ifdef HAVE_LIBSSL
#define BUFFER_SIZE 8192

static int md5_file(const char *path, unsigned char output[16])
{
	FILE *f;
	size_t n;
	MD5_CTX ctx;
	unsigned char *buf;

	CALLOC(buf, BUFFER_SIZE, sizeof(unsigned char), return 1);

	if((f = fopen(path, "rb")) == NULL) {
		free(buf);
		return 1;
	}

	MD5_Init(&ctx);

	while((n = fread(buf, 1, BUFFER_SIZE, f)) > 0) {
		MD5_Update(&ctx, buf, n);
	}

	MD5_Final(output, &ctx);

	memset(&ctx, 0, sizeof(MD5_CTX));
	free(buf);

	if(ferror(f) != 0) {
		fclose(f);
		return 2;
	}

	fclose(f);
	return 0;
}

/* third param is so we match the PolarSSL definition */
static int sha2_file(const char *path, unsigned char output[32], int is224)
{
	FILE *f;
	size_t n;
	SHA256_CTX ctx;
	unsigned char *buf;

	CALLOC(buf, BUFFER_SIZE, sizeof(unsigned char), return 1);

	if((f = fopen(path, "rb")) == NULL) {
		free(buf);
		return 1;
	}

	if(is224) {
		SHA224_Init(&ctx);
	} else {
		SHA256_Init(&ctx);
	}

	while((n = fread(buf, 1, BUFFER_SIZE, f)) > 0) {
		if(is224) {
			SHA224_Update(&ctx, buf, n);
		} else {
			SHA256_Update(&ctx, buf, n);
		}
	}

	if(is224) {
		SHA224_Final(output, &ctx);
	} else {
		SHA256_Final(output, &ctx);
	}

	memset(&ctx, 0, sizeof(SHA256_CTX));
	free(buf);

	if(ferror(f) != 0) {
		fclose(f);
		return 2;
	}

	fclose(f);
	return 0;
}
#endif

/** Get the md5 sum of file.
 * @param filename name of the file
 * @return the checksum on success, NULL on error
 * @addtogroup alpm_misc
 */
char SYMEXPORT *alpm_compute_md5sum(const char *filename)
{
	unsigned char output[16];
	char *md5sum;
	int ret, i;

	ASSERT(filename != NULL, return NULL);

	/* allocate 32 chars plus 1 for null */
	CALLOC(md5sum, 33, sizeof(char), return NULL);
	/* defined above for OpenSSL, otherwise defined in md5.h */
	ret = md5_file(filename, output);

	if(ret > 0) {
		free(md5sum);
		return NULL;
	}

	/* Convert the result to something readable */
	for (i = 0; i < 16; i++) {
		/* sprintf is acceptable here because we know our output */
		sprintf(md5sum +(i * 2), "%02x", output[i]);
	}

	return md5sum;
}

/** Get the sha256 sum of file.
 * @param filename name of the file
 * @return the checksum on success, NULL on error
 * @addtogroup alpm_misc
 */
char SYMEXPORT *alpm_compute_sha256sum(const char *filename)
{
	unsigned char output[32];
	char *sha256sum;
	int ret, i;

	ASSERT(filename != NULL, return NULL);

	/* allocate 64 chars plus 1 for null */
	CALLOC(sha256sum, 65, sizeof(char), return NULL);
	/* defined above for OpenSSL, otherwise defined in sha2.h */
	ret = sha2_file(filename, output, 0);

	if(ret > 0) {
		free(sha256sum);
		return NULL;
	}

	/* Convert the result to something readable */
	for (i = 0; i < 32; i++) {
		/* sprintf is acceptable here because we know our output */
		sprintf(sha256sum +(i * 2), "%02x", output[i]);
	}

	return sha256sum;
}

int _alpm_test_checksum(const char *filepath, const char *expected,
		enum _alpm_csum type)
{
	char *computed;
	int ret;

	if(type == ALPM_CSUM_MD5) {
		computed = alpm_compute_md5sum(filepath);
	} else if(type == ALPM_CSUM_SHA256) {
		computed = alpm_compute_sha256sum(filepath);
	} else {
		return -1;
	}

	if(expected == NULL || computed == NULL) {
		ret = -1;
	} else if(strcmp(expected, computed) != 0) {
		ret = 1;
	} else {
		ret = 0;
	}

	FREE(computed);
	return ret;
}

/* Note: does NOT handle sparse files on purpose for speed. */
int _alpm_archive_fgets(struct archive *a, struct archive_read_buffer *b)
{
	char *i = NULL;
	int64_t offset;
	int done = 0;

	/* ensure we start populating our line buffer at the beginning */
	b->line_offset = b->line;

	while(1) {
		/* have we processed this entire block? */
		if(b->block + b->block_size == b->block_offset) {
			if(b->ret == ARCHIVE_EOF) {
				/* reached end of archive on the last read, now we are out of data */
				goto cleanup;
			}

			/* zero-copy - this is the entire next block of data. */
			b->ret = archive_read_data_block(a, (void *)&b->block,
					&b->block_size, &offset);
			b->block_offset = b->block;

			/* error, cleanup */
			if(b->ret < ARCHIVE_OK) {
				goto cleanup;
			}
		}

		/* loop through the block looking for EOL characters */
		for(i = b->block_offset; i < (b->block + b->block_size); i++) {
			/* check if read value was null or newline */
			if(*i == '\0' || *i == '\n') {
				done = 1;
				break;
			}
		}

		/* allocate our buffer, or ensure our existing one is big enough */
		if(!b->line) {
			/* set the initial buffer to the read block_size */
			CALLOC(b->line, b->block_size + 1, sizeof(char), b->ret = -ENOMEM; goto cleanup);
			b->line_size = b->block_size + 1;
			b->line_offset = b->line;
		} else {
			size_t needed = (size_t)((b->line_offset - b->line)
					+ (i - b->block_offset) + 1);
			if(needed > b->max_line_size) {
				b->ret = -ERANGE;
				goto cleanup;
			}
			if(needed > b->line_size) {
				/* need to realloc + copy data to fit total length */
				char *new;
				CALLOC(new, needed, sizeof(char), b->ret = -ENOMEM; goto cleanup);
				memcpy(new, b->line, b->line_size);
				b->line_size = needed;
				b->line_offset = new + (b->line_offset - b->line);
				free(b->line);
				b->line = new;
			}
		}

		if(done) {
			size_t len = (size_t)(i - b->block_offset);
			memcpy(b->line_offset, b->block_offset, len);
			b->line_offset[len] = '\0';
			b->block_offset = ++i;
			/* this is the main return point; from here you can read b->line */
			return ARCHIVE_OK;
		} else {
			/* we've looked through the whole block but no newline, copy it */
			size_t len = (size_t)(b->block + b->block_size - b->block_offset);
			memcpy(b->line_offset, b->block_offset, len);
			b->line_offset += len;
			b->block_offset = i;
			/* there was no new data, return what is left; saved ARCHIVE_EOF will be
			 * returned on next call */
			if(len == 0) {
				b->line_offset[0] = '\0';
				return ARCHIVE_OK;
			}
		}
	}

cleanup:
	{
		int ret = b->ret;
		FREE(b->line);
		memset(b, 0, sizeof(b));
		return ret;
	}
}

int _alpm_splitname(const char *target, char **name, char **version,
		unsigned long *name_hash)
{
	/* the format of a db entry is as follows:
	 *    package-version-rel/
	 *    package-version-rel/desc (we ignore the filename portion)
	 * package name can contain hyphens, so parse from the back- go back
	 * two hyphens and we have split the version from the name.
	 */
	const char *pkgver, *end;

	if(target == NULL) {
		return -1;
	}

	/* remove anything trailing a '/' */
	end = strchr(target, '/');
	if(!end) {
		end = target + strlen(target);
	}

	/* do the magic parsing- find the beginning of the version string
	 * by doing two iterations of same loop to lop off two hyphens */
	for(pkgver = end - 1; *pkgver && *pkgver != '-'; pkgver--);
	for(pkgver = pkgver - 1; *pkgver && *pkgver != '-'; pkgver--);
	if(*pkgver != '-' || pkgver == target) {
		return -1;
	}

	/* copy into fields and return */
	if(version) {
		if(*version) {
			FREE(*version);
		}
		/* version actually points to the dash, so need to increment 1 and account
		 * for potential end character */
		STRNDUP(*version, pkgver + 1, end - pkgver - 1, return -1);
	}

	if(name) {
		if(*name) {
			FREE(*name);
		}
		STRNDUP(*name, target, pkgver - target, return -1);
		if(name_hash) {
			*name_hash = _alpm_hash_sdbm(*name);
		}
	}

	return 0;
}

/**
 * Hash the given string to an unsigned long value.
 * This is the standard sdbm hashing algorithm.
 * @param str string to hash
 * @return the hash value of the given string
 */
unsigned long _alpm_hash_sdbm(const char *str)
{
	unsigned long hash = 0;
	int c;

	if(!str) {
		return hash;
	}
	while((c = *str++)) {
		hash = c + (hash << 6) + (hash << 16) - hash;
	}

	return hash;
}

off_t _alpm_strtoofft(const char *line)
{
	char *end;
	unsigned long long result;
	errno = 0;

	/* we are trying to parse bare numbers only, no leading anything */
	if(line[0] < '0' || line[0] > '9') {
		return (off_t)-1;
	}
	result = strtoull(line, &end, 10);
	if (result == 0 && end == line) {
		/* line was not a number */
		return (off_t)-1;
	} else if (result == ULLONG_MAX && errno == ERANGE) {
		/* line does not fit in unsigned long long */
		return (off_t)-1;
	} else if (*end) {
		/* line began with a number but has junk left over at the end */
		return (off_t)-1;
	}

	return (off_t)result;
}

time_t _alpm_parsedate(const char *line)
{
	if(isalpha((unsigned char)line[0])) {
		/* initialize to null in case of failure */
		struct tm tmp_tm;
		memset(&tmp_tm, 0, sizeof(struct tm));
		setlocale(LC_TIME, "C");
		strptime(line, "%a %b %e %H:%M:%S %Y", &tmp_tm);
		setlocale(LC_TIME, "");
		return mktime(&tmp_tm);
	}
	return (time_t)atol(line);
}

/**
 * Wrapper around access() which takes a dir and file argument
 * separately and generates an appropriate error message.
 * If dir is NULL file will be treated as the whole path.
 * @param handle an alpm handle
 * @param dir directory path ending with and slash
 * @param file filename
 * @param amode access mode as described in access()
 * @return int value returned by access()
 */

int _alpm_access(alpm_handle_t *handle, const char *dir, const char *file, int amode)
{
	size_t len = 0;
	int ret = 0;

	if (dir) {
		char *check_path;

		len = strlen(dir) + strlen(file) + 1;
		CALLOC(check_path, len, sizeof(char), RET_ERR(handle, ALPM_ERR_MEMORY, -1));
		snprintf(check_path, len, "%s%s", dir, file);

		ret = access(check_path, amode);
		free(check_path);
	} else {
		dir = "";
		ret = access(file, amode);
	}

	if(ret != 0) {
		if (amode & R_OK) {
			_alpm_log(handle, ALPM_LOG_DEBUG, "\"%s%s\" is not readable: %s\n",
					dir, file, strerror(errno));
		}
		if (amode & W_OK) {
			_alpm_log(handle, ALPM_LOG_DEBUG, "\"%s%s\" is not writable: %s\n",
					dir, file, strerror(errno));
		}
		if (amode & X_OK) {
			_alpm_log(handle, ALPM_LOG_DEBUG, "\"%s%s\" is not executable: %s\n",
					dir, file, strerror(errno));
		}
		if (amode == F_OK) {
			_alpm_log(handle, ALPM_LOG_DEBUG, "\"%s%s\" does not exist: %s\n",
					dir, file, strerror(errno));
		}
	}
	return ret;
}

#ifndef HAVE_STRNDUP
/* A quick and dirty implementation derived from glibc */
static size_t strnlen(const char *s, size_t max)
{
    register const char *p;
    for(p = s; *p && max--; ++p);
    return (p - s);
}

char *strndup(const char *s, size_t n)
{
  size_t len = strnlen(s, n);
  char *new = (char *) malloc(len + 1);

  if(new == NULL)
    return NULL;

  new[len] = '\0';
  return (char *)memcpy(new, s, len);
}
#endif

/* vim: set ts=2 sw=2 noet: */
