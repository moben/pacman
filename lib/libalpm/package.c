/*
 *  package.c
 *
 *  Copyright (c) 2006-2011 Pacman Development Team <pacman-dev@archlinux.org>
 *  Copyright (c) 2002-2006 by Judd Vinet <jvinet@zeroflux.org>
 *  Copyright (c) 2005 by Aurelien Foret <orelien@chez.com>
 *  Copyright (c) 2005, 2006 by Christian Hamar <krics@linuxforum.hu>
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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* libalpm */
#include "package.h"
#include "alpm_list.h"
#include "log.h"
#include "util.h"
#include "db.h"
#include "delta.h"
#include "handle.h"
#include "deps.h"

/** \addtogroup alpm_packages Package Functions
 * @brief Functions to manipulate libalpm packages
 * @{
 */

/** Free a package. */
int SYMEXPORT alpm_pkg_free(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return -1);

	/* Only free packages loaded in user space */
	if(pkg->origin == PKG_FROM_FILE) {
		_alpm_pkg_free(pkg);
	}

	return 0;
}

/** Check the integrity (with md5) of a package from the sync cache. */
int SYMEXPORT alpm_pkg_checkmd5sum(alpm_pkg_t *pkg)
{
	char *fpath;
	int retval;

	ASSERT(pkg != NULL, return -1);
	pkg->handle->pm_errno = 0;
	/* We only inspect packages from sync repositories */
	ASSERT(pkg->origin == PKG_FROM_SYNCDB,
			RET_ERR(pkg->handle, ALPM_ERR_WRONG_ARGS, -1));

	fpath = _alpm_filecache_find(pkg->handle, pkg->filename);

	retval = _alpm_test_checksum(fpath, pkg->md5sum, ALPM_CSUM_MD5);

	if(retval == 0) {
		return 0;
	} else if(retval == 1) {
		pkg->handle->pm_errno = ALPM_ERR_PKG_INVALID;
		retval = -1;
	}

	return retval;
}

/* Default package accessor functions. These will get overridden by any
 * backend logic that needs lazy access, such as the local database through
 * a lazy-load cache. However, the defaults will work just fine for fully-
 * populated package structures. */
static const char *_pkg_get_desc(alpm_pkg_t *pkg)        { return pkg->desc; }
static const char *_pkg_get_url(alpm_pkg_t *pkg)         { return pkg->url; }
static time_t _pkg_get_builddate(alpm_pkg_t *pkg)        { return pkg->builddate; }
static time_t _pkg_get_installdate(alpm_pkg_t *pkg)      { return pkg->installdate; }
static const char *_pkg_get_packager(alpm_pkg_t *pkg)    { return pkg->packager; }
static const char *_pkg_get_arch(alpm_pkg_t *pkg)        { return pkg->arch; }
static off_t _pkg_get_isize(alpm_pkg_t *pkg)             { return pkg->isize; }
static alpm_pkgreason_t _pkg_get_reason(alpm_pkg_t *pkg)    { return pkg->reason; }
static int _pkg_has_scriptlet(alpm_pkg_t *pkg)           { return pkg->scriptlet; }

static alpm_list_t *_pkg_get_licenses(alpm_pkg_t *pkg)   { return pkg->licenses; }
static alpm_list_t *_pkg_get_groups(alpm_pkg_t *pkg)     { return pkg->groups; }
static alpm_list_t *_pkg_get_depends(alpm_pkg_t *pkg)    { return pkg->depends; }
static alpm_list_t *_pkg_get_optdepends(alpm_pkg_t *pkg) { return pkg->optdepends; }
static alpm_list_t *_pkg_get_conflicts(alpm_pkg_t *pkg)  { return pkg->conflicts; }
static alpm_list_t *_pkg_get_provides(alpm_pkg_t *pkg)   { return pkg->provides; }
static alpm_list_t *_pkg_get_replaces(alpm_pkg_t *pkg)   { return pkg->replaces; }
static alpm_filelist_t *_pkg_get_files(alpm_pkg_t *pkg)  { return &(pkg->files); }
static alpm_list_t *_pkg_get_backup(alpm_pkg_t *pkg)     { return pkg->backup; }

static void *_pkg_changelog_open(alpm_pkg_t UNUSED *pkg)
{
	return NULL;
}

static size_t _pkg_changelog_read(void UNUSED *ptr, size_t UNUSED size,
		const alpm_pkg_t UNUSED *pkg, UNUSED void *fp)
{
	return 0;
}

static int _pkg_changelog_close(const alpm_pkg_t UNUSED *pkg,
		void UNUSED *fp)
{
	return EOF;
}

static int _pkg_force_load(alpm_pkg_t UNUSED *pkg) { return 0; }

/** The standard package operations struct. Get fields directly from the
 * struct itself with no abstraction layer or any type of lazy loading.
 */
struct pkg_operations default_pkg_ops = {
	.get_desc        = _pkg_get_desc,
	.get_url         = _pkg_get_url,
	.get_builddate   = _pkg_get_builddate,
	.get_installdate = _pkg_get_installdate,
	.get_packager    = _pkg_get_packager,
	.get_arch        = _pkg_get_arch,
	.get_isize       = _pkg_get_isize,
	.get_reason      = _pkg_get_reason,
	.has_scriptlet   = _pkg_has_scriptlet,

	.get_licenses    = _pkg_get_licenses,
	.get_groups      = _pkg_get_groups,
	.get_depends     = _pkg_get_depends,
	.get_optdepends  = _pkg_get_optdepends,
	.get_conflicts   = _pkg_get_conflicts,
	.get_provides    = _pkg_get_provides,
	.get_replaces    = _pkg_get_replaces,
	.get_files       = _pkg_get_files,
	.get_backup      = _pkg_get_backup,

	.changelog_open  = _pkg_changelog_open,
	.changelog_read  = _pkg_changelog_read,
	.changelog_close = _pkg_changelog_close,

	.force_load      = _pkg_force_load,
};

/* Public functions for getting package information. These functions
 * delegate the hard work to the function callbacks attached to each
 * package, which depend on where the package was loaded from. */
const char SYMEXPORT *alpm_pkg_get_filename(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->filename;
}

const char SYMEXPORT *alpm_pkg_get_name(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->name;
}

const char SYMEXPORT *alpm_pkg_get_version(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->version;
}

alpm_pkgfrom_t SYMEXPORT alpm_pkg_get_origin(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return -1);
	pkg->handle->pm_errno = 0;
	return pkg->origin;
}

const char SYMEXPORT *alpm_pkg_get_desc(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_desc(pkg);
}

const char SYMEXPORT *alpm_pkg_get_url(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_url(pkg);
}

time_t SYMEXPORT alpm_pkg_get_builddate(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return -1);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_builddate(pkg);
}

time_t SYMEXPORT alpm_pkg_get_installdate(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return -1);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_installdate(pkg);
}

const char SYMEXPORT *alpm_pkg_get_packager(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_packager(pkg);
}

const char SYMEXPORT *alpm_pkg_get_md5sum(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->md5sum;
}

const char SYMEXPORT *alpm_pkg_get_sha256sum(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->sha256sum;
}

const char SYMEXPORT *alpm_pkg_get_base64_sig(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->base64_sig;
}

const char SYMEXPORT *alpm_pkg_get_arch(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_arch(pkg);
}

off_t SYMEXPORT alpm_pkg_get_size(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return -1);
	pkg->handle->pm_errno = 0;
	return pkg->size;
}

off_t SYMEXPORT alpm_pkg_get_isize(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return -1);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_isize(pkg);
}

alpm_pkgreason_t SYMEXPORT alpm_pkg_get_reason(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return -1);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_reason(pkg);
}

alpm_list_t SYMEXPORT *alpm_pkg_get_licenses(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_licenses(pkg);
}

alpm_list_t SYMEXPORT *alpm_pkg_get_groups(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_groups(pkg);
}

alpm_list_t SYMEXPORT *alpm_pkg_get_depends(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_depends(pkg);
}

alpm_list_t SYMEXPORT *alpm_pkg_get_optdepends(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_optdepends(pkg);
}

alpm_list_t SYMEXPORT *alpm_pkg_get_conflicts(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_conflicts(pkg);
}

alpm_list_t SYMEXPORT *alpm_pkg_get_provides(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_provides(pkg);
}

alpm_list_t SYMEXPORT *alpm_pkg_get_replaces(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_replaces(pkg);
}

alpm_list_t SYMEXPORT *alpm_pkg_get_deltas(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->deltas;
}

alpm_filelist_t SYMEXPORT *alpm_pkg_get_files(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_files(pkg);
}

alpm_list_t SYMEXPORT *alpm_pkg_get_backup(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->get_backup(pkg);
}

alpm_db_t SYMEXPORT *alpm_pkg_get_db(alpm_pkg_t *pkg)
{
	/* Sanity checks */
	ASSERT(pkg != NULL, return NULL);
	ASSERT(pkg->origin != PKG_FROM_FILE, return NULL);
	pkg->handle->pm_errno = 0;

	return pkg->origin_data.db;
}

/** Open a package changelog for reading. */
void SYMEXPORT *alpm_pkg_changelog_open(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;
	return pkg->ops->changelog_open(pkg);
}

/** Read data from an open changelog 'file stream'. */
size_t SYMEXPORT alpm_pkg_changelog_read(void *ptr, size_t size,
		const alpm_pkg_t *pkg, void *fp)
{
	ASSERT(pkg != NULL, return 0);
	pkg->handle->pm_errno = 0;
	return pkg->ops->changelog_read(ptr, size, pkg, fp);
}

/** Close a package changelog for reading. */
int SYMEXPORT alpm_pkg_changelog_close(const alpm_pkg_t *pkg, void *fp)
{
	ASSERT(pkg != NULL, return -1);
	pkg->handle->pm_errno = 0;
	return pkg->ops->changelog_close(pkg, fp);
}

int SYMEXPORT alpm_pkg_has_scriptlet(alpm_pkg_t *pkg)
{
	ASSERT(pkg != NULL, return -1);
	pkg->handle->pm_errno = 0;
	return pkg->ops->has_scriptlet(pkg);
}

static void find_requiredby(alpm_pkg_t *pkg, alpm_db_t *db, alpm_list_t **reqs)
{
	const alpm_list_t *i;
	pkg->handle->pm_errno = 0;

	for(i = _alpm_db_get_pkgcache(db); i; i = i->next) {
		alpm_pkg_t *cachepkg = i->data;
		alpm_list_t *j;
		for(j = alpm_pkg_get_depends(cachepkg); j; j = j->next) {
			if(_alpm_depcmp(pkg, j->data)) {
				const char *cachepkgname = cachepkg->name;
				if(alpm_list_find_str(*reqs, cachepkgname) == NULL) {
					*reqs = alpm_list_add(*reqs, strdup(cachepkgname));
				}
			}
		}
	}
}

/** Compute the packages requiring a given package. */
alpm_list_t SYMEXPORT *alpm_pkg_compute_requiredby(alpm_pkg_t *pkg)
{
	const alpm_list_t *i;
	alpm_list_t *reqs = NULL;
	alpm_db_t *db;

	ASSERT(pkg != NULL, return NULL);
	pkg->handle->pm_errno = 0;

	if(pkg->origin == PKG_FROM_FILE) {
		/* The sane option; search locally for things that require this. */
		find_requiredby(pkg, pkg->handle->db_local, &reqs);
	} else {
		/* We have a DB package. if it is a local package, then we should
		 * only search the local DB; else search all known sync databases. */
		db = pkg->origin_data.db;
		if(db->status & DB_STATUS_LOCAL) {
			find_requiredby(pkg, db, &reqs);
		} else {
			for(i = pkg->handle->dbs_sync; i; i = i->next) {
				db = i->data;
				find_requiredby(pkg, db, &reqs);
			}
			reqs = alpm_list_msort(reqs, alpm_list_count(reqs), _alpm_str_cmp);
		}
	}
	return reqs;
}

/** @} */

alpm_file_t *_alpm_file_copy(alpm_file_t *dest,
		const alpm_file_t *src)
{
	STRDUP(dest->name, src->name, return NULL);
	dest->size = src->size;
	dest->mode = src->mode;

	return dest;
}

/* Helper function for comparing files list entries
 */
int _alpm_files_cmp(const void *f1, const void *f2)
{
	const alpm_file_t *file1 = f1;
	const alpm_file_t *file2 = f2;
	return strcmp(file1->name, file2->name);
}

alpm_pkg_t *_alpm_pkg_new(void)
{
	alpm_pkg_t* pkg;

	CALLOC(pkg, 1, sizeof(alpm_pkg_t), return NULL);

	return pkg;
}

/**
 * Duplicate a package data struct.
 * @param pkg the package to duplicate
 * @param new_ptr location to store duplicated package pointer
 * @return 0 on success, -1 on fatal error, 1 on non-fatal error
 */
int _alpm_pkg_dup(alpm_pkg_t *pkg, alpm_pkg_t **new_ptr)
{
	alpm_pkg_t *newpkg;
	alpm_list_t *i;
	int ret = 0;

	if(!pkg || !pkg->handle) {
		return -1;
	}

	if(!new_ptr) {
		RET_ERR(pkg->handle, ALPM_ERR_WRONG_ARGS, -1);
	}

	if(pkg->ops->force_load(pkg)) {
		_alpm_log(pkg->handle, ALPM_LOG_WARNING,
				_("could not fully load metadata for package %s-%s\n"),
				pkg->name, pkg->version);
		ret = 1;
		pkg->handle->pm_errno = ALPM_ERR_PKG_INVALID;
	}

	CALLOC(newpkg, 1, sizeof(alpm_pkg_t), goto cleanup);

	newpkg->name_hash = pkg->name_hash;
	STRDUP(newpkg->filename, pkg->filename, goto cleanup);
	STRDUP(newpkg->name, pkg->name, goto cleanup);
	STRDUP(newpkg->version, pkg->version, goto cleanup);
	STRDUP(newpkg->desc, pkg->desc, goto cleanup);
	STRDUP(newpkg->url, pkg->url, goto cleanup);
	newpkg->builddate = pkg->builddate;
	newpkg->installdate = pkg->installdate;
	STRDUP(newpkg->packager, pkg->packager, goto cleanup);
	STRDUP(newpkg->md5sum, pkg->md5sum, goto cleanup);
	STRDUP(newpkg->sha256sum, pkg->md5sum, goto cleanup);
	STRDUP(newpkg->arch, pkg->arch, goto cleanup);
	newpkg->size = pkg->size;
	newpkg->isize = pkg->isize;
	newpkg->scriptlet = pkg->scriptlet;
	newpkg->reason = pkg->reason;

	newpkg->licenses   = alpm_list_strdup(pkg->licenses);
	for(i = pkg->replaces; i; i = i->next) {
		newpkg->replaces = alpm_list_add(newpkg->replaces, _alpm_dep_dup(i->data));
	}
	newpkg->groups     = alpm_list_strdup(pkg->groups);
	if(pkg->files.count) {
		size_t filenum;
		size_t len = sizeof(alpm_file_t) * pkg->files.count;
		MALLOC(newpkg->files.files, len, goto cleanup);
		for(filenum = 0; filenum < pkg->files.count; filenum++) {
			if(!_alpm_file_copy(newpkg->files.files + filenum,
						pkg->files.files + filenum)) {
				goto cleanup;
			}
		}
		newpkg->files.count = pkg->files.count;
	}
	for(i = pkg->backup; i; i = i->next) {
		newpkg->backup = alpm_list_add(newpkg->backup, _alpm_backup_dup(i->data));
	}
	for(i = pkg->depends; i; i = i->next) {
		newpkg->depends = alpm_list_add(newpkg->depends, _alpm_dep_dup(i->data));
	}
	newpkg->optdepends = alpm_list_strdup(pkg->optdepends);
	for(i = pkg->conflicts; i; i = i->next) {
		newpkg->conflicts = alpm_list_add(newpkg->conflicts, _alpm_dep_dup(i->data));
	}
	for(i = pkg->provides; i; i = i->next) {
		newpkg->provides = alpm_list_add(newpkg->provides, _alpm_dep_dup(i->data));
	}
	for(i = pkg->deltas; i; i = i->next) {
		newpkg->deltas = alpm_list_add(newpkg->deltas, _alpm_delta_dup(i->data));
	}

	/* internal */
	newpkg->infolevel = pkg->infolevel;
	newpkg->origin = pkg->origin;
	if(newpkg->origin == PKG_FROM_FILE) {
		newpkg->origin_data.file = strdup(pkg->origin_data.file);
	} else {
		newpkg->origin_data.db = pkg->origin_data.db;
	}
	newpkg->ops = pkg->ops;
	newpkg->handle = pkg->handle;

	*new_ptr = newpkg;
	return ret;

cleanup:
	_alpm_pkg_free(newpkg);
	RET_ERR(pkg->handle, ALPM_ERR_MEMORY, -1);
}

void _alpm_pkg_free(alpm_pkg_t *pkg)
{
	if(pkg == NULL) {
		return;
	}

	FREE(pkg->filename);
	FREE(pkg->name);
	FREE(pkg->version);
	FREE(pkg->desc);
	FREE(pkg->url);
	FREE(pkg->packager);
	FREE(pkg->md5sum);
	FREE(pkg->sha256sum);
	FREE(pkg->base64_sig);
	FREE(pkg->arch);

	FREELIST(pkg->licenses);
	alpm_list_free_inner(pkg->replaces, (alpm_list_fn_free)_alpm_dep_free);
	alpm_list_free(pkg->replaces);
	FREELIST(pkg->groups);
	if(pkg->files.count) {
		size_t i;
		for(i = 0; i < pkg->files.count; i++) {
			free(pkg->files.files[i].name);
		}
		free(pkg->files.files);
	}
	alpm_list_free_inner(pkg->backup, (alpm_list_fn_free)_alpm_backup_free);
	alpm_list_free(pkg->backup);
	alpm_list_free_inner(pkg->depends, (alpm_list_fn_free)_alpm_dep_free);
	alpm_list_free(pkg->depends);
	FREELIST(pkg->optdepends);
	alpm_list_free_inner(pkg->conflicts, (alpm_list_fn_free)_alpm_dep_free);
	alpm_list_free(pkg->conflicts);
	alpm_list_free_inner(pkg->provides, (alpm_list_fn_free)_alpm_dep_free);
	alpm_list_free(pkg->provides);
	alpm_list_free_inner(pkg->deltas, (alpm_list_fn_free)_alpm_delta_free);
	alpm_list_free(pkg->deltas);
	alpm_list_free(pkg->delta_path);
	alpm_list_free(pkg->removes);

	if(pkg->origin == PKG_FROM_FILE) {
		FREE(pkg->origin_data.file);
	}
	FREE(pkg);
}

/* This function should be used when removing a target from upgrade/sync target list
 * Case 1: If pkg is a loaded package file (PKG_FROM_FILE), it will be freed.
 * Case 2: If pkg is a pkgcache entry (PKG_FROM_CACHE), it won't be freed,
 *         only the transaction specific fields of pkg will be freed.
 */
void _alpm_pkg_free_trans(alpm_pkg_t *pkg)
{
	if(pkg == NULL) {
		return;
	}

	if(pkg->origin == PKG_FROM_FILE) {
		_alpm_pkg_free(pkg);
		return;
	}

	alpm_list_free(pkg->removes);
	pkg->removes = NULL;
}

/* Is spkg an upgrade for localpkg? */
int _alpm_pkg_compare_versions(alpm_pkg_t *spkg, alpm_pkg_t *localpkg)
{
	return alpm_pkg_vercmp(spkg->version, localpkg->version);
}

/* Helper function for comparing packages
 */
int _alpm_pkg_cmp(const void *p1, const void *p2)
{
	const alpm_pkg_t *pkg1 = p1;
	const alpm_pkg_t *pkg2 = p2;
	return strcmp(pkg1->name, pkg2->name);
}

/* Test for existence of a package in a alpm_list_t*
 * of alpm_pkg_t*
 */
alpm_pkg_t *_alpm_pkg_find(alpm_list_t *haystack, const char *needle)
{
	alpm_list_t *lp;
	unsigned long needle_hash;

	if(needle == NULL || haystack == NULL) {
		return NULL;
	}

	needle_hash = _alpm_hash_sdbm(needle);

	for(lp = haystack; lp; lp = lp->next) {
		alpm_pkg_t *info = lp->data;

		if(info) {
			if(info->name_hash != needle_hash) {
				continue;
			}

			/* finally: we had hash match, verify string match */
			if(strcmp(info->name, needle) == 0) {
				return info;
			}
		}
	}
	return NULL;
}

/** Test if a package should be ignored.
 *
 * Checks if the package is ignored via IgnorePkg, or if the package is
 * in a group ignored via IgnoreGroup.
 *
 * @param handle the context handle
 * @param pkg the package to test
 *
 * @return 1 if the package should be ignored, 0 otherwise
 */
int _alpm_pkg_should_ignore(alpm_handle_t *handle, alpm_pkg_t *pkg)
{
	alpm_list_t *groups = NULL;

	/* first see if the package is ignored */
	if(alpm_list_find_str(handle->ignorepkg, pkg->name)) {
		return 1;
	}

	/* next see if the package is in a group that is ignored */
	for(groups = handle->ignoregroup; groups; groups = groups->next) {
		char *grp = groups->data;
		if(alpm_list_find_str(alpm_pkg_get_groups(pkg), grp)) {
			return 1;
		}
	}

	return 0;
}

/* vim: set ts=2 sw=2 noet: */
