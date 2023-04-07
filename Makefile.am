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

COVERAGE_INFO = baseline.info \
  test_defi_filtered.info total_coverage.info \
  baseline_filtered.info functional_test.info functional_test_filtered.info \
  test_defi_coverage.info test_defi.info

dist-hook:
	-$(GIT) archive --format=tar HEAD -- src/clientversion.cpp | $(AMTAR) -C $(top_distdir) -xf -

$(DEFID_BIN): FORCE
	$(MAKE) -C src $(@F)

$(DEFI_CLI_BIN): FORCE
	$(MAKE) -C src $(@F)

$(DEFI_TX_BIN): FORCE
	$(MAKE) -C src $(@F)

if USE_LCOV
LCOV_FILTER_PATTERN=-p "/usr/include/" -p "/usr/lib/" -p "src/leveldb/" -p "src/bench/" -p "src/univalue" -p "src/crypto/ctaes" -p "src/secp256k1"

baseline.info:
	$(LCOV) -c -i -d $(abs_builddir)/src -o $@

baseline_filtered.info: baseline.info
	$(abs_builddir)/contrib/filter-lcov.py $(LCOV_FILTER_PATTERN) $< $@
	$(LCOV) -a $@ $(LCOV_OPTS) -o $@

test_defi.info: baseline_filtered.info
	$(MAKE) -C src/ check
	$(LCOV) -c $(LCOV_OPTS) -d $(abs_builddir)/src -t test_defi -o $@
	$(LCOV) -z $(LCOV_OPTS) -d $(abs_builddir)/src

test_defi_filtered.info: test_defi.info
	$(abs_builddir)/contrib/filter-lcov.py $(LCOV_FILTER_PATTERN) $< $@
	$(LCOV) -a $@ $(LCOV_OPTS) -o $@

functional_test.info: test_defi_filtered.info
	@TIMEOUT=15 test/functional/test_runner.py $(EXTENDED_FUNCTIONAL_TESTS)
	$(LCOV) -c $(LCOV_OPTS) -d $(abs_builddir)/src --t functional-tests -o $@
	$(LCOV) -z $(LCOV_OPTS) -d $(abs_builddir)/src

functional_test_filtered.info: functional_test.info
	$(abs_builddir)/contrib/filter-lcov.py $(LCOV_FILTER_PATTERN) $< $@
	$(LCOV) -a $@ $(LCOV_OPTS) -o $@

test_defi_coverage.info: baseline_filtered.info test_defi_filtered.info
	$(LCOV) -a $(LCOV_OPTS) baseline_filtered.info -a test_defi_filtered.info -o $@

total_coverage.info: test_defi_filtered.info functional_test_filtered.info
	$(LCOV) -a $(LCOV_OPTS) baseline_filtered.info -a test_defi_filtered.info -a functional_test_filtered.info -o $@ | $(GREP) "\%" | $(AWK) '{ print substr($$3,2,50) "/" $$5 }' > coverage_percent.txt

test_defi.coverage/.dirstamp:  test_defi_coverage.info
	$(GENHTML) -s $(LCOV_OPTS) $< -o $(@D)
	@touch $@

total.coverage/.dirstamp: total_coverage.info
	$(GENHTML) -s $(LCOV_OPTS) $< -o $(@D)
	@touch $@

cov: test_defi.coverage/.dirstamp total.coverage/.dirstamp

endif

dist_noinst_SCRIPTS = autogen.sh

EXTRA_DIST = $(DIST_SHARE) $(DIST_CONTRIB) $(DIST_DOCS) $(BIN_CHECKS)

EXTRA_DIST += \
    test/functional \
    test/fuzz

EXTRA_DIST += \
    test/util/defi-util-test.py \
    test/util/data/defi-util-test.json \
    test/util/data/blanktxv1.hex \
    test/util/data/blanktxv1.json \
    test/util/data/blanktxv2.hex \
    test/util/data/blanktxv2.json \
    test/util/data/tt-delin1-out.hex \
    test/util/data/tt-delin1-out.json \
    test/util/data/tt-delout1-out.hex \
    test/util/data/tt-delout1-out.json \
    test/util/data/tt-locktime317000-out.hex \
    test/util/data/tt-locktime317000-out.json \
    test/util/data/tx394b54bb.hex \
    test/util/data/txcreate1.hex \
    test/util/data/txcreate1.json \
    test/util/data/txcreate2.hex \
    test/util/data/txcreate2.json \
    test/util/data/txcreatedata1.hex \
    test/util/data/txcreatedata1.json \
    test/util/data/txcreatedata2.hex \
    test/util/data/txcreatedata2.json \
    test/util/data/txcreatedata_seq0.hex \
    test/util/data/txcreatedata_seq0.json \
    test/util/data/txcreatedata_seq1.hex \
    test/util/data/txcreatedata_seq1.json \
    test/util/data/txcreatemultisig1.hex \
    test/util/data/txcreatemultisig1.json \
    test/util/data/txcreatemultisig2.hex \
    test/util/data/txcreatemultisig2.json \
    test/util/data/txcreatemultisig3.hex \
    test/util/data/txcreatemultisig3.json \
    test/util/data/txcreatemultisig4.hex \
    test/util/data/txcreatemultisig4.json \
    test/util/data/txcreatemultisig5.json \
    test/util/data/txcreateoutpubkey1.hex \
    test/util/data/txcreateoutpubkey1.json \
    test/util/data/txcreateoutpubkey2.hex \
    test/util/data/txcreateoutpubkey2.json \
    test/util/data/txcreateoutpubkey3.hex \
    test/util/data/txcreateoutpubkey3.json \
    test/util/data/txcreatescript1.hex \
    test/util/data/txcreatescript1.json \
    test/util/data/txcreatescript2.hex \
    test/util/data/txcreatescript2.json \
    test/util/data/txcreatescript3.hex \
    test/util/data/txcreatescript3.json \
    test/util/data/txcreatescript4.hex \
    test/util/data/txcreatescript4.json \
    test/util/data/txcreatesignv1.hex \
    test/util/data/txcreatesignv1.json \
    test/util/data/txcreatesignv2.hex \
    test/util/rpcauth-test.py

CLEANFILES = 

.INTERMEDIATE: $(COVERAGE_INFO)

DISTCHECK_CONFIGURE_FLAGS = --enable-man

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

