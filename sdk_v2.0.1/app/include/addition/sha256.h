#ifndef _SHA256_H
#define _SHA256_H

#include "sdk_include.h"

#define IOTGO_USING_MBEDTLS_SHA256

#ifndef IOTGO_USING_MBEDTLS_SHA256

typedef struct
{
    uint32 total[2];
    uint32 state[8];
    uint8 buffer[64];
}
sha256_context;

void ICACHE_FLASH_ATTR sha256_starts( sha256_context *ctx );
void ICACHE_FLASH_ATTR sha256_update( sha256_context *ctx, uint8 *input, uint32 length );
void ICACHE_FLASH_ATTR sha256_finish( sha256_context *ctx, uint8 digest[32] );

#else /* #ifndef IOTGO_USING_MBEDTLS_SHA256 */

/**
 * \brief          SHA-256 context structure
 */
typedef struct
{
    uint32_t total[2];          /*!< number of bytes processed  */
    uint32_t state[8];          /*!< intermediate digest state  */
    unsigned char buffer[64];   /*!< data block being processed */
    int is224;                  /*!< 0 => SHA-256, else SHA-224 */
}
mbedtls_sha256_context;

typedef mbedtls_sha256_context sha256_context;


/**
 * \brief          SHA-256 context setup
 *
 * \param ctx      context to be initialized
 * \param is224    0 = use SHA256, 1 = use SHA224
 */
void ICACHE_FLASH_ATTR mbedtls_sha256_starts( mbedtls_sha256_context *ctx, int is224 );

/**
 * \brief          SHA-256 process buffer
 *
 * \param ctx      SHA-256 context
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 */
void ICACHE_FLASH_ATTR mbedtls_sha256_update( mbedtls_sha256_context *ctx, const unsigned char *input,
                    size_t ilen );

/**
 * \brief          SHA-256 final digest
 *
 * \param ctx      SHA-256 context
 * \param output   SHA-224/256 checksum result
 */
void ICACHE_FLASH_ATTR mbedtls_sha256_finish( mbedtls_sha256_context *ctx, unsigned char output[32] );

#define sha256_starts(ctx)                  mbedtls_sha256_starts((ctx), 0)
#define sha256_update(ctx, input, length)   mbedtls_sha256_update((ctx), (input), (length))
#define sha256_finish(ctx, digest)          mbedtls_sha256_finish((ctx), (digest))

#endif /* #ifndef IOTGO_USING_MBEDTLS_SHA256 */

#endif /* sha256.h */

