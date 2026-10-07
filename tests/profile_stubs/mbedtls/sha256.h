#pragma once
#include <openssl/evp.h>
#include <stddef.h>
typedef struct { EVP_MD_CTX *ctx; } mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context *s){s->ctx=EVP_MD_CTX_new();}
static inline int mbedtls_sha256_starts(mbedtls_sha256_context *s,int is224){return s->ctx&&!is224&&EVP_DigestInit_ex(s->ctx,EVP_sha256(),NULL)==1?0:-1;}
static inline int mbedtls_sha256_update(mbedtls_sha256_context *s,const unsigned char *b,size_t n){return EVP_DigestUpdate(s->ctx,b,n)==1?0:-1;}
static inline int mbedtls_sha256_finish(mbedtls_sha256_context *s,unsigned char b[32]){unsigned n=0;return EVP_DigestFinal_ex(s->ctx,b,&n)==1&&n==32?0:-1;}
static inline void mbedtls_sha256_free(mbedtls_sha256_context *s){EVP_MD_CTX_free(s->ctx);}
