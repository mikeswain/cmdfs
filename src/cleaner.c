/*
	Cmdfs2 : cleaner.c

	Threaded (cache) directory cleaner. Will unobtrusively remove stale files
	from a given directory using cache parameters supplied on construction.

	Copyright (C) 2010  Mike Swain

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "cmdfs.h"
#include <pthread.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

#define SLEEP_MIN 2	  // Minumum sleep period between scans (won't attempt less)
#define SLEEP_MAX 64  // Maximum approx 1 minutes (will speed up exponentially if it finds work)

typedef struct {
	struct stat st;
	char *name;
} entry_t;

int timespeccmp( const struct timespec *a, const struct timespec *b) {
	return a->tv_sec == b->tv_sec ?
		( a->tv_nsec == b->tv_nsec ? 0 : a->tv_nsec < b->tv_nsec ? -1 : 1 ) :
		a->tv_sec < b->tv_sec ? -1 : 1 ;
}
int entry_atim_compare( const void *_a, const void *_b) {
	const entry_t *a = (const entry_t *)_a;
	const entry_t *b = (const entry_t *)_b;
	return -timespeccmp(&a->st.st_atim,&b->st.st_atim);
}



void *cleaner_run( void *_cleaner ) {
	cleaner_t *c = (cleaner_t *)_cleaner;
	struct timespec lastmod = { 0,0 };
	while (!c->stop) {
		long now = time(NULL);
		struct stat st;
		if (  !stat(c->dir, &st) && (c->age_lim > 0 || timespeccmp(&st.st_ctim,&lastmod))) { // or a file has changed
			// Something in dir has changed, I may have work to do
			lastmod = st.st_ctim;
			DIR *dirp = opendir(c->dir);
			if ( dirp ) {
				long dirsize = 0;
				int entries_size = 4096;
				int entries_count = 0;
				entry_t *entries = malloc(entries_size * sizeof(entry_t));
				struct dirent *dp = alloc_dirent(c->dir);
				struct dirent *dptr;
				while ( !readdir_r(dirp,dp,&dptr) && dptr != NULL ) {
					if (strcmp(dp->d_name,"..") && strcmp(dp->d_name,".")) {
						if ( entries_count >= entries_size ) {
							entries_size *= 2;
							entries = realloc(entries,entries_size);
						}
						entry_t *entry = entries + entries_count;
						char *fname;
						if (asprintf(&fname,"%s/%s",c->dir,dp->d_name)) {
							if ( !stat(fname,&entry->st) ) {
								if (S_ISREG(entry->st.st_mode) && !access(fname,O_RDWR)) {
									if ( c->age_lim <= 0 || (now - entry->st.st_mtim.tv_sec) < c->age_lim ) {
										// Add to list of entries to be considered for culling
										entry->name = strdup(dp->d_name);
										entries_count++;
										dirsize += entry->st.st_size / 1024; // total size in Kb
									}
									else {
										// Definately too old, cull it now
										if ( !unlink(fname))
											log_debug("Expired file %s removed",fname);
										else
											log_error("Failed to cull file from cache directory %s (%s)",fname,strerror(errno));
									}
								}
							}
							else {
								log_debug("Cleaner unable to stat %s - ignored (%s)",fname,strerror(errno));
							}
							free(fname);
						}
					}
				}
				if ( 	(c->size_lim > 0 && dirsize > c->size_lim) ||
						(c->entry_lim > 0 && entries_count > c->entry_lim) ) {
					// Directory is over size limit or entry limit, sort by access time ready to cull
					qsort(entries,entries_count,sizeof(entry_t),entry_atim_compare);
					long totalsize = 0;
					int oversize = 0;
					int overcount = 0;
					for ( int i = 0; i < entries_count; i++ ) {
						entry_t *entry = entries + i;
						if ( !oversize && !overcount) {
							totalsize += entry->st.st_size;
							oversize = c->size_lim > 0 && (totalsize/(1024*1024)) > c->size_lim;
							overcount = c->entry_lim > 0 && i > c->entry_lim;
						}
						else {
							char *fname;
							if (!asprintf(&fname,"%s/%s",c->dir,entry->name)) {
							if ( !unlink(fname))
									log_debug("Culled %s (%s)",fname,oversize ? "exceeded directory size limit" : "exceeded directory entry limit");
								else
									log_error("Failed to cull file from cache directory %s (%s)",fname,strerror(errno));
								free(fname);
							}
						}
					}

					// Items were culled, reduce sleep period
					if ( c->sleep > 5 )
						c->sleep /= 2;

				}
				else {
					// No items were culled, increase sleep period
					if ( c->sleep < SLEEP_MAX )
						c->sleep *= 2;

				}
				for ( int i = 0; i < entries_count; i++ ) {
					free(entries[i].name);
				}

				free(entries);
			}
			else {
				log_warning("Cleaner unable to open cache directory %s (%s)",c->dir,strerror(errno));
			}
		}
		if ( sleep(c->sleep) ) {
			// signal - exit
			log_warning("Cleaner thread terminated by signal");
			return (void *)-1;
		}
	}
	return NULL;
}


cleaner_t *cleaner_create( const char *dir, long size_lim, long entry_lim, long age_lim ) {
	assert(size_lim > 0 || entry_lim > 0 || age_lim > 0); // one must be specified
	cleaner_t *rv = calloc(1,sizeof(cleaner_t));
	rv->size_lim = size_lim;
	rv->entry_lim = entry_lim;
	rv->age_lim = age_lim;
	rv->dir = strdup(dir);
	rv->sleep = 1;
	pthread_create(&rv->thread,NULL,cleaner_run,rv);
	return rv;
}

/*
 * Destroy cleaner c
 */
void cleaner_destroy(cleaner_t *c) {
	void *retval;
	c->stop = 1;
	if ( c->thread ) {
		pthread_cancel(c->thread);
		pthread_join(c->thread,&retval);
	}
	if ( c->dir)
		free((void *)c->dir);
	free(c);
}
