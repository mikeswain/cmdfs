/*
	Cmdfs2 : monitor.c

	Threaded directory monitor

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
#include <sys/inotify.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

int wd_t_compare( const void *_a, const void *_b) {
	const wd_t *a = (const wd_t *)_a;
	const wd_t *b = (const wd_t *)_b;
	return a->wd < b->wd ? -1 : a->wd > b->wd ? 1 : 0;
}

void monitor_add_directory( monitor_t *m, const char *path ) {
	int wd = inotify_add_watch(m->fd,path,IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM);
	if ( wd ) {
		if ( m->wd_lut_count > m->wd_lut_size) {
			m->wd_lut_size *= 2; // need more space in buffer - double it
			m->wd_lut = realloc(m->wd_lut,m->wd_lut_size*sizeof(wd_t));
		}
		m->wd_lut[m->wd_lut_count].wd = wd;
		m->wd_lut[m->wd_lut_count++].path = strdup(path);
		qsort(m->wd_lut,m->wd_lut_count,sizeof(wd_t),wd_t_compare); // keep wd list sorted
		int count = 0, size = 4096;
		char **dirlist = malloc(size*sizeof(char *));
		DIR *dir = opendir(path);
		struct dirent *dp = alloc_dirent(path);
		struct dirent *ptr;
		// build temp list of subdirs to add - otherwise recurse can end up with too many dir ptrs open
		while ( !readdir_r(dir,dp,&ptr) && ptr != NULL ) {
			if ( strcmp(dp->d_name,".") && strcmp(dp->d_name,"..")) {
       				char subpath = asprintf("%s/%s",path,dp->d_name);
				int mode=quick_stat(subpath,dp);
				if ( mode!=-1 && // no link follow - option?
					 S_ISDIR(mode) &&
					 !access(subpath,R_OK) ) {
					if ( count > size ) {
						size *= 2;
						dirlist = realloc(dirlist,size * sizeof(char*));
					}
					dirlist[count++] = subpath;
				}
				else {
				  free(subpath);
				}
			}
		}
		free(dp);
		closedir(dir);
		for ( int i = 0; i < count; i++ ) {
			monitor_add_directory(m,dirlist[i]);
			free(dirlist[i]);
		}
		free(dirlist);
	}
}

void monitor_remove_directory( monitor_t *m, char *name ) {
	// remove all records with leading path as one found
	const char *root = strdup(name);
	wd_t *src = m->wd_lut;
	wd_t *dst = m->wd_lut;
	int new_count = 0;
	while (src < m->wd_lut + m->wd_lut_count) {
		if ( strncmp(src->path,root,strlen(root)) ) {
			if ( dst != src )
				*dst++ = *src; // copy
			else
				dst++;
			new_count++;
		}
		else {
			if (!inotify_rm_watch(m->fd,src->wd)) {
				log_warning("Unable to remove watch for %s",name);
			}
			free(src->path); // destroy
		}
		src++;
	}
	free((void *)root);
	m->wd_lut_count = new_count;
}

void *monitor_run( void *_monitor ) {
	log_debug("monitor run");
	monitor_t *m = (monitor_t *)_monitor;
	if ( (m->fd = inotify_init()) != -1) {
		monitor_add_directory(m,m->rootdir);
		int bufsize = 1024 * sizeof(struct inotify_event);
		char *eventbuf = malloc(bufsize);

		for (;;) {
			int r = read(m->fd,eventbuf,bufsize);
			if ( r == 0 || (r < 0 && errno == EINVAL) ) {
				bufsize *= 2;
				eventbuf = realloc(eventbuf,bufsize);
			}
			else if ( r < 0 ) {
				if ( errno == EINTR )
					continue; // debugger
				log_error("Read failed from inotify: %s\n",strerror(errno));
				m->status = errno;
				break;
			}
			else {
				char *eptr = eventbuf;
				while ( eptr < eventbuf+r) {
					struct inotify_event *event = (struct inotify_event *)eptr;
					wd_t find = { .wd = event->wd };
					wd_t *wd_found = bsearch(&find,m->wd_lut,m->wd_lut_count,sizeof(wd_t),wd_t_compare);
					if ( wd_found && event->name && event->len > 0) {
						char name[strlen(wd_found->path) + event->len+2];
						sprintf(name,"%s/",wd_found->path);
						strncat(name, event->name, event->len);
						if ( event->mask & (IN_MOVED_TO | IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE )) {
							struct stat st;
							if ( !stat(name,&st) ) {
								if ( S_ISREG(st.st_mode)) {
									// Is a new file - stat the corresponding file in the fuse namespace to encache
									// if needed
									char dst[strlen(m->mountdir)+strlen(name)+2];
									sprintf(dst,name[0] == '/'?"%s%s":"%s/%s",m->mountdir,name+strlen(m->rootdir));
									if (stat(dst,&st))
										log_warning("Monitor failed to stat %s\n",dst);
									else
										log_debug("New file %s cached",dst);

								}
								else if ( S_ISDIR(st.st_mode)) {
									// is a new directory - watch it, and any subdirs
									monitor_add_directory(m,name);
									log_debug("New directory %s watched",name);
								}
							}

						}
						else if ( event->mask & (IN_DELETE | IN_MOVED_FROM) ) {
							if ( !(event->mask & IN_ISDIR) ) {
								// clean any cached file
								vfile_t *f = file_create_from_src(name);
								file_decache(f);
								file_destroy(f);
								log_debug("File %s decached",name);
							}
							else {
								// directory
								monitor_remove_directory(m,name);
								log_debug("Directory %s removed",name);
							}
						}
					}
					eptr += sizeof(struct inotify_event) + event->len;
				}
			}
		}
		free(eventbuf);
		close(m->fd);
	}
	else {
		log_error("Failed to initialize inotify: %s\n",strerror(errno));
		m->status = errno;
	}
	return (void *)m;


}

monitor_t *monitor_create( const char *rootdir, const char *mountdir ) {

	monitor_t *rv = calloc(1,sizeof(monitor_t));
	rv->rootdir = strdup(rootdir);
	rv->mountdir = strdup(mountdir);
	rv->wd_lut_size = 1024;
	rv->wd_lut = malloc(rv->wd_lut_size * sizeof(wd_t));

	pthread_create(&rv->thread,NULL,monitor_run,rv);
	return rv;
}

void monitor_destroy(monitor_t *m) {
	void *retval;

	while (m->wd_lut_count > 0)
		monitor_remove_directory(m,m->wd_lut[0].path);
	m->stop = 1;
	if ( m->thread ) {
		pthread_cancel(m->thread);
		pthread_join(m->thread,&retval);
	}
	log_debug("Monitor exit with status: %s", m->status);
	if ( m->rootdir)
		free((void *)m->rootdir);
	if ( m->mountdir)
		free((void *)m->mountdir);
	if ( m->wd_lut )
		free( m->wd_lut );
	free(m);
}




