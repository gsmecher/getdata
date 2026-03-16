/* Chunked encoding test -- generated stub */
#define ENC_SUFFIX ".gz"
#define ENC_ENCODED GD_GZIP_ENCODED
#define ENC_CHUNK_SIZE 10000

#include "test.h"

#if !defined TEST_GZIP || !defined USE_GZIP
#define ENC_SKIP_TEST 1
#endif

#include "enc_put_offs.c"
