/*
	Cmdfs2 : Cmdfs.h

	Header for Cmdfs code

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
#ifndef CMDFS_H_
#define CMDFS_H_
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <syslog.h>

// Global Program options
typedef struct {
   int link_thru;
   int hide_empty_dirs;
   int stat_pass_thru;
   int monitor;
   const char *mount_dir;
   const char *cache_dir;
   const char *base_dir;
   unsigned long cache_entries;
   unsigned long cache_size;
   long cache_expiry;
   unsigned long cache_max_wait;
   const char *command;
   const char **fnmatch;
   int fnmatch_c;
   regex_t *path_regexps;
   int path_regexp_cnt;
   regex_t *exclude_regexps;
   int exclude_regexp_cnt;
   regex_t *mime_regexps;
   int mime_regexp_cnt;
} options_t;


// Virtual file abstraction
typedef struct  {
	char *src;
	char *dst;
	char *cached;
	const char *command;
	int fdh;
} vfile_t ;

vfile_t *file_create_from_src(const char *src);
vfile_t *file_create_from_dst(const char *dst);
const char *file_get_dest( vfile_t *f );
const char *file_get_src( vfile_t *f );
const char *file_get_cached_path(vfile_t *f);
const char *file_get_command(vfile_t *f);
const char *file_encache(vfile_t *f);
void file_decache( vfile_t *f );
int file_get_handle(vfile_t *f);
void file_destroy( vfile_t *f );



// Directory monitoring
typedef struct {
	int wd;
	char *path;
} wd_t;

typedef struct {
	const char *rootdir;
	const char *mountdir;
	int fd;
	pthread_t thread;
	int stop;
	wd_t *wd_lut;
	int wd_lut_count;
	int wd_lut_size;
	int status;

} monitor_t;

monitor_t *monitor_create( const char *rootdir, const char *mountdir );
void monitor_destroy(monitor_t *m);
void *monitor_run( void *_monitor ); // note void ptr for threaded use

// Cache cleaning thread
typedef struct {
	const char *dir;
	pthread_t thread;
	int stop;
	long size_lim; 	// limit on directory size, in Kb (0 = no limit)
	long entry_lim; // limit on size, in number of entries (0 = no limit)
	long age_lim;   // max age (secs) of valid files
	long sleep;		// Time next cycle will sleep for
} cleaner_t;

/*
 * Create a cleaner thread for directory dir. A file will be deleted if:
 * 	it makes the total directory size in excess of of size_lim (if >0) Mb will
 *  it makes the directory entry count in excess of entry_lim (if >0)
 *  it was last modified longer ago than age_lim (if>0) secs
 * One or more of size_lim,entry_lim, age_lim must be specified.
 * Directory contents which are not regular, writeable files will be untouched and excluded
 * from the size and count totals. Candidate files to be removed will be chosen by oldest access time
 */
cleaner_t *cleaner_create( const char *dir, long size_lim, long entry_lim, long age_lim);

/*
 * Destroy cleaner c
 */
void cleaner_destroy(cleaner_t *m);



// Error logging
void log_error( const char *fmt, ...);
void log_warning( const char *fmt, ...);
void log_debug( const char *fmt, ...);

// Utils
char *token_substitute(const char *str, const char *token, const char *value );
char *tokens_substitute(const char *str, const char *tokens[], const char *values[] );
const char *hash_path(const char *path);
const char *makepath( const char *path );
char *alloc_path(const char *dirpath);
struct dirent *alloc_dirent(const char *dirpath);
int quick_stat(char *fullpath, struct dirent *dp );

typedef struct dir_info_s {
    int mode;
    const char *path;
    const char *dir;
    const char *name;
} dir_info;

int dir_visit( const char *root, int depth,
  int (*visitor)( const dir_info *, void * ),
  void *data);
#endif /* CMDFS_H_ */
