/*
	Cmdfs2 : hash.c

	Simple path has function

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
		default:
			*d++ = *s;
		}
	}
	while ( *s++ );
	return rv;
}
