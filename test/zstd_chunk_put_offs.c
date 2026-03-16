/* Chunked encoding test -- generated stub */
#define ENC_SUFFIX ".zst"
#define ENC_ENCODED GD_ZSTD_ENCODED
#define ENC_CHUNK_SIZE 10000

#include "test.h"

#if !defined TEST_ZSTD || !defined USE_ZSTD
#define ENC_SKIP_TEST 1
#endif

#include "enc_put_offs.c"
