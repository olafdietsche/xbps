/*-
 * Copyright (c) 2012-2013 Juan Romero Pardines.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "xbps_api_impl.h"

/**
 * @file lib/pkgdb.c
 * @brief Package database handling routines
 * @defgroup pkgdb Package database handling functions
 *
 * Functions to manipulate the main package database plist file (pkgdb).
 *
 * The following image shown below shows the proplib structure used
 * by the main package database plist:
 *
 * @image html images/xbps_pkgdb_array.png
 *
 * Legend:
 *  - <b>Salmon filled box</b>: \a XBPS_PKGDB file internalized.
 *  - <b>White filled box</b>: mandatory objects.
 *  - <b>Grey filled box</b>: optional objects.
 *  - <b>Green filled box</b>: possible value set in the object, only one
 *    of them is set.
 *
 * Text inside of white boxes are the key associated with the object, its
 * data type is specified on its edge, i.e array, bool, integer, string,
 * dictionary.
 */
int HIDDEN
xbps_pkgdb_init(struct xbps_handle *xhp)
{
	int rv;

	assert(xhp != NULL);

	if (xhp->pkgdb != NULL)
		return 0;

	if ((rv = xbps_pkgdb_update(xhp, false)) != 0) {
		if (rv != ENOENT)
			xbps_dbg_printf(xhp, "[pkgdb] cannot internalize "
			    "pkgdb array: %s\n", strerror(rv));

		return rv;
	}
	xbps_dbg_printf(xhp, "[pkgdb] initialized ok.\n");

	return 0;
}

int
xbps_pkgdb_update(struct xbps_handle *xhp, bool flush)
{
	prop_dictionary_t pkgdb_storage;
	char *plist;
	static int cached_rv;
	int rv = 0;

	if (cached_rv && !flush)
		return cached_rv;

	plist = xbps_xasprintf("%s/%s", xhp->metadir, XBPS_PKGDB);
	if (xhp->pkgdb && flush) {
		pkgdb_storage = prop_dictionary_internalize_from_file(plist);
		if (pkgdb_storage == NULL ||
		    !prop_dictionary_equals(xhp->pkgdb, pkgdb_storage)) {
			/* flush dictionary to storage */
			if (!prop_dictionary_externalize_to_file(xhp->pkgdb, plist)) {
				free(plist);
				return errno;
			}
		}
		if (pkgdb_storage)
			prop_object_release(pkgdb_storage);

		prop_object_release(xhp->pkgdb);
		xhp->pkgdb = NULL;
		cached_rv = 0;
	}
	/* update copy in memory */
	if ((xhp->pkgdb = prop_dictionary_internalize_from_file(plist)) == NULL) {
		if (errno == ENOENT)
			xhp->pkgdb = prop_dictionary_create();
		else
			xbps_error_printf("cannot access to pkgdb: %s\n", strerror(errno));

		cached_rv = rv = errno;
	}
	free(plist);

	return rv;
}

void HIDDEN
xbps_pkgdb_release(struct xbps_handle *xhp)
{
	assert(xhp != NULL);

	if (xhp->pkgdb == NULL)
		return;

	if (prop_object_type(xhp->pkg_metad) == PROP_TYPE_DICTIONARY)
	       prop_object_release(xhp->pkg_metad);

	prop_object_release(xhp->pkgdb);
	xhp->pkgdb = NULL;
	xbps_dbg_printf(xhp, "[pkgdb] released ok.\n");
}

int
xbps_pkgdb_foreach_reverse_cb(struct xbps_handle *xhp,
			      int (*fn)(struct xbps_handle *, prop_object_t, void *, bool *),
			      void *arg)
{
	prop_array_t allkeys;
	prop_object_t obj;
	prop_dictionary_t pkgd;
	size_t i;
	int rv;
	bool done = false;

	if ((rv = xbps_pkgdb_init(xhp)) != 0)
		return rv;

	allkeys = prop_dictionary_all_keys(xhp->pkgdb);
	assert(allkeys);

	for (i = prop_array_count(allkeys); i > 0; i--) {
		obj = prop_array_get(allkeys, i);
		pkgd = prop_dictionary_get_keysym(xhp->pkgdb, obj);
		rv = (*fn)(xhp, pkgd, arg, &done);
		if (rv != 0 || done)
			break;
	}
	prop_object_release(allkeys);
	return rv;
}

int
xbps_pkgdb_foreach_cb(struct xbps_handle *xhp,
		      int (*fn)(struct xbps_handle *, prop_object_t, void *, bool *),
		      void *arg)
{
	prop_object_t obj;
	prop_object_iterator_t iter;
	prop_dictionary_t pkgd;
	int rv;
	bool done = false;

	if ((rv = xbps_pkgdb_init(xhp)) != 0)
		return rv;

	iter = prop_dictionary_iterator(xhp->pkgdb);
	assert(iter);
	while ((obj = prop_object_iterator_next(iter))) {
		pkgd = prop_dictionary_get_keysym(xhp->pkgdb, obj);
		rv = (*fn)(xhp, pkgd, arg, &done);
		if (rv != 0 || done)
			break;
	}
	prop_object_iterator_release(iter);
	return rv;
}

prop_dictionary_t
xbps_pkgdb_get_pkg(struct xbps_handle *xhp, const char *pkg)
{
	if (xbps_pkgdb_init(xhp) != 0)
		return NULL;

	return xbps_find_pkg_in_dict(xhp->pkgdb, pkg);
}

prop_dictionary_t
xbps_pkgdb_get_virtualpkg(struct xbps_handle *xhp, const char *vpkg)
{
	if (xbps_pkgdb_init(xhp) != 0)
		return NULL;

	return xbps_find_virtualpkg_in_dict(xhp, xhp->pkgdb, vpkg);
}

static prop_dictionary_t
get_pkg_metadata(struct xbps_handle *xhp, prop_dictionary_t pkgd)
{
	prop_dictionary_t pkg_metad;
	const char *pkgver;
	char *pkgname, *plist;

	prop_dictionary_get_cstring_nocopy(pkgd, "pkgver", &pkgver);
	pkgname = xbps_pkg_name(pkgver);
	assert(pkgname);

	if ((pkg_metad = prop_dictionary_get(xhp->pkg_metad, pkgname)) != NULL) {
		free(pkgname);
		return pkg_metad;
	}
	plist = xbps_xasprintf("%s/.%s.plist", xhp->metadir, pkgname);
	pkg_metad = prop_dictionary_internalize_from_file(plist);
	free(plist);

	if (pkg_metad == NULL) {
		xbps_dbg_printf(xhp, "[pkgdb] cannot read %s metadata: %s\n",
		    pkgver, strerror(errno));
		free(pkgname);
		return NULL;
	}

	if (xhp->pkg_metad == NULL)
		xhp->pkg_metad = prop_dictionary_create();

	prop_dictionary_set(xhp->pkg_metad, pkgname, pkg_metad);
	prop_object_release(pkg_metad);
	free(pkgname);

	return pkg_metad;
}

static void
generate_full_revdeps_tree(struct xbps_handle *xhp)
{
	prop_array_t rundeps, pkg;
	prop_dictionary_t pkgd;
	prop_object_t obj;
	prop_object_iterator_t iter;
	const char *pkgver, *pkgdep, *vpkgname;
	char *curpkgname;
	unsigned int i;
	bool alloc;

	if (xhp->pkgdb_revdeps)
		return;

	xhp->pkgdb_revdeps = prop_dictionary_create();

	iter = prop_dictionary_iterator(xhp->pkgdb);
	assert(iter);

	while ((obj = prop_object_iterator_next(iter))) {
		pkgd = prop_dictionary_get_keysym(xhp->pkgdb, obj);
		rundeps = prop_dictionary_get(pkgd, "run_depends");
		if (rundeps == NULL || !prop_array_count(rundeps))
			continue;

		for (i = 0; i < prop_array_count(rundeps); i++) {
			alloc = false;
			prop_array_get_cstring_nocopy(rundeps, i, &pkgdep);
			curpkgname = xbps_pkgpattern_name(pkgdep);
			if (curpkgname == NULL)
				curpkgname = xbps_pkg_name(pkgdep);
			assert(curpkgname);
			vpkgname = vpkg_user_conf(xhp, curpkgname, false);
			if (vpkgname == NULL)
				vpkgname = curpkgname;

			pkg = prop_dictionary_get(xhp->pkgdb_revdeps, vpkgname);
			if (pkg == NULL) {
				alloc = true;
				pkg = prop_array_create();
			}
			prop_dictionary_get_cstring_nocopy(pkgd, "pkgver", &pkgver);
			if (!xbps_match_string_in_array(pkg, pkgver)) {
				prop_array_add_cstring_nocopy(pkg, pkgver);
				prop_dictionary_set(xhp->pkgdb_revdeps, vpkgname, pkg);
			}
			free(curpkgname);
			if (alloc)
				prop_object_release(pkg);
		}
	}
	prop_object_iterator_release(iter);
}

prop_array_t
xbps_pkgdb_get_pkg_revdeps(struct xbps_handle *xhp, const char *pkg)
{
	prop_array_t res;
	prop_dictionary_t pkgd;
	const char *pkgver;
	char *pkgname;

	if ((pkgd = xbps_pkgdb_get_pkg(xhp, pkg)) == NULL)
		return NULL;

	generate_full_revdeps_tree(xhp);
	prop_dictionary_get_cstring_nocopy(pkgd, "pkgver", &pkgver);
	pkgname = xbps_pkg_name(pkgver);
	res = prop_dictionary_get(xhp->pkgdb_revdeps, pkgname);
	free(pkgname);

	return res;
}

prop_dictionary_t
xbps_pkgdb_get_pkg_metadata(struct xbps_handle *xhp, const char *pkg)
{
	prop_dictionary_t pkgd;

	pkgd = xbps_pkgdb_get_pkg(xhp, pkg);
	if (pkgd == NULL)
		return NULL;

	return get_pkg_metadata(xhp, pkgd);
}
