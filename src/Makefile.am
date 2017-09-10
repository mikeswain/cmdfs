bin_PROGRAMS = cmdfs
cmdfs_SOURCES = cmdfs.c cleaner.c util.c log.c monitor.c vfile.c cmdfs.h
cmdfs_CFLAGS= -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -Wall -fmessage-length=0  -std=c99 -pthread -DCACHE_ROOT=\"$(CACHE_ROOT)\"
cmdfs_LDADD = -lfuse -lpthread
