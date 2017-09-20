/*
	Cmdfs2 : util.c

	Miscellaneous utility functions

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
#include <limits.h>
#include <fcntl.h>
#include <dirent.h>

int path_max(const char *dirpath) {
	int pathmax = pathconf(dirpath, _PC_PATH_MAX);
	if (pathmax == -1)         /* Limit not defined, or error */
	    pathmax = PATH_MAX;         /* Take a guess */
	return pathmax;

}
char *alloc_path(const char *dirpath) {
	return malloc(path_max(dirpath));
}

struct dirent *alloc_dirent(const char *dirpath) {
	int name_max = pathconf(dirpath, _PC_NAME_MAX);
	if (name_max == -1)         /* Limit not defined, or error */
	    name_max = NAME_MAX;    /* Take a guess */
	return (struct dirent *)malloc(offsetof(struct dirent, d_name) + name_max + 1);
}

int quick_stat(char *fullpath, struct dirent *dp ) {
#ifdef _DIRENT_HAVE_D_TYPE
	return DTTOIF(dp->d_type);
#else
	struct stat st;
	return stat(fullpath,&st) ? -1: st.mode;
#endif
}


/*
 * Given a token (e.g "%s") replace all occurrences of it with the given value
 * If the first character is doubled (e.g. %%) treat as quoted. Returns
 * allocated string
 */
char *token_substitute(const char *str, const char *token, const char *value ) {
	int size = strlen(value)+strlen(str);
	char *rv = calloc(1,size);
	const char *s = str;
	const char *l = s;
	int vlen = strlen(value);
	int tlen = strlen(token);
	while ( s < str+strlen(s) &&  (s = strstr(s,token))) {
		int slen = s-l;
		if ( s == str || *(s-1) != *token ) {// check for escaped first char
			if ( (strlen(rv) + vlen + slen) > size ) {
				size *= 2;
				rv = realloc(rv,size);
			}
			strncat(rv,l,slen);
			strcat(rv,value);
			s = l = s + tlen;
		}
		else
			s += tlen;
	}
	strcat(rv, l );
	return rv;
}

/*
 * Replace a whole bunch of tokens, using null terminated lists
 */

char *tokens_substitute(const char *str, const char *tokens[], const char *values[] ) {
	const char *token = *tokens++;
	const char *value = *values++;
	char *rv = NULL;
	while (token && value) {
		char *new = token_substitute(rv?rv:str,token,value);
		if (rv) free(rv);
		rv = new;
		token = *tokens++;
		value = *values++;
	}
	return rv?rv:strdup(str);
}


/*
 * Given a path name return a filename uniquely representing it
 */
const char *hash_path(const char *path) {
	char *rv = malloc(strlen(path)*2+1);
	const char *s = path;
	char *d = rv;
	do {
		switch (*s) {
		case '/':
			*d++ = '$'; break;
		case '$':
			*d++ = *s;
			/* no break */
		default:
			*d++ = *s; break;
		}
	}
	while ( *s++ );
	return rv;
}

/*
 * make path
 */

const char* makepath( const char *path ) {
	char cpath[path_max(path)];
	const char *rv;
	int err = 0;
	while ( !err && (!(rv = realpath(path,cpath)) && errno == ENOENT)) {
		err = mkdir(cpath,0777);
	}
	return rv;
}

/**
* Traverse the directory tree rooted at root, calling vistor function for each directory entry recursively
* Entry info is passed to visitor including full path, parent path, filename, mode
* returns 0 on success, -ve on faiure (check errno)
* if the vistor function return non-zero, traversal is aborted and +ve is returned
*/
int dir_visit( const char *root, int depth,
  int (*visitor)( const dir_info *, void *data ),
  void *data)
{
	assert(root);
	assert(visitor);
	int rv = 0;
	if ( depth < 0 ) depth = INT_MAX;

	DIR *fd = opendir(root);
	if ( fd ) {
		char fullpath[path_max(root)];
		dir_info visit;
		visit.dir = root;

		sprintf(fullpath,"%s/",strcmp(root,"/")?root:"");
		int dirsize = 0;
		int dircount = 0;
		char **dirlist = NULL;

		struct dirent *dpp = alloc_dirent(root);
		if (!dpp) { rv = -1; goto error; }
		struct dirent *dp;
		while (!rv && !readdir_r(fd,dpp,&dp) && dp != NULL) {
			strcpy(fullpath+strlen(root)+1,dp->d_name);
			visit.mode = quick_stat(fullpath,dp);
			//if ( visit.mode == -1 )
				//continue; // failed to stat - ignore
			// call visitor
			visit.path = fullpath;
			visit.name = dp->d_name;
			if ((visitor)(&visit,data)) {
				rv = 1;
				break; // exit requested
			}
			if ( S_ISDIR(visit.mode) && strcmp(dp->d_name,".") && strcmp(dp->d_name,"..")) {
				if ( dircount >= dirsize ) {
					if ( dirlist ) {
						dirsize *= 2;
						char **newdirlist = (char **)realloc(dirlist,dirsize * sizeof(char*));
						if (!newdirlist) { rv = -1; break; }
						dirlist = newdirlist;
					}
					else {
						 dirsize = 4096;
						 dirlist = (char **)malloc(dirsize*sizeof(char*));
						 if ( !dirlist ) { rv = -1; break; }
					}
				}
				dirlist[dircount++] = strdup(fullpath);
			}
		}
		closedir(fd);
		fd = NULL;
		if( !rv && dirlist && depth > 0) {
			for ( int i = 0; i < dircount; i++ ) {
				if ( dirlist[i] && (rv=dir_visit(dirlist[i],depth-1,visitor,data))) {
					break; // error or exit
				}
			}
		}
error:
		if( dirlist ) {
			for ( int i = 0; i < dircount; i++ ) {
				if ( dirlist[i] ) free(dirlist[i]);
			}
		}
		if (fd ) closedir(fd);
	}
	return rv;
}
