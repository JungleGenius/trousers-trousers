
/*
 * Licensed Materials - Property of IBM
 *
 * trousers - An open source TCG Software Stack
 *
 * (C) Copyright International Business Machines Corp. 2004-2006
 *
 */

/*
 * symmetric.c - openssl TSS crypto routines
 *
 * Kent Yoder <shpedoikal@gmail.com>
 *
 */

#include <string.h>

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include "trousers/tss.h"
#include "trousers/trousers.h"
#include "spi_internal_types.h"
#include "spi_utils.h"
#include "tsplog.h"


/*
 * Hopefully this will make the code clearer since
 * OpenSSL returns 1 on success
 */
#define EVP_SUCCESS 1

TSS_RESULT
Trspi_Encrypt_ECB(UINT16 alg, BYTE *key, BYTE *in, UINT32 in_len, BYTE *out,
		  UINT32 *out_len)
{
	TSS_RESULT result = TSS_SUCCESS;
	EVP_CIPHER_CTX ctx;
	UINT32 tmp;

	switch (alg) {
		case TSS_ALG_AES:
			break;
		default:
			result = TSPERR(TSS_E_INTERNAL_ERROR);
			goto done;
			break;
	}

	EVP_CIPHER_CTX_init(&ctx);

	if (!EVP_EncryptInit(&ctx, EVP_aes_256_ecb(), key, NULL)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}

	if (*out_len < in_len + EVP_CIPHER_CTX_block_size(&ctx) - 1) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		goto done;
	}

	if (!EVP_EncryptUpdate(&ctx, out, (int *)out_len, in, in_len)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}

	if (!EVP_EncryptFinal(&ctx, out + *out_len, (int *)&tmp)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}
	*out_len += tmp;
done:
	EVP_CIPHER_CTX_cleanup(&ctx);
	return result;
}

TSS_RESULT
Trspi_Decrypt_ECB(UINT16 alg, BYTE *key, BYTE *in, UINT32 in_len, BYTE *out,
		  UINT32 *out_len)
{
	TSS_RESULT result = TSS_SUCCESS;
	EVP_CIPHER_CTX ctx;
	UINT32 tmp;

	switch (alg) {
		case TSS_ALG_AES:
			break;
		default:
			result = TSPERR(TSS_E_INTERNAL_ERROR);
			goto done;
			break;
	}

	EVP_CIPHER_CTX_init(&ctx);

	if (!EVP_DecryptInit(&ctx, EVP_aes_256_ecb(), key, NULL)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}

	if (!EVP_DecryptUpdate(&ctx, out, (int *)out_len, in, in_len)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}

	if (!EVP_DecryptFinal(&ctx, out + *out_len, (int *)&tmp)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}
	*out_len += tmp;
done:
	EVP_CIPHER_CTX_cleanup(&ctx);
	return result;
}

TSS_RESULT
Trspi_SymEncrypt(UINT16 alg, BYTE mode, BYTE *key, BYTE *iv, BYTE *in, UINT32 in_len, BYTE *out,
		 UINT32 *out_len)
{
	TSS_RESULT result = TSS_SUCCESS;
	EVP_CIPHER_CTX ctx;
	EVP_CIPHER *cipher;
	BYTE *def_iv = NULL, *outiv_ptr;
	UINT32 tmp;
	int iv_len, outiv_len;

	if (*out_len > INT_MAX)
		outiv_len = INT_MAX;
	else
		outiv_len = *(int *)out_len;

	/* TPM 1.1 had no defines for symmetric encryption modes, must use CBC */
	switch (mode) {
		case TR_SYM_MODE_CBC:
		case TCPA_ES_NONE:
		case TSS_ES_NONE:
			break;
		default:
			LogDebug("Invalid mode in doing symmetric decryption");
			return TSPERR(TSS_E_INTERNAL_ERROR);
	}

	switch (alg) {
		case TSS_ALG_AES:
		case TCPA_ALG_AES:
			cipher = (EVP_CIPHER *)EVP_aes_128_cbc();
			break;
		case TSS_ALG_DES:
		case TCPA_ALG_DES:
			cipher = (EVP_CIPHER *)EVP_des_cbc();
			break;
		case TSS_ALG_3DES:
		case TCPA_ALG_3DES:
			cipher = (EVP_CIPHER *)EVP_des_ede3_cbc();
			break;
		default:
			return TSPERR(TSS_E_INTERNAL_ERROR);
			break;
	}

	EVP_CIPHER_CTX_init(&ctx);

	/* If the iv passed in is NULL, create a new random iv and prepend it to the ciphertext */
	iv_len = EVP_CIPHER_iv_length(cipher);
	if (iv == NULL) {
		def_iv = malloc(iv_len);
		if (def_iv == NULL) {
			LogError("malloc of %d bytes failed.", iv_len);
			return TSPERR(TSS_E_OUTOFMEMORY);
		}
		RAND_bytes(def_iv, iv_len);

		memcpy(out, def_iv, iv_len);
		outiv_ptr = &out[iv_len];
		outiv_len -= iv_len;
	} else {
		def_iv = iv;
		outiv_ptr = out;
	}

	if (!EVP_EncryptInit(&ctx, (const EVP_CIPHER *)cipher, key, def_iv)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}

	if ((UINT32)outiv_len < in_len + (EVP_CIPHER_CTX_block_size(&ctx) * 2) - 1) {
		LogDebug("Not enough space to do symmetric encryption");
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		goto done;
	}

	if (!EVP_EncryptUpdate(&ctx, outiv_ptr, &outiv_len, in, in_len)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}

	if (!EVP_EncryptFinal(&ctx, outiv_ptr + outiv_len, (int *)&tmp)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}

	outiv_len += tmp;
	*out_len = outiv_len;
done:
	if (def_iv != iv) {
		*out_len += iv_len;
		free(def_iv);
	}
	EVP_CIPHER_CTX_cleanup(&ctx);
	return result;
}

TSS_RESULT
Trspi_SymDecrypt(UINT16 alg, BYTE mode, BYTE *key, BYTE *iv, BYTE *in, UINT32 in_len, BYTE *out,
		 UINT32 *out_len)
{
	TSS_RESULT result = TSS_SUCCESS;
	EVP_CIPHER_CTX ctx;
	EVP_CIPHER *cipher;
	BYTE *def_iv = NULL, *iniv_ptr;
	UINT32 tmp;
	int iv_len, iniv_len;

	if (in_len > INT_MAX)
		return TSS_E_BAD_PARAMETER;

	/* TPM 1.1 had no defines for symmetric encryption modes, must use CBC */
	switch (mode) {
		case TR_SYM_MODE_CBC:
		case TCPA_ES_NONE:
		case TSS_ES_NONE:
			break;
		default:
			LogDebug("Invalid mode in doing symmetric decryption");
			return TSPERR(TSS_E_INTERNAL_ERROR);
	}

	switch (alg) {
		case TSS_ALG_AES:
		case TCPA_ALG_AES:
			cipher = (EVP_CIPHER *)EVP_aes_128_cbc();
			break;
		case TSS_ALG_DES:
		case TCPA_ALG_DES:
			cipher = (EVP_CIPHER *)EVP_des_cbc();
			break;
		case TSS_ALG_3DES:
		case TCPA_ALG_3DES:
			cipher = (EVP_CIPHER *)EVP_des_ede3_cbc();
			break;
		default:
			return TSPERR(TSS_E_INTERNAL_ERROR);
			break;
	}

	EVP_CIPHER_CTX_init(&ctx);

	/* If the iv is NULL, assume that its prepended to the ciphertext */
	if (iv == NULL) {
		iv_len = EVP_CIPHER_iv_length(cipher);
		def_iv = malloc(iv_len);
		if (def_iv == NULL) {
			LogError("malloc of %d bytes failed.", iv_len);
			return TSPERR(TSS_E_OUTOFMEMORY);
		}

		memcpy(def_iv, in, iv_len);
		iniv_ptr = &in[iv_len];
		iniv_len = in_len - iv_len;
	} else {
		def_iv = iv;
		iniv_ptr = in;
		iniv_len = in_len;
	}

	if (!EVP_DecryptInit(&ctx, cipher, key, def_iv)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}

	if (!EVP_DecryptUpdate(&ctx, out, (int *)out_len, iniv_ptr, iniv_len)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}

	if (!EVP_DecryptFinal(&ctx, out + *out_len, (int *)&tmp)) {
		result = TSPERR(TSS_E_INTERNAL_ERROR);
		DEBUG_print_openssl_errors();
		goto done;
	}

	*out_len += tmp;
done:
	if (def_iv != iv)
		free(def_iv);
	EVP_CIPHER_CTX_cleanup(&ctx);
	return result;
}