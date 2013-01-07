AM_CFLAGS = $(WARN_CFLAGS) $(WERROR_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir)/common -I$(top_srcdir)/include

AM_YFLAGS = --warnings=all,error --report=all

noinst_PROGRAMS = \
	build/ucategory \
	build/uwordbreak

build_ucategory_SOURCES = \
	build/ucategory.c \
	build/udata.h

build_uwordbreak_SOURCES = \
	build/uwordbreak.c \
	build/udata.h

include_HEADERS = \
	include/nmc.h

pkginclude_HEADERS = \
	include/nmc/list.h

noinst_LIBRARIES = \
	lib/libbuffer.a

lib_libbuffer_a_SOURCES = \
	common/buffer.h \
	lib/buffer.c

lib_LIBRARIES = \
	lib/libnmc.a

lib_libnmc_a_SOURCES = \
	common/private.h \
	common/buffer.h \
	lib/buffer.c \
	lib/error.c \
	lib/error.h \
	lib/grammar.y \
	lib/node.c \
	lib/node.h \
	lib/output.c \
	lib/ucategory.h \
	lib/unicode.c \
	lib/unicode.h
lib_libnmc_a_LIBADD = $(LIBOBJS)

bin_PROGRAMS = src/nmc

src_nmc_SOURCES = \
	common/private.h \
	src/nmc.c

src_nmc_LDADD = \
	lib/libbuffer.a \
	lib/libnmc.a

UNICODE_VERSION = 6.3.0
UNICODE_BASE_URL = http://www.unicode.org/Public/$(UNICODE_VERSION)/ucd
UNICODE_AUXILIARY_URL = http://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/auxiliary
UNICODE_DATA_TXT = UnicodeData-$(UNICODE_VERSION)d1.txt
UNICODE_WORD_BREAK_PROPERTY_TXT = WordBreakProperty-$(UNICODE_VERSION)d1.txt

data/$(UNICODE_DATA_TXT):
	$(AM_V_GEN)rm -f $@ $@.tmp
	$(AM_V_at)$(MKDIR_P) $(srcdir)/data
	$(AM_V_at)$(WGET) -O$@.tmp $(UNICODE_BASE_URL)/$(UNICODE_DATA_TXT)
	$(AM_V_at)mv $@.tmp $@

data/$(UNICODE_WORD_BREAK_PROPERTY_TXT):
	$(AM_V_GEN)rm -f $@ $@.tmp
	$(AM_V_at)$(MKDIR_P) $(srcdir)/data
	$(AM_V_at)$(WGET) -O$@.tmp $(UNICODE_AUXILIARY_URL)/$(UNICODE_WORD_BREAK_PROPERTY_TXT)
	$(AM_V_at)mv $@.tmp $@

lib/ucategory.h: $(srcdir)/build/ucategory data/$(UNICODE_DATA_TXT)
	$(AM_V_GEN)rm -f $@ $@.tmp
	$(AM_V_at)$(srcdir)/build/ucategory < data/$(UNICODE_DATA_TXT) > $@.tmp
	$(AM_V_at)mv $@.tmp $@

lib/uwordbreak.h: $(srcdir)/build/uwordbreak data/$(UNICODE_WORD_BREAK_PROPERTY_TXT)
	$(AM_V_GEN)rm -f $@ $@.tmp
	$(AM_V_at)$(srcdir)/build/uwordbreak < data/$(UNICODE_WORD_BREAK_PROPERTY_TXT) > $@.tmp
	$(AM_V_at)mv $@.tmp $@

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
	  $(SHELL) '$(TESTSUITE)' -C '$(testdir)' $(TESTSUITEFLAGS) PRENMC='$(VALGRIND) -q' \
	    VALGRIND_OPTS='--leak-check=full --show-reachable=yes'; \
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
