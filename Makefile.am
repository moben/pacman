SUBDIRS = lib/libalpm src/util src/pacman scripts etc test/pacman test/util contrib
if WANT_DOC
SUBDIRS += doc
endif

ACLOCAL_AMFLAGS = -I m4 --install

# Make sure we test and build manpages when doing distcheck
DISTCHECK_CONFIGURE_FLAGS = --enable-doc --disable-git-version

# Some files automatically included, so they aren't specified below:
#   AUTHORS, COPYING, NEWS, README
EXTRA_DIST = HACKING

# Sample makepkg prototype files
pkgdatadir = ${datadir}/${PACKAGE}
dist_pkgdata_DATA = \
	proto/PKGBUILD.proto \
	proto/PKGBUILD-split.proto \
	proto/proto.install \
	proto/ChangeLog.proto

# run the pactest test suite and vercmp tests
check-local: test/pacman test/util src/pacman src/util
	LC_ALL=C $(PYTHON) $(top_srcdir)/test/pacman/pactest.py --debug=1 \
		--test $(top_srcdir)/test/pacman/tests/*.py \
		-p $(top_builddir)/src/pacman/pacman
	$(SH) $(top_srcdir)/test/util/pacsorttest.sh \
		$(top_builddir)/src/util/pacsort
	$(SH) $(top_srcdir)/test/util/vercmptest.sh \
		$(top_builddir)/src/util/vercmp

# create the pacman DB and cache directories upon install
install-data-local:
	for dir in "$(DESTDIR)$(localstatedir)/lib/pacman" "$(DESTDIR)$(localstatedir)/cache/pacman/pkg"; do \
	  test -z "$$dir" || $(MKDIR_P) "$$dir"; \
	done

update-po:
	$(MAKE) -C lib/libalpm/po update-po
	$(MAKE) -C scripts/po update-po
	$(MAKE) -C src/pacman/po update-po

# vim:set ts=2 sw=2 noet:
