# Copyright (c) 2013-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

ACLOCAL_AMFLAGS = -I build-aux/m4
SUBDIRS = src

.PHONY: deploy FORCE

export PYTHONPATH

DEFID_BIN=$(top_builddir)/src/$(DEFI_DAEMON_NAME)$(EXEEXT)
DEFI_CLI_BIN=$(top_builddir)/src/$(DEFI_CLI_NAME)$(EXEEXT)
DEFI_TX_BIN=$(top_builddir)/src/$(DEFI_TX_NAME)$(EXEEXT)

support_examples_dir = $(pkgdatadir)/conf-examples
support_examples__DATA = $(top_srcdir)/contrib/conf-examples/default.conf \
		$(top_srcdir)/contrib/conf-examples/pruned.conf

support_rpcauth_dir = $(pkgdatadir)/rpcauth
support_rpcauth__DATA = $(top_srcdir)/contrib/rpcauth/rpcauth.py \
		$(top_srcdir)/contrib/rpcauth/README.md

support_service_dir = $(pkgdatadir)/service
support_service__DATA = \
		$(top_srcdir)/contrib/init/defid.init \
		$(top_srcdir)/contrib/init/defid.openrc \
		$(top_srcdir)/contrib/init/defid.openrcconf \
		$(top_srcdir)/contrib/init/defid.service

support_bash_dir = $(datadir)/bash-completion/completions
support_bash__DATA = $(top_srcdir)/contrib/bash-completion/defi-cli.bash-completion \
		$(top_srcdir)/contrib/bash-completion/defi-tx.bash-completion \
		$(top_srcdir)/contrib/bash-completion/defid.bash-completion

support_extra_dir = $(pkgdatadir)
support_extra__DATA = $(top_srcdir)/README.md $(top_srcdir)/LICENSE

empty :=
space := $(empty) $(empty)

DIST_DOCS = $(wildcard doc/*.md) $(wildcard doc/release-notes/*.md)
DIST_CONTRIB = $(top_srcdir)/contrib/bash-completion/defi-cli.bash-completion \
	       $(top_srcdir)/contrib/bash-completion/defi-tx.bash-completion \
	       $(top_srcdir)/contrib/bash-completion/defid.bash-completion \
	       $(top_srcdir)/contrib/init \
	       $(top_srcdir)/contrib/install_db4.sh
DIST_SHARE = \
  $(top_srcdir)/contrib/genbuild.sh \
  $(top_srcdir)/contrib/rpcauth

BIN_CHECKS=$(top_srcdir)/contrib/devtools/symbol-check.py \
           $(top_srcdir)/contrib/devtools/security-check.py

dist-hook:
	-$(GIT) archive --format=tar HEAD -- src/clientversion.cpp | $(AMTAR) -C $(top_distdir) -xf -

$(DEFID_BIN): FORCE
	$(MAKE) -C src $(@F)

$(DEFI_CLI_BIN): FORCE
	$(MAKE) -C src $(@F)

$(DEFI_TX_BIN): FORCE
	$(MAKE) -C src $(@F)


dist_noinst_SCRIPTS = autogen.sh

EXTRA_DIST = $(DIST_SHARE) $(DIST_CONTRIB) $(DIST_DOCS) $(BIN_CHECKS)

EXTRA_DIST += \
    test/functional \
    test/fuzz

EXTRA_DIST += \
    test/util/defi-util-test.py \
    test/util/rpcauth-test.py \
    $(wildcard test/util/data/*)

CLEANFILES = 

.INTERMEDIATE: 

DISTCHECK_CONFIGURE_FLAGS = 

doc/doxygen/.stamp: doc/Doxyfile FORCE
	$(MKDIR_P) $(@D)
	$(DOXYGEN) $^
	$(AM_V_at) touch $@

if HAVE_DOXYGEN
docs: doc/doxygen/.stamp
else
docs:
	@echo "error: doxygen not found"
endif

clean-docs:
	rm -rf doc/doxygen

clean-local: clean-docs
	rm -rf coverage_percent.txt test_defi.coverage/ total.coverage/ test/tmp/ cache/
	rm -rf test/functional/__pycache__ test/functional/test_framework/__pycache__ test/cache contrib/rpcauth/__pycache__
	rm -rf osx_volname dist/ dpi36.background.tiff dpi72.background.tiff

