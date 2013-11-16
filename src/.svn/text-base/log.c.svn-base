/*
	Cmdfs2 : log.c

	logging redirect

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
#include <stdarg.h>
#include <syslog.h>
void log_error( const char *fmt, ...) {
	va_list args;
	va_start (args, fmt);
	vsyslog(LOG_ERR,fmt,args);
	va_end (args); // end processing the argument list.
}

void log_warning( const char *fmt, ...) {
	va_list args;
	va_start (args, fmt);
	vsyslog(LOG_WARNING,fmt,args);
	va_end (args); // end processing the argument list.
}

void log_debug( const char *fmt, ...) {
	va_list args;
	va_start (args, fmt);
	vsyslog(LOG_DEBUG,fmt,args);
	va_end (args); // end processing the argument list.
}
