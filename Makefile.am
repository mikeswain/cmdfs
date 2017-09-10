SUBDIRS = src test

dist_doc_DATA = README README.html COPYING INSTALL AUTHORS ChangeLog
man_MANS = cmdfs.1

CACHE_ROOT = $(localstatedir)/cache/cmdfs

install-data-hook:
	test -d "$(CACHE_ROOT)" || $(MKDIR_P) "$(CACHE_ROOT)"
	chmod 777 $(CACHE_ROOT)
