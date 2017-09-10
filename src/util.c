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

char *alloc_path(const char *dirpath) {
	int path_max = pathconf(dirpath, _PC_PATH_MAX);
	if (path_max == -1)         /* Limit not defined, or error */
	    path_max = PATH_MAX;         /* Take a guess */
	return (char *)malloc(path_max);
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
int makedirsX( const char *path ) {

	char cpath[strlen(path)];
	const char *last = path, *next;
	int err = 0;
	struct stat st;

	while (!err && (next=strchr(last,'/'))) {
		strncpy(cpath+(last-path),last,next-last);
		last = next+1;
		err = stat(cpath,&st) && mkdir(cpath,0777);

	}
	if ( !err )
		err = stat(path,&st) && mkdir(path,0777);
	return err;
}
 */

const char* makepath( const char *path ) {
	char cpath[PATH_MAX];
	const char *rv;
	int err = 0;
	while ( !err && (!(rv = realpath(path,cpath)) && errno == ENOENT)) {
		err = mkdir(cpath,0777);
	}
	return rv;
}

