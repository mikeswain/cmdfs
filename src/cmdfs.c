/**
* Required by fuse.h
*/
#define FUSE_USE_VERSION 26

#include "config.h"
#include "cmdfs.h"

#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <fuse/fuse_opt.h>
#include <dirent.h>
#include <stdint.h>
#include <pthread.h>


options_t options = {
	.hide_empty_dirs = 0,
	.link_thru = 0,
	.stat_pass_thru = 0,
	.monitor = 0,
	.mount_dir = NULL,
	.base_dir = NULL,
	.cache_dir = NULL,
	.cache_entries = 0,
	.cache_size = 0,
	.cache_expiry = -1L,
	.cache_max_wait = 600,
	.command = NULL,
	.fnmatch = NULL,
	.fnmatch_c = 0,
	.path_regexps = NULL,
	.path_regexp_cnt = 0,
	.exclude_regexps = NULL,
	.exclude_regexp_cnt = 0,
	.mime_regexps = NULL,
	.mime_regexp_cnt = 0
};

monitor_t *monitor = NULL;
cleaner_t *cleaner = NULL;


static int is_empty_visitor( const dir_info *visit, void *data ) {
	int rv = 0;
	if ( S_ISREG(visit->mode) ) {
		if ( options.link_thru ) {
			rv = 1; // there is a regular file and linked thru, so not empty
		}
		else {
				vfile_t *f = file_create_from_src(visit->path);
				rv = file_get_command(f) != NULL; // virtual file is to be generated so not empty
				file_destroy(f);
		}
	}
	return rv;
}
static int is_empty(const char *dirpath) {
	return dir_visit(dirpath,-1,is_empty_visitor,NULL) == 0;
}


int cmdfs_open(const char *path, struct fuse_file_info *info) {
	info->fh = (uint64_t)(long)file_create_from_dst(path);

	return 0;
}

int cmdfs_getattr(const char *path, struct stat *st) {
	vfile_t *f = file_create_from_dst(path);
	int rv = 0;
	const char *src = file_get_src(f);
	if ( stat(src,st))
		rv = -errno;
	else if (S_ISREG(st->st_mode)) {
		if ( file_get_command(f)) {
			// is covered by command
		  const char *cacheName = file_get_cached_path(f);
		  struct stat dststat;
		  int cacheExists = cacheName && !stat(cacheName,&dststat);
		  int cacheIsFile = cacheExists && S_ISREG(dststat.st_mode);

		  if (options.stat_pass_thru && !cacheIsFile) { // either can pass stat through and uncached, or stat of cached file failed
		    if ( stat(src, st))
		      rv = -errno;
		  } else {
		    if ( !cacheExists && stat(file_encache(f),&dststat)) // okay, wasn't cached before so cache and stat
		      rv = -errno;
		    else {
		      st->st_size = dststat.st_size;
		      st->st_mode &= S_IFREG | 0444; // always readonly
		    }
		  }
		}
		else if (options.link_thru) {
			// Return read only link
			st->st_mode = S_IFLNK | (st->st_mode & 0444);
			st->st_nlink = 2;
			st->st_size = strlen(path);
		}
		else
			// Pretend nothing's there
			rv = -ENOENT;
	}
	else if (S_ISDIR(st->st_mode) && options.hide_empty_dirs && is_empty(src)) {
		// Pretend nothing's there - directory and subdirs are empty of matching files
		rv = -ENOENT;
	}
	file_destroy(f);
	return rv;
}

struct readdir_collector {
	fuse_fill_dir_t fill;
	off_t offset;
	struct fuse_file_info *info;
	int pos;
	void *buf;
};
static int readdir_visitor( const dir_info *visit, void *data ) {
	//log_debug(visit->path);
	struct readdir_collector *c = (struct readdir_collector *)data;
	if (++c->pos > c->offset ) {
		int isParent = !strcmp(visit->name,"/") || !strcmp(visit->name,".."); // special case '..' looks in mount's parent
    const char *cpath = visit->path;
		char parent[strlen(options.mount_dir)+4];
		if ( isParent ) {
			sprintf(parent,"%s/..",options.mount_dir);
			cpath = parent;
		}
		if (isParent || (S_ISDIR(visit->mode) && // its a dir
				!(options.hide_empty_dirs && is_empty(cpath))) || // its empty and we're hiding empties
				options.link_thru) { // or linking thru
			if (c->fill(c->buf,visit->name,NULL,c->pos))
				return -1; // break out
		}
		else if (!options.link_thru && S_ISREG(visit->mode)) {
			// we don want to show if it doesn't match command filter
			vfile_t *f = file_create_from_src(cpath);
			if ( file_get_command(f) && c->fill(c->buf,visit->name,NULL,c->pos)) {
					file_destroy(f);
					return -1;
			}
			file_destroy(f);
		}
	}
	return 0;
}

int cmdfs_readdir(const char *path, void *buf, fuse_fill_dir_t fill, off_t offset, struct fuse_file_info *info) {
	vfile_t *d = file_create_from_dst(path);
	int rv = 0;
	const char *src = file_get_src(d);
	if (options.hide_empty_dirs && is_empty(src)) {
		rv = -ENOENT; // dir hidden
	}
	else  {
		struct readdir_collector collect = {
			.fill = fill,
			.offset = offset,
			.info = info,
			.buf = buf
		};
		if ( dir_visit(src,0,readdir_visitor,&collect) < 0 )
			rv = -errno;
	}
	file_destroy(d);
	return rv;

}

int cmdfs_release(const char *path, struct fuse_file_info *info) {
	vfile_t *f = (vfile_t *)(long)info->fh;
	if ( f ) {
		file_destroy(f);
		info->fh = 0;
	}
	return 0;

}

int cmdfs_readlink(const char *path, char *buf, size_t size) {
	vfile_t *l = file_create_from_dst(path);
	strcpy(buf,file_get_src(l));
	file_destroy(l);
	return 0;
}
int cmdfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *info) {
	vfile_t *f = (vfile_t *)(long)info->fh;
	if ( f ) {
		lseek(file_get_handle(f),offset,SEEK_SET);
		return read(file_get_handle(f), buf, size);
	}
	else
		return -EIO;
}

void *cmdfs_init(struct fuse_conn_info *conn) {
	if ( options.monitor ) {
		monitor = monitor_create(options.base_dir,options.mount_dir);
		log_debug("monitor thread created");
	}
	if ( options.cache_entries || options.cache_size || options.cache_expiry > 0 ) {
		cleaner = cleaner_create(options.cache_dir,options.cache_size,options.cache_entries, options.cache_expiry);
		log_debug("cleaner thread created");
	}
	return NULL;
}

void cmdfs_destroy(void *p) {
	if ( monitor ) {
		monitor_destroy(monitor);
		log_debug("monitor thread destroyed");
	}
	if ( cleaner ) {
		cleaner_destroy(cleaner);
		log_debug("cleaner thread destroyed");
	}
	log_debug("end of session");
}
static struct fuse_operations cmdfs_operations = {

	.init = cmdfs_init,
	.getattr   = cmdfs_getattr,
    .readdir   = cmdfs_readdir,
    .open   = cmdfs_open,
    .read   = cmdfs_read,
    .release = cmdfs_release,
    .readlink = cmdfs_readlink,
    .destroy = cmdfs_destroy
};


/** macro to define options */
#define CMDFS_OPT_KEY(t, p, v) { t, offsetof(options_t, p), v }

/** keys for FUSE_OPT_ options */
enum
{
   KEY_HELP,
   KEY_VERSION,
   KEY_EXTENSION,
   KEY_PATH_RE,
	 KEY_EXCLUDE_RE,
   KEY_MIME_RE
};

struct fuse_opt cmdfs_opts[] = {

	CMDFS_OPT_KEY("link-thru", link_thru, 1),
	CMDFS_OPT_KEY("nolink-thru", link_thru, 0),
	CMDFS_OPT_KEY("stat-pass-thru", stat_pass_thru, 1),
	CMDFS_OPT_KEY("nostat-pass-thru", stat_pass_thru, 0),
	CMDFS_OPT_KEY("hide-empty-dirs", hide_empty_dirs, 1),
	CMDFS_OPT_KEY("nohide-empty-dirs", hide_empty_dirs, 0),
	CMDFS_OPT_KEY("monitor",   monitor, 1),
	CMDFS_OPT_KEY("nomonitor",   monitor, 0),

	CMDFS_OPT_KEY("cache-dir=%s",   cache_dir, 0),
	CMDFS_OPT_KEY("cache-size=%lu",   cache_size, 0),
	CMDFS_OPT_KEY("cache-entries=%lu",   cache_entries, 0),
	CMDFS_OPT_KEY("cache-expiry=%lu",   cache_expiry, 0),
	CMDFS_OPT_KEY("command=%s",   command, 0),
	FUSE_OPT_KEY("extension=%s",KEY_EXTENSION),
	FUSE_OPT_KEY("path-re=%s",KEY_PATH_RE),
	FUSE_OPT_KEY("exclude-re=%s",KEY_EXCLUDE_RE),
	FUSE_OPT_KEY("mime-re=%s",KEY_MIME_RE),

	FUSE_OPT_KEY("-V",             KEY_VERSION),
	FUSE_OPT_KEY("--version",      KEY_VERSION),
	FUSE_OPT_KEY("-h",             KEY_HELP),
	FUSE_OPT_KEY("--help",         KEY_HELP),

	FUSE_OPT_END
};

static char *get_regerror (int errcode, regex_t *compiled) {
	size_t length = regerror (errcode, compiled, NULL, 0);
	char *buffer = (char *)malloc (length);
	(void) regerror (errcode, compiled, buffer, length);
	return buffer;
}

static int cmdfs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	 char *val = strchr(arg,'=');
     switch (key) {
     case KEY_HELP:
             fprintf(stderr,
                     "usage: %s source-dir mountpoint [options]\n"
                     "\n"
                     "general options:\n"
                     "    -o opt,[opt...]  mount options\n"
                     "    -h   --help      print help\n"
                     "    -V   --version   print version\n"
                     "\n"
                     "Cmdfs options:\n"
                     "    -o command=<shell command> (dd)\n"
                     "    -o extension=ext1[;ext2[;...]]\n"
                     "    -o path-re=<regular expression>\n"
										 "    -o exclude-re=<regular expression>\n"
                     "    -o mime-re=<regular expression>\n"
            		 "    -o [no]link-thru (nolink-thru)\n"
                         "    -o [no]stat_pass_thru (nostat_pass_thru)\n"
            		 "    -o [no]hide-empty-dirs (nohide-empty-dirs)\n"
            		 "    -o [no]monitor (nomonitor)\n"
            		 "    -o [no]stat-pass-thru (stat-pass-thru)\n"
            		 "    -o cache-dir=<dir> (%s/<user>/<source-dir>)\n"
            		 "    -o cache-size=<size in Mb> (no limit)\n"
            		 "    -o cache-entries=<count> (no limit)\n"
            		 "    -o cache-expiry=<time in secs> (no expiry)\n"
                     , outargs->argv[0], CACHE_ROOT);
             fuse_opt_add_arg(outargs, "-ho");
             fuse_main(outargs->argc, outargs->argv, &cmdfs_operations, NULL);
             exit(1);

     case KEY_VERSION:
             fprintf(stderr, "%s\n", PACKAGE_STRING);
             fuse_opt_add_arg(outargs, "--version");
             fuse_main(outargs->argc, outargs->argv, &cmdfs_operations, NULL);
             exit(0);
     case KEY_EXTENSION:
    	if (val && strlen(val)>0) {
    		char *exts = strdup(val+1);
    		char *save_ptr = NULL, *ext;
    		for ( ext = strtok_r(exts,";",&save_ptr); ext != NULL; ext = strtok_r(NULL,";",&save_ptr)) {
    			char *expr;
    			int err;
    			options.path_regexps = realloc(options.path_regexps,sizeof(regex_t)*++options.path_regexp_cnt);
    			if ( asprintf(&expr,".*/.*\\.%s",ext) < 0 )
    				exit(0);
    			if ((err=regcomp(options.path_regexps+(options.path_regexp_cnt-1), expr,REG_ICASE|REG_NOSUB))) {
    				log_error("Error compiling regex: %s",get_regerror(err,options.path_regexps+(options.path_regexp_cnt-1)));
    				return 1;
    			}
    			free(expr);
    		}
    		free(exts);
    		log_debug("extension: %s\n",val+1);
			return 0;
    	}
     case KEY_PATH_RE:
    	if (val && strlen(val)>0) {
			int err;
			options.path_regexps = realloc(options.path_regexps,sizeof(regex_t)*++options.path_regexp_cnt);
			if ((err=regcomp(options.path_regexps+(options.path_regexp_cnt-1), val+1,0))) {
				log_error("Error compiling path regex: %s",get_regerror(err,options.path_regexps+(options.path_regexp_cnt-1)));
				return 1;
			}
			log_debug("path-re: %s",val+1);
			return 0;
    	}
			case KEY_EXCLUDE_RE:
     	if (val && strlen(val)>0) {
 			int err;
 			options.exclude_regexps = realloc(options.exclude_regexps,sizeof(regex_t)*++options.exclude_regexp_cnt);
 			if ((err=regcomp(options.exclude_regexps+(options.exclude_regexp_cnt-1), val+1,0))) {
 				log_error("Error compiling exclude regex: %s",get_regerror(err,options.exclude_regexps+(options.exclude_regexp_cnt-1)));
 				return 1;
 			}
 			log_debug("exclude-re: %s",val+1);
 			return 0;
     	}
     case KEY_MIME_RE:
    	if (val && strlen(val)>0) {
			int err;
			options.mime_regexps = realloc(options.mime_regexps,sizeof(regex_t)*++options.mime_regexp_cnt);
			if ((err=regcomp(options.mime_regexps+(options.mime_regexp_cnt-1), val+1,0))) {
				log_error("Error compiling regex: %s",get_regerror(err,options.mime_regexps+(options.mime_regexp_cnt-1)));
				return 1;
			}
			log_debug("mime-re: %s",val+1);
			return 0;
    	}
	 case FUSE_OPT_KEY_NONOPT:
		 // base dir can be supplied as first argument (allows fstab config)
		 if (!options.base_dir) {
			 options.base_dir = canonicalize_file_name(arg);
			 return 0;
		 }
		 else if ( !options.mount_dir ) {
			 options.mount_dir = canonicalize_file_name(arg);
			 // drop thru as fuse picks this up
		 }
     }

     return 1;
}

void dump_options() {
	log_debug("base_dir: %s",options.base_dir);
	log_debug("mount_dir: %s",options.mount_dir);
	log_debug("cache_dir: %s",options.cache_dir);
	log_debug("cache_size: %lu",options.cache_size);
	log_debug("cache_expiry: %ld",options.cache_expiry);
	log_debug("monitor: %d",options.monitor);
	log_debug("link_thru: %d",options.link_thru);
	log_debug("hide_empty_dirs: %d ",options.hide_empty_dirs);
	log_debug("stat_pass_thru: %d",options.stat_pass_thru);
	log_debug("command: %s\n",options.command);
	for ( int i = 0; i < options.fnmatch_c; i++)
		log_debug("extension: %s\n",options.fnmatch[i]);

}

int main(int argc, char **argv) {

	log_debug("starting session");
	int ret =0;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	if (fuse_opt_parse(&args, &options, cmdfs_opts, cmdfs_opt_proc) == -1) {
		/** error parsing options */
		goto exit;
	}

	// default, token substitute and canonicalize cachedir
	if ( !options.cache_dir && asprintf((char **)&options.cache_dir,"%s/%%u/%%b",CACHE_ROOT)<0) {
		log_error("allocate cache directory string");
		goto exit;
	}
	const char *tokens[] = {"%u","%b","%m",NULL };
	const char *values[] = { getlogin(),options.base_dir,options.mount_dir,NULL };
	const char *toksub = tokens_substitute(options.cache_dir,tokens,values);
	free((void*)options.cache_dir);
	if (!(options.cache_dir = makepath(toksub)) ) {
		log_error("Could not create/find cache directory %s (%s)", toksub, strerror(errno));
		goto exit;
	}
	free((void *)toksub);

	if ( options.command == NULL ) {
		options.command = strdup("dd"); // default just copy original file
	}

	dump_options();

	ret = fuse_main(args.argc, args.argv, &cmdfs_operations, NULL);
exit:
	fuse_opt_free_args(&args);
	return ret;
}
