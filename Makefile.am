AM_CFLAGS = $(WARN_CFLAGS) $(WERROR_CFLAGS) $(XML_CFLAGS)

AM_YFLAGS = -d -v

BUILT_SOURCES = \
	src/grammar.h

bin_PROGRAMS = src/nmc

src_nmc_SOURCES = \
	src/parser.c \
	src/nmc.c \
	src/grammar.y \
	src/validator.c

src_nmc_LDADD = $(XML_LIBS)

check_SCRIPTS = test/nmc

TESTSUITE_AT =

TESTSUITE = $(testdir)/testsuite

AUTOTEST = $(AUTOM4TE) --language=autotest

testdir = $(srcdir)/test

$(testdir)/package.m4: $(top_srcdir)/configure.ac
	:;{ \
	echo 'm4_define([AT_PACKAGE_NAME],' && \
	echo '	[@PACKAGE_NAME@])' && \
	echo 'm4_define([AT_PACKAGE_TARNAME],' && \
	echo '	[@PACKAGE_TARNAME@])' && \
	echo 'm4_define([AT_PACKAGE_VERSION],' && \
	echo '	[@PACKAGE_VERSION@])' && \
	echo 'm4_define([AT_PACKAGE_STRING],' && \
	echo '	[@PACKAGE_STRING@])' && \
	echo 'm4_define([AT_PACKAGE_BUGREPORT],' && \
	echo '	[@PACKAGE_BUGREPORT@])'; \
	echo 'm4_define([AT_PACKAGE_URL],' && \
	echo '	[@PACKAGE_URL@])'; \
	} >'$@'

check-local: $(testdir)/atconfig $(testdir)/atlocal $(TESTSUITE)
	$(SHELL) '$(TESTSUITE)' -C '$(testdir)' $(TESTSUITEFLAGS)

installcheck-local: $(testdir)/atconfig $(testdir)/atlocal $(TESTSUITE)
	$(SHELL) '$(TESTSUITE)' -C '$(testdir)' AUTOTEST_PATH='$(bindir)' $(TESTSUITEFLAGS)

clean-local:
	test ! -f '$(TESTSUITE)' || $(SHELL) '$(TESTSUITE)' -C '$(testdir)' --clean

$(TESTSUITE): $(testdir)/testsuite.at $(testdir)/package.m4 $(TESTSUITE_AT)
	$(AUTOTEST) -I '$(testdir)' -o $@.tmp $@.at
	mv $@.tmp $@

EXTRA_DIST = \
	     $(testdir)/atlocal.in \
	     $(testdir)/package.m4 \
	     $(testdir)/testsuite.at \
	     $(TESTSUITE_AT) \
	     $(TESTSUITE)
