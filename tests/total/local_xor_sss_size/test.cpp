#include <sstream>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <fstream>

#include <sys/poll.h>

#include "s3.h"
#include "clock.h"
#include "encode.hh"
#include <cstdlib>
#include <cstring>

// concurrency includes
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

//#define DEBUG
#define OPTIMIZE
#define VALID_OBJECT_NAME_CHARS  "abcdefghijklmnopqrstuvwxyz-0123456789"
#define VALID_BUCKET_NAME_CHARS  "abcdefghijklmnopqrstuvwxyz-0123456789"
#define DEFAULT_SEED             "12345"
#define DEFAULT_AES_KEY          "aosenuth;qljrxkcgaoesnub',.p/c"
#define MAX_NUM_SHARES           10
#define MAX_PARALLEL_REQ         6 // from trial and error testing on the local machine

enum encoding_type_t {
  ENCODING_TYPE_NONE,
  ENCODING_TYPE_AES,
  ENCODING_TYPE_SSS,
  ENCODING_TYPE_XOR,
  ENCODING_TYPE_MAX
};

enum request_type_t {
  REQUEST_TYPE_SET,
  REQUEST_TYPE_GET,
  REQUEST_TYPE_BOTH,
  REQUEST_TYPE_MAX
};

// concurrency variables
std::mutex mtx_g;
std::condition_variable cv_g;
std::atomic<int> finished_g(0);
std::vector<std::thread> threads_g(MAX_NUM_SHARES);

// timing variables
int hour_g;
  
std::string readable_fs( double size )
{
  const char* units[] = {"", "K", "M", "G", "TB", "PB", "EB", "ZB", "YB"};
  std::stringstream ret;
  int i = 0;
  while (size >= 1024) {
    size /= 1024;
    i++;
  }
  ret << size << units[i];
  return ret.str();
}

int put_simple( std::string bucket_name,
		std::string object_name,
		std::size_t size )
{
  int ret = 0;
  char *buf = (char *)malloc(size);
  if (!buf)
  {
    std::cerr << "Error allocating memory" << std::endl;
    return 1;
  }

  if ( (ret = put_object( bucket_name.c_str(),
			  object_name.c_str(),
			  buf,
			  size)) )
  {
    std::cerr << "Error calling put_object()" << std::endl;
  }
  
  if (buf)
    free(buf);

  return ret;
}

int get_simple( std::string bucket_name,
		std::string object_name,
		size_t *object_size = NULL )
{
  int ret = 0;
  std::string full_name;
  char *buf = NULL;
  size_t frag_len = 0;

  full_name += bucket_name + "/";
  full_name += object_name;

//  std::cout << "full_name: " << full_name << std::endl;
//  std::cout << "frag_len: " << frag_len << std::endl;
  if ( (buf = get_object((char *)full_name.c_str(), (int *)&frag_len)) )
    free(buf);
  else
  {
    std::cerr << "Error calling get_object()" << std::endl;
    return 1;
  }
  if ( object_size )
    *object_size = frag_len;

  return ret;
}

// increments atomic counter when finished and signals 'cv_g'
int put_concurrent( std::string bucket_name,
                    std::string object_name,
                    char *buf,
                    std::size_t size )
{
  int ret = 0;

  if ( (ret = put_object( bucket_name.c_str(),
                          object_name.c_str(),
                          buf,
                          size)) )
  {
    std::cerr << "Error calling put_object()" << std::endl;
    std::cout << bucket_name << "/" << object_name << std::endl;
  }

  std::atomic_fetch_add(&finished_g,1);
  cv_g.notify_all(); // TODO: possible concurrent call issue
  
  return ret;
}

// increments atomic counter when finished and signals 'cv_g'
int get_concurrent( std::string bucket_name,
                    std::string object_name,
		    std::string & frag,
                    size_t *object_size = NULL )
{
  int ret = 0;
  std::string full_name;
  char *buf = NULL;
  size_t frag_len = 0;

  full_name += bucket_name + "/";
  full_name += object_name;

  frag.clear();
  if ( (buf = get_object((char *)full_name.c_str(), (int *)&frag_len)) )
    frag.append(buf,frag_len);
  else
  {
    std::cerr << "Error calling get_object()" << std::endl;
    return 1;
  }
  if ( object_size )
    *object_size = frag_len;

  std::atomic_fetch_add(&finished_g,1);
  cv_g.notify_all(); // TODO: possible concurrent call issue

  return ret;
}

std::string req_to_string( request_type_t req_type )
{
  switch (req_type)
  {
  case REQUEST_TYPE_SET:
    return "SET";
    break;
  case REQUEST_TYPE_GET:
    return "GET";
    break;
  case REQUEST_TYPE_BOTH:
    return "BOTH";
    break;
  default:
    return "INVALID";
  }
}
std::string enc_to_string( encoding_type_t enc_type )
{
  switch (enc_type)
  {
  case ENCODING_TYPE_NONE:
    return "NONE";
    break;
  case ENCODING_TYPE_AES:
    return "AES";
    break;
  case ENCODING_TYPE_SSS:
    return "SSS";
    break;
  case ENCODING_TYPE_XOR:
    return "XOR";
    break;
  default:
    return "INVALID";
  }
}

// returns error num, or 0 if valid
int check_request( char *arg, request_type_t *request )
{
  if ( arg )
  {
    if ( !strcmp(arg,"GET") )
      *request = REQUEST_TYPE_GET;
    else if ( !strcmp(arg,"SET") )
      *request = REQUEST_TYPE_SET;
    else
      return 1;
  }
  return 0;
}
// returns error num, or 0 if valid
int check_encoding( char *arg, encoding_type_t *encoding )
{
  if ( arg )
  {
    if ( !strcmp(arg,"NONE") )
      *encoding = ENCODING_TYPE_NONE;
    else if ( !strcmp(arg,"AES") )
      *encoding = ENCODING_TYPE_AES;
    else if ( !strcmp(arg,"SSS") )
      *encoding = ENCODING_TYPE_SSS;
    else if ( !strcmp(arg,"XOR") )
      *encoding = ENCODING_TYPE_XOR;
    else
      return 1;
  }
  return 0;
}
// returns the index of the first invalid char + 1, or 0 if valid
int check_object_name( char *arg, std::string &object_name )
{
  std::string valid_chars(VALID_OBJECT_NAME_CHARS);
  object_name = std::string(arg);

  std::size_t found = object_name.find_first_not_of(valid_chars);
  if ( found != std::string::npos )
    return found+1;

  return 0;
}
// returns the index of the first invalid char + 1, or 0 if valid
int check_bucket_name( char *arg, std::string &bucket_name )
{
  std::string valid_chars(VALID_BUCKET_NAME_CHARS);
  bucket_name = std::string(arg);
//  std::cout << "bucket_name: " << bucket_name << std::endl;

  // check length
  if ( bucket_name.length() < 3 )
    return 2+1;
  if ( bucket_name.length() > 63 )
    return 63+1;

  // check label values
  std::size_t start = 0, end;
  std::string label;
  while (1)
  {
    end = bucket_name.find('.',start);
    end = (end == std::string::npos) ? bucket_name.length() : end;
    if ( (end - start) < 2 )
      return end+1;
    label = bucket_name.substr(start,end-start);
//    std::cout << "label: " << label << std::endl;
//    std::cout << "start,end: " << start << "," << end << std::endl;
    std::size_t found = label.find_first_not_of(valid_chars);
    if ( found != std::string::npos )
      return start + found+1;
    if ( label[0] == '-' )
      return start+1;
    if ( label[label.length()-1] == '-' )
      return end;
    if ( end == (bucket_name.length()-1) )
      return end+1;
    if ( end == bucket_name.length() )
      break;
    start = end+1;
  }
  
  return 0;
}

int main( int argc, char *argv[])
{
  int ret = 0;

  // init globals
  accessKeyIdG = getenv("S3_ACCESS_KEY_ID");
  secretAccessKeyG = getenv("S3_SECRET_ACCESS_KEY");
  uriStyleG = S3UriStyleVirtualHost;
  srand(time(NULL));

  // init timing variables
  time_t now = time(NULL);
  struct tm *tm_struct = localtime(&now);
  hour_g = tm_struct->tm_hour;


  // init concurrency variables
  std::unique_lock<std::mutex> lck(mtx_g);

  encoding_type_t enc_type;
  request_type_t req_type;
  std::string object_name, bucket_name;
  size_t object_size;
  int opt_idx = 1;
  std::vector<std::string> frags;
  std::string object;
  int threshold = 1, num_shares = 1;

  { // process command line params
    if ( argc < opt_idx+1 )
    {
      std::cerr << "Not enough params" << std::endl;
      return 1;
    }
    if ( check_request(argv[opt_idx++], &req_type) )
    {
      std::cerr << "Invalid request: " << argv[opt_idx-1] << std::endl;
      return opt_idx-1;
    }
    if ( argc < opt_idx+1 )
    {
      std::cerr << "Not enough params" << std::endl;
      return 1;
    }
    if ( check_encoding(argv[opt_idx++], &enc_type) )
    {
      std::cerr << "Invalid encoding: " << argv[opt_idx-1] << std::endl;
      return opt_idx-1;
    }
    if ( (enc_type == ENCODING_TYPE_SSS) || (enc_type == ENCODING_TYPE_XOR) )
    {
      if ( argc < opt_idx+2 )
      {
	std::cerr << "Not enough params" << std::endl;
	return 1;
      }
      if ( !(threshold = atoi(argv[opt_idx++])) )
      {
	std::cerr << "Invalid threshold shares: " << argv[opt_idx-1] << std::endl;
	return opt_idx-1;
      }
      if ( !(num_shares = atoi(argv[opt_idx++])) )
      {
	std::cerr << "Invalid number of encoding shares: " << argv[opt_idx-1] << std::endl;
	return opt_idx-1;
      }
      if ( (enc_type == ENCODING_TYPE_XOR) && (threshold != num_shares) )
      {
	std::cerr << "Invalid threshold shares for XOR: " << threshold << std::endl;
	return opt_idx-2;
      }
    }
    if ( argc < opt_idx+1 )
    {
      std::cerr << "Not enough params" << std::endl;
      return 1;
    }
    if ( !(object_size = atoi(argv[opt_idx++])) )
    {
      std::cerr << "Invalid object size: " << argv[opt_idx-1] << std::endl;
      return opt_idx-1;
    }
    if ( argc < opt_idx+1 )
    {
      std::cerr << "Not enough params" << std::endl;
      return 1;
    }
    if ( (ret = check_bucket_name(argv[opt_idx++], bucket_name)) )
    {
      std::cerr << "Invalid bucket name: " << argv[opt_idx-1] << std::endl;
      std::cerr << "                   ";
      for ( int i=0; i<ret-1; i++ )
	fprintf(stderr," ");
      fprintf(stderr,"-> <-\n");
      return opt_idx-1;
    }
    if ( argc < opt_idx+1 )
    {
      std::cerr << "Not enough params" << std::endl;
      return 1;
    }
    if ( (ret = check_object_name(argv[opt_idx++], object_name)) )
    {
      std::cerr << "Invalid object name: " << argv[opt_idx-1] << std::endl;
      std::cerr << "                   ";
      for ( int i=0; i<ret-1; i++ )
	fprintf(stderr," ");
      fprintf(stderr,"-> <-\n");
      return opt_idx-1;
    }
  }

  // create random object of correct size
  object.resize(object_size,'x');
  
  // buckets to use for this test
  std::vector<std::string> buckets;
  buckets.push_back("aws-test-use1");
  buckets.push_back("aws-test-usw1");
  buckets.push_back("aws-test-usw2");
  buckets.push_back("aws-test-euw1");
  buckets.push_back("aws-test-apne1");
  buckets.push_back("aws-test-apse1");
  buckets.push_back("aws-test-apse2");
  buckets.push_back("aws-test-sae1");

  switch (req_type)
  {
    case REQUEST_TYPE_SET:
      {
      clock_start("SET total latency");
      clock_start("SET encoding latency");
      switch (enc_type)
      {
      case ENCODING_TYPE_NONE:
      {	
	frags.push_back(object);
	break;
      }
      case ENCODING_TYPE_AES:
      {
	std::string ciphertext, aes_key(DEFAULT_AES_KEY);
	
	ciphertext = AESEncrypt(object, aes_key);
	frags.push_back(ciphertext);

	break;
      }
      case ENCODING_TYPE_SSS:
      {
	size_t frag_c_len;
	char **frags_c;
	std::string seed(DEFAULT_SEED);
#ifdef DEBUG_TIMING
	clock_start("Encode data");
#endif
	frags_c = ShamirsSecretShare(threshold, num_shares, object,
			      DEFAULT_SEED, &frag_c_len);
	for ( int i=0; i<num_shares; i++ )
	{
	  frags.push_back(std::string(frags_c[i],frag_c_len));
	  free(frags_c[i]);
	}
	free(frags_c);
	// EDIT
//	clock_end("SET encoding latency");
//
//	char *object_c;
//
//	clock_start("GET decoding latency");
//	object_c = ShamirsSecretRecover(threshold, frags);
//	object.assign(object_c,frags[0].length());
//	clock_end("GET decoding latency");
//
//	for ( size_t i=0; i<clock_size(); i++ )
//	  {
//	    CLOCK_VAL elapsed_time = clock_get(i);
//	    std::cout << req_to_string(req_type) << ","
//		      << enc_to_string(enc_type) << ","
//		      << threshold << ","
//		      << num_shares << ","
//		      << object_size << ","
//		      << hour_g << ","
//		      << elapsed_time.first << ","
//		      << elapsed_time.second.tv_sec << "."
//		      << std::setfill('0') << std::setw(6)
//		      << std::fixed << elapsed_time.second.tv_usec << std::endl;
//	  }
	break;
      }
      case ENCODING_TYPE_XOR:
      {
	size_t frag_c_len;
	char **frags_c;
	std::string seed(DEFAULT_SEED);

	frags_c = XORSecretShare(num_shares, object,
			      DEFAULT_SEED, &frag_c_len);
	for ( int i=0; i<num_shares; i++ )
	{
	  frags.push_back(std::string(frags_c[i],frag_c_len));
	  free(frags_c[i]);
	}
	free(frags_c);
	// EDIT
//	clock_end("SET encoding latency");
//
//	char *object_c;
//
//	clock_start("GET decoding latency");
//	object_c = XORSecretRecover(frags);
//	object.assign(object_c,frags[0].length());
//	clock_end("GET decoding latency");
//
//	for ( size_t i=0; i<clock_size(); i++ )
//	  {
//	    CLOCK_VAL elapsed_time = clock_get(i);
//	    std::cout << req_to_string(req_type) << ","
//		      << enc_to_string(enc_type) << ","
//		      << threshold << ","
//		      << num_shares << ","
//		      << object_size << ","
//		      << hour_g << ","
//		      << elapsed_time.first << ","
//		      << elapsed_time.second.tv_sec << "."
//		      << std::setfill('0') << std::setw(6)
//		      << std::fixed << elapsed_time.second.tv_usec << std::endl;
//	  }
	break;
      }
      default:
      {
	std::cerr << "Unsupported encoding type" << std::endl;
	break;
      }
      }
      // EDIT
//      return ret;
      clock_end("SET encoding latency");

      clock_start("SET network latency");
      // concurrent puts, just waits for them all to finish
//      for ( int i=0; i<num_shares; i++ )
//	threads_g.at(i) = std::thread(put_concurrent, buckets[i%buckets.size()], object_name, (char *)frags[i].c_str(), frags[0].length());

      // can only have MAX_PARALLEL_REQ requests going on at once, or fails with NameLookupError in curl about half the time
      int started = 0;
      for ( int i=0; i<MAX_PARALLEL_REQ && started<num_shares ; ++i,++started )
	threads_g.at(i) = std::thread(put_concurrent, buckets[i%buckets.size()], object_name, (char *)frags[i].c_str(), frags[0].length());
      
      while ( std::atomic_load(&finished_g) < num_shares )
      {
	  cv_g.wait(lck);
	  if ( started < num_shares )
	  {
	    threads_g.at(started) = std::thread(put_concurrent, buckets[started%buckets.size()], object_name, (char *)frags[started].c_str(), frags[0].length());
	    ++started;
	  }
      }

      for ( int i=0; i<num_shares; i++ )
	threads_g[i].join();

//      if ( (ret = put_simple(bucket_name, object_name, object_size)) )
//      {
//	std::cerr << "Error calling put_simple()" << std::endl;
//	return ret;
//      }
      clock_end("SET network latency");
      clock_end("SET total latency");
      for ( size_t i=0; i<clock_size(); i++ )
      {
	CLOCK_VAL elapsed_time = clock_get(i);
	std::cout << req_to_string(req_type) << ","
		  << enc_to_string(enc_type) << ","
		  << threshold << ","
		  << num_shares << ","
		  << object_size << ","
		  << hour_g << ","
		  << elapsed_time.first << ","
		  << elapsed_time.second.tv_sec << "."
		      << std::setfill('0') << std::setw(6)
		  << std::fixed << elapsed_time.second.tv_usec << std::endl;
      }
      break;
    }
    case REQUEST_TYPE_GET:
    {
      clock_start("GET total latency");

      clock_start("GET network latency");
      {
	frags.resize(num_shares);
#ifdef OPTIMIZE
	// concurrent gets, waits for threshold threads to finish,
	//   uses same buckets/regions every time, uses same object name for each fragment since they are each in a different bucket
	// can only have MAX_PARALLEL_REQ requests at the same time
	for ( int i=0; i<num_shares && i<MAX_PARALLEL_REQ ; ++i )
	{
	  threads_g.at(i) = std::thread(get_concurrent, buckets[i%buckets.size()], object_name, std::ref(frags[i]), &object_size);
	  threads_g[i].detach();
	}

	while ( std::atomic_load(&finished_g) < threshold )
	  cv_g.wait(lck);
#else
	for ( int i=0; i<threshold; i++ )
	  threads_g.at(i) = std::thread(get_concurrent, buckets[i%buckets.size()], object_name, std::ref(frags[i]), &object_size);
	for ( int i=0; i<threshold; i++ )
	  threads_g[i].join();
#endif
      }
      clock_end("GET network latency");

      clock_start("GET decoding latency");
      {
	switch (enc_type)
	{
	case ENCODING_TYPE_NONE:
	{	
	  object.assign(frags[0]);
	  break;
	}
	case ENCODING_TYPE_AES:
	{
	  std::string plaintext, aes_key(DEFAULT_AES_KEY);

	  plaintext = AESDecrypt(frags[0], aes_key);
	  object.assign(plaintext);

	  break;
	}
	case ENCODING_TYPE_SSS:
	{
	  char *object_c;

	  object_c = ShamirsSecretRecover(threshold, frags);
	  object.assign(object_c,frags[0].length());

	  free(object_c);

	  break;
	}
	case ENCODING_TYPE_XOR:
	{
	  char *object_c;

	  object_c = XORSecretRecover(frags);
	  object.assign(object_c,frags[0].length());

	  free(object_c);

	  break;
	}
	default:
	{
	  std::cerr << "Unsupported encoding type" << std::endl;
	  break;
	}
	}
      }
      clock_end("GET decoding latency");

      clock_end("GET total latency");

      for ( size_t i=0; i<clock_size(); i++ )
      {
	CLOCK_VAL elapsed_time = clock_get(i);
	std::cout << req_to_string(req_type) << ","
		  << enc_to_string(enc_type) << ","
		  << threshold << ","
		  << num_shares << ","
	  //		  << bucket_name << ","
		  << object_size << ","
		  << hour_g << ","
		  << elapsed_time.first << ","
		  << elapsed_time.second.tv_sec << "."
		  << std::setfill('0') << std::setw(6)
		  << std::fixed << elapsed_time.second.tv_usec << std::endl;
      }
      break;
    }
    default:
    {
      std::cerr << "Unsupported request type" << std::endl;
      break;
    }
  }
  return ret;
}
