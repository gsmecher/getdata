/* Copyright (C) 2026 G. Smecher
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
/* Stress test: chunked zstd writes and reads with varying parameters.
 *
 * Tests round-trip correctness across chunk boundaries, close/reopen cycles,
 * FRAMEOFFSET interaction, multiple data types, and derived fields.
 */
#include "test.h"

#if !defined TEST_ZSTD || !defined USE_ZSTD
int main(void) { return 77; }
#else

/* Fill buffer with deterministic data keyed by sample offset, so we can
 * verify reads independently of write order. */
static void fill_expected(uint8_t *buf, int start_sample, int count) {
  int i;
  for (i = 0; i < count; i++)
    buf[i] = (uint8_t)((start_sample + i) * 7 + 0x55);
}

static int test_basic_roundtrip(int chunk_size, int spf, int n_samples,
    int frame_offset)
{
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  char fmtbuf[256];
  uint8_t *wr, *rd, *expected;
  int i, n, m, r = 0;
  off_t nf, expect_nf;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  snprintf(fmtbuf, sizeof(fmtbuf),
      "/ENCODING zstd\n/CHUNK %d\n/FRAMEOFFSET %d\ndata RAW UINT8 %d\n",
      chunk_size, frame_offset, spf);
  MAKERAWFILE(format, fmtbuf, strlen(fmtbuf));

  wr = malloc((size_t)n_samples);
  rd = malloc((size_t)n_samples);
  expected = malloc((size_t)n_samples);
  if (!wr || !rd || !expected) { r = 1; goto done; }

  fill_expected(wr, frame_offset * spf, n_samples);

  /* Write */
  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);
  n = gd_putdata(D, "data", frame_offset, 0, 0, n_samples, GD_UINT8, wr);
  if (gd_error(D) != GD_E_OK || n != n_samples) {
    fprintf(stderr, "  roundtrip(chunk=%d,spf=%d,n=%d,fo=%d): "
        "write failed: n=%d err=%d\n",
        chunk_size, spf, n_samples, frame_offset, n, gd_error(D));
    r = 1;
  }
  gd_close(D);

  /* Reopen read-only and read back */
  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);

  /* Verify nframes */
  nf = gd_nframes(D);
  expect_nf = frame_offset + (n_samples + spf - 1) / spf;
  if (nf != expect_nf) {
    fprintf(stderr, "  roundtrip(chunk=%d,spf=%d,n=%d,fo=%d): "
        "nframes=%ld expected=%ld\n",
        chunk_size, spf, n_samples, frame_offset, (long)nf,
        (long)expect_nf);
    r = 1;
  }

  memset(rd, 0xAA, (size_t)n_samples);
  m = gd_getdata(D, "data", frame_offset, 0, 0, n_samples, GD_UINT8, rd);
  if (gd_error(D) != GD_E_OK || m != n_samples) {
    fprintf(stderr, "  roundtrip(chunk=%d,spf=%d,n=%d,fo=%d): "
        "read failed: m=%d err=%d\n",
        chunk_size, spf, n_samples, frame_offset, m, gd_error(D));
    r = 1;
  }

  fill_expected(expected, frame_offset * spf, n_samples);
  for (i = 0; i < n_samples && i < m; i++) {
    if (rd[i] != expected[i]) {
      fprintf(stderr, "  roundtrip(chunk=%d,spf=%d,n=%d,fo=%d): "
          "mismatch at sample %d: got 0x%02x expected 0x%02x\n",
          chunk_size, spf, n_samples, frame_offset, i, rd[i], expected[i]);
      r = 1;
      break;
    }
  }

  gd_discard(D);

done:
  free(wr);
  free(rd);
  free(expected);
  rmdirfile();
  return r;
}

/* Test incremental appends across chunk boundaries with close/reopen cycles */
static int test_incremental_append(int chunk_size, int spf)
{
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  char fmtbuf[256];
  int total = chunk_size * spf * 5;  /* 5 chunks worth */
  int batch = spf * 3;               /* write 3 frames at a time */
  uint8_t batch_buf[1024];
  uint8_t *rd, *expected;
  int written = 0, this_batch, i, r = 0;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  snprintf(fmtbuf, sizeof(fmtbuf),
      "/ENCODING zstd\n/CHUNK %d\ndata RAW UINT8 %d\n",
      chunk_size, spf);
  MAKERAWFILE(format, fmtbuf, strlen(fmtbuf));

  /* Write in batches, closing and reopening between each */
  while (written < total) {
    this_batch = (total - written < batch) ? total - written : batch;
    if (this_batch > (int)sizeof(batch_buf)) this_batch = (int)sizeof(batch_buf);

    fill_expected(batch_buf, written, this_batch);

    D = gd_open(filedir, GD_RDWR | GD_VERBOSE);
    i = gd_putdata(D, "data", written / spf, written % spf, 0, this_batch,
        GD_UINT8, batch_buf);
    if (gd_error(D) != GD_E_OK || i != this_batch) {
      fprintf(stderr, "  incremental(chunk=%d,spf=%d): "
          "write at %d failed: n=%d err=%d\n",
          chunk_size, spf, written, i, gd_error(D));
      gd_discard(D);
      r = 1;
      goto done;
    }
    gd_close(D);
    written += this_batch;
  }

  /* Read back the entire dataset */
  rd = malloc((size_t)total);
  expected = malloc((size_t)total);
  if (!rd || !expected) { r = 1; goto done2; }

  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  i = gd_getdata(D, "data", 0, 0, 0, total, GD_UINT8, rd);
  if (gd_error(D) != GD_E_OK || i != total) {
    fprintf(stderr, "  incremental(chunk=%d,spf=%d): "
        "read failed: n=%d err=%d\n",
        chunk_size, spf, i, gd_error(D));
    r = 1;
  }

  fill_expected(expected, 0, total);
  for (i = 0; i < total; i++) {
    if (rd[i] != expected[i]) {
      fprintf(stderr, "  incremental(chunk=%d,spf=%d): "
          "mismatch at sample %d: got 0x%02x expected 0x%02x\n",
          chunk_size, spf, i, rd[i], expected[i]);
      r = 1;
      break;
    }
  }
  gd_discard(D);

done2:
  free(rd);
  free(expected);
done:
  rmdirfile();
  return r;
}

/* Test reading a sub-range that starts or ends exactly on chunk boundaries */
static int test_boundary_reads(int chunk_size, int spf)
{
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  char fmtbuf[256];
  int total = chunk_size * spf * 4;
  int chunk_samples = chunk_size * spf;
  int start, count, b, pos;
  uint8_t *wr, *rd, *expected;
  uint8_t val, exp;
  int i, r = 0;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  snprintf(fmtbuf, sizeof(fmtbuf),
      "/ENCODING zstd\n/CHUNK %d\ndata RAW UINT8 %d\n",
      chunk_size, spf);
  MAKERAWFILE(format, fmtbuf, strlen(fmtbuf));

  wr = malloc((size_t)total);
  rd = malloc((size_t)total);
  expected = malloc((size_t)total);
  if (!wr || !rd || !expected) { r = 1; goto done; }

  fill_expected(wr, 0, total);

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);
  gd_putdata(D, "data", 0, 0, 0, total, GD_UINT8, wr);
  gd_close(D);

  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);

  /* Read exactly one chunk */
  memset(rd, 0xAA, (size_t)total);
  i = gd_getdata(D, "data", chunk_size, 0, 0, chunk_samples, GD_UINT8, rd);
  fill_expected(expected, chunk_samples, chunk_samples);
  if (i != chunk_samples || memcmp(rd, expected, (size_t)chunk_samples) != 0) {
    fprintf(stderr, "  boundary(chunk=%d,spf=%d): "
        "exact-one-chunk read failed: n=%d\n",
        chunk_size, spf, i);
    r = 1;
  }

  /* Read starting at last sample of chunk 0 through first sample of chunk 2 */
  start = chunk_samples - 1;
  count = chunk_samples + 2;
  memset(rd, 0xAA, (size_t)total);
  i = gd_getdata(D, "data", start / spf, start % spf, 0, count,
      GD_UINT8, rd);
  fill_expected(expected, start, count);
  if (i != count || memcmp(rd, expected, (size_t)count) != 0) {
    fprintf(stderr, "  boundary(chunk=%d,spf=%d): "
        "cross-boundary read failed: n=%d expected=%d\n",
        chunk_size, spf, i, count);
    r = 1;
  }

  /* Read single sample at each chunk boundary */
  for (b = 0; b < 4; b++) {
    pos = b * chunk_samples;
    val = 0xAA;
    fill_expected(&exp, pos, 1);
    i = gd_getdata(D, "data", pos / spf, pos % spf, 0, 1, GD_UINT8, &val);
    if (i != 1 || val != exp) {
      fprintf(stderr, "  boundary(chunk=%d,spf=%d): "
          "single-sample at boundary %d failed: got 0x%02x expected 0x%02x\n",
          chunk_size, spf, b, val, exp);
      r = 1;
    }
  }

  gd_discard(D);

done:
  free(wr);
  free(rd);
  free(expected);
  rmdirfile();
  return r;
}

/* Test that a LINCOM derived field works correctly over chunked data */
static int test_derived_field(int chunk_size, int spf)
{
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  char fmtbuf[256];
  int total = chunk_size * spf * 3;
  uint8_t *wr;
  double *rd, exp;
  int i, r = 0;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  snprintf(fmtbuf, sizeof(fmtbuf),
      "/ENCODING zstd\n/CHUNK %d\n"
      "data RAW UINT8 %d\n"
      "scaled LINCOM data 2.0 1.0\n",
      chunk_size, spf);
  MAKERAWFILE(format, fmtbuf, strlen(fmtbuf));

  wr = malloc((size_t)total);
  rd = calloc((size_t)total, sizeof(double));
  if (!wr || !rd) { r = 1; goto done; }

  fill_expected(wr, 0, total);

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);
  gd_putdata(D, "data", 0, 0, 0, total, GD_UINT8, wr);
  gd_close(D);

  /* Read through the derived field */
  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  i = gd_getdata(D, "scaled", 0, 0, 0, total, GD_FLOAT64, rd);
  if (gd_error(D) != GD_E_OK || i != total) {
    fprintf(stderr, "  derived(chunk=%d,spf=%d): "
        "read failed: n=%d err=%d\n",
        chunk_size, spf, i, gd_error(D));
    r = 1;
  }

  for (i = 0; i < total; i++) {
    exp = (double)wr[i] * 2.0 + 1.0;
    if (rd[i] != exp) {
      fprintf(stderr, "  derived(chunk=%d,spf=%d): "
          "mismatch at sample %d: got %g expected %g\n",
          chunk_size, spf, i, rd[i], exp);
      r = 1;
      break;
    }
  }

  gd_discard(D);

done:
  free(wr);
  free(rd);
  rmdirfile();
  return r;
}

/* Test with FLOAT64 data type to verify multi-byte sample handling at chunk
 * boundaries */
static int test_float64_chunks(int chunk_size, int spf)
{
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  char fmtbuf[256];
  int total = chunk_size * spf * 3;
  double *wr, *rd;
  int n, m, r = 0;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  snprintf(fmtbuf, sizeof(fmtbuf),
      "/ENCODING zstd\n/CHUNK %d\ndata RAW FLOAT64 %d\n",
      chunk_size, spf);
  MAKERAWFILE(format, fmtbuf, strlen(fmtbuf));

  wr = malloc((size_t)total * sizeof(double));
  rd = malloc((size_t)total * sizeof(double));
  if (!wr || !rd) { r = 1; goto done; }

  for (n = 0; n < total; n++)
    wr[n] = (double)n * 1.5 + 0.25;

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);
  n = gd_putdata(D, "data", 0, 0, 0, total, GD_FLOAT64, wr);
  if (n != total) {
    fprintf(stderr, "  float64(chunk=%d,spf=%d): write=%d expected=%d\n",
        chunk_size, spf, n, total);
    r = 1;
  }
  gd_close(D);

  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  m = gd_getdata(D, "data", 0, 0, 0, total, GD_FLOAT64, rd);
  if (m != total) {
    fprintf(stderr, "  float64(chunk=%d,spf=%d): read=%d expected=%d\n",
        chunk_size, spf, m, total);
    r = 1;
  }

  for (n = 0; n < total && n < m; n++) {
    if (rd[n] != wr[n]) {
      fprintf(stderr, "  float64(chunk=%d,spf=%d): "
          "mismatch at %d: got %g expected %g\n",
          chunk_size, spf, n, rd[n], wr[n]);
      r = 1;
      break;
    }
  }

  gd_discard(D);

done:
  free(wr);
  free(rd);
  rmdirfile();
  return r;
}

/* Test write spanning many chunks in a single call */
static int test_wide_write(int chunk_size, int spf, int n_chunks)
{
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  char fmtbuf[256];
  int total = chunk_size * spf * n_chunks;
  uint8_t *wr, *rd;
  int n, m, r = 0;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  snprintf(fmtbuf, sizeof(fmtbuf),
      "/ENCODING zstd\n/CHUNK %d\ndata RAW UINT8 %d\n",
      chunk_size, spf);
  MAKERAWFILE(format, fmtbuf, strlen(fmtbuf));

  wr = malloc((size_t)total);
  rd = malloc((size_t)total);
  if (!wr || !rd) { r = 1; goto done; }

  fill_expected(wr, 0, total);

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);
  n = gd_putdata(D, "data", 0, 0, 0, total, GD_UINT8, wr);
  if (n != total) {
    fprintf(stderr, "  wide_write(chunk=%d,spf=%d,nchunks=%d): "
        "write=%d expected=%d\n", chunk_size, spf, n_chunks, n, total);
    r = 1;
  }
  gd_close(D);

  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  m = gd_getdata(D, "data", 0, 0, 0, total, GD_UINT8, rd);
  if (m != total || memcmp(wr, rd, (size_t)total) != 0) {
    fprintf(stderr, "  wide_write(chunk=%d,spf=%d,nchunks=%d): "
        "readback failed: m=%d\n", chunk_size, spf, n_chunks, m);
    r = 1;
  }
  gd_discard(D);

done:
  free(wr);
  free(rd);
  rmdirfile();
  return r;
}

/* Random read/write patterns */
static int test_random_access(int chunk_size, int spf, int n_ops)
{
  const char *filedir = "dirfile";
  const char *format = "dirfile/format";
  char fmtbuf[256];
  int total = chunk_size * spf * 10;
  uint8_t *reference, *init, *rd;
  uint8_t buf[16];
  int pos, len, j;
  int i, r = 0;
  DIRFILE *D;

  rmdirfile();
  mkdir(filedir, 0700);

  snprintf(fmtbuf, sizeof(fmtbuf),
      "/ENCODING zstd\n/CHUNK %d\ndata RAW UINT8 %d\n",
      chunk_size, spf);
  MAKERAWFILE(format, fmtbuf, strlen(fmtbuf));

  reference = calloc(1, (size_t)total);
  if (!reference) return 1;

  D = gd_open(filedir, GD_RDWR | GD_VERBOSE);

  /* First, write the entire range so all chunks exist */
  init = malloc((size_t)total);
  if (!init) { r = 1; goto done; }
  fill_expected(init, 0, total);
  memcpy(reference, init, (size_t)total);
  gd_putdata(D, "data", 0, 0, 0, total, GD_UINT8, init);
  free(init);
  if (gd_error(D) != GD_E_OK) {
    fprintf(stderr, "  random(chunk=%d,spf=%d): init write failed\n",
        chunk_size, spf);
    r = 1;
    goto done;
  }

  /* Random writes, tracking expected state */
  srand(0xDEADBEEF);
  for (i = 0; i < n_ops; i++) {
    pos = (int)((unsigned)rand() % (total - 16));
    len = (int)((unsigned)rand() % 16) + 1;

    for (j = 0; j < len; j++) {
      buf[j] = (uint8_t)(rand() & 0xFF);
      reference[pos + j] = buf[j];
    }

    gd_putdata(D, "data", pos / spf, pos % spf, 0, len, GD_UINT8, buf);
    if (gd_error(D) != GD_E_OK) {
      fprintf(stderr, "  random(chunk=%d,spf=%d): write at %d len %d "
          "failed err=%d\n", chunk_size, spf, pos, len, gd_error(D));
      r = 1;
      break;
    }
  }

  gd_close(D);

  /* Verify by reading everything back */
  D = gd_open(filedir, GD_RDONLY | GD_VERBOSE);
  rd = malloc((size_t)total);
  if (!rd) { r = 1; goto done; }
  i = gd_getdata(D, "data", 0, 0, 0, total, GD_UINT8, rd);
  if (i != total) {
    fprintf(stderr, "  random(chunk=%d,spf=%d): "
        "readback got %d expected %d\n", chunk_size, spf, i, total);
    r = 1;
  }
  for (i = 0; i < total; i++) {
    if (rd[i] != reference[i]) {
      fprintf(stderr, "  random(chunk=%d,spf=%d): "
          "mismatch at %d: got 0x%02x expected 0x%02x\n",
          chunk_size, spf, i, rd[i], reference[i]);
      r = 1;
      break;
    }
  }
  free(rd);

done:
  gd_discard(D);
  free(reference);
  rmdirfile();
  return r;
}

int main(void)
{
  int r = 0;

  /* Basic round-trip with various chunk sizes and SPF values */
  r |= test_basic_roundtrip(1, 1, 20, 0);
  r |= test_basic_roundtrip(2, 1, 20, 0);
  r |= test_basic_roundtrip(2, 4, 40, 0);
  r |= test_basic_roundtrip(3, 1, 30, 0);
  r |= test_basic_roundtrip(5, 8, 200, 0);
  r |= test_basic_roundtrip(10, 1, 100, 0);

  /* With FRAMEOFFSET */
  r |= test_basic_roundtrip(2, 1, 20, 5);
  r |= test_basic_roundtrip(2, 4, 40, 10);
  r |= test_basic_roundtrip(5, 1, 50, 3);

  /* Incremental appends with close/reopen */
  r |= test_incremental_append(2, 1);
  r |= test_incremental_append(2, 4);
  r |= test_incremental_append(3, 2);

  /* Chunk boundary edge cases */
  r |= test_boundary_reads(2, 1);
  r |= test_boundary_reads(2, 4);
  r |= test_boundary_reads(5, 1);

  /* Derived field over chunked data */
  r |= test_derived_field(2, 1);
  r |= test_derived_field(2, 4);

  /* FLOAT64 (multi-byte samples at chunk boundaries) */
  r |= test_float64_chunks(2, 1);
  r |= test_float64_chunks(2, 4);

  /* Wide writes spanning many chunks */
  r |= test_wide_write(2, 1, 10);
  r |= test_wide_write(2, 4, 8);
  r |= test_wide_write(1, 1, 20);

  /* Random access patterns */
  r |= test_random_access(2, 1, 100);
  r |= test_random_access(3, 4, 100);

  return r;
}
#endif
