/* Copyright (C) 2008-2017 D. V. Wiebe
 *
 ***************************************************************************
 *
 * This file is part of the GetData project.
 *
 * GetData is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * GetData is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with GetData; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "internal.h"

#ifdef USE_MODULES
#ifdef USE_PTHREAD
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif
static pthread_mutex_t gd_mutex_ = PTHREAD_MUTEX_INITIALIZER;
#endif

#ifdef HAVE_LTDL_H
#include <ltdl.h>
#endif

static int framework_initialised = 0;
#endif

/* encoding schemas */
#define GD_EF_NULL_SET NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, \
  NULL, NULL
#define GD_EF_GENERIC_SET &_GD_GenericName, NULL, NULL, NULL, NULL, NULL, \
  NULL, NULL, &_GD_GenericMove, &_GD_GenericUnlink, NULL
#define GD_EF_GENERICNOP_SET &_GD_GenericName, NULL, NULL, NULL, NULL, NULL, \
  NULL, &_GD_NopSync, &_GD_GenericMove, &_GD_GenericUnlink, NULL

#ifdef USE_MODULES
#define GD_EXT_ENCODING_NULL(sc,ex,ec,af,ff) \
{ sc,ex,ec,af,ff,GD_EF_PROVIDES,GD_EF_NULL_SET }
#define GD_EXT_ENCODING_GEN(sc,ex,ec,af,ff) \
{ sc,ex,ec,af,ff,GD_EF_PROVIDES,GD_EF_GENERIC_SET }
#define GD_EXT_ENCODING_GENOP(sc,ex,ec,af,ff) \
{ sc,ex,ec,af,ff,GD_EF_PROVIDES,GD_EF_GENERICNOP_SET }
#else
#define GD_EXT_ENCODING(sc,ex,ec,af,ff) { sc,ex,ec,af,ff,0,GD_INT_FUNCS }
#define GD_EXT_ENCODING_NULL GD_EXT_ENCODING
#define GD_EXT_ENCODING_GEN GD_EXT_ENCODING
#define GD_EXT_ENCODING_GENOP GD_EXT_ENCODING
#endif
struct encoding_t _GD_ef[GD_N_SUBENCODINGS] = {
  { GD_UNENCODED, "", GD_EF_ECOR, NULL, "none", 0,
    &_GD_GenericName, &_GD_RawOpen, &_GD_RawClose, &_GD_RawSeek, &_GD_RawRead,
    &_GD_RawSize, &_GD_RawWrite, &_GD_RawSync, &_GD_GenericMove,
    &_GD_GenericUnlink, NULL /* strerr */
  },

#ifdef USE_GZIP
#define GD_EF_PROVIDES \
  GD_EF_OPEN | GD_EF_CLOSE | GD_EF_SEEK | GD_EF_READ | GD_EF_SIZE | \
  GD_EF_WRITE | GD_EF_STRERR
#define GD_INT_FUNCS \
  &_GD_GenericName, &_GD_GzipOpen, &_GD_GzipClose, &_GD_GzipSeek, \
  &_GD_GzipRead, &_GD_GzipSize, &_GD_GzipWrite, &_GD_NopSync, \
  &_GD_GenericMove, &_GD_GenericUnlink, &_GD_GzipStrerr
#else
#define GD_EF_PROVIDES 0
#define GD_INT_FUNCS GD_EF_GENERICNOP_SET
#endif
  GD_EXT_ENCODING_GENOP(GD_GZIP_ENCODED, ".gz", GD_EF_ECOR | GD_EF_OOP, "Gzip",
      "gzip"),
#undef GD_INT_FUNCS
#undef GD_EF_PROVIDES


#ifdef USE_BZIP2
#define GD_EF_PROVIDES \
  GD_EF_OPEN | GD_EF_CLOSE | GD_EF_SEEK | GD_EF_READ | GD_EF_SIZE | \
  GD_EF_WRITE | GD_EF_STRERR
#define GD_INT_FUNCS \
  &_GD_GenericName, &_GD_Bzip2Open, &_GD_Bzip2Close, &_GD_Bzip2Seek, \
  &_GD_Bzip2Read, &_GD_Bzip2Size, &_GD_Bzip2Write, &_GD_NopSync, \
  &_GD_GenericMove, &_GD_GenericUnlink, &_GD_Bzip2Strerr
#else
#define GD_INT_FUNCS GD_EF_GENERICNOP_SET
#define GD_EF_PROVIDES 0
#endif
  GD_EXT_ENCODING_GENOP(GD_BZIP2_ENCODED, ".bz2", GD_EF_ECOR | GD_EF_OOP,
      "Bzip2", "bzip2"),
#undef GD_INT_FUNCS
#undef GD_EF_PROVIDES


#ifdef USE_SLIM
#define GD_EF_PROVIDES \
  GD_EF_OPEN | GD_EF_CLOSE | GD_EF_SEEK | GD_EF_READ | GD_EF_SIZE | GD_EF_STRERR
#define GD_INT_FUNCS \
  &_GD_GenericName, &_GD_SlimOpen, &_GD_SlimClose, &_GD_SlimSeek, \
  &_GD_SlimRead, &_GD_SlimSize, NULL /* WRITE */, &_GD_NopSync, \
  &_GD_GenericMove, &_GD_GenericUnlink, &_GD_SlimStrerr
#else
#define GD_INT_FUNCS GD_EF_GENERICNOP_SET
#define GD_EF_PROVIDES 0
#endif
  GD_EXT_ENCODING_GENOP(GD_SLIM_ENCODED, ".slm", GD_EF_ECOR, "Slim", "slim"),
#undef GD_INT_FUNCS
#undef GD_EF_PROVIDES


/* We only provide write support for .xz files, not .lzma */
#ifdef USE_LZMA
#define GD_EF_PROVIDES \
  GD_EF_OPEN | GD_EF_CLOSE | GD_EF_SEEK | GD_EF_READ | GD_EF_SIZE | \
  GD_EF_WRITE | GD_EF_SYNC | GD_EF_STRERR
#define GD_INT_FUNCS \
  &_GD_GenericName, &_GD_LzmaOpen, &_GD_LzmaClose, &_GD_LzmaSeek, \
  &_GD_LzmaRead, &_GD_LzmaSize, &_GD_LzmaWrite, &_GD_LzmaSync, \
  &_GD_GenericMove, &_GD_GenericUnlink, &_GD_LzmaStrerr
#else
#define GD_INT_FUNCS GD_EF_GENERIC_SET
#define GD_EF_PROVIDES 0
#endif
  GD_EXT_ENCODING_GEN(GD_LZMA_ENCODED, ".xz", GD_EF_ECOR | GD_EF_OOP, "Lzma",
      "lzma"),
#undef GD_INT_FUNCS
#undef GD_EF_PROVIDES

#ifdef USE_LZMA
#define GD_EF_PROVIDES \
  GD_EF_OPEN | GD_EF_CLOSE | GD_EF_SEEK | GD_EF_READ | GD_EF_SIZE | GD_EF_STRERR
#define GD_INT_FUNCS \
  &_GD_GenericName, &_GD_LzmaOpen, &_GD_LzmaClose, &_GD_LzmaSeek, \
  &_GD_LzmaRead, &_GD_LzmaSize, NULL /* WRITE */, NULL /* SYNC */, \
  &_GD_GenericMove, &_GD_GenericUnlink, &_GD_LzmaStrerr
#else
#define GD_INT_FUNCS GD_EF_GENERIC_SET
#define GD_EF_PROVIDES 0
#endif
  GD_EXT_ENCODING_GEN(GD_LZMA_ENCODED, ".lzma", GD_EF_ECOR, "Lzma", "lzma"),
#undef GD_INT_FUNCS
#undef GD_EF_PROVIDES


  { GD_TEXT_ENCODED, ".txt", 0, NULL, "text", 0,
    &_GD_GenericName, &_GD_AsciiOpen, &_GD_AsciiClose, &_GD_AsciiSeek,
    &_GD_AsciiRead, &_GD_AsciiSize, &_GD_AsciiWrite, &_GD_AsciiSync,
    &_GD_GenericMove, &_GD_GenericUnlink, NULL /* strerr */
  },

  { GD_SIE_ENCODED, ".sie", GD_EF_ECOR | GD_EF_SWAP, NULL, "sie", 0,
    &_GD_GenericName, &_GD_SampIndOpen, &_GD_SampIndClose, &_GD_SampIndSeek,
    &_GD_SampIndRead, &_GD_SampIndSize, &_GD_SampIndWrite, &_GD_SampIndSync,
    &_GD_GenericMove, &_GD_GenericUnlink, NULL /* strerr */
  },


#ifdef USE_ZZIP
#define GD_EF_PROVIDES \
  GD_EF_NAME | GD_EF_OPEN | GD_EF_CLOSE | GD_EF_SEEK | GD_EF_READ | \
  GD_EF_SIZE | GD_EF_STRERR
#define GD_INT_FUNCS \
  &_GD_ZzipName, &_GD_ZzipOpen, &_GD_ZzipClose, &_GD_ZzipSeek, &_GD_ZzipRead, \
  &_GD_ZzipSize, NULL /* WRITE */, NULL /* SYNC */, NULL /* MOVE */, \
  NULL /* UNLINK */, &_GD_ZzipStrerr
#else
#define GD_INT_FUNCS GD_EF_NULL_SET
#define GD_EF_PROVIDES 0
#endif
  GD_EXT_ENCODING_NULL(GD_ZZIP_ENCODED, NULL, GD_EF_ECOR | GD_EF_EDAT, "Zzip",
      "zzip"),
#undef GD_INT_FUNCS
#undef GD_EF_PROVIDES


#ifdef USE_ZZSLIM
#define GD_EF_PROVIDES \
  GD_EF_NAME | GD_EF_OPEN | GD_EF_CLOSE | GD_EF_SEEK | GD_EF_READ | \
  GD_EF_SIZE | GD_EF_STRERR
#define GD_INT_FUNCS \
  &_GD_ZzslimName, &_GD_ZzslimOpen, &_GD_ZzslimClose, &_GD_ZzslimSeek, \
  &_GD_ZzslimRead, &_GD_ZzslimSize, NULL /* WRITE */, NULL /* SYNC */, \
  NULL /* MOVE */, NULL /* UNLINK */, &_GD_ZzslimStrerr
#else
#define GD_INT_FUNCS GD_EF_NULL_SET
#define GD_EF_PROVIDES 0
#endif
  GD_EXT_ENCODING_NULL(GD_ZZSLIM_ENCODED, NULL, GD_EF_ECOR | GD_EF_EDAT,
      "Zzslim", "zzslim"),
#undef GD_INT_FUNCS
#undef GD_EF_PROVIDES


#ifdef USE_FLAC
#define GD_EF_PROVIDES \
  GD_EF_OPEN | GD_EF_CLOSE | GD_EF_SEEK | GD_EF_READ | GD_EF_SIZE | \
  GD_EF_WRITE | GD_EF_STRERR
#define GD_INT_FUNCS \
  &_GD_GenericName, &_GD_FlacOpen, &_GD_FlacClose, &_GD_FlacSeek, \
  &_GD_FlacRead, &_GD_FlacSize, &_GD_FlacWrite, &_GD_NopSync, \
  &_GD_GenericMove, &_GD_GenericUnlink, &_GD_FlacStrerr
#else
#define GD_INT_FUNCS GD_EF_GENERICNOP_SET
#define GD_EF_PROVIDES 0
#endif
  GD_EXT_ENCODING_GENOP(GD_FLAC_ENCODED, ".flac", GD_EF_SWAP | GD_EF_OOP,
      "Flac", "flac"),
#undef GD_INT_FUNCS
#undef GD_EF_PROVIDES


#ifdef USE_ZSTD
#define GD_EF_PROVIDES \
  GD_EF_OPEN | GD_EF_CLOSE | GD_EF_SEEK | GD_EF_READ | GD_EF_SIZE | \
  GD_EF_WRITE | GD_EF_SYNC | GD_EF_STRERR
#define GD_INT_FUNCS \
  &_GD_ZstdName, &_GD_ZstdOpen, &_GD_ZstdClose, &_GD_ZstdSeek, \
  &_GD_ZstdRead, &_GD_ZstdSize, &_GD_ZstdWrite, &_GD_ZstdSync, \
  &_GD_GenericMove, &_GD_GenericUnlink, &_GD_ZstdStrerr
#else
#define GD_INT_FUNCS GD_EF_GENERIC_SET
#define GD_EF_PROVIDES 0
#endif
  GD_EXT_ENCODING_GEN(GD_ZSTD_ENCODED, ".zst", GD_EF_ECOR | GD_EF_EDAT,
      "Zstd", "zstd"),
#undef GD_INT_FUNCS
#undef GD_EF_PROVIDES


  { GD_ENC_UNSUPPORTED, NULL, 0, "", "", 0,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
  }
};

void _GD_InitialiseFramework(void)
{
  dtracevoid();

#ifdef USE_MODULES
#ifdef USE_PTHREAD
  if (!framework_initialised) {
    pthread_mutex_lock(&gd_mutex_);
#endif
    /* check again */
    if (!framework_initialised) {
      framework_initialised = 1;
      lt_dlinit();
    }
#ifdef USE_PTHREAD
    pthread_mutex_unlock(&gd_mutex_);
  }
#endif
#endif
  dreturnvoid();
}

#define _GD_EncodingUnderstood(encoding) \
  ((encoding == GD_UNENCODED || encoding == GD_SLIM_ENCODED || \
    encoding == GD_GZIP_ENCODED || encoding == GD_BZIP2_ENCODED || \
    encoding == GD_TEXT_ENCODED || encoding == GD_LZMA_ENCODED || \
    encoding == GD_SIE_ENCODED || encoding == GD_ZZIP_ENCODED || \
    encoding == GD_ZZSLIM_ENCODED || encoding == GD_FLAC_ENCODED || \
    encoding == GD_ZSTD_ENCODED))

#ifdef USE_MODULES
static void *_GD_ResolveSymbol(lt_dlhandle lib, struct encoding_t *restrict enc,
    const char *restrict name)
{
  void* func;
  char symbol[100];

  dtrace("%p, %p, \"%s\"", lib, enc, name);
  /* create the symbol name */
  sprintf(symbol, "lt_libgetdata%s_LTX_GD_%s%s", enc->affix, enc->affix, name);
  symbol[13] -= 'A' - 'a';
  func = lt_dlsym(lib, symbol);

  dreturn("%p", func);
  return func;
}
#endif

#define GETDATA_MODULEPREFIX GETDATA_MODULEDIR "/libgetdata"
int _GD_MissingFramework(int encoding, unsigned int funcs)
{
  int ret;

  dtrace("%i, 0x%X", encoding, funcs);

#ifdef USE_MODULES
#ifdef USE_PTHREAD
  pthread_mutex_lock(&gd_mutex_);
#endif

  /* set up the encoding library if required */
  if (_GD_ef[encoding].provides) {
    char *library;
    lt_dlhandle lib;

    /* make the library name */
    library = malloc(sizeof(GETDATA_MODULEDIR) +
        strlen(_GD_ef[encoding].affix) + sizeof(GD_GETDATA_VERSION) + 13);
    if (!library) {
      _GD_ef[encoding].provides = 0;
#ifdef USE_PTHREAD
      pthread_mutex_unlock(&gd_mutex_);
#endif
      dreturn("%i", 1);
      return 1;
    }

    sprintf(library, GETDATA_MODULEPREFIX "%s-" GD_GETDATA_VERSION,
        _GD_ef[encoding].affix);

    /* affix starts with a capital letter, we need to lowercasify it --
     * also, sizeof includes the trailing NUL in its count */
    library[sizeof(GETDATA_MODULEPREFIX) - 1] -= 'A' - 'a';

    /* open */
    if ((lib = lt_dlopenext(library)) == NULL) {
      /* if that didn't work, look for it in the search path */
      if ((lib = lt_dlopenext(library + sizeof(GETDATA_MODULEDIR))) == NULL)
      {
        free(library);
        _GD_ef[encoding].provides = 0;
#ifdef USE_PTHREAD
        pthread_mutex_unlock(&gd_mutex_);
#endif
        dreturn("%i", 1);
        return 1;
      }
    }
    free(library);

    /* Try to resolve the symbols */
    if (_GD_ef[encoding].provides & GD_EF_NAME)
      _GD_ef[encoding].name = (gd_ef_name_t)_GD_ResolveSymbol(lib,
          _GD_ef + encoding, "Name");
    if (_GD_ef[encoding].provides & GD_EF_OPEN)
      _GD_ef[encoding].open = (gd_ef_open_t)_GD_ResolveSymbol(lib,
          _GD_ef + encoding, "Open");
    if (_GD_ef[encoding].provides & GD_EF_CLOSE)
      _GD_ef[encoding].close = (gd_ef_close_t)_GD_ResolveSymbol(lib,
          _GD_ef + encoding, "Close");
    if (_GD_ef[encoding].provides & GD_EF_SEEK)
      _GD_ef[encoding].seek = (gd_ef_seek_t)_GD_ResolveSymbol(lib,
          _GD_ef + encoding, "Seek");
    if (_GD_ef[encoding].provides & GD_EF_READ)
      _GD_ef[encoding].read = (gd_ef_read_t)_GD_ResolveSymbol(lib,
          _GD_ef + encoding, "Read");
    if (_GD_ef[encoding].provides & GD_EF_SIZE)
      _GD_ef[encoding].size = (gd_ef_size_t)_GD_ResolveSymbol(lib,
          _GD_ef + encoding, "Size");
    if (_GD_ef[encoding].provides & GD_EF_WRITE)
      _GD_ef[encoding].write = (gd_ef_write_t)_GD_ResolveSymbol(lib,
          _GD_ef + encoding, "Write");
    if (_GD_ef[encoding].provides & GD_EF_SYNC)
      _GD_ef[encoding].sync = (gd_ef_sync_t)_GD_ResolveSymbol(lib,
          _GD_ef + encoding, "Sync");
    if (_GD_ef[encoding].provides & GD_EF_UNLINK)
      _GD_ef[encoding].unlink = (gd_ef_unlink_t)_GD_ResolveSymbol(lib,
          _GD_ef + encoding, "Unlink");

    /* we tried our best, don't bother trying again */
    _GD_ef[encoding].provides = 0;
  }
#ifdef USE_PTHREAD
  pthread_mutex_unlock(&gd_mutex_);
#endif
#endif

  ret =
    (funcs & GD_EF_NAME    && _GD_ef[encoding].name    == NULL) ||
    (funcs & GD_EF_OPEN    && _GD_ef[encoding].open    == NULL) ||
    (funcs & GD_EF_CLOSE   && _GD_ef[encoding].close   == NULL) ||
    (funcs & GD_EF_SEEK    && _GD_ef[encoding].seek    == NULL) ||
    (funcs & GD_EF_READ    && _GD_ef[encoding].read    == NULL) ||
    (funcs & GD_EF_SIZE    && _GD_ef[encoding].size    == NULL) ||
    (funcs & GD_EF_WRITE   && _GD_ef[encoding].write   == NULL) ||
    (funcs & GD_EF_SYNC    && _GD_ef[encoding].sync    == NULL) ||
    (funcs & GD_EF_UNLINK  && _GD_ef[encoding].unlink  == NULL) ||
    (funcs & GD_EF_STRERR  && _GD_ef[encoding].strerr  == NULL);

  dreturn("%i", ret);
  return ret;
}

static int _GD_MoveOver(DIRFILE *restrict D, int dirfd,
    struct gd_raw_file_ *restrict file)
{
#ifdef HAVE_FCHMOD
  int fd;
  struct stat stat_buf;
  mode_t mode, tmode;
#endif
  dtrace("%p, %i, %p", D, dirfd, file);

#ifdef HAVE_FCHMOD
  if (gd_StatAt(D, dirfd, file[1].name, &stat_buf, 0))
    tmode = 0644;
  else
    tmode = stat_buf.st_mode;

  if (gd_StatAt(D, dirfd, file[0].name, &stat_buf, 0))
    mode = tmode;
  else
    mode = stat_buf.st_mode;
#endif

  if (gd_RenameAt(D, dirfd, file[1].name, dirfd, file[0].name)) {
    int move_errno = errno;
    if (gd_UnlinkAt(D, dirfd, file[1].name, 0) == 0) {
      free(file[1].name);
      file[1].name = NULL;
    }
    errno = move_errno;
    _GD_SetError(D, GD_E_UNCLEAN_DB, GD_E_UNCLEAN_CALL, NULL, 0, "gd_RenameAt");
    D->flags |= GD_INVALID;
    dreturn("%i", -1);
    return -1;
  }

#ifdef HAVE_FCHMOD
  if (tmode != mode) {
    fd = gd_OpenAt(file->D, dirfd, file[0].name, O_RDONLY, 0666);
    fchmod(fd, mode);
    close(fd);
  }
#endif

  dreturn("%i", 0);
  return 0;
}

/* Close a raw file, taking care of cleaning-up out-of-place writes, and
 * discarding temporary files.  When we're moving an entry to a new fragment,
 * fragment != E->fragment_index */
int _GD_FiniRawIO(DIRFILE *D, const gd_entry_t *E, int fragment, int flags)
{
  const struct gd_fragment_t *frag = &D->fragment[fragment];
  const int clotemp = (flags & GD_FINIRAW_CLOTEMP) ? 1 : 0;
  const int old_mode = E->e->u.raw.file[0].mode;
  const int oop_write = ((_GD_ef[E->e->u.raw.file[0].subenc].flags & GD_EF_OOP)
      && (old_mode & GD_FILE_WRITE)) ? 1 : 0;
  dtrace("%p, %p, %i, 0x%X", D, E, fragment, flags);

  if ((E->e->u.raw.file[clotemp].idata >= 0) ||
      (clotemp == 0 && oop_write && (E->e->u.raw.file[1].idata >= 0)))
  {
    /* close the secondary file in write mode (but not temp mode) */
    if (oop_write && E->e->u.raw.file[1].idata >= 0) {
      if (E->e->u.raw.file[0].idata >= 0) {
        /* copy the rest of the input to the output */
        char *buffer;
        int n_read, n_wrote, n_to_write;

        buffer = _GD_Malloc(D, GD_BUFFER_SIZE);
        if (buffer == NULL) {
          dreturn("%i", -1);
          return -1;
        }

        do {
          n_to_write = n_read = (*_GD_ef[E->e->u.raw.file[0].subenc].read)(
              E->e->u.raw.file, buffer, E->EN(raw,data_type),
              GD_BUFFER_SIZE / GD_SIZE(E->EN(raw,data_type)));
          if (n_read < 0) {
            free(buffer);
            _GD_SetEncIOError(D, GD_E_IO_READ, E->e->u.raw.file + 0);
            dreturn("%i", -1);
            return -1;
          } else while (n_to_write > 0) {
            n_wrote = (*_GD_ef[E->e->u.raw.file[0].subenc].write)(
                E->e->u.raw.file + 1, buffer, E->EN(raw,data_type), n_to_write);
            if (n_wrote < 0) {
              free(buffer);
              _GD_SetEncIOError(D, GD_E_IO_WRITE, E->e->u.raw.file + 0);
              dreturn("%i", -1);
              return -1;
            }
            n_to_write -= n_wrote;
          }
        } while (n_read > 0);

        free(buffer);
      }

      if ((*_GD_ef[E->e->u.raw.file[0].subenc].close)(E->e->u.raw.file + 1)) {
        dreturn("%i", -1);
        return -1;
      }
    }

    /* close the file */
    if ((E->e->u.raw.file[clotemp].idata >= 0) &&
      (*_GD_ef[E->e->u.raw.file[clotemp].subenc].close)(E->e->u.raw.file +
          clotemp))
    {
      if (D->error == GD_E_OK)
        _GD_SetEncIOError(D, GD_E_IO_CLOSE, E->e->u.raw.file + clotemp);
      dreturn("%i", 1);
      return 1;
    }
  }

  if (flags & GD_FINIRAW_DEFER) {
    dreturn("%i", 0);
    return 0;
  }

  /* take care of moving things into place */
  if (oop_write || clotemp) {
    if (flags & GD_FINIRAW_DISCARD) {
      /* Throw away the temporary file */
      if (E->e->u.raw.file[1].name != NULL &&
          gd_UnlinkAt(D, frag->dirfd, E->e->u.raw.file[1].name, 0))
      {
        if (D->error == GD_E_OK)
          _GD_SetEncIOError(D, GD_E_IO_UNLINK, E->e->u.raw.file + 1);
        dreturn("%i", -1);
        return -1;
      }
    } else {
      /* Move the old file over the new file */
      if (_GD_MoveOver(D, frag->dirfd, E->e->u.raw.file)) {
        dreturn("%i", -1);
        return -1;
      }
    }

    free(E->e->u.raw.file[1].name);
    E->e->u.raw.file[1].name = NULL;
  }

  /* Clear atime */
  E->e->u.raw.atime = 0;

  /* Delete this entry from the opened list if it's there */
  if (D->open_limit) {
    long i;
    
    for (i = 0; i < D->open_raws; ++i)
      if (D->opened[i] == E) {
        memmove(D->opened + i, D->opened + i + 1,
            sizeof(D->opened[0]) * (D->open_raws - i - 1));
        D->open_raws--;
        D->open_fds -= E->e->u.raw.fd_count;
        break;
      }
  }

  dreturn("%i", 0);
  return 0;
}

/* Perform a RAW field write */
ssize_t _GD_WriteOut(const gd_entry_t *E, const struct encoding_t *enc,
    const void *ptr, gd_type_t type, size_t n, int temp)
{
  ssize_t n_wrote;

  dtrace("%p, %p, %p, 0x%X, %" PRIuSIZE ", %i", E, enc, ptr, type, n, temp);

  if (temp)
    n_wrote = (*enc->write)(E->e->u.raw.file + 1, ptr, type, n);
  else {
    if (enc->flags & GD_EF_OOP) {
      n_wrote = (*enc->write)(E->e->u.raw.file + 1, ptr, type, n);

      if (n_wrote > 0 && E->e->u.raw.file[0].idata >= 0) {
        /* advance the read pointer by the appropriate amount */
        if ((*enc->seek)(E->e->u.raw.file, E->e->u.raw.file[0].pos + n_wrote,
              E->EN(raw,data_type), GD_FILE_READ) < 0)
        {
          n_wrote = -1;
        }
      }
    } else 
      n_wrote = (*enc->write)(E->e->u.raw.file, ptr, type, n);
  }

  dreturn("%" PRIdSIZE, n_wrote);
  return n_wrote;
}

/* Build a chunk filebase string: "fieldname/N" where N is the frame offset.
 * The caller must free the returned string.  Returns NULL on allocation
 * failure. */
static char *_GD_ChunkFilebase(DIRFILE *D, const gd_entry_t *E,
    off64_t chunk_frame)
{
  char *chunk_filebase;

  /* 22 = "/" (1) + max PRId64 digits (20) + NUL (1) */
  chunk_filebase = _GD_Malloc(D, strlen(E->e->u.raw.filebase) + 22);
  if (chunk_filebase == NULL)
    return NULL;
  sprintf(chunk_filebase, "%s/%" PRId64, E->e->u.raw.filebase,
      (int64_t)chunk_frame);
  return chunk_filebase;
}

/* Ensure the chunk directory exists for a field.  Creates it if necessary.
 * Returns 0 on success, non-zero on error. */
static int _GD_EnsureChunkDir(DIRFILE *D, gd_entry_t *E)
{
  int dirfd = D->fragment[E->fragment_index].dirfd;
  struct stat st;

  if (gd_StatAt(D, dirfd, E->e->u.raw.filebase, &st, 0) == 0) {
    if (S_ISDIR(st.st_mode))
      return 0;  /* directory already exists */

    /* A non-directory exists where we need the chunk directory.
     * _GD_RechunkField cleans up old data files during chunk
     * transitions, so this should not happen. */
    _GD_SetError(D, GD_E_IO, GD_E_IO_OPEN, NULL, 0, NULL);
    return 1;
  }

  if (gd_MkdirAt(D, dirfd, E->e->u.raw.filebase, 0777) && errno != EEXIST) {
    _GD_SetError(D, GD_E_IO, GD_E_IO_OPEN, NULL, 0, NULL);
    return 1;
  }

  return 0;
}

/* Open a raw file, if necessary; also check for required functions */
int _GD_InitRawIO(DIRFILE *D, gd_entry_t *E, const char *filebase, int fragment,
    const struct encoding_t *enc, unsigned int funcs, unsigned int mode,
    int swap)
{
  const struct gd_fragment_t *frag;
  const int touch = mode & GD_FILE_TOUCH;
  int oop_write = 0;

  dtrace("%p, %p, \"%s\", %i, %p, 0x%X, 0x%X, %i", D, E, filebase,
      fragment, enc, funcs, mode, swap);

  frag = &D->fragment[E->fragment_index];

  /* For chunked fields, individual chunk files are managed by
   * _GD_EnsureChunk (called from _GD_Seek).  Chunk-unaware callers
   * (filebase=NULL, fragment=-1) are handled here: touch creates the
   * chunk directory, everything else is a no-op.  _GD_EnsureChunk
   * itself passes a non-NULL filebase and falls through to the normal
   * open path below. */
  if (filebase == NULL && fragment == -1 && frag->chunk_size > 0)
  {
    if (touch) {
      /* Create the chunk directory, then create an empty chunk-0 file so
       * the field has a concrete presence on disk (consistent with the
       * non-chunked touch behaviour which creates an empty flat file). */
      char *chunk0_filebase;
      struct gd_raw_file_ chunk0_file;
      int chunk0_dirfd;

      if (_GD_EnsureChunkDir(D, E)) {
        dreturn("%i", 1);
        return 1;
      }

      /* Only create chunk 0 if no chunks exist yet */
      chunk0_filebase = _GD_ChunkFilebase(D, E, 0);
      if (chunk0_filebase == NULL) {
        dreturn("%i", 1);
        return 1;
      }

      chunk0_dirfd = D->fragment[E->fragment_index].dirfd;

      memset(&chunk0_file, 0, sizeof(chunk0_file));
      chunk0_file.D = D;
      chunk0_file.subenc = GD_ENC_RAW;
      chunk0_file.idata = -1;

      if (!(*_GD_ef[GD_ENC_RAW].name)(D, NULL, &chunk0_file,
            chunk0_filebase, 0, 0))
      {
        struct stat st;
        if (gd_StatAt(D, chunk0_dirfd, chunk0_file.name, &st, 0) != 0) {
          /* File doesn't exist: create it empty */
          if ((*_GD_ef[GD_ENC_RAW].open)(chunk0_dirfd, &chunk0_file,
                E->EN(raw,data_type), 0, GD_FILE_WRITE) == 0)
          {
            (*_GD_ef[GD_ENC_RAW].close)(&chunk0_file);
          }
        } else {
          free(chunk0_file.name);
        }
      }
      free(chunk0_filebase);
    }
    dreturn("%i", 0);
    return 0;
  }

  if (mode & (GD_FILE_WRITE | GD_FILE_TOUCH))
    funcs |= GD_EF_WRITE;

  /* If this is a temp file or we're just touching it, we don't register
   * it in the open field list, otherwise, try autoclosing first */
  if (!(mode & (GD_FILE_TEMP | GD_FILE_TOUCH))) {
    E->e->u.raw.fd_count = 1;
    if (_GD_AutoClose(D, 1)) {
      dreturn("%i", 1);
      return 1;
    }
  }

  mode &= ~GD_FILE_TOUCH;

  if (!(mode & GD_FILE_TEMP)) {
    if (!_GD_Supports(D, E, GD_EF_NAME | GD_EF_OPEN | funcs)) {
      dreturn("%i", 1);
      return 1;
    }

    enc = _GD_ef + E->e->u.raw.file[0].subenc;
    oop_write = ((enc->flags & GD_EF_OOP) && mode == GD_FILE_WRITE) ? 1 : 0;

    /* In this case, we need two free descriptors */
    if (oop_write) {
      E->e->u.raw.fd_count = 2;
      if (_GD_AutoClose(D, 2)) {
        dreturn("%i", 1);
        return 1;
      }
    }

    /* Do nothing, if possible */
    if (!touch && (((mode & GD_FILE_READ) && (E->e->u.raw.file[0].idata >= 0)
            && (E->e->u.raw.file[0].mode & GD_FILE_READ))
          || ((mode & GD_FILE_WRITE) && (E->e->u.raw.file[oop_write].idata >= 0)
            && (E->e->u.raw.file[0].mode & GD_FILE_WRITE))))
    {
      dreturn("%i", 0);
      return 0;
    }

    /* close the file, if necessary */
    if ((E->e->u.raw.file[0].idata >= 0 || ((enc->flags & GD_EF_OOP)
            && (E->e->u.raw.file[1].idata >= 0)))
        && ((mode == GD_FILE_READ && (enc->flags & GD_EF_OOP)
            && (E->e->u.raw.file[0].mode & GD_FILE_WRITE))
          || (mode == GD_FILE_WRITE
            && !(E->e->u.raw.file[0].mode & GD_FILE_WRITE))))
    {
      if (_GD_FiniRawIO(D, E, E->fragment_index, GD_FINIRAW_KEEP)) {
        dreturn("%i", 1);
        return 1;
      }
    }
    if (oop_write)
      E->e->u.raw.file[1].subenc = E->e->u.raw.file[0].subenc;
  }

  if (filebase == NULL)
    filebase = E->e->u.raw.filebase;

  if (fragment == -1)
    fragment = E->fragment_index;

  frag = &D->fragment[fragment];

  if (oop_write || mode & GD_FILE_TEMP) {
    /* create temporary file in file[1] */
    if ((*enc->name)(D, (const char*)frag->enc_data,
          E->e->u.raw.file + 1, filebase, 1, 0))
    {
      ; /* error already set */
      dreturn("%i", 1);
      return 1;
    } else if ((*enc->open)(frag->dirfd, E->e->u.raw.file + 1,
          E->EN(raw,data_type), swap, GD_FILE_WRITE | GD_FILE_TEMP))
    {
      _GD_SetEncIOError(D, GD_E_IO_OPEN, E->e->u.raw.file + 1);
      dreturn("%i", 1);
      return 1;
    }

    if (oop_write) {
      /* The read file in OOP mode is flagged as RW. */
      mode = GD_FILE_RDWR;
    } else {
      /* Temp file creation complete */
      dreturn("%i", 0);
      return 0;
    }
  }

  /* open a regular file, if necessary */
  if (E->e->u.raw.file[0].idata < 0) {
    if ((*enc->name)(D, (const char*)frag->enc_data,
          E->e->u.raw.file, filebase, 0, 0))
    {
      dreturn("%i", 1);
      return 1;
    } else if ((*enc->open)(frag->dirfd, E->e->u.raw.file,
          E->EN(raw,data_type), swap, mode))
    {
      /* In oop_write mode, it doesn't matter if the old file doesn't exist */
      if (!oop_write || errno != ENOENT) {
        _GD_SetEncIOError(D, GD_E_IO_OPEN, E->e->u.raw.file + 0);
        dreturn("%i", 1);
        return 1;
      }
      E->e->u.raw.file[0].mode = mode;
    }
  }

  if (touch)
    _GD_FiniRawIO(D, E, fragment, GD_FINIRAW_KEEP);
  else {
    E->e->u.raw.atime = time(NULL);
    if (D->open_limit > 0 && !(mode & GD_FILE_TEMP)) {
      /* Add this file to the top of the opened list.  This keeps the list
       * sorted if it was already */
      memmove(D->opened + 1, D->opened, sizeof(D->opened[0]) * D->open_raws);
      D->opened[0] = E;
      D->open_raws++;
      D->open_fds += E->e->u.raw.fd_count;
    }
  }

  dreturn("%i", 0);
  return 0;
}

/* Figure out the encoding scheme */
static unsigned long _GD_ResolveEncoding(DIRFILE *restrict D,
    const char *restrict name, const char *restrict enc_data,
    unsigned long scheme, int dirfd, struct gd_raw_file_ *restrict file)
{
  char *candidate;
  int i;
  const size_t len = strlen(name);
  struct stat statbuf;

  dtrace("%p, \"%s\", \"%s\", 0x%08lx, %i, %p", D, name, enc_data, scheme,
      dirfd, file);

  for (i = 0; _GD_ef[i].scheme != GD_ENC_UNSUPPORTED; i++) {
    if (scheme == GD_AUTO_ENCODED || scheme == _GD_ef[i].scheme) {
      if (_GD_ef[i].ext) {
        candidate = malloc(len + strlen(_GD_ef[i].ext) + 1);
        if (!candidate)
          continue;

        sprintf(candidate, "%s%s", name, _GD_ef[i].ext);
      } else {
        if (_GD_MissingFramework(i, GD_EF_NAME))
          continue;

        if ((*_GD_ef[i].name)(D, enc_data, file, name, 0, 1))
          continue;

        candidate = file->name;
        file->name = NULL;
      }

      if (gd_StatAt(D, dirfd, candidate, &statbuf, 0) == 0)
        if (S_ISREG(statbuf.st_mode)) {
          if (file != NULL)
            file->subenc = i;
          free(candidate);
          dreturn("%08lx", _GD_ef[i].scheme);
          return _GD_ef[i].scheme;
        }
      free(candidate);
    }
  }

  if (scheme != 0 && file != NULL) {
    for (i = 0; _GD_ef[i].scheme != GD_ENC_UNSUPPORTED; i++)
      if (scheme == _GD_ef[i].scheme) {
        file->subenc = i;
        dreturn("0x%08lx", _GD_ef[i].scheme);
        return _GD_ef[i].scheme;
      }
  }

  dreturn("%08lx", (unsigned long)GD_AUTO_ENCODED);
  return GD_AUTO_ENCODED;
}

int _GD_Supports(DIRFILE *D, const gd_entry_t *E, unsigned int funcs)
{
  dtrace("%p, %p, 0x%X", D, E, funcs);

  /* Figure out the dirfile encoding type, if required */
  if (D->fragment[E->fragment_index].encoding == GD_AUTO_ENCODED) {
    D->fragment[E->fragment_index].encoding =
      _GD_ResolveEncoding(D, E->e->u.raw.filebase,
          (const char*)D->fragment[E->fragment_index].enc_data, GD_AUTO_ENCODED,
          D->fragment[E->fragment_index].dirfd, E->e->u.raw.file);
  }

  /* If the encoding scheme is unknown, complain */
  if (D->fragment[E->fragment_index].encoding == GD_AUTO_ENCODED) {
    _GD_SetError(D, GD_E_UNKNOWN_ENCODING, GD_E_UNENC_UNDET, NULL, 0, NULL);
    dreturn("%i", 0);
    return 0;
  }

  /* Figure out the encoding subtype, if required */
  if (E->e->u.raw.file[0].subenc == GD_ENC_UNKNOWN)
    _GD_ResolveEncoding(D, E->e->u.raw.filebase,
        (const char*)D->fragment[E->fragment_index].enc_data,
        D->fragment[E->fragment_index].encoding,
        D->fragment[E->fragment_index].dirfd, E->e->u.raw.file);

  /* check for our function(s) */
  if (_GD_MissingFramework(E->e->u.raw.file[0].subenc, funcs)) {
    _GD_SetError(D, GD_E_UNSUPPORTED, 0, NULL, 0, NULL);
    dreturn("%i", 0);
    return 0;
  }

  dreturn("%i", 1);
  return 1;
}

int _GD_GenericName(DIRFILE *restrict D,
    const char *restrict enc_data gd_unused_,
    struct gd_raw_file_ *restrict file, const char *restrict base, int temp,
    int resolv gd_unused_)
{
  dtrace("%p, <unused>, %p, \"%s\", %i, <unused>", D, file, base, temp);

  if (file->name == NULL) {
    file->D = D;
    file->name = _GD_Malloc(D, strlen(base) + (temp ? 8 :
          strlen(_GD_ef[file->subenc].ext) + 1));
    if (file->name == NULL) {
      dreturn("%i", -1);
      return -1;
    }

    sprintf(file->name, "%s%s", base,
        temp ? "_XXXXXX" : _GD_ef[file->subenc].ext);
  }

  dreturn("%i (%s)", 0, file->name);
  return 0;
}

/* Create zero-filled raw chunk files for any missing chunks between the
 * last known chunk and the target chunk_frame.  This ensures that reads
 * across chunk boundaries return zeros for unwritten regions, matching the
 * gap-fill behavior of non-chunked fields. */
static void _GD_FillChunkGaps(DIRFILE *D, gd_entry_t *E, off64_t chunk_frame)
{
  int fragment = E->fragment_index;
  int dirfd = D->fragment[fragment].dirfd;
  off64_t chunk_size = D->fragment[fragment].chunk_size;
  off64_t spf = E->EN(raw,spf);
  gd_type_t data_type = E->EN(raw,data_type);
  off64_t gap_start, gap_frame;
  size_t chunk_bytes;
  char *buf, *gap_filebase;
  struct gd_raw_file_ gap_file;

  /* Nothing to fill if this is the first chunk or immediately adjacent */
  if (E->e->u.raw.last_chunk < 0 ||
      chunk_frame <= E->e->u.raw.last_chunk + chunk_size)
    return;

  chunk_bytes = (size_t)(chunk_size * spf) * GD_SIZE(data_type);
  buf = calloc(1, chunk_bytes);
  if (buf == NULL)
    return;

  gap_start = E->e->u.raw.last_chunk + chunk_size;

  for (gap_frame = gap_start; gap_frame < chunk_frame;
      gap_frame += chunk_size)
  {
    gap_filebase = _GD_ChunkFilebase(D, E, gap_frame);
    if (gap_filebase == NULL)
      break;

    memset(&gap_file, 0, sizeof(gap_file));
    gap_file.D = D;
    gap_file.subenc = GD_ENC_RAW;
    gap_file.idata = -1;

    if ((*_GD_ef[GD_ENC_RAW].name)(D, NULL, &gap_file, gap_filebase, 0, 0)) {
      free(gap_filebase);
      break;
    }

    if ((*_GD_ef[GD_ENC_RAW].open)(dirfd, &gap_file, data_type, 0,
          GD_FILE_WRITE) == 0)
    {
      (*_GD_ef[GD_ENC_RAW].write)(&gap_file, buf, data_type,
          (size_t)(chunk_size * spf));
      (*_GD_ef[GD_ENC_RAW].close)(&gap_file);
    }

    free(gap_file.name);
    free(gap_file.edata);
    free(gap_filebase);
  }

  free(buf);
}

/* Probe whether a chunk file exists (raw cursor or encoded) and optionally
 * return its size in samples.  Prefers raw over encoded -- a raw file is
 * always authoritative (it may be the active cursor, or a survivor of a
 * crash during finalization).
 * Returns 1 if the chunk exists, 0 if not, -1 on error. */
int _GD_ProbeChunk(DIRFILE *D, gd_entry_t *E, off64_t chunk_frame,
    off64_t *size_out)
{
  int frag = E->fragment_index;
  int subenc = E->e->u.raw.file[0].subenc;
  int saved_errno;
  struct gd_raw_file_ probe;
  char *chunk_filebase;
  off64_t chunk_nf;
  int ret;

  dtrace("%p, %p, %" PRId64 ", %p", D, E, (int64_t)chunk_frame, size_out);

  chunk_filebase = _GD_ChunkFilebase(D, E, chunk_frame);
  if (chunk_filebase == NULL) {
    dreturn("%i", -1);
    return -1;
  }

  /* Try raw first (cursor or crash survivor) */
  memset(&probe, 0, sizeof(probe));
  probe.D = D;
  probe.subenc = GD_ENC_RAW;
  probe.idata = -1;

  if ((*_GD_ef[GD_ENC_RAW].name)(D, NULL, &probe, chunk_filebase, 0, 0)) {
    free(chunk_filebase);
    dreturn("%i", -1);
    return -1;
  }

  errno = 0;
  chunk_nf = (*_GD_ef[GD_ENC_RAW].size)(D->fragment[frag].dirfd, &probe,
      E->EN(raw,data_type), 0);
  saved_errno = errno;
  free(probe.name);
  free(probe.edata);

  if (chunk_nf >= 0) {
    if (size_out)
      *size_out = chunk_nf;
    free(chunk_filebase);
    dreturn("%i", 1);
    return 1;
  }

  if (saved_errno != ENOENT) {
    free(chunk_filebase);
    dreturn("%i", -1);
    return -1;
  }

  /* Raw not found -- try configured encoding (finalized chunk) */
  if (subenc != GD_ENC_RAW) {
    memset(&probe, 0, sizeof(probe));
    probe.D = D;
    probe.subenc = subenc;
    probe.idata = -1;

    if ((*_GD_ef[subenc].name)(D, (const char*)D->fragment[frag].enc_data,
          &probe, chunk_filebase, 0, 0))
    {
      free(chunk_filebase);
      dreturn("%i", -1);
      return -1;
    }

    errno = 0;
    chunk_nf = (*_GD_ef[subenc].size)(D->fragment[frag].dirfd, &probe,
        E->EN(raw,data_type), _GD_FileSwapBytes(D, E));
    saved_errno = errno;
    free(probe.name);
    free(probe.edata);

    if (chunk_nf >= 0) {
      if (size_out)
        *size_out = chunk_nf;
      free(chunk_filebase);
      dreturn("%i", 1);
      return 1;
    }

    ret = (saved_errno == ENOENT) ? 0 : -1;
    free(chunk_filebase);
    dreturn("%i", ret);
    return ret;
  }

  free(chunk_filebase);
  dreturn("%i", 0);
  return 0;
}

/* Iterate over all chunk files for a chunked field, calling cb for each.
 * The callback receives the chunk's frame offset and its filename (relative
 * to the fragment directory).  Returns 0 on success, -1 on error, or the
 * non-zero return value from the callback if it stopped iteration early. */
int _GD_ForEachChunk(DIRFILE *D, gd_entry_t *E, gd_chunk_cb_t cb, void *data)
{
  int frag = E->fragment_index;
  int dirfd = D->fragment[frag].dirfd;
  const char *filebase = E->e->u.raw.filebase;
  const char *ext = _GD_ef[E->e->u.raw.file[0].subenc].ext;
  size_t ext_len;
  DIR *dir;
  struct dirent entry, *result;
  char *chunkdir_path;
  int ret = 0;
  size_t n_chunks = 0, chunks_alloc = 0;
  off64_t *chunk_frames = NULL;
  char **chunk_paths = NULL;
  size_t i;

  dtrace("%p, %p, %p, %p", D, E, cb, data);

  ext_len = ext ? strlen(ext) : 0;

  /* Open the field's chunk directory.  Build a path relative to the
   * fragment directory for opendir (portable across all platforms). */
  chunkdir_path = _GD_MakeFullPathOnly(D, dirfd, filebase);
  if (chunkdir_path == NULL) {
    dreturn("%i", -1);
    return -1;
  }

  dir = opendir(chunkdir_path);
  free(chunkdir_path);
  if (dir == NULL) {
    /* No directory means no chunks -- not an error */
    if (errno == ENOENT) {
      dreturn("%i", 0);
      return 0;
    }
    dreturn("%i", -1);
    return -1;
  }

  /* Collect all chunk entries before closing the directory.  Callbacks may
   * modify the directory (e.g. unlinking files), which is unsafe during
   * FindFirst/FindNext iteration on Windows. */
  while (_GD_ReadDir(dir, &entry, &result) == 0 && result != NULL) {
    const char *name = result->d_name;
    off64_t chunk_frame;
    char *endp, *fullpath;

    /* Skip . and .. */
    if (name[0] == '.' && (name[1] == '\0' ||
          (name[1] == '.' && name[2] == '\0')))
      continue;

    /* Parse frame number from filename: "N" or "N.ext" */
    errno = 0;
    chunk_frame = (off64_t)strtoll(name, &endp, 10);
    if (errno != 0 || endp == name)
      continue;

    /* Accept both encoded (N.ext) and raw (N) */
    if (*endp == '\0') {
      /* Raw cursor / crash survivor */
    } else if (ext_len > 0 && strcmp(endp, ext) == 0) {
      /* Encoded finalized chunk */
    } else {
      continue;
    }

    /* Build full path relative to the fragment directory:
     * "fieldname/filename" */
    fullpath = malloc(strlen(filebase) + 1 + strlen(name) + 1);
    if (fullpath == NULL) {
      ret = -1;
      break;
    }
    sprintf(fullpath, "%s/%s", filebase, name);

    /* Grow arrays if needed */
    if (n_chunks >= chunks_alloc) {
      void *ptr;
      chunks_alloc = chunks_alloc ? chunks_alloc * 2 : 16;
      ptr = realloc(chunk_frames, chunks_alloc * sizeof(*chunk_frames));
      if (ptr == NULL) {
        free(fullpath);
        ret = -1;
        break;
      }
      chunk_frames = ptr;
      ptr = realloc(chunk_paths, chunks_alloc * sizeof(*chunk_paths));
      if (ptr == NULL) {
        free(fullpath);
        ret = -1;
        break;
      }
      chunk_paths = ptr;
    }

    chunk_frames[n_chunks] = chunk_frame;
    chunk_paths[n_chunks] = fullpath;
    n_chunks++;
  }

  closedir(dir);

  /* Now invoke callbacks outside the directory iteration */
  if (ret == 0) {
    for (i = 0; i < n_chunks; i++) {
      ret = cb(D, E, chunk_frames[i], chunk_paths[i], data);
      if (ret)
        break;
    }
  }

  for (i = 0; i < n_chunks; i++)
    free(chunk_paths[i]);
  free(chunk_paths);
  free(chunk_frames);

  dreturn("%i", ret);
  return ret;
}

/* Callback for _GD_PopulateChunkCache: track min/max chunk frame */
static int _GD_ChunkCacheCb(DIRFILE *D gd_unused_, gd_entry_t *E,
    off64_t chunk_frame, const char *name gd_unused_, void *data gd_unused_)
{
  if (E->e->u.raw.first_chunk < 0 || chunk_frame < E->e->u.raw.first_chunk)
    E->e->u.raw.first_chunk = chunk_frame;
  if (chunk_frame > E->e->u.raw.last_chunk)
    E->e->u.raw.last_chunk = chunk_frame;
  return 0;
}

/* Return the total number of samples across all chunks for a chunked field.
 * Populates and updates the chunk cache as needed.  Returns -1 on error or
 * if no chunks exist. */
off64_t _GD_ChunkedSampleCount(DIRFILE *D, gd_entry_t *E)
{
  off64_t chunk_size = D->fragment[E->fragment_index].chunk_size;
  off64_t last, last_chunk_samples = 0;
  off64_t full;

  dtrace("%p, %p", D, E);

  if (E->e->u.raw.last_chunk < 0)
    _GD_PopulateChunkCache(D, E);

  if (E->e->u.raw.last_chunk < 0) {
    dreturn("%i", -1);
    return -1;
  }

  last = E->e->u.raw.last_chunk;

  if (_GD_ProbeChunk(D, E, last, &last_chunk_samples) <= 0) {
    dreturn("%i", -1);
    return -1;
  }

  /* Scan forward while the last known chunk is full */
  full = chunk_size * E->EN(raw,spf);
  while (last_chunk_samples >= full) {
    off64_t next_size;
    if (_GD_ProbeChunk(D, E, last + chunk_size, &next_size) <= 0)
      break;
    last += chunk_size;
    last_chunk_samples = next_size;
  }
  E->e->u.raw.last_chunk = last;

  dreturn("%" PRId64, (int64_t)(last * E->EN(raw,spf) + last_chunk_samples));
  return last * E->EN(raw,spf) + last_chunk_samples;
}

/* Populate the chunk cache (first_chunk, last_chunk) for a chunked field. */
void _GD_PopulateChunkCache(DIRFILE *D, gd_entry_t *E)
{
  dtrace("%p, %p", D, E);

  E->e->u.raw.first_chunk = -1;
  E->e->u.raw.last_chunk = -1;

  _GD_ForEachChunk(D, E, _GD_ChunkCacheCb, NULL);

  dreturnvoid();
}

/* Finalize a raw cursor chunk: read its contents, write them through the
 * configured encoding, and delete the raw file.  The file must already be
 * closed before calling this.  Returns 0 on success. */
int _GD_FinalizeChunk(DIRFILE *D, gd_entry_t *E)
{
  int fragment = E->fragment_index;
  int dirfd = D->fragment[fragment].dirfd;
  gd_type_t data_type = E->EN(raw,data_type);
  int swap = _GD_FileSwapBytes(D, E);
  int oop, configured_subenc, i, ret;
  unsigned long enc;
  unsigned int open_mode;
  off64_t raw_nsamples;
  ssize_t nread, nwrote;
  size_t buflen;
  char *chunk_filebase, *raw_name, *final_name, *buf;
  struct gd_raw_file_ raw_file, enc_file;

  dtrace("%p, %p", D, E);

  /* Find the configured encoding subenc.  If it's raw or unrecognized,
   * there is nothing to compress. */
  configured_subenc = -1;
  enc = D->fragment[fragment].encoding;
  for (i = 0; i < GD_N_SUBENCODINGS - 1; ++i)
    if (_GD_ef[i].scheme == enc) {
      configured_subenc = i;
      break;
    }
  if (configured_subenc <= GD_ENC_RAW || configured_subenc >= GD_ENC_UNKNOWN) {
    dreturn("%i", 0);
    return 0;
  }

  chunk_filebase = _GD_ChunkFilebase(D, E, E->e->u.raw.active_chunk);
  if (chunk_filebase == NULL) {
    dreturn("%i", 1);
    return 1;
  }

  /* Read the raw cursor file into memory */
  memset(&raw_file, 0, sizeof(raw_file));
  raw_file.D = D;
  raw_file.subenc = GD_ENC_RAW;
  raw_file.idata = -1;

  ret = 1;
  raw_name = NULL;
  final_name = NULL;
  buf = NULL;

  if ((*_GD_ef[GD_ENC_RAW].name)(D, NULL, &raw_file, chunk_filebase, 0, 0))
    goto cleanup;

  raw_name = raw_file.name;
  raw_nsamples = (*_GD_ef[GD_ENC_RAW].size)(dirfd, &raw_file, data_type, 0);
  if (raw_nsamples <= 0) {
    ret = 0;  /* empty or missing -- nothing to finalize */
    goto cleanup;
  }

  buflen = (size_t)raw_nsamples * GD_SIZE(data_type);
  buf = malloc(buflen);
  if (buf == NULL)
    goto cleanup;

  if ((*_GD_ef[GD_ENC_RAW].open)(dirfd, &raw_file, data_type, 0,
        GD_FILE_READ))
    goto cleanup;

  (*_GD_ef[GD_ENC_RAW].seek)(&raw_file, 0, data_type, GD_FILE_READ);
  nread = (*_GD_ef[GD_ENC_RAW].read)(&raw_file, buf, data_type,
      (size_t)raw_nsamples);
  (*_GD_ef[GD_ENC_RAW].close)(&raw_file);

  if (nread < raw_nsamples)
    goto cleanup;

  /* Write through the configured encoding.  OOP encodings write to a temp
   * file that is renamed into place; non-OOP encodings write directly. */
  oop = (_GD_ef[configured_subenc].flags & GD_EF_OOP) ? 1 : 0;

  memset(&enc_file, 0, sizeof(enc_file));
  enc_file.D = D;
  enc_file.subenc = configured_subenc;
  enc_file.idata = -1;

  /* Generate the final encoded filename */
  if ((*_GD_ef[configured_subenc].name)(D,
        (const char*)D->fragment[fragment].enc_data,
        &enc_file, chunk_filebase, 0, 0))
    goto cleanup;

  if (oop) {
    /* Save the final name; generate a temp name for writing */
    final_name = enc_file.name;
    enc_file.name = NULL;
    if ((*_GD_ef[configured_subenc].name)(D,
          (const char*)D->fragment[fragment].enc_data,
          &enc_file, chunk_filebase, 1, 0))
      goto cleanup;
  }

  open_mode = oop ? GD_FILE_TEMP : GD_FILE_WRITE;
  if ((*_GD_ef[configured_subenc].open)(dirfd, &enc_file, data_type, swap,
        open_mode))
    goto cleanup;

  (*_GD_ef[configured_subenc].seek)(&enc_file, 0, data_type, GD_FILE_WRITE);
  nwrote = (*_GD_ef[configured_subenc].write)(&enc_file, buf, data_type,
      (size_t)nread);
  (*_GD_ef[configured_subenc].close)(&enc_file);

  if (nwrote < nread)
    goto cleanup;

  if (oop)
    gd_RenameAt(D, dirfd, enc_file.name, dirfd, final_name);

  /* Delete the raw cursor file */
  gd_UnlinkAt(D, dirfd, raw_name, 0);

  E->e->u.raw.cursor_is_raw = 0;
  ret = 0;

cleanup:
  free(buf);
  free(enc_file.name);
  free(final_name);
  free(raw_name);
  free(chunk_filebase);

  dreturn("%i", ret);
  return ret;
}

/* Decompress an encoded chunk to a raw file so the OOP cursor can modify it.
 * If no encoded chunk exists, this is a no-op.  Returns 0 on success. */
static int _GD_UnencodeChunk(DIRFILE *D, gd_entry_t *E,
    const char *chunk_filebase, int subenc)
{
  int fragment = E->fragment_index;
  int dirfd = D->fragment[fragment].dirfd;
  struct gd_raw_file_ enc_probe, raw_tmp;
  off64_t enc_nsamples;
  char *buf;
  size_t buflen;
  ssize_t nr;

  dtrace("%p, %p, \"%s\", %i", D, E, chunk_filebase, subenc);

  memset(&enc_probe, 0, sizeof(enc_probe));
  enc_probe.D = D;
  enc_probe.subenc = subenc;
  enc_probe.idata = -1;

  if ((*_GD_ef[subenc].name)(D, (const char*)D->fragment[fragment].enc_data,
        &enc_probe, chunk_filebase, 0, 0))
  {
    dreturn("%i", 0);
    return 0;  /* name failed -- no encoded chunk */
  }

  errno = 0;
  enc_nsamples = (*_GD_ef[subenc].size)(dirfd, &enc_probe,
      E->EN(raw,data_type), _GD_FileSwapBytes(D, E));

  if (enc_nsamples <= 0) {
    free(enc_probe.name);
    free(enc_probe.edata);
    dreturn("%i", 0);
    return 0;  /* empty or missing */
  }

  buflen = (size_t)enc_nsamples * GD_SIZE(E->EN(raw,data_type));
  buf = malloc(buflen);
  if (buf == NULL) {
    free(enc_probe.name);
    free(enc_probe.edata);
    dreturn("%i", 1);
    return 1;
  }

  if ((*_GD_ef[subenc].open)(dirfd, &enc_probe, E->EN(raw,data_type),
        _GD_FileSwapBytes(D, E), GD_FILE_READ))
  {
    free(buf);
    free(enc_probe.name);
    free(enc_probe.edata);
    dreturn("%i", 1);
    return 1;
  }

  (*_GD_ef[subenc].seek)(&enc_probe, 0, E->EN(raw,data_type), GD_FILE_READ);
  nr = (*_GD_ef[subenc].read)(&enc_probe, buf, E->EN(raw,data_type),
      (size_t)enc_nsamples);
  (*_GD_ef[subenc].close)(&enc_probe);

  if (nr <= 0) {
    free(buf);
    free(enc_probe.name);
    free(enc_probe.edata);
    dreturn("%i", 1);
    return 1;
  }

  /* Write decompressed data to a raw file */
  memset(&raw_tmp, 0, sizeof(raw_tmp));
  raw_tmp.D = D;
  raw_tmp.subenc = GD_ENC_RAW;
  raw_tmp.idata = -1;

  if ((*_GD_ef[GD_ENC_RAW].name)(D, NULL, &raw_tmp, chunk_filebase, 0, 0)) {
    free(buf);
    free(enc_probe.name);
    free(enc_probe.edata);
    dreturn("%i", 1);
    return 1;
  }

  if ((*_GD_ef[GD_ENC_RAW].open)(dirfd, &raw_tmp, E->EN(raw,data_type), 0,
        GD_FILE_WRITE))
  {
    free(buf);
    free(raw_tmp.name);
    free(enc_probe.name);
    free(enc_probe.edata);
    dreturn("%i", 1);
    return 1;
  }

  (*_GD_ef[GD_ENC_RAW].seek)(&raw_tmp, 0, E->EN(raw,data_type), GD_FILE_WRITE);
  (*_GD_ef[GD_ENC_RAW].write)(&raw_tmp, buf, E->EN(raw,data_type), (size_t)nr);
  (*_GD_ef[GD_ENC_RAW].close)(&raw_tmp);
  free(raw_tmp.name);
  free(buf);

  /* Delete the encoded file now that raw exists */
  gd_UnlinkAt(D, dirfd, enc_probe.name, 0);
  free(enc_probe.name);
  free(enc_probe.edata);

  dreturn("%i", 0);
  return 0;
}

/* Ensure the correct chunk file is open for the given sample offset.
 * If chunk_size is 0 (no chunking), this is a no-op.
 *
 * For writes with non-OOP encodings, chunks are opened directly through the
 * configured encoding for in-place writes.  For OOP encodings, a raw cursor
 * is used and finalized (compressed) when the cursor moves to a different
 * chunk.
 *
 * For reads, finalized (encoded) chunks are opened via the configured
 * encoding; raw cursor chunks (from OOP encodings or crash survivors) are
 * opened as raw.
 *
 * On success, *s0 is adjusted to be chunk-local and returns 0.
 * On error, returns non-zero with the error set on D. */
int _GD_EnsureChunk(DIRFILE *D, gd_entry_t *E, off64_t *s0,
    unsigned int mode)
{
  const struct gd_fragment_t *frag = &D->fragment[E->fragment_index];
  off64_t chunk_size, spf, chunk_frame;
  unsigned long enc;
  int saved_subenc, i, have_raw;
  struct stat st;
  char *chunk_filebase;

  dtrace("%p, %p, %" PRId64 ", 0x%X", D, E, (int64_t)*s0, mode);

  chunk_size = frag->chunk_size;
  if (chunk_size <= 0) {
    dreturn("%i", 0);
    return 0;
  }

  spf = E->EN(raw,spf);
  chunk_frame = (*s0 / spf) / chunk_size * chunk_size;

  /* Already have the right chunk open? */
  if (E->e->u.raw.file[0].idata >= 0 &&
      E->e->u.raw.active_chunk == chunk_frame)
  {
    *s0 -= chunk_frame * spf;
    dreturn("%i", 0);
    return 0;
  }

  /* Find the configured encoding's subenc index from the fragment.
   * We can't use file[0].subenc because it may have been set to 0 (raw)
   * for the cursor chunk. */
  enc = frag->encoding;
  saved_subenc = 0;
  for (i = 0; i < GD_N_SUBENCODINGS - 1; ++i)
    if (_GD_ef[i].scheme == enc) {
      saved_subenc = i;
      break;
    }

  /* Close and finalize current chunk if one is open */
  if (E->e->u.raw.file[0].idata >= 0) {
    if (_GD_FiniRawIO(D, E, E->fragment_index, GD_FINIRAW_KEEP)) {
      dreturn("%i", 1);
      return 1;
    }

    if (E->e->u.raw.cursor_is_raw) {
      /* Restore configured subenc before finalizing */
      E->e->u.raw.file[0].subenc = saved_subenc;
      if (_GD_FinalizeChunk(D, E)) {
        dreturn("%i", 1);
        return 1;
      }
    }
  }

  /* Restore configured subenc (may have been set to 0 for cursor) */
  E->e->u.raw.file[0].subenc = saved_subenc;
  E->e->u.raw.cursor_is_raw = 0;

  /* Free old filename so _GD_GenericName regenerates it for the new chunk */
  free(E->e->u.raw.file[0].name);
  E->e->u.raw.file[0].name = NULL;

  chunk_filebase = _GD_ChunkFilebase(D, E, chunk_frame);
  if (chunk_filebase == NULL) {
    dreturn("%i", 1);
    return 1;
  }

  if (mode & GD_FILE_WRITE) {
    /* Ensure the chunk directory exists */
    if (_GD_EnsureChunkDir(D, E)) {
      free(chunk_filebase);
      dreturn("%i", 1);
      return 1;
    }

    /* Zero-fill any missing chunks between the last written chunk and
     * the target, so reads across the gap return zeros. */
    _GD_FillChunkGaps(D, E, chunk_frame);

    if (_GD_ef[saved_subenc].flags & GD_EF_OOP) {
      /* OOP encoding: use raw cursor, finalize on chunk switch.
       * Decompress any existing finalized chunk to raw first. */
      _GD_UnencodeChunk(D, E, chunk_filebase, saved_subenc);

      E->e->u.raw.file[0].subenc = GD_ENC_RAW;
      if (_GD_InitRawIO(D, E, chunk_filebase, -1, NULL, 0, mode,
            _GD_FileSwapBytes(D, E)))
      {
        E->e->u.raw.file[0].subenc = saved_subenc;
        free(chunk_filebase);
        dreturn("%i", 1);
        return 1;
      }
      E->e->u.raw.file[0].subenc = GD_ENC_RAW;  /* re-assert */
      E->e->u.raw.cursor_is_raw = 1;
    } else {
      /* Non-OOP encoding: open directly, encoding handles in-place writes */
      E->e->u.raw.file[0].subenc = saved_subenc;
      if (_GD_InitRawIO(D, E, chunk_filebase, -1, NULL, 0, mode,
            _GD_FileSwapBytes(D, E)))
      {
        free(chunk_filebase);
        dreturn("%i", 1);
        return 1;
      }
    }
  } else {
    /* Read path: prefer raw (cursor / crash survivor) over encoded
     * (finalized).  Probe with stat first to avoid verbose open errors. */
    have_raw = (gd_StatAt(D, frag->dirfd, chunk_filebase, &st, 0) == 0);

    if (have_raw) {
      E->e->u.raw.file[0].subenc = GD_ENC_RAW;
      if (_GD_InitRawIO(D, E, chunk_filebase, -1, NULL, 0, mode,
            _GD_FileSwapBytes(D, E)))
      {
        E->e->u.raw.file[0].subenc = saved_subenc;
        free(chunk_filebase);
        dreturn("%i", 1);
        return 1;
      }
      E->e->u.raw.cursor_is_raw = 1;
    } else {
      /* Probe for the encoded file before trying _GD_InitRawIO, to avoid
       * verbose error messages for the expected gap case. */
      int have_enc = 0;
      struct gd_raw_file_ enc_probe;

      memset(&enc_probe, 0, sizeof(enc_probe));
      enc_probe.subenc = saved_subenc;
      enc_probe.idata = -1;

      if (!_GD_MissingFramework(saved_subenc, GD_EF_NAME) &&
          !(*_GD_ef[saved_subenc].name)(D, (const char*)frag->enc_data,
            &enc_probe, chunk_filebase, 0, 0))
      {
        have_enc = (gd_StatAt(D, frag->dirfd, enc_probe.name, &st, 0) == 0);
        free(enc_probe.name);
      }

      if (!have_enc) {
        /* Neither raw nor encoded exists -- it's a gap */
        free(chunk_filebase);
        E->e->u.raw.active_chunk = chunk_frame;
        *s0 -= chunk_frame * spf;
        dreturn("%i", 0);
        return 0;
      }

      /* Try configured encoding (finalized chunk) */
      E->e->u.raw.file[0].subenc = saved_subenc;
      if (_GD_InitRawIO(D, E, chunk_filebase, -1, NULL, 0, mode,
            _GD_FileSwapBytes(D, E)))
      {
        free(chunk_filebase);
        dreturn("%i", 1);
        return 1;
      }
    }
  }

  free(chunk_filebase);
  E->e->u.raw.active_chunk = chunk_frame;
  *s0 -= chunk_frame * spf;

  /* Update chunk cache */
  if (E->e->u.raw.first_chunk < 0 || chunk_frame < E->e->u.raw.first_chunk)
    E->e->u.raw.first_chunk = chunk_frame;
  if (chunk_frame > E->e->u.raw.last_chunk)
    E->e->u.raw.last_chunk = chunk_frame;

  dreturn("%i", 0);
  return 0;
}

/* This function assumes that the new encoding has no fragment->enc_data. */
static void _GD_RecodeFragment(DIRFILE* D, unsigned long encoding, int fragment,
    int move)
{
  unsigned int i;

  dtrace("%p, %lx, %i, %i", D, encoding, fragment, move);

  /* check protection */
  if (D->fragment[fragment].protection & GD_PROTECT_FORMAT) {
    _GD_SetError(D, GD_E_PROTECTED, GD_E_PROTECTED_FORMAT, NULL, 0,
        D->fragment[fragment].cname);
    dreturnvoid();
    return;
  }

  if (move && encoding != D->fragment[fragment].encoding) {
    for (i = 0; i < D->n_entries; ++i)
      if (D->entry[i]->fragment_index == fragment &&
          D->entry[i]->field_type == GD_RAW_ENTRY)
      {
        if (_GD_TransformField(D, D->entry[i], encoding,
              D->fragment[fragment].byte_sex,
              D->fragment[fragment].frame_offset,
              D->fragment[fragment].chunk_size, fragment, NULL))
          break;
      }

    if (D->error) {
      dreturnvoid();
      return;
    }
  } else {
    for (i = 0; i < D->n_entries; ++i)
      if (D->entry[i]->fragment_index == fragment &&
          D->entry[i]->field_type == GD_RAW_ENTRY)
      {
        _GD_FiniRawIO(D, D->entry[i], fragment, GD_FINIRAW_KEEP);
        free(D->entry[i]->e->u.raw.file[0].name);
        D->entry[i]->e->u.raw.file[0].name = NULL;
        D->entry[i]->e->u.raw.file[0].subenc = GD_ENC_UNKNOWN;
      }
  }

  free(D->fragment[fragment].enc_data);
  D->fragment[fragment].enc_data = NULL;
  D->fragment[fragment].encoding = encoding;
  D->fragment[fragment].modified = 1;
  D->flags &= ~GD_HAVE_VERSION;

  dreturnvoid();
}

int gd_alter_encoding(DIRFILE* D, unsigned long encoding, int fragment,
    int move)
{
  int i;

  dtrace("%p, %lu, %i, %i", D, (unsigned long)encoding, fragment, move);

  GD_RETURN_ERR_IF_INVALID(D);

  if ((D->flags & GD_ACCMODE) != GD_RDWR)
    _GD_SetError(D, GD_E_ACCMODE, 0, NULL, 0, NULL);
  else if (fragment < GD_ALL_FRAGMENTS || fragment >= D->n_fragment)
    _GD_SetError(D, GD_E_BAD_INDEX, 0, NULL, 0, NULL);
  else if (!_GD_EncodingUnderstood(encoding))
    _GD_SetError(D, GD_E_UNKNOWN_ENCODING, GD_E_UNENC_TARGET, NULL, 0, NULL);
  else if (fragment == GD_ALL_FRAGMENTS) {
    for (i = 0; i < D->n_fragment; ++i) {
      _GD_RecodeFragment(D, encoding, i, move);

      if (D->error)
        break;
    }
  } else
    _GD_RecodeFragment(D, encoding, fragment, move);

  GD_RETURN_ERROR(D);
}

unsigned long gd_encoding(DIRFILE* D, int fragment) gd_nothrow
{
  unsigned long reported_encoding = GD_ENC_UNSUPPORTED;
  unsigned int i;

  dtrace("%p, %i", D, fragment);

  GD_RETURN_IF_INVALID(D, "%i", 0);

  if (fragment < 0 || fragment >= D->n_fragment) {
    _GD_SetError(D, GD_E_BAD_INDEX, 0, NULL, 0, NULL);
    dreturn("%i", 0);
    return 0;
  }

  /* Attempt to figure out the encoding, if it's not known */
  if (D->fragment[fragment].encoding == GD_AUTO_ENCODED) {
    /* locate a RAW field in this fragment */
    for (i = 0; i < D->n_entries; ++i)
      if (D->entry[i]->fragment_index == fragment &&
          D->entry[i]->field_type == GD_RAW_ENTRY)
      {
        D->fragment[fragment].encoding =
          _GD_ResolveEncoding(D, D->entry[i]->e->u.raw.filebase,
              (const char*)D->fragment[fragment].enc_data, GD_AUTO_ENCODED,
              D->fragment[fragment].dirfd, D->entry[i]->e->u.raw.file);

        if (D->fragment[fragment].encoding != GD_AUTO_ENCODED)
          break;
      }
  }

  if (D->fragment[fragment].encoding != GD_AUTO_ENCODED)
    reported_encoding = D->fragment[fragment].encoding;

  dreturn("%lx", (unsigned long)reported_encoding);
  return reported_encoding;
}

/* report whether a particular encoding is supported */
int gd_encoding_support(unsigned long encoding) gd_nothrow
{
  int i;

  const unsigned int read_funcs = GD_EF_NAME | GD_EF_OPEN | GD_EF_CLOSE |
    GD_EF_SEEK | GD_EF_READ | GD_EF_SIZE;
  const unsigned int write_funcs = read_funcs | GD_EF_WRITE | GD_EF_SYNC |
    GD_EF_MOVE | GD_EF_UNLINK;

  dtrace("0x%lX", encoding);

  /* make sure we have a valid encoding */
  if (!_GD_EncodingUnderstood(encoding)) {
    dreturn("%i", GD_E_UNKNOWN_ENCODING);
    return GD_E_UNKNOWN_ENCODING;
  }

  /* spin up ltdl if needed */
  _GD_InitialiseFramework();

  /* Loop through valid subencodings checking for write support */
  for (i = 0; _GD_ef[i].scheme != GD_ENC_UNSUPPORTED; i++)
    if (_GD_ef[i].scheme == encoding) {
      if (!_GD_MissingFramework(i, write_funcs)) {
        dreturn("%i", GD_RDWR);
        return GD_RDWR;
      }
    }

  /* No write support; try read support */
  for (i = 0; _GD_ef[i].scheme != GD_ENC_UNSUPPORTED; i++)
    if (_GD_ef[i].scheme == encoding) {
      if (!_GD_MissingFramework(i, read_funcs)) {
        dreturn("%i", GD_RDONLY);
        return GD_RDONLY;
      }
    }

  /* nope */
  dreturn("%i", GD_E_UNSUPPORTED);
  return GD_E_UNSUPPORTED;
}

/* This is basically the non-existant POSIX funcion mkstempat.  There are two
 * approaches we could take here:
 * 1) fchdir to dirfd, use mkstemp to grab a file descriptor; fchdir back to
 *    cwd, but this isn't thread-safe, so we're stuck with:
 * 2) use mktemp to generate a "unique" file name, and then try to openat it
 *    exclusively; repeat as necessary.
 */
int _GD_MakeTempFile(const DIRFILE *D gd_unused_d, int dirfd, char *tmpl)
{
  int fd = -1;
  char *tmp = strdup(tmpl);

  dtrace("%p, %i, \"%s\"", D, dirfd, tmpl);

  if (!tmp) {
    dreturn("%i", -1);
    return -1;
  }

  do {
    strcpy(tmpl, tmp);
    mktemp(tmpl);
    if (tmpl[0] == 0) {
      free(tmp);
      dreturn("%i", -1);
      return -1;
    }

    fd = gd_OpenAt(D, dirfd, tmpl, O_RDWR | O_CREAT | O_EXCL, 0666);
  } while (errno == EEXIST);

  free(tmp);

  dreturn("%i", fd);
  return fd;
}

/* Like _GD_MakeTempFile, but creates a temporary directory.  Uses mktemp +
 * mkdir in a retry loop rather than mkdtemp, which is not universally
 * available (e.g. macOS under strict C99). */
int _GD_MakeTempDir(const DIRFILE *D gd_unused_d, int dirfd, char *tmpl)
{
  int ret;
  char *tmp = strdup(tmpl);

  dtrace("%p, %i, \"%s\"", D, dirfd, tmpl);

  if (!tmp) {
    dreturn("%i", -1);
    return -1;
  }

  do {
    strcpy(tmpl, tmp);
    mktemp(tmpl);
    if (tmpl[0] == 0) {
      free(tmp);
      dreturn("%i", -1);
      return -1;
    }

    ret = gd_MkdirAt(D, dirfd, tmpl, 0777);
  } while (ret && errno == EEXIST);

  free(tmp);

  dreturn("%i", ret);
  return ret;
}

int _GD_GenericUnlink(int dirfd, struct gd_raw_file_* file)
{
  int r;

  dtrace("%i, %p", dirfd, file);

  r = gd_UnlinkAt(file->D, dirfd, file->name, 0);

  dreturn("%i", r);
  return r;
}

int _GD_GenericMove(int olddirfd, struct gd_raw_file_ *restrict file,
    int newdirfd, char *restrict new_path)
{
  int r, rename_errno;

  dtrace("%i, %p, %i, \"%s\"", olddirfd, file, newdirfd, new_path);

  r = gd_RenameAt(file->D, olddirfd, file->name, newdirfd, new_path);

  rename_errno = errno;

  if (!r) {
    free(file->name);
    file->name = new_path;
  } else
    free(new_path);

  errno = rename_errno;

  dreturn("%i", r);
  return r;
}

/* Callback for _GD_ChunkUnlink: unlink each chunk file */
static int _GD_ChunkUnlinkCb(DIRFILE *D, gd_entry_t *E,
    off64_t chunk_frame gd_unused_, const char *filename, void *data gd_unused_)
{
  int dirfd = D->fragment[E->fragment_index].dirfd;
  return gd_UnlinkAt(D, dirfd, filename, 0) ? -1 : 0;
}

/* Unlink all chunk files for a chunked field, then remove the directory. */
int _GD_ChunkUnlink(DIRFILE *D, gd_entry_t *E)
{
  int dirfd = D->fragment[E->fragment_index].dirfd;
  char *path;
  int ret;

  dtrace("%p, %p", D, E);

  ret = _GD_ForEachChunk(D, E, _GD_ChunkUnlinkCb, NULL);

  /* Remove the now-empty chunk directory */
  path = _GD_MakeFullPathOnly(D, dirfd, E->e->u.raw.filebase);
  if (path != NULL) {
    rmdir(path);
    free(path);
  }

  E->e->u.raw.first_chunk = -1;
  E->e->u.raw.last_chunk = -1;

  dreturn("%i", ret);
  return ret;
}

/* Move (rename) all chunk files for a chunked field by renaming the
 * chunk directory itself. */
int _GD_ChunkMove(DIRFILE *D, gd_entry_t *E, int newdirfd,
    const char *new_filebase)
{
  int olddirfd = D->fragment[E->fragment_index].dirfd;
  int ret;

  dtrace("%p, %p, %i, \"%s\"", D, E, newdirfd, new_filebase);

  ret = gd_RenameAt(D, olddirfd, E->e->u.raw.filebase, newdirfd,
      new_filebase);

  E->e->u.raw.first_chunk = -1;
  E->e->u.raw.last_chunk = -1;

  dreturn("%i", ret ? -1 : 0);
  return ret ? -1 : 0;
}

struct chunk_expire_data {
  off64_t threshold;  /* delete chunks with frame offset < threshold */
};

/* Callback for _GD_ChunkExpire: unlink chunks before threshold */
static int _GD_ChunkExpireCb(DIRFILE *D, gd_entry_t *E,
    off64_t chunk_frame, const char *filename, void *data)
{
  struct chunk_expire_data *t = (struct chunk_expire_data *)data;
  int dirfd = D->fragment[E->fragment_index].dirfd;

  if (chunk_frame < t->threshold)
    return gd_UnlinkAt(D, dirfd, filename, 0) ? -1 : 0;

  return 0;
}

/* Remove chunk files with frame offset < threshold.
 * Returns 0 on success, -1 on error. */
int _GD_ChunkExpire(DIRFILE *D, gd_entry_t *E, off64_t threshold)
{
  struct chunk_expire_data t;
  int ret;

  dtrace("%p, %p, %" PRId64, D, E, (int64_t)threshold);

  t.threshold = threshold;

  ret = _GD_ForEachChunk(D, E, _GD_ChunkExpireCb, &t);

  /* Invalidate cache -- first_chunk may have changed */
  E->e->u.raw.first_chunk = -1;
  E->e->u.raw.last_chunk = -1;

  dreturn("%i", ret);
  return ret;
}

/* This function does nothing */
int _GD_NopSync(struct gd_raw_file_ *file gd_unused_)
{
  dtrace("<unused>");

  dreturn("%i", 0);
  return 0;
}
/* vim: ts=2 sw=2 et tw=80
*/
