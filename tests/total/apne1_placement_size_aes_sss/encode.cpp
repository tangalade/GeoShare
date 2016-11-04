#define _CRT_SECURE_NO_DEPRECATE
#define CRYPTOPP_DEFAULT_NO_DLL
#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include "cryptopp/dll.h"
#include "cryptopp/md5.h"
#include "cryptopp/ripemd.h"
#include "cryptopp/rng.h"
#include "cryptopp/gzip.h"
#include "cryptopp/default.h"
#include "cryptopp/randpool.h"
#include "cryptopp/ida.h"
#include "cryptopp/base64.h"
#include "cryptopp/socketft.h"
#include "cryptopp/wait.h"
#include "cryptopp/factory.h"
#include "cryptopp/whrlpool.h"
#include "cryptopp/tiger.h"
#include "cryptopp/pwdbased.h"
#include "cryptopp/bench.h"
#include "cryptopp/randpool.h"
#include "cryptopp/hex.h"
#include <iostream>
#include <sstream>
#include <time.h>
#include <stdio.h>
#include <cstdlib>
#include <sys/poll.h>
#include <random>
#include <stdint.h>
#include <vector>
#include <chrono>

//#ifdef CRYPTOPP_WIN32_AVAILABLE
//#include <windows.h>
//#endif
USING_NAMESPACE(CryptoPP)
//USING_NAMESPACE(std)

#include <string>

#define AES_ITERATIONS 100000

/*
 * Shamir's secret sharing
 */
char** ShamirsSecretShare(int threshold, int nShares, std::string data,
		   std::string seed, size_t *frag_length)
{

  assert(nShares<=1000);
  AutoSeededRandomPool rng;
  rng.IncorporateEntropy((byte *)seed.c_str(), seed.length());
  ChannelSwitch *channelSwitch;// = new ChannelSwitch();

  ArraySource source((byte *) data.c_str(), data.length() + 4, false, new SecretSharing(rng, threshold, nShares, channelSwitch = new ChannelSwitch));

  size_t share_length =  data.length() + 12 - data.length() % 4;
  char **buf = (char **) malloc(nShares * sizeof(char *));
  for (int i = 0; i < nShares; i++)
    buf[i] =(char *) malloc(share_length);

  vector_member_ptrs<ArraySink> sinks(nShares);
  std::string channel;

  //Array to store the encoded fragments
  //char *buf[nShares];
	
  for (int i=0; i<nShares; ++i)
    {
      //sinks[i].reset(new ArraySink((byte *) buf.at(i).c_str(), buf.at(i).length()));
      sinks[i].reset(new ArraySink((byte *) buf[i], share_length));
      channel = WordToString<word32>(i);
      sinks[i]->Put((byte *) channel.data(), 4);
      channelSwitch->AddRoute(channel, *sinks[i], DEFAULT_CHANNEL);
    }
  source.PumpAll();
  *frag_length = share_length;
  return buf;
}

/*
 * Shamir's secret recovering
 */
char* ShamirsSecretRecover(int threshold, std::vector<std::string> fragments)
{
  size_t fragLength = fragments[0].length();
  int nFragments = fragments.size();
  if(threshold > nFragments)
  {
    std::cout << "Not enough fragments. Get your act together and come back." << std::endl;
    return NULL;
  }

  char *output = (char *) malloc(nFragments * fragLength);	//This holds the recovered output. Not sure how many bytes to allocate.
  SecretRecovery recovery(threshold, new ArraySink((byte *)output, fragLength));
  vector_member_ptrs<ArraySource> sources(threshold);
  SecByteBlock channel(4);

  //Create the sources and channels and what not.
  for(int i = 0; i < threshold; i++) 
  {
    sources[i].reset(new ArraySource((byte *)fragments[i].c_str(), fragLength, false));
    sources[i]->Pump(4);
    sources[i]->Get(channel, 4);
    sources[i]->Attach(new ChannelSwitch(recovery, std::string((char *)channel.begin(), 4)));
  }

  while(sources[0]->Pump(256))
  {
    for(int i = 1; i < threshold; ++i)
    {
      sources[i]->Pump(256);
    }
  }
 
  for(int i = 0; i < threshold; ++i)
    sources[i]->PumpAll();
  return output;
}

/*
 * XOR secret sharing
 */
char** XORSecretShare(int nShares, std::string data,
		      std::string seed, size_t *frag_length)
{
  char **shares = (char **)malloc(nShares*sizeof(char *));
  // TODO: see if need seed_seq, or can just use string
  std::seed_seq seed_s(seed.begin(),seed.end());
  std::mt19937 rand(seed_s);

  char *final = (char *)malloc(data.length());
  memcpy(final,data.c_str(),data.length());
  for( int i = 0; i < nShares-1; i++ )
  {
    char *share = (char *)malloc(data.length());
    for ( size_t j=0; j<data.length(); j++ )
    {
      uint8_t num = rand();
      memcpy(&(share[j]),&num,1);
      final[j] ^= num;
    }
    shares[i] = share;
  }
  shares[nShares-1] = final;

  *frag_length = data.length();

  return shares;
}

/*
 * XOR secret recovering
 */
char* XORSecretRecover(std::vector<std::string> shares)
{
  if ( shares.empty() )
  {
    std::cout << "No shares to recover secret" << std::endl;
    return NULL;
  }

  char* final = (char *)malloc(shares[0].length());
  memcpy(final, shares[0].c_str(), shares[0].length());

  for ( int i=1; i<shares.size(); i++ )
  {
    const char *share = shares.at(i).c_str();
    for ( int j=0; j<shares[0].length(); j++ )
      final[j] ^= share[j];
  }

  return final;
}

/*
 * AES Encryption
 */
std::string AESEncrypt(std::string pt, std::string key_str)
{
  std::string plain = pt;
  std::string cipher;

  unsigned char salt_arr[AES::DEFAULT_KEYLENGTH];
  for ( unsigned i=0; i<sizeof(salt_arr); i++ )
    salt_arr[i] = '0';
  unsigned char iv_arr[AES::BLOCKSIZE];
  for ( unsigned i=0; i<sizeof(iv_arr); i++ )
    iv_arr[i] = '0';

  SecByteBlock salt(salt_arr,sizeof(salt_arr));
  SecByteBlock iv(iv_arr,sizeof(iv_arr));

  SecByteBlock derived_key(AES::DEFAULT_KEYLENGTH);

  PKCS5_PBKDF2_HMAC<SHA256> pbkdf;
  pbkdf.DeriveKey(
		  derived_key, derived_key.size(),
		  0x00,
		  (byte *)key_str.data(), key_str.size(),
		  salt, salt.size(),
		  AES_ITERATIONS
		  );

  CBC_Mode<AES>::Encryption e(derived_key, derived_key.size(), iv);
  StringSource encryptor(plain,true,
			 new StreamTransformationFilter( e, new HexEncoder( new StringSink(cipher)))
			 );
  return cipher;
}

/*
 * AES Decryption
 */
std::string AESDecrypt (std::string ct, std::string key_str)
{
  std::string plain;
  std::string cipher = ct;

  unsigned char salt_arr[AES::DEFAULT_KEYLENGTH];
  for ( unsigned i=0; i<sizeof(salt_arr); i++ )
    salt_arr[i] = '0';
  unsigned char iv_arr[AES::BLOCKSIZE];
  for ( unsigned i=0; i<sizeof(iv_arr); i++ )
    iv_arr[i] = '0';

  SecByteBlock salt(salt_arr,sizeof(salt_arr));
  SecByteBlock iv(iv_arr,sizeof(iv_arr));

  SecByteBlock derived_key(AES::DEFAULT_KEYLENGTH);

  PKCS5_PBKDF2_HMAC<SHA256> pbkdf;
  pbkdf.DeriveKey(
		  derived_key, derived_key.size(),
		  0x00,
		  (byte *)key_str.data(), key_str.size(),
		  salt, salt.size(),
		  AES_ITERATIONS
		  );

  CBC_Mode<AES>::Decryption d(derived_key, derived_key.size(), iv);
  StringSource decryptor(cipher, true,
			 new HexDecoder( new StreamTransformationFilter(d, new StringSink(plain)))
			 );

  return plain;
}

