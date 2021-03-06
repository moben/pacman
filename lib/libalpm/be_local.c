/*
 *  be_local.c : backend for the local database
 *
 *  Copyright (c) 2006-2011 Pacman Development Team <pacman-dev@archlinux.org>
 *  Copyright (c) 2002-2006 by Judd Vinet <jvinet@zeroflux.org>
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdint.h> /* intmax_t */
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <limits.h> /* PATH_MAX */

/* libalpm */
#include "db.h"
#include "alpm_list.h"
#include "log.h"
#include "util.h"
#include "alpm.h"
#include "handle.h"
#include "package.h"
#include "deps.h"

static int local_db_read(alpm_pkg_t *info, alpm_dbinfrq_t inforeq);

#define LAZY_LOAD(info, errret) \
	do { \
		if(!(pkg->infolevel & info)) { \
			local_db_read(pkg, info); \
		} \
	} while(0)


/* Cache-specific accessor functions. These implementations allow for lazy
 * loading by the files backend when a data member is actually needed
 * rather than loading all pieces of information when the package is first
 * initialized.
 */

static const char *_cache_get_desc(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, NULL);
	return pkg->desc;
}

static const char *_cache_get_url(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, NULL);
	return pkg->url;
}

static time_t _cache_get_builddate(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, 0);
	return pkg->builddate;
}

static time_t _cache_get_installdate(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, 0);
	return pkg->installdate;
}

static const char *_cache_get_packager(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, NULL);
	return pkg->packager;
}

static const char *_cache_get_arch(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, NULL);
	return pkg->arch;
}

static off_t _cache_get_isize(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, -1);
	return pkg->isize;
}

static alpm_pkgreason_t _cache_get_reason(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, -1);
	return pkg->reason;
}

static alpm_list_t *_cache_get_licenses(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, NULL);
	return pkg->licenses;
}

static alpm_list_t *_cache_get_groups(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, NULL);
	return pkg->groups;
}

static int _cache_has_scriptlet(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_SCRIPTLET, NULL);
	return pkg->scriptlet;
}

static alpm_list_t *_cache_get_depends(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, NULL);
	return pkg->depends;
}

static alpm_list_t *_cache_get_optdepends(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, NULL);
	return pkg->optdepends;
}

static alpm_list_t *_cache_get_conflicts(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, NULL);
	return pkg->conflicts;
}

static alpm_list_t *_cache_get_provides(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, NULL);
	return pkg->provides;
}

static alpm_list_t *_cache_get_replaces(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_DESC, NULL);
	return pkg->replaces;
}

static alpm_filelist_t *_cache_get_files(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_FILES, NULL);
	return &(pkg->files);
}

static alpm_list_t *_cache_get_backup(alpm_pkg_t *pkg)
{
	LAZY_LOAD(INFRQ_FILES, NULL);
	return pkg->backup;
}

/**
 * Open a package changelog for reading. Similar to fopen in functionality,
 * except that the returned 'file stream' is from the database.
 * @param pkg the package (from db) to read the changelog
 * @return a 'file stream' to the package changelog
 */
static void *_cache_changelog_open(alpm_pkg_t *pkg)
{
	alpm_db_t *db = alpm_pkg_get_db(pkg);
	char *clfile = _alpm_local_db_pkgpath(db, pkg, "changelog");
	FILE *f = fopen(clfile, "r");
	free(clfile);
	return f;
}

/**
 * Read data from an open changelog 'file stream'. Similar to fread in
 * functionality, this function takes a buffer and amount of data to read.
 * @param ptr a buffer to fill with raw changelog data
 * @param size the size of the buffer
 * @param pkg the package that the changelog is being read from
 * @param fp a 'file stream' to the package changelog
 * @return the number of characters read, or 0 if there is no more data
 */
static size_t _cache_changelog_read(void *ptr, size_t size,
		const alpm_pkg_t UNUSED *pkg, void *fp)
{
	return fread(ptr, 1, size, (FILE *)fp);
}

/**
 * Close a package changelog for reading. Similar to fclose in functionality,
 * except that the 'file stream' is from the database.
 * @param pkg the package that the changelog was read from
 * @param fp a 'file stream' to the package changelog
 * @return whether closing the package changelog stream was successful
 */
static int _cache_changelog_close(const alpm_pkg_t UNUSED *pkg, void *fp)
{
	return fclose((FILE *)fp);
}

static int _cache_force_load(alpm_pkg_t *pkg)
{
	return local_db_read(pkg, INFRQ_ALL);
}


/** The local database operations struct. Get package fields through
 * lazy accessor methods that handle any backend loading and caching
 * logic.
 */
static struct pkg_operations local_pkg_ops = {
	.get_desc        = _cache_get_desc,
	.get_url         = _cache_get_url,
	.get_builddate   = _cache_get_builddate,
	.get_installdate = _cache_get_installdate,
	.get_packager    = _cache_get_packager,
	.get_arch        = _cache_get_arch,
	.get_isize       = _cache_get_isize,
	.get_reason      = _cache_get_reason,
	.has_scriptlet   = _cache_has_scriptlet,
	.get_licenses    = _cache_get_licenses,
	.get_groups      = _cache_get_groups,
	.get_depends     = _cache_get_depends,
	.get_optdepends  = _cache_get_optdepends,
	.get_conflicts   = _cache_get_conflicts,
	.get_provides    = _cache_get_provides,
	.get_replaces    = _cache_get_replaces,
	.get_files       = _cache_get_files,
	.get_backup      = _cache_get_backup,

	.changelog_open  = _cache_changelog_open,
	.changelog_read  = _cache_changelog_read,
	.changelog_close = _cache_changelog_close,

	.force_load      = _cache_force_load,
};

static int checkdbdir(alpm_db_t *db)
{
	struct stat buf;
	const char *path = _alpm_db_path(db);

	if(stat(path, &buf) != 0) {
		_alpm_log(db->handle, ALPM_LOG_DEBUG, "database dir '%s' does not exist, creating it\n",
				path);
		if(_alpm_makepath(path) != 0) {
			RET_ERR(db->handle, ALPM_ERR_SYSTEM, -1);
		}
	} else if(!S_ISDIR(buf.st_mode)) {
		_alpm_log(db->handle, ALPM_LOG_WARNING, _("removing invalid database: %s\n"), path);
		if(unlink(path) != 0 || _alpm_makepath(path) != 0) {
			RET_ERR(db->handle, ALPM_ERR_SYSTEM, -1);
		}
	}
	return 0;
}

static int is_dir(const char *path, struct dirent *entry)
{
#ifdef HAVE_STRUCT_DIRENT_D_TYPE
	if(entry->d_type != DT_UNKNOWN) {
		return (entry->d_type == DT_DIR);
	}
#endif
	{
		char buffer[PATH_MAX];
		struct stat sbuf;

		snprintf(buffer, PATH_MAX, "%s/%s", path, entry->d_name);

		if(!stat(buffer, &sbuf)) {
			return S_ISDIR(sbuf.st_mode);
		}
	}

	return 0;
}

static int local_db_validate(alpm_db_t *db)
{
	struct dirent *ent = NULL;
	const char *dbpath;
	DIR *dbdir;
	int ret = -1;

	if(db->status & DB_STATUS_VALID) {
		return 0;
	}
	if(db->status & DB_STATUS_INVALID) {
		return -1;
	}

	dbpath = _alpm_db_path(db);
	if(dbpath == NULL) {
		RET_ERR(db->handle, ALPM_ERR_DB_OPEN, -1);
	}
	dbdir = opendir(dbpath);
	if(dbdir == NULL) {
		if(errno == ENOENT) {
			/* database dir doesn't exist yet */
			db->status |= DB_STATUS_VALID;
			db->status &= ~DB_STATUS_INVALID;
			db->status &= ~DB_STATUS_EXISTS;
			db->status |= DB_STATUS_MISSING;
			return 0;
		} else {
			RET_ERR(db->handle, ALPM_ERR_DB_OPEN, -1);
		}
	}
	db->status |= DB_STATUS_EXISTS;
	db->status &= ~DB_STATUS_MISSING;

	while((ent = readdir(dbdir)) != NULL) {
		const char *name = ent->d_name;
		char path[PATH_MAX];

		if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
			continue;
		}
		if(!is_dir(dbpath, ent)) {
			continue;
		}

		snprintf(path, PATH_MAX, "%s%s/depends", dbpath, name);
		if(access(path, F_OK) == 0) {
			/* we found a depends file- bail */
			db->status &= ~DB_STATUS_VALID;
			db->status |= DB_STATUS_INVALID;
			db->handle->pm_errno = ALPM_ERR_DB_VERSION;
			goto done;
		}
	}
	/* we found no depends file after full scan */
	db->status |= DB_STATUS_VALID;
	db->status &= ~DB_STATUS_INVALID;
	ret = 0;

done:
	if(dbdir) {
		closedir(dbdir);
	}

	return ret;
}

static int local_db_populate(alpm_db_t *db)
{
	size_t est_count;
	int count = 0;
	struct stat buf;
	struct dirent *ent = NULL;
	const char *dbpath;
	DIR *dbdir;

	if(db->status & DB_STATUS_INVALID) {
		RET_ERR(db->handle, ALPM_ERR_DB_INVALID, -1);
	}
	/* note: DB_STATUS_MISSING is not fatal for local database */

	dbpath = _alpm_db_path(db);
	if(dbpath == NULL) {
		/* pm_errno set in _alpm_db_path() */
		return -1;
	}

	dbdir = opendir(dbpath);
	if(dbdir == NULL) {
		if(errno == ENOENT) {
			/* no database existing yet is not an error */
			db->status &= ~DB_STATUS_EXISTS;
			db->status |= DB_STATUS_MISSING;
			return 0;
		}
		RET_ERR(db->handle, ALPM_ERR_DB_OPEN, -1);
	}
	if(fstat(dirfd(dbdir), &buf) != 0) {
		RET_ERR(db->handle, ALPM_ERR_DB_OPEN, -1);
	}
	db->status |= DB_STATUS_EXISTS;
	db->status &= ~DB_STATUS_MISSING;
	if(buf.st_nlink >= 2) {
		est_count = buf.st_nlink;
	} else {
		/* Some filesystems don't subscribe to the two-implicit links school of
		 * thought, e.g. BTRFS, HFS+. See
		 * http://kerneltrap.org/mailarchive/linux-btrfs/2010/1/23/6723483/thread
		 */
		est_count = 0;
		while(readdir(dbdir) != NULL) {
			est_count++;
		}
		rewinddir(dbdir);
	}
	if(est_count >= 2) {
		/* subtract the two extra pointers to get # of children */
		est_count -= 2;
	}

	/* initialize hash at 50% full */
	db->pkgcache = _alpm_pkghash_create(est_count * 2);
	if(db->pkgcache == NULL){
		closedir(dbdir);
		RET_ERR(db->handle, ALPM_ERR_MEMORY, -1);
	}

	while((ent = readdir(dbdir)) != NULL) {
		const char *name = ent->d_name;

		alpm_pkg_t *pkg;

		if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
			continue;
		}
		if(!is_dir(dbpath, ent)) {
			continue;
		}

		pkg = _alpm_pkg_new();
		if(pkg == NULL) {
			closedir(dbdir);
			RET_ERR(db->handle, ALPM_ERR_MEMORY, -1);
		}
		/* split the db entry name */
		if(_alpm_splitname(name, &(pkg->name), &(pkg->version),
					&(pkg->name_hash)) != 0) {
			_alpm_log(db->handle, ALPM_LOG_ERROR, _("invalid name for database entry '%s'\n"),
					name);
			_alpm_pkg_free(pkg);
			continue;
		}

		/* duplicated database entries are not allowed */
		if(_alpm_pkghash_find(db->pkgcache, pkg->name)) {
			_alpm_log(db->handle, ALPM_LOG_ERROR, _("duplicated database entry '%s'\n"), pkg->name);
			_alpm_pkg_free(pkg);
			continue;
		}

		pkg->origin = PKG_FROM_LOCALDB;
		pkg->origin_data.db = db;
		pkg->ops = &local_pkg_ops;
		pkg->handle = db->handle;

		/* explicitly read with only 'BASE' data, accessors will handle the rest */
		if(local_db_read(pkg, INFRQ_BASE) == -1) {
			_alpm_log(db->handle, ALPM_LOG_ERROR, _("corrupted database entry '%s'\n"), name);
			_alpm_pkg_free(pkg);
			continue;
		}

		/* add to the collection */
		_alpm_log(db->handle, ALPM_LOG_FUNCTION, "adding '%s' to package cache for db '%s'\n",
				pkg->name, db->treename);
		db->pkgcache = _alpm_pkghash_add(db->pkgcache, pkg);
		count++;
	}

	closedir(dbdir);
	if(count > 0) {
		db->pkgcache->list = alpm_list_msort(db->pkgcache->list, (size_t)count, _alpm_pkg_cmp);
	}
	_alpm_log(db->handle, ALPM_LOG_DEBUG, "added %d packages to package cache for db '%s'\n",
			count, db->treename);

	return count;
}

/* Note: the return value must be freed by the caller */
char *_alpm_local_db_pkgpath(alpm_db_t *db, alpm_pkg_t *info, const char *filename)
{
	size_t len;
	char *pkgpath;
	const char *dbpath;

	dbpath = _alpm_db_path(db);
	len = strlen(dbpath) + strlen(info->name) + strlen(info->version) + 3;
	len += filename ? strlen(filename) : 0;
	MALLOC(pkgpath, len, RET_ERR(db->handle, ALPM_ERR_MEMORY, NULL));
	sprintf(pkgpath, "%s%s-%s/%s", dbpath, info->name, info->version,
			filename ? filename : "");
	return pkgpath;
}

#define READ_NEXT() do { \
	if(fgets(line, sizeof(line), fp) == NULL && !feof(fp)) goto error; \
	_alpm_strip_newline(line); \
} while(0)

#define READ_AND_STORE(f) do { \
	READ_NEXT(); \
	STRDUP(f, line, goto error); \
} while(0)

#define READ_AND_STORE_ALL(f) do { \
	char *linedup; \
	if(fgets(line, sizeof(line), fp) == NULL) {\
		if(!feof(fp)) goto error; else break; \
	} \
	if(_alpm_strip_newline(line) == 0) break; \
	STRDUP(linedup, line, goto error); \
	f = alpm_list_add(f, linedup); \
} while(1) /* note the while(1) and not (0) */

#define READ_AND_SPLITDEP(f) do { \
	if(fgets(line, sizeof(line), fp) == NULL) {\
		if(!feof(fp)) goto error; else break; \
	} \
	if(_alpm_strip_newline(line) == 0) break; \
	f = alpm_list_add(f, _alpm_splitdep(line)); \
} while(1) /* note the while(1) and not (0) */

static int local_db_read(alpm_pkg_t *info, alpm_dbinfrq_t inforeq)
{
	FILE *fp = NULL;
	char line[1024];
	char *pkgpath;
	alpm_db_t *db = info->origin_data.db;

	/* bitmask logic here:
	 * infolevel: 00001111
	 * inforeq:   00010100
	 * & result:  00000100
	 * == to inforeq? nope, we need to load more info. */
	if((info->infolevel & inforeq) == inforeq) {
		/* already loaded all of this info, do nothing */
		return 0;
	}

	if(info->infolevel & INFRQ_ERROR) {
		/* We've encountered an error loading this package before. Don't attempt
		 * repeated reloads, just give up. */
		return -1;
	}

	_alpm_log(db->handle, ALPM_LOG_FUNCTION, "loading package data for %s : level=0x%x\n",
			info->name, inforeq);

	pkgpath = _alpm_local_db_pkgpath(db, info, NULL);
	if(!pkgpath || access(pkgpath, F_OK)) {
		/* directory doesn't exist or can't be opened */
		_alpm_log(db->handle, ALPM_LOG_DEBUG, "cannot find '%s-%s' in db '%s'\n",
				info->name, info->version, db->treename);
		goto error;
	}
	free(pkgpath);

	/* clear out 'line', to be certain - and to make valgrind happy */
	memset(line, 0, sizeof(line));

	/* DESC */
	if(inforeq & INFRQ_DESC && !(info->infolevel & INFRQ_DESC)) {
		char *path = _alpm_local_db_pkgpath(db, info, "desc");
		if(!path || (fp = fopen(path, "r")) == NULL) {
			_alpm_log(db->handle, ALPM_LOG_ERROR, _("could not open file %s: %s\n"), path, strerror(errno));
			free(path);
			goto error;
		}
		free(path);
		while(!feof(fp)) {
			if(fgets(line, sizeof(line), fp) == NULL && !feof(fp)) {
				goto error;
			}
			if(_alpm_strip_newline(line) == 0) {
				/* length of stripped line was zero */
				continue;
			}
			if(strcmp(line, "%NAME%") == 0) {
				READ_NEXT();
				if(strcmp(line, info->name) != 0) {
					_alpm_log(db->handle, ALPM_LOG_ERROR, _("%s database is inconsistent: name "
								"mismatch on package %s\n"), db->treename, info->name);
				}
			} else if(strcmp(line, "%VERSION%") == 0) {
				READ_NEXT();
				if(strcmp(line, info->version) != 0) {
					_alpm_log(db->handle, ALPM_LOG_ERROR, _("%s database is inconsistent: version "
								"mismatch on package %s\n"), db->treename, info->name);
				}
			} else if(strcmp(line, "%DESC%") == 0) {
				READ_AND_STORE(info->desc);
			} else if(strcmp(line, "%GROUPS%") == 0) {
				READ_AND_STORE_ALL(info->groups);
			} else if(strcmp(line, "%URL%") == 0) {
				READ_AND_STORE(info->url);
			} else if(strcmp(line, "%LICENSE%") == 0) {
				READ_AND_STORE_ALL(info->licenses);
			} else if(strcmp(line, "%ARCH%") == 0) {
				READ_AND_STORE(info->arch);
			} else if(strcmp(line, "%BUILDDATE%") == 0) {
				READ_NEXT();
				info->builddate = _alpm_parsedate(line);
			} else if(strcmp(line, "%INSTALLDATE%") == 0) {
				READ_NEXT();
				info->installdate = _alpm_parsedate(line);
			} else if(strcmp(line, "%PACKAGER%") == 0) {
				READ_AND_STORE(info->packager);
			} else if(strcmp(line, "%REASON%") == 0) {
				READ_NEXT();
				info->reason = (alpm_pkgreason_t)atoi(line);
			} else if(strcmp(line, "%SIZE%") == 0) {
				READ_NEXT();
				info->isize = _alpm_strtoofft(line);
			} else if(strcmp(line, "%REPLACES%") == 0) {
				READ_AND_SPLITDEP(info->replaces);
			} else if(strcmp(line, "%DEPENDS%") == 0) {
				READ_AND_SPLITDEP(info->depends);
			} else if(strcmp(line, "%OPTDEPENDS%") == 0) {
				READ_AND_STORE_ALL(info->optdepends);
			} else if(strcmp(line, "%CONFLICTS%") == 0) {
				READ_AND_SPLITDEP(info->conflicts);
			} else if(strcmp(line, "%PROVIDES%") == 0) {
				READ_AND_SPLITDEP(info->provides);
			}
		}
		fclose(fp);
		fp = NULL;
		info->infolevel |= INFRQ_DESC;
	}

	/* FILES */
	if(inforeq & INFRQ_FILES && !(info->infolevel & INFRQ_FILES)) {
		char *path = _alpm_local_db_pkgpath(db, info, "files");
		if(!path || (fp = fopen(path, "r")) == NULL) {
			_alpm_log(db->handle, ALPM_LOG_ERROR, _("could not open file %s: %s\n"), path, strerror(errno));
			free(path);
			goto error;
		}
		free(path);
		while(fgets(line, sizeof(line), fp)) {
			_alpm_strip_newline(line);
			if(strcmp(line, "%FILES%") == 0) {
				size_t files_count = 0, files_size = 0;
				alpm_file_t *files = NULL;

				while(fgets(line, sizeof(line), fp) && _alpm_strip_newline(line)) {
					if(files_count >= files_size) {
						size_t old_size = files_size;
						if(files_size == 0) {
							files_size = 8;
						} else {
							files_size *= 2;
						}
						files = realloc(files, sizeof(alpm_file_t) * files_size);
						if(!files) {
							ALLOC_FAIL(sizeof(alpm_file_t) * files_size);
							goto error;
						}
						/* ensure all new memory is zeroed out, in both the initial
						 * allocation and later reallocs */
						memset(files + old_size, 0,
								sizeof(alpm_file_t) * (files_size - old_size));
					}
					STRDUP(files[files_count].name, line, goto error);
					/* TODO: lstat file, get mode/size */
					files_count++;
				}
				/* attempt to hand back any memory we don't need */
				files = realloc(files, sizeof(alpm_file_t) * files_count);
				info->files.count = files_count;
				info->files.files = files;
			} else if(strcmp(line, "%BACKUP%") == 0) {
				while(fgets(line, sizeof(line), fp) && _alpm_strip_newline(line)) {
					alpm_backup_t *backup;
					CALLOC(backup, 1, sizeof(alpm_backup_t), goto error);
					if(_alpm_split_backup(line, &backup)) {
						goto error;
					}
					info->backup = alpm_list_add(info->backup, backup);
				}
			}
		}
		fclose(fp);
		fp = NULL;
		info->infolevel |= INFRQ_FILES;
	}

	/* INSTALL */
	if(inforeq & INFRQ_SCRIPTLET && !(info->infolevel & INFRQ_SCRIPTLET)) {
		char *path = _alpm_local_db_pkgpath(db, info, "install");
		if(access(path, F_OK) == 0) {
			info->scriptlet = 1;
		}
		free(path);
		info->infolevel |= INFRQ_SCRIPTLET;
	}

	return 0;

error:
	info->infolevel |= INFRQ_ERROR;
	if(fp) {
		fclose(fp);
	}
	return -1;
}

int _alpm_local_db_prepare(alpm_db_t *db, alpm_pkg_t *info)
{
	mode_t oldmask;
	int retval = 0;
	char *pkgpath;

	if(checkdbdir(db) != 0) {
		return -1;
	}

	oldmask = umask(0000);
	pkgpath = _alpm_local_db_pkgpath(db, info, NULL);

	if((retval = mkdir(pkgpath, 0755)) != 0) {
		_alpm_log(db->handle, ALPM_LOG_ERROR, _("could not create directory %s: %s\n"),
				pkgpath, strerror(errno));
	}

	free(pkgpath);
	umask(oldmask);

	return retval;
}

int _alpm_local_db_write(alpm_db_t *db, alpm_pkg_t *info, alpm_dbinfrq_t inforeq)
{
	FILE *fp = NULL;
	mode_t oldmask;
	alpm_list_t *lp;
	int retval = 0;

	if(db == NULL || info == NULL || !(db->status & DB_STATUS_LOCAL)) {
		return -1;
	}

	/* make sure we have a sane umask */
	oldmask = umask(0022);

	/* DESC */
	if(inforeq & INFRQ_DESC) {
		char *path;
		_alpm_log(db->handle, ALPM_LOG_DEBUG, "writing %s-%s DESC information back to db\n",
				info->name, info->version);
		path = _alpm_local_db_pkgpath(db, info, "desc");
		if(!path || (fp = fopen(path, "w")) == NULL) {
			_alpm_log(db->handle, ALPM_LOG_ERROR, _("could not open file %s: %s\n"),
					path, strerror(errno));
			retval = -1;
			free(path);
			goto cleanup;
		}
		free(path);
		fprintf(fp, "%%NAME%%\n%s\n\n"
						"%%VERSION%%\n%s\n\n", info->name, info->version);
		if(info->desc) {
			fprintf(fp, "%%DESC%%\n"
							"%s\n\n", info->desc);
		}
		if(info->groups) {
			fputs("%GROUPS%\n", fp);
			for(lp = info->groups; lp; lp = lp->next) {
				fprintf(fp, "%s\n", (char *)lp->data);
			}
			fprintf(fp, "\n");
		}
		if(info->replaces) {
			fputs("%REPLACES%\n", fp);
			for(lp = info->replaces; lp; lp = lp->next) {
				char *depstring = alpm_dep_compute_string(lp->data);
				fprintf(fp, "%s\n", depstring);
				free(depstring);
			}
			fprintf(fp, "\n");
		}
		if(info->url) {
			fprintf(fp, "%%URL%%\n"
							"%s\n\n", info->url);
		}
		if(info->licenses) {
			fputs("%LICENSE%\n", fp);
			for(lp = info->licenses; lp; lp = lp->next) {
				fprintf(fp, "%s\n", (char *)lp->data);
			}
			fprintf(fp, "\n");
		}
		if(info->arch) {
			fprintf(fp, "%%ARCH%%\n"
							"%s\n\n", info->arch);
		}
		if(info->builddate) {
			fprintf(fp, "%%BUILDDATE%%\n"
							"%ld\n\n", info->builddate);
		}
		if(info->installdate) {
			fprintf(fp, "%%INSTALLDATE%%\n"
							"%ld\n\n", info->installdate);
		}
		if(info->packager) {
			fprintf(fp, "%%PACKAGER%%\n"
							"%s\n\n", info->packager);
		}
		if(info->isize) {
			/* only write installed size, csize is irrelevant once installed */
			fprintf(fp, "%%SIZE%%\n"
							"%jd\n\n", (intmax_t)info->isize);
		}
		if(info->reason) {
			fprintf(fp, "%%REASON%%\n"
							"%u\n\n", info->reason);
		}
		if(info->depends) {
			fputs("%DEPENDS%\n", fp);
			for(lp = info->depends; lp; lp = lp->next) {
				char *depstring = alpm_dep_compute_string(lp->data);
				fprintf(fp, "%s\n", depstring);
				free(depstring);
			}
			fprintf(fp, "\n");
		}
		if(info->optdepends) {
			fputs("%OPTDEPENDS%\n", fp);
			for(lp = info->optdepends; lp; lp = lp->next) {
				fprintf(fp, "%s\n", (char *)lp->data);
			}
			fprintf(fp, "\n");
		}
		if(info->conflicts) {
			fputs("%CONFLICTS%\n", fp);
			for(lp = info->conflicts; lp; lp = lp->next) {
				char *depstring = alpm_dep_compute_string(lp->data);
				fprintf(fp, "%s\n", depstring);
				free(depstring);
			}
			fprintf(fp, "\n");
		}
		if(info->provides) {
			fputs("%PROVIDES%\n", fp);
			for(lp = info->provides; lp; lp = lp->next) {
				char *depstring = alpm_dep_compute_string(lp->data);
				fprintf(fp, "%s\n", depstring);
				free(depstring);
			}
			fprintf(fp, "\n");
		}

		fclose(fp);
		fp = NULL;
	}

	/* FILES */
	if(inforeq & INFRQ_FILES) {
		char *path;
		_alpm_log(db->handle, ALPM_LOG_DEBUG, "writing %s-%s FILES information back to db\n",
				info->name, info->version);
		path = _alpm_local_db_pkgpath(db, info, "files");
		if(!path || (fp = fopen(path, "w")) == NULL) {
			_alpm_log(db->handle, ALPM_LOG_ERROR, _("could not open file %s: %s\n"),
					path, strerror(errno));
			retval = -1;
			free(path);
			goto cleanup;
		}
		free(path);
		if(info->files.count) {
			size_t i;
			fprintf(fp, "%%FILES%%\n");
			for(i = 0; i < info->files.count; i++) {
				const alpm_file_t *file = info->files.files + i;
				fprintf(fp, "%s\n", file->name);
			}
			fprintf(fp, "\n");
		}
		if(info->backup) {
			fprintf(fp, "%%BACKUP%%\n");
			for(lp = info->backup; lp; lp = lp->next) {
				const alpm_backup_t *backup = lp->data;
				fprintf(fp, "%s\t%s\n", backup->name, backup->hash);
			}
			fprintf(fp, "\n");
		}
		fclose(fp);
		fp = NULL;
	}

	/* INSTALL */
	/* nothing needed here (script is automatically extracted) */

cleanup:
	umask(oldmask);

	if(fp) {
		fclose(fp);
	}

	return retval;
}

int _alpm_local_db_remove(alpm_db_t *db, alpm_pkg_t *info)
{
	int ret = 0;
	char *pkgpath = _alpm_local_db_pkgpath(db, info, NULL);

	/* TODO explicit file removes and then an rmdir? */
	ret = _alpm_rmrf(pkgpath);
	free(pkgpath);
	if(ret != 0) {
		ret = -1;
	}
	return ret;
}

struct db_operations local_db_ops = {
	.validate         = local_db_validate,
	.populate         = local_db_populate,
	.unregister       = _alpm_db_unregister,
};

alpm_db_t *_alpm_db_register_local(alpm_handle_t *handle)
{
	alpm_db_t *db;

	_alpm_log(handle, ALPM_LOG_DEBUG, "registering local database\n");

	db = _alpm_db_new("local", 1);
	if(db == NULL) {
		handle->pm_errno = ALPM_ERR_DB_CREATE;
		return NULL;
	}
	db->ops = &local_db_ops;
	db->handle = handle;

	if(local_db_validate(db)) {
		/* pm_errno set in local_db_validate() */
		_alpm_db_free(db);
		return NULL;
	}

	handle->db_local = db;
	return db;
}

/* vim: set ts=2 sw=2 noet: */
