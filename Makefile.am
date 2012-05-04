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
	$(SHELL) '$(TESTSUITE)' AUTOTEST_PATH='$(top_srcdir)/bin' $(TESTSUITEFLAGS)

installcheck-local: $(testdir)/atconfig $(testdir)/atlocal $(TESTSUITE)
	$(SHELL) '$(TESTSUITE)' AUTOTEST_PATH='$(bindir)' $(TESTSUITEFLAGS)

clean-local:
	test ! -f '$(TESTSUITE)' || $(SHELL) '$(TESTSUITE)' --clean

$(TESTSUITE): $(testdir)/testsuite.at $(testdir)/package.m4 $(TESTSUITE_AT)
	$(AUTOTEST) -I '$(testdir)' -o $@.tmp $@.at
	mv $@.tmp $@

EXTRA_DIST = \
	     $(testdir)/atlocal.in \
	     $(testdir)/package.m4 \
	     $(testdir)/testsuite.at \
	     $(TESTSUITE_AT) \
	     $(TESTSUITE)
