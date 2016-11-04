#ifndef ENCODE_HH
#define ENCODE_HH

#include <vector>
#include <string>
#include "cryptopp/aes.h"

#define AES_BLOCKSIZE CryptoPP::AES::BLOCKSIZE

/*
 *  Encodes secret into shares using Shamir's secret sharing
 */
char** ShamirsSecretShare(int threshold, int nShares, std::string data,
		   std::string seed, size_t *frag_length);
/*
 * Decodes shares into secret using Shamir's secret sharing
 */
char *ShamirsSecretRecover(int threshold, std::vector<std::string> fragments);

/*
 *  Encodes secret into shares using XOR secret sharing
 */
char** XORSecretShare(int nShares, std::string data,
		      std::string seed, size_t *frag_length);
/*
 * Decodes shares into secret using XOR secret sharing
 */
char *XORSecretRecover(std::vector<std::string> fragments);

/*
 * AES Encryption
 */
std::string AESEncrypt(std::string pt, std::string key_str);

/*
 * AES Decryption
 */
std::string AESDecrypt (std::string ct, std::string key_str);

#endif
