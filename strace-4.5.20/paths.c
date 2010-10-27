// mini-library for manipulating file paths on UNIX-like systems
// by Philip Guo

#include "paths.h"

extern char CDE_exec_mode;

// This forces gcc to use an older version of realpath from glibc 2.0,
// to maximize backwards compatibility
// See: http://www.trevorpounds.com/blog/?p=103
__asm__(".symver realpath,realpath@GLIBC_2.0");

#include <stdarg.h>
extern char* format(const char *format, ...);

static struct path* new_path(char is_abspath);

// note that realpath_strdup and realpath_nofollow seem to do funny
// things to arguments that are directories (okay for regular files,
// though)

// calls realpath and mallocs a new result string
// Pre-req: filename must actually exist on the filesystem!
//
// mallocs a new string
char* realpath_strdup(char* filename) {
  assert(!CDE_exec_mode);
  
  char path[PATH_MAX];
  path[0] = '\0';
  char* ret = realpath(filename, path);
  assert(ret); // the target path must actually exist!

  assert(path[0] == '/'); // must be an absolute path
  return strdup(path);
}

// mallocs a new string
char* readlink_strdup(char* filename) {
  assert(!CDE_exec_mode);

  char path[PATH_MAX];
  path[0] = '\0';
  int len = readlink(filename, path, sizeof path);
  assert(path[0] != '\0');

  assert(len >= 0);
  path[len] = '\0'; // wow, readlink doesn't put the cap on the end!
  return strdup(path);
}


// representing and manipulating path components (courtesy of Goanna)

static void empty_path(struct path *path) {
  int pos = 0;
  path->depth = 0;
  if (path->stack) {
    while (path->stack[pos]) {
      free(path->stack[pos]);
      path->stack[pos] = NULL;
      pos++;
    }
  }
}


// pop the last element of path
void path_pop(struct path* p) {
  if (p->depth == 0) {
    return;
  }

  free(p->stack[p->depth-1]);
  p->stack[p->depth-1] = NULL;
  p->depth--;
}

static struct path* new_path_internal(char* path, char is_abspath) {
  int stackleft;

  path = strdup(path); // so that we don't clobber the original
  char* path_dup_base = path; // for free()

  struct path* base = new_path(is_abspath);

  if (is_abspath) {
    empty_path(base);
    path++;
  }

  stackleft = base->stacksize - base->depth - 1;

  do {
    char *p;
    while (stackleft <= 1) {
      base->stacksize *= 2;
      stackleft = base->stacksize / 2;
      base->stacksize++;
      stackleft++;
      base->stack =
        (struct namecomp **)realloc(base->stack, base->stacksize * sizeof(struct namecomp*));
      assert(base->stack);
    }

    // Skip multiple adjoining slashes
    while (*path == '/') {
      path++;
    }

    p = strchr(path, '/');
    // put a temporary stop-gap ... uhhh, this assumes path isn't read-only
    if (p) {
      *p = '\0';
    }

    if (path[0] == '\0') {
      base->stack[base->depth] = NULL;
      // We are at the end (or root), do nothing.
    }
    else if (!strcmp(path, ".")) {
      base->stack[base->depth] = NULL;
      // This doesn't change anything.
    }
    else if (!strcmp(path, "..")) {
      if (base->depth > 0) {
        free(base->stack[--base->depth]);
        base->stack[base->depth] = NULL;
        stackleft++;
      }
    }
    else {
      base->stack[base->depth] =
        (struct namecomp *)malloc(sizeof(struct namecomp) + strlen(path) + 1);
      assert(base->stack[base->depth]);
      strcpy(base->stack[base->depth]->str, path);
      base->stack[base->depth]->len = strlen(path);
      base->depth++;
      base->stack[base->depth] = NULL;
      stackleft--;
    }

    // Put it back the way it was
    if (p) {
      *p++ = '/';
    }
    path = p;
  } while (path);

  free(path_dup_base);
  return base;
}


// creates a canonicalized path object by removing all instances of '.'
// and '..' from an absolute path
//
// mallocs a new path object, must free using delete_path(),
// NOT using ordinary free()
struct path* new_path_from_abspath(char* path) {
  assert(IS_ABSPATH(path));
  return new_path_internal(path, 1);
}

// creates a path object from a relative path, resolving it relative to
// base, which must be an absolute path
//
// mallocs a new path object, must free using delete_path(),
// NOT using ordinary free()
struct path* new_path_from_relpath(char* relpath, char* base) {
  assert(!IS_ABSPATH(relpath));
  assert(IS_ABSPATH(base));

  char* tmp = format("%s/%s", base, relpath);
  struct path* ret = new_path_from_abspath(tmp);

  free(tmp);
  return ret;
}

// canonicalizes an absolute path, mallocs a new string
char* canonicalize_abspath(char* abspath) {
  struct path* p = new_path_from_abspath(abspath);
  char* ret = path2str(p, 0);
  delete_path(p);
  return ret;
}

// canonicalizes a relative path with respect to base, mallocs a new string
char* canonicalize_relpath(char* relpath, char* base) {
  struct path* p = new_path_from_relpath(relpath, base);
  char* ret = path2str(p, 0);
  delete_path(p);
  return ret;
}

char* canonicalize_path(char* path, char* relpath_base) {
  if (IS_ABSPATH(path)) {
    return canonicalize_abspath(path);
  }
  else {
    return canonicalize_relpath(path, relpath_base);
  }
}


static struct path* new_path(char is_abspath) {
  struct path* ret = (struct path *)malloc(sizeof(struct path));
  assert(ret);

  ret->stacksize = 1;
  ret->is_abspath = is_abspath;
  ret->depth = 0;
  ret->stack = (struct namecomp **)malloc(sizeof(struct namecomp *));
  assert(ret->stack);
  ret->stack[0] = NULL;
  return ret;
}

void delete_path(struct path *path) {
  assert(path);
  if (path->stack) {
    int pos = 0;
    while (path->stack[pos]) {
      free(path->stack[pos]);
      path->stack[pos] = NULL;
      pos++;
    }
    free(path->stack);
  }
  free(path);
}


// mallocs a new string and populates it with up to
// 'depth' path components (if depth is 0, uses entire path)
char* path2str(struct path* path, int depth) {
  int i;
  int destlen = 2; // at least have room for '/' and null terminator ('\0')

  // simply use path->depth if depth is out of range
  if (depth <= 0 || depth > path->depth) {
    depth = path->depth;
  }

  for (i = 0; i < depth; i++) {
    destlen += path->stack[i]->len + 1;
  }

  char* dest = (char *)malloc(destlen);

  char* ret = dest;

  // print a leading '/' for absolute paths
  if (path->is_abspath) {
    *dest++ = '/';
    destlen--;
  }

  for (i = 0; i < depth; i++) {
    assert(destlen >= path->stack[i]->len + 1);

    memcpy(dest, path->stack[i]->str, path->stack[i]->len);
    dest += path->stack[i]->len;
    destlen -= path->stack[i]->len;

    if (i < depth - 1) { // do we have a successor?
      assert(destlen >= 2);
      *dest++ = '/';
      destlen--;
    }
  }

  *dest = '\0';

  return ret;
}


// emulate "mkdir -p" functionality
// if pop_one is non-zero, then pop last element
// before doing "mkdir -p"
void mkdir_recursive(char* fullpath, int pop_one) {
  // use a sneaky new_path_internal call so that we can accept relative
  // paths in fullpath
  struct path* p = new_path_internal(fullpath, IS_ABSPATH(fullpath));

  if (pop_one) {
    path_pop(p); // e.g., ignore filename portion to leave just the dirname
  }

  int i;
  for (i = 1; i <= p->depth; i++) {
    char* dn = path2str(p, i);
    mkdir(dn, 0777);
    free(dn);
  }
  delete_path(p);
}

#ifdef PGBOVINE_DEPRECATED
// gets the absolute path of filename, WITHOUT following any symlinks
// (for relative paths, calculate their locations relative to
//  relative_path_basedir)
//
// however, note that if any parent directory specified in filename is a
// symlink, it WILL follow the symlink.  it just doesn't follow symlinks
// specified by the (basename of) file
//
// mallocs a new string
char* realpath_nofollow_DEPRECATED(char* filename, char* relative_path_basedir) {
  assert(!CDE_exec_mode);
  assert(IS_ABSPATH(relative_path_basedir));

  char* ret = NULL;
  if (IS_ABSPATH(filename)) {
    char* bn = basename(filename); // doesn't destroy its arg

    char* filename_copy = strdup(filename); // dirname() destroys its arg
    char* dir = dirname(filename_copy);

    char* dir_realpath = realpath_strdup(dir);
    ret = format("%s/%s", dir_realpath, bn);
    free(dir_realpath);
    free(filename_copy);
  }
  else {
    // for relative links, find them with respect to relative_path_basedir
    char* tmp = format("%s/%s", relative_path_basedir, filename);
    char* bn = basename(tmp); // doesn't destroy its arg

    char* tmp_copy = strdup(tmp); // dirname() destroys its arg
    char* dir = dirname(tmp_copy);

    char* dir_realpath = realpath_strdup(dir);
    ret = format("%s/%s", dir_realpath, bn);

    free(dir_realpath);
    free(tmp_copy);
    free(tmp);
  }

  assert(ret);
  return ret;
}
#endif // PGBOVINE_DEPRECATED


// return 1 iff the absolute path of filename is within target_dir
// (for relative paths, calculate their locations relative to
//  relative_path_basedir)
//
// WARNING: this does WEIRD things if filename isn't a file but rather
// it's a directory.  e.g., "/home/pgbovine" is NOT within "/home/pgbovine"
// since, technically a file named 'pgbovine' is NOT within "/home/pgbovine"
//
// in short, ONLY PASS IN FILES as filename, NOT directories
//
// Pre-req: filename must actually exist on the filesystem!
int file_is_within_dir(char* filename, char* target_dir, char* relative_path_basedir) {
  char* cano_filename = canonicalize_path(filename, relative_path_basedir);
  int cano_filename_len = strlen(cano_filename);

  char* fake_cano_dir = canonicalize_abspath(target_dir);
  // very subtle --- if fake_cano_dir isn't simply '/' (root directory),
  // tack on a '/' to the end of fake_cano_dir, so that we
  // don't get misled by substring comparisons.  canonicalize_abspath
  // does NOT put on a trailing '/' for directories.
  //
  // e.g., "/home/pgbovine/hello.txt" is NOT within the
  // "/home/pgbovine/hello" directory, so we need to tack on an extra
  // '/' to the end of cano_dir to make it "/home/pgbovine/hello/" in
  // order to avoid a false match
  int fake_cano_dir_len = strlen(fake_cano_dir);

  char* cano_dir = NULL;
  if (fake_cano_dir_len > 1) {
    cano_dir = (char*)malloc(fake_cano_dir_len + 2);
    strcpy(cano_dir, fake_cano_dir);
    cano_dir[fake_cano_dir_len] = '/';
    cano_dir[fake_cano_dir_len + 1] = '\0';
  }
  else {
    cano_dir = strdup(fake_cano_dir);
  }
  assert(cano_dir);

  int cano_dir_len = strlen(cano_dir);

  // now that they are canonicalized, we can do a simple substring comparison:
  char is_within_pwd = 0;
  if ((cano_dir_len <= cano_filename_len) &&
      (strncmp(cano_dir, cano_filename, cano_dir_len) == 0)) {
    is_within_pwd = 1;
  }

  free(fake_cano_dir);
  free(cano_dir);
  free(cano_filename);

  return is_within_pwd;
}

#ifdef PGBOVINE_DEPRECATED
// return 1 iff the absolute path of filename is within target_dir
// (for relative paths, calculate their locations relative to
//  relative_path_basedir)
//
// Pre-req: filename must actually exist on the filesystem!
int file_is_within_dir_OLD(char* filename, char* target_dir, char* relative_path_basedir) {
  assert(!CDE_exec_mode);
  assert(IS_ABSPATH(relative_path_basedir));

  char* path_to_check = NULL;
  if (IS_ABSPATH(filename)) {
    path_to_check = strdup(filename);
  }
  else {
    // note that the target program might have done a chdir, so we need to handle that ;)
    path_to_check = format("%s/%s", relative_path_basedir, filename);
  }
  assert(path_to_check);

  // just do a substring comparison against target_dir
  char* path_to_check_copy = strdup(path_to_check);
  char* dn = dirname(path_to_check_copy);

  char* dn_realpath = realpath_strdup(dn);
  int dn_len = strlen(dn_realpath);

  char* targetdir_realpath = realpath_strdup(target_dir);
  int targetdir_len = strlen(targetdir_realpath);

  // special case hack - if dn_realpath ends with '/.', then take its dirname
  // AGAIN to get rid of this annoyance :)
  while ((dn_len >= 2) &&
          dn_realpath[dn_len - 2] == '/' &&
          dn_realpath[dn_len - 1] == '.') {
    dn_realpath = dirname(dn_realpath);
    dn_len = strlen(dn_realpath);
  }

  char is_within_pwd = 0;
  if ((targetdir_len <= dn_len) && strncmp(dn_realpath, targetdir_realpath, targetdir_len) == 0) {
    is_within_pwd = 1;
  }

  free(path_to_check);
  free(path_to_check_copy);
  free(dn_realpath);
  free(targetdir_realpath);

  return is_within_pwd;
}
#endif // PGBOVINE_DEPRECATED


// useful utility function from ccache codebase
// http://ccache.samba.org/
/* Construct a string according to a format. Caller frees. */
char* format(const char *format, ...) {
  va_list ap;
  char *ptr = NULL;

  va_start(ap, format);
  vasprintf(&ptr, format, ap);
  va_end(ap);

  assert(*ptr);
  return ptr;
}

