AM_CFLAGS = $(WARN_CFLAGS) $(WERROR_CFLAGS)

AM_YFLAGS = -d -v

noinst_PROGRAMS = \
	build/ucategory

build_ucategory_SOURCES = \
	build/ucategory.c

BUILT_SOURCES = \
	src/grammar.h

bin_PROGRAMS = src/nmc

src_nmc_SOURCES = \
	src/list.h \
	src/parser.c \
	src/nmc.c \
	src/nmc.h \
	src/node.c \
	src/node.h \
	src/string.c \
	src/string.h \
	src/grammar.y \
	src/ucategory.h \
	src/unicode.c \
	src/unicode.h

src/ucategory.h: $(srcdir)/build/ucategory
	$(srcdir)/build/ucategory < UnicodeData.txt > $@

check_SCRIPTS = test/nmc

TESTSUITE_AT =

TESTSUITE = $(testdir)/testsuite

AUTOTEST = $(AUTOM4TE) --language=autotest
AUTOTESTFLAGS = -I $(testdir)

testdir = $(srcdir)/test

$(testdir)/package.m4: $(top_srcdir)/configure.ac
	$(AM_V_GEN)rm -f $@ $@.tmp
	$(AM_V_at){ \
	  echo 'm4_define([AT_PACKAGE_NAME], [$(PACKAGE_NAME)])' && \
	  echo 'm4_define([AT_PACKAGE_TARNAME], [$(PACKAGE_TARNAME)])' && \
	  echo 'm4_define([AT_PACKAGE_VERSION], [$(PACKAGE_VERSION)])' && \
	  echo 'm4_define([AT_PACKAGE_STRING], [$(PACKAGE_STRING)])' && \
	  echo 'm4_define([AT_PACKAGE_BUGREPORT], [$(PACKAGE_BUGREPORT)])' && \
	  echo 'm4_define([AT_PACKAGE_URL], [$(PACKAGE_URL)])'; \
	} > $@.tmp
	$(AM_V_at)mv $@.tmp $@

check-local: $(testdir)/atconfig $(testdir)/atlocal $(TESTSUITE)
	@echo '$(TESTSUITE): Entering directory `$(testdir)'\'
	$(SHELL) '$(TESTSUITE)' -C '$(testdir)' $(TESTSUITEFLAGS)
	@echo '$(TESTSUITE): Leaving directory `$(testdir)'\'

installcheck-local: $(testdir)/atconfig $(testdir)/atlocal $(TESTSUITE)
	@echo '$(TESTSUITE): Entering directory `$(testdir)'\'
	$(SHELL) '$(TESTSUITE)' -C '$(testdir)' AUTOTEST_PATH='$(bindir)' $(TESTSUITEFLAGS)
	@echo '$(TESTSUITE): Leaving directory `$(testdir)'\'

clean-local:
	if test -f '$(TESTSUITE)'; then \
	  echo '$(TESTSUITE): Entering directory `$(testdir)'\'; \
	  $(SHELL) '$(TESTSUITE)' -C '$(testdir)' --clean; \
	  echo '$(TESTSUITE): Leaving directory `$(testdir)'\'; \
	fi

$(TESTSUITE): $(testdir)/testsuite.at $(testdir)/package.m4 $(TESTSUITE_AT)
	$(AM_V_GEN)$(AUTOTEST) $(AUTOTESTFLAGS) -o $@.tmp $<
	$(AM_V_at)mv $@.tmp $@

.PHONY: maintainer-check-valgrind
maintainer-check-valgrind: $(BUILT_SOURCES) all-am $(testdir)/atconfig $(testdir)/atlocal $(TESTSUITE)
	if test -n '$(VALGRIND)'; then \
	  echo '$(TESTSUITE): Entering directory `$(testdir)'\'; \
	  VALGRIND_OPTS='--leak-check=full --show-reachable=yes --suppressions=../../valgrind.supp' \
	  $(SHELL) '$(TESTSUITE)' -C '$(testdir)' $(TESTSUITEFLAGS) PRENMC='$(VALGRIND) -q'; \
	  echo '$(TESTSUITE): Leaving directory `$(testdir)'\'; \
	fi

.PHONY: maintainer-check
maintainer-check: maintainer-check-valgrind

EXTRA_DIST = \
	     $(testdir)/atlocal.in \
	     $(testdir)/package.m4 \
	     $(testdir)/testsuite.at \
	     $(TESTSUITE_AT) \
	     $(TESTSUITE)
