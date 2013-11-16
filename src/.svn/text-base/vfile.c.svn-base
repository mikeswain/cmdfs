/*
	Cmdfs2 : vfile.c

	Virtual file implementation

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
#include <fnmatch.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <sys/file.h>

#define SHELL "/bin/sh" 		// shell exec
#define SHELL_NAME "sh" 		// name to provide in argv[0]
#define TOKEN "%f"				// token to substtue with filename
#define FILE_ENV_VAR "INPUT_FILE"

extern options_t options;





vfile_t *file_create_from_src(const char *src) {
	vfile_t *rv = (vfile_t *)calloc(1,sizeof(vfile_t));
	rv->fdh = -1;
	rv->src = strdup(src);
	if ( strcmp(src,"/") && src[strlen(src)-1] == '/')
		rv->src[strlen(src)-1] = '\0'; // on non-root, trim trailing '/' if present
	assert(!strncmp(src,options.base_dir,strlen(options.base_dir)));
	rv->dst = rv->src + strlen(options.base_dir);
	return rv;
}

vfile_t *file_create_from_dst(const char *dst) {
	char src[strlen(options.base_dir)+strlen(dst)+1];
	strcpy(src,options.base_dir);
	strcat(src,dst);
	return file_create_from_src(src);
}


const char *file_get_dest( vfile_t *f ) {
	if ( !f->dst ) {
		f->dst = f->src+strlen(options.base_dir);
	}
	return f->dst;
}
const char *file_get_src( vfile_t *f ) {
	if ( !f->src ) {
		if ( asprintf(&f->src,"%s%s",options.base_dir,f->dst) < 0 )
			log_error("Src filename: %s",strerror(errno));
	}
	return f->src;
}

const char *file_get_cached_path(vfile_t *f) {
	if (!f->cached) {
		struct stat st;
		if ( stat(options.cache_dir,&st) ) {
			mkdir(options.cache_dir,0777);
		}
		const char *filename = hash_path(file_get_src(f)+strlen(options.base_dir)+1);
		if (asprintf(&f->cached,"%s/%s",options.cache_dir,filename) < 0)
			log_error("Cache filename: %s",strerror(errno));
		free( (void *)filename );
	}
	return f->cached;
}

const char *file_encache(vfile_t *f) {
	struct stat scache;
	struct stat ssrc;
	const char *rv = file_get_cached_path(f);
	const char *src = file_get_src(f);
	int retry = 3;
	if ( f->fdh == -1 ) {
		f->fdh = open(rv,O_RDONLY); // hold open
		if (f->fdh >= 0) {
			// attempt to get an shared lock (non-blocking)
			if ( flock(f->fdh,LOCK_SH | LOCK_NB) == -1 && errno == EWOULDBLOCK) {
				// okay log info message and block until we get it
				log_debug("Waiting on shared lock for cached file %s",rv);
				if ( flock(f->fdh,LOCK_SH ) ) {
					// couldn't caquire lock
					log_error("Failed to acquire shared lock for %s (%s)",rv,strerror(errno));
					// TODO something more sensible!
				}
				else
					log_debug("Lock acquired for cached file %s",rv);
			}
			flock(f->fdh,LOCK_UN); // unlock now
		}
	}
	do {
		if ( (f->fdh == -1 && errno == ENOENT) || // no cached file
			 (!fstat(f->fdh,&scache)  && // passed fstat
			 ((options.cache_expiry >= 0 && (time(NULL) - scache.st_mtime) > options.cache_expiry) || // expired
				(!stat(src,&ssrc) && scache.st_mtime < ssrc.st_mtime  )))) { // cache out of date
			// re-creation needed
			if (f->fdh >= 0)
				close(f->fdh); // will reopen for write in child
			char *command = token_substitute(file_get_command(f),TOKEN,src);
			// kick off subprocess
			int pid = fork();
			if ( !pid ) {
				int infile = open(src,O_RDONLY);
				if ( infile < 0 ) {
					log_error("Opening source file %s for read (%s)",src,strerror(errno));
				}
				else {
					int outfile = open(rv,O_WRONLY | O_CREAT | O_TRUNC, 0);
					if ( outfile < 0 ) {
						log_error("Opening cache file %s for write (%s)",rv,strerror(errno));
					}
					else {
						// subprocess attempt to get exclusive lock, will be released on exit
						if ( flock(outfile,LOCK_EX | LOCK_NB) == -1 ) {
							if (errno == EWOULDBLOCK) {
								// This is unusual, my parent managed to get a shared lock just now but
								// Someone else has got in and is now already recreating.I won't bother then, I'll just wait until
								// I can get a shared lock then exit, when I should have a shiny new version created by someone else
								log_debug("Waiting in subprocess for shared lock %s",rv);
								if (flock(outfile,LOCK_SH))
									log_error("Unable to obtain wait for completion of writing process on %s (%s)",rv,strerror(errno));
								else
									log_debug("File recreated by other process %s",rv);
							}
							else
								log_error("Unable to obtain exclusive (write) lock on %s (%s)",strerror(errno));
						}
						else if ( !fchmod(outfile,0600) ) {
							if (dup2(infile,STDIN_FILENO) != -1 ) {
								if ( dup2(outfile,STDOUT_FILENO) != -1) {
									char *envp[2];
									if ( asprintf(&envp[0],"%s=%s",FILE_ENV_VAR,src) >= 0 ) {
										envp[1] = NULL;
										execle(SHELL,SHELL_NAME,"-c",command,NULL,envp);
										log_error("execle failed %s (%s)",command, strerror(errno));
									}
									else
										log_error("Creating child environment (%s)",strerror(errno));
								}
								else
									log_error("Redirecting child output to file %s (%s)",rv,strerror(errno));
							}
							else
								log_error("Redirecting child input from file %s (%s)",src,strerror(errno));
						}
						else
							log_error("Setting cache file permissions file %s (%s)",rv,strerror(errno));
					}
				}
				_exit(0);
			}
			else if ( pid < 0 ) {
				log_error("Command launch failed: %s (%s)",command,strerror(errno));
			}
			else {
				int status = 0;
				if ( waitpid(pid,&status,0) < 0 ) {
					log_error("Wait for command failed: %s (%s)",command,strerror(errno));
					rv = NULL;
				}
				if ( status ) {
					file_decache(f);
					log_warning("Command returned %s non-zero status %d (decached)",command,status);
				}
				else
					f->fdh = open(rv,O_RDONLY); // hold open

			}
			free(command);
		}
	} while ( f->fdh == -1  && --retry > 0 );
	return rv;

}

int file_get_handle( vfile_t *f ) {
	file_encache(f);
	return f->fdh;
}

const char *file_get_command(vfile_t *f) {
	const char *src = file_get_src(f);
	if ( src ) {
		if ( !f->command && options.fnmatch_c > 0) {
			const char *filename = basename(src);
			for ( int i = 0; !f->command && i < options.fnmatch_c; i++) {
				if (!fnmatch(options.fnmatch[i],filename,0))
					f->command = strdup(options.command);
			}
		}
		if (!f->command && options.path_regexp_cnt > 0 ) {
			// Check path
			for ( int i = 0; !f->command && i < options.path_regexp_cnt; i++) {
				if (!regexec(&options.path_regexps[i],src,0,NULL,0))
					f->command = strdup(options.command);
			}
		}
		if ( !f->command && options.mime_regexp_cnt > 0 ) {
			// Check mime
			char *file_cmd,*mime = NULL;
			size_t n = 0;
			if ( asprintf( &file_cmd,"file -b -L --mime-type -e ascii %s",src ) < 0 ) {
				log_debug("Cache filename: %s",strerror(errno));
				return NULL;
			}
			else {

				FILE *out = popen(file_cmd,"r");
				if (out) {
					if ( getline(&mime,&n,out) > 0 ) {
						for ( int i = 0; !f->command && i < options.mime_regexp_cnt; i++) {
							if (!regexec(&options.mime_regexps[i],mime,0,NULL,0))
								f->command = strdup(options.command);
						}
					}
					if ( mime )
						free(mime);
					pclose(out);
				}
				free(file_cmd);
			}
		}
	}
	else
		log_debug("no source path %s");
	return f->command;
}

void file_decache( vfile_t *f ) {
	const char *cached = file_get_cached_path(f);
	struct stat cst;
	if ( cached && !stat(cached,&cst) && S_ISREG(cst.st_mode)) {
		unlink(cached);
	}
}

void file_destroy( vfile_t *f ) {
	if ( f ) {
		if (f->src)
			free(f->src);
		if (f->cached)
			free(f->cached);
		if (f->command)
			free((char*)f->command);
		if ( f->fdh >= 0 )
			close(f->fdh);
		free(f);
	}
}

