#include <sstream>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <fstream>

#include <sys/poll.h>

#include "s3.h"
#include "clock.h"
#include "encode.hh"
#include "args.h"
#include <cstdlib>
#include <cstring>

// concurrency includes
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

//#define DEBUG
#define OPTIMIZE
#define VALID_OBJECT_NAME_CHARS  "abcdefghijklmnopqrstuvwxyz-0123456789/"
#define VALID_BUCKET_NAME_CHARS  "abcdefghijklmnopqrstuvwxyz-0123456789"
#define DEFAULT_SEED             "12345"
#define DEFAULT_AES_KEY          "aosenuth;qljrxkcgaoesnub',.p/c"
#define MAX_NUM_SHARES           10
#define MAX_PARALLEL_REQ         6 // from trial and error testing on the local machine
#define PRIME_FILE               "prime"

enum encoding_type_t {
  ENCODING_TYPE_NONE,
  ENCODING_TYPE_SPLIT,
  ENCODING_TYPE_AES,
  ENCODING_TYPE_SSS,
  ENCODING_TYPE_AES_SSS,
  ENCODING_TYPE_XOR,
  ENCODING_TYPE_MAX
};
struct Encoding {
  encoding_type_t type;
  int threshold;
  int num_shares;
  static const std::map<encoding_type_t,std::string> mapping;
    
  Encoding() : threshold(1), num_shares(1), type(ENCODING_TYPE_MAX){};
  const std::string to_string() const
  {
    if ( mapping.find(type) == mapping.end() )
      return "INVALID";
    std::stringstream name;
    name << (mapping.at(type));
    if ( type == ENCODING_TYPE_SPLIT )
      name << "(" << num_shares << ")";
    if ( type == ENCODING_TYPE_AES ||
	 type == ENCODING_TYPE_SSS ||
	 type == ENCODING_TYPE_AES_SSS ||
	 type == ENCODING_TYPE_XOR )
      name << "(" << threshold << "," << num_shares << ")";
    return name.str();
  };
  bool parse( std::string arg )
  {
    size_t name_end = arg.find("(");
    std::string name = arg.substr(0,name_end);

    type == ENCODING_TYPE_MAX;
    for ( std::map<encoding_type_t,std::string>::const_iterator it=mapping.begin();
	  it != mapping.end(); ++it )
    {
      if ( !it->second.compare(name) )
      {
	type = it->first;
	break;
      }
    }
    
    if ( type == ENCODING_TYPE_MAX )
      return false;

    if ( type == ENCODING_TYPE_NONE || type == ENCODING_TYPE_AES )
      if ( name_end == std::string::npos )
	return true;
      else
	return false;
    if ( name_end == std::string::npos )
      return false;

    std::string params = arg.substr(name_end);
    if ( params.back() != ')' || params.length() <= 2 )
      return false;
    
    if ( type == ENCODING_TYPE_SPLIT )
      if ( !(num_shares = atoi(params.substr(1,params.length()-2).c_str())) )
	return false;
      else
      {
	threshold = num_shares;
	return true;
      }

    size_t comma_pos = params.find(",");
    if ( !(comma_pos < params.length()-2 && comma_pos > 1) )
      return false;
    std::string first = params.substr(1,comma_pos-1);
    std::string second = params.substr(comma_pos+1,params.length()-1-comma_pos);
    if ( !(threshold = atoi(first.c_str())) )
      return false;
    if ( !(num_shares = atoi(second.c_str())) )
      return false;
    
    return true;
  }
};
const std::map<encoding_type_t,std::string> Encoding::mapping = {
  {ENCODING_TYPE_NONE,"NONE"},
  {ENCODING_TYPE_SPLIT,"SPLIT"},
  {ENCODING_TYPE_AES,"AES"},
  {ENCODING_TYPE_SSS,"SSS"},
  {ENCODING_TYPE_AES_SSS,"AES-SSS"},
  {ENCODING_TYPE_XOR,"XOR"}
};
std::ostream& operator<<(std::ostream& os, const Encoding& obj)
{
  os << obj.to_string();
  return os;
}

enum request_type_t {
  REQUEST_TYPE_SET,
  REQUEST_TYPE_GET,
  REQUEST_TYPE_BOTH,
  REQUEST_TYPE_MAX
};
struct Request {
  request_type_t type;
  static const std::map<request_type_t,std::string> mapping;
    
  const std::string to_string() const
  {
    if ( mapping.find(type) == mapping.end() )
      return "INVALID";
    return mapping.find(type)->second;
  };
  bool parse( const char *arg )
  {
    if ( !arg )
      return false;
    
    type == REQUEST_TYPE_MAX;
    for ( std::map<request_type_t,std::string>::const_iterator it=mapping.begin();
	  it != mapping.end(); ++it )
    {
      if ( !it->second.compare(arg) )
      {
	type = it->first;
	return true;
      }
    }
    return false;
  }
};
const std::map<request_type_t,std::string> Request::mapping = {
  {REQUEST_TYPE_SET,"SET"},
  {REQUEST_TYPE_GET,"GET"},
  {REQUEST_TYPE_BOTH,"BOTH"}
};
std::ostream& operator<<(std::ostream& os, const Request& obj)
{
  os << obj.to_string();
  return os;
}

// concurrency variables
std::mutex mtx_g;
std::condition_variable cv_g;
std::atomic<int> finished_g(0);
std::vector<std::thread> threads_g;

// timing variables
int hour_g;
  
int global_init()
{
  // init globals
  accessKeyIdG = getenv("S3_ACCESS_KEY_ID");
  secretAccessKeyG = getenv("S3_SECRET_ACCESS_KEY");
  uriStyleG = S3UriStyleVirtualHost;
  srand(time(NULL));

  // init timing variables
  time_t now = time(NULL);
  struct tm *tm_struct = localtime(&now);
  hour_g = tm_struct->tm_hour;

  return 1;
}

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

std::string random_string( size_t length )
{
  auto randchar = []() -> char
    {
      const char charset[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
      const size_t max_index = (sizeof(charset) - 1);
      return charset[ rand() % max_index ];
    };
  std::string str(length,0);
  std::generate_n( str.begin(), length, randchar );
  return str;
}

// increments atomic counter when finished and signals 'cv_g'
int put_concurrent( std::string bucket_name,
                    std::string object_name,
                    char *buf,
                    std::size_t size )
{
  int ret = 0;

  while ( (ret = put_object( bucket_name.c_str(),
			     object_name.c_str(),
			     buf,
			     size)) )
  {
    std::cerr << "Error calling put_object()" << std::endl;
    std::cerr << bucket_name << "/" << object_name << std::endl;
  }

  std::atomic_fetch_add(&finished_g,1);
  cv_g.notify_all(); // TODO: possible concurrent call issue
  
  return ret;
}

// increments atomic counter when finished and signals 'cv_g'
int get_concurrent( std::string bucket_name,
                    std::string object_name,
		    std::string & frag,
                    size_t *object_size = NULL
		    )
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

  std::atomic_fetch_add(&finished_g, 1);
  cv_g.notify_all(); // TODO: possible concurrent call issue

  return ret;
}

// Assigns <arg> to <object_name>
//   returns the index of the first invalid char + 1, or 0 if valid
int check_object_name( const char *arg, std::string &object_name )
{
  std::string valid_chars(VALID_OBJECT_NAME_CHARS);
  object_name = std::string(arg);

  std::size_t found = object_name.find_first_not_of(valid_chars);
  if ( found != std::string::npos )
    return found+1;

  return 0;
}

bool prime_s3_connections(std::vector<std::string> buckets, size_t connections_per_bucket)
{
  std::string tmp;
  size_t object_size_trash;
  std::atomic_store(&finished_g,0);
  std::unique_lock<std::mutex> lck(mtx_g);
  for ( int i=0; i<buckets.size(); ++i )
  {
    threads_g.push_back(std::thread(get_concurrent,
				    buckets[i],
				    PRIME_FILE,
				    std::ref(tmp),
				    &object_size_trash));
  
  }
  while ( std::atomic_load(&finished_g) < buckets.size() )
    cv_g.wait(lck);

  for ( int i=0; i<threads_g.size(); ++i )
    threads_g[i].join();
}

bool encode_object( std::string object, Encoding enc, std::vector<std::string>& frags )
{
  switch (enc.type)
  {
    case ENCODING_TYPE_NONE:
    {	
      frags.push_back(object);
      break;
    }
    case ENCODING_TYPE_SPLIT:
    {
      size_t frag_size = object.size() / enc.num_shares;
      for ( int i=0; i<enc.num_shares; i++ )
      {
	if ( i == (enc.num_shares-1) )
	  frags.push_back(object.substr(i*frag_size));
	else
	  frags.push_back(object.substr(i*frag_size,frag_size));
      }
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
      frags_c = ShamirsSecretShare(enc.threshold, enc.num_shares, object,
				   DEFAULT_SEED, &frag_c_len);
      for ( int i=0; i<enc.num_shares; i++ )
      {
	frags.push_back(std::string(frags_c[i],frag_c_len));
	free(frags_c[i]);
      }
      free(frags_c);
      break;
    }
    case ENCODING_TYPE_AES_SSS:
    {
      // NOTE: first frag is ciphertext, rest frags are SSS shares of AES key
      std::string ciphertext, aes_key(DEFAULT_AES_KEY);
	
      ciphertext = AESEncrypt(object, aes_key);
      frags.push_back(ciphertext);

      size_t frag_c_len;
      char **frags_c;
      std::string seed(DEFAULT_SEED);
      frags_c = ShamirsSecretShare(enc.threshold, enc.num_shares, aes_key,
				   DEFAULT_SEED, &frag_c_len);
      for ( int i=0; i<enc.num_shares; i++ )
      {
	frags.push_back(std::string(frags_c[i],frag_c_len));
	free(frags_c[i]);
      }
      free(frags_c);
      break;
    }
    case ENCODING_TYPE_XOR:
    {
      size_t frag_c_len;
      char **frags_c;
      std::string seed(DEFAULT_SEED);

      frags_c = XORSecretShare(enc.num_shares, object,
			       DEFAULT_SEED, &frag_c_len);
      for ( int i=0; i<enc.num_shares; i++ )
      {
	frags.push_back(std::string(frags_c[i],frag_c_len));
	free(frags_c[i]);
      }
      free(frags_c);
      break;
    }
    default:
    {
      std::cerr << "Unsupported encoding type" << std::endl;
      return false;;
    }
  }
  return true;
}

bool decode_object( std::vector<std::string> frags, Encoding enc, std::string& object )
{
  switch (enc.type)
  {
    case ENCODING_TYPE_NONE:
    {	
      object.assign(frags[0]);
      break;
    }
    case ENCODING_TYPE_SPLIT:
    {	
      for ( int i=0; i<frags.size(); i++ )
	object.append(frags[i]);
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
      std::vector<std::string> done_frags;
	      
      for ( int i=0; i<frags.size(); i++ )
	if ( frags[i].length() > 0 )
	  done_frags.push_back(frags[i]);

      object_c = ShamirsSecretRecover(enc.threshold, done_frags);
      object.assign(object_c,object.size());
      free(object_c);

      break;
    }
    case ENCODING_TYPE_AES_SSS:
    {
      // NOTE: first frag is ciphertext, rest are SSS shares of AES
      std::string plaintext, aes_key(DEFAULT_AES_KEY);
      char *aes_key_c;
      std::vector<std::string> key_frags;

      for ( int i=1; i<frags.size(); ++i )
	if ( frags[0].length() > 0 )
	  key_frags.push_back(frags[i]);
      aes_key_c = ShamirsSecretRecover(enc.threshold, key_frags);
      // TODO: just use the default key for now
      //	  aes_key.assign(aes_key_c,frags[1].length());
      free(aes_key_c);
	  
      plaintext = AESDecrypt(frags[0], aes_key);
      object.assign(plaintext);

      break;
    }
    case ENCODING_TYPE_XOR:
    {
      char *object_c;

      object_c = XORSecretRecover(frags);
      object.assign(object_c,object.size());

      free(object_c);

      break;
    }
    default:
    {
      std::cerr << "Unsupported encoding type" << std::endl;
      return false;
    }
  }
  return true;
}

int main( int argc, char *argv[] )
{
  int ret = 0;

  // init globals
  if ( !global_init() )
    return 1;

  // parse command line args
  argc-=(argc>0); argv+=(argc>0); // skip program name argv[0] if present
  option::Stats stats(args_usage_g, argc, argv);
  option::Option options[stats.options_max], buffer[stats.buffer_max];
  option::Parser parse(args_usage_g, argc, argv, options, buffer);

  if (parse.error())
    return 1;

  if (options[ARGS_HELP] || argc == 0)
  {
    int columns = getenv("COLUMNS")? atoi(getenv("COLUMNS")) : 80;
    option::printUsage(fwrite, stdout, args_usage_g, columns);
    return 0;
  }

  for ( int i=0; i<(sizeof(args_required_g)/sizeof(*args_required_g)); i++ )
  {
    int j;
    for ( j=0; j<parse.optionsCount(); j++ )
      if ( buffer[j].index() == args_required_g[i] )
	break;
    if ( j == parse.optionsCount() )
      for ( j=0; j<(sizeof(args_usage_g)/sizeof(*args_usage_g)); j++ )
	if ( args_usage_g[j].index == args_required_g[i] )
	{
	  std::cerr << "Missing -" << args_usage_g[j].shortopt
		    << " --" << args_usage_g[j].longopt << " option" << std::endl;
	  return i;
	}
  }

  Encoding enc_type_;
  Request req_type_;
  std::string object_name;
  size_t object_size;
  std::vector<std::string> buckets;
  std::vector<std::string> object_names;
  std::string object;
  int network_loops=1;

  if ( !enc_type_.parse(options[ARGS_ENC_TYPE].arg) )
  {
    std::cerr << "Invalid encoding type: " << options[ARGS_ENC_TYPE].arg << std::endl;
    return 1;
  }
  if ( !req_type_.parse(options[ARGS_REQ_TYPE].arg) )
  {
    std::cerr << "Invalid request type: " << options[ARGS_REQ_TYPE].arg << std::endl;
    return 1;
  }
  if ( check_object_name(options[ARGS_OBJECT_NAME].arg, object_name) )
  {
    std::cerr << "Invalid object_name: " << options[ARGS_OBJECT_NAME].arg << std::endl;
    return 1;
  }
  if ( !(object_size = atoi(options[ARGS_OBJECT_SIZE].arg)) )
  {
    std::cerr << "Invalid object size: " << options[ARGS_OBJECT_SIZE].arg << std::endl;
    return 1;
  }
  {
    std::string dest(options[ARGS_DESTINATIONS].arg);
    std::istringstream ss(dest);
    std::string item, prefix("aws-test-");
    while (std::getline(ss, item, ','))
      buckets.push_back(prefix + item);
    if ( buckets.size() < enc_type_.num_shares )
    {
      std::cerr << "Invalid destinations: "
		<< "need " << enc_type_.num_shares
		<< ", have " << buckets.size() << std::endl;
      return 1;
    }
    if ( enc_type_.type == ENCODING_TYPE_AES_SSS )
      buckets.insert(buckets.begin(),prefix + "apne1");
  }

  if ( options[ARGS_NETWORK_LOOPS] && !(network_loops = atoi(options[ARGS_NETWORK_LOOPS].arg)) )
  {
    std::cerr << "Invalid network loops: " << options[ARGS_NETWORK_LOOPS].arg << std::endl;
    return 1;
  }

  // EDIT: so that object names will not repeat over multiple calls to test.cpp
  //object_name += random_string(10);

  if ( enc_type_.type == ENCODING_TYPE_AES_SSS )
    object_names.push_back(object_name+"-ciphertext");
  for ( int i=0; i<enc_type_.num_shares; ++i )
    object_names.push_back(object_name+"-"+std::to_string(i));

  if ( options[ARGS_VERBOSE] )
  {
    std::cout << "Encoding: " << enc_type_ << std::endl;
    std::cout << "Request: " << req_type_ << std::endl;
    std::cout << "Object name: " << object_name << std::endl;
    std::cout << "Object size: " << object_size << std::endl;
    std::cout << "Destinations: ";
    for ( std::vector<std::string>::const_iterator it=buckets.begin(); it != buckets.end(); it++ )
      std::cout << *it << ", ";
    std::cout << std::endl;
    if ( network_loops > 1 )
      std::cout << "Network loops: " << network_loops << std::endl;
  }

  // prime network connections (initiate GET to each bucket)
  if ( options[ARGS_VERBOSE] )
    std::cout << "priming " << buckets.size() << " bucket connections, " << 1 << " each" << std::endl;
  if ( options[ARGS_PRIME] )
    prime_s3_connections(buckets, 1);

  // lock mutex used for thread notification
  std::unique_lock<std::mutex> lck(mtx_g);

  std::vector<std::string> frags;

  // create random object of correct size
  object.resize(object_size,'x');
  

  switch (req_type_.type)
  {
    case REQUEST_TYPE_SET:
    {
      clock_start("SET total latency");
      {
	clock_start("SET encoding latency");
	{
	  if ( !encode_object(object, enc_type_, frags) )
	    return 1;
	}
	clock_end("SET encoding latency");
	
	for ( int net_loop=0; net_loop<network_loops; net_loop++ )
	{
	  //	  std::cout << "starting loop " << k << std::endl;
	  threads_g.clear();
	  clock_start("SET network latency " + std::to_string(net_loop));
	  {
	    // can only have MAX_PARALLEL_REQ requests going on at once
	    //   or fails with NameLookupError in curl about half the time
	    int started = 0;
	    // starts up to MAX_PARALLEL_REQ requests
	    for ( int i=0; i<MAX_PARALLEL_REQ && i<object_names.size() ; ++i,++started )
	    {
	      threads_g.push_back(std::thread(put_concurrent,
					      buckets[i%buckets.size()],
					      object_names[i]+std::to_string(net_loop),
					      (char *)frags[i].c_str(),
					      frags[i].length()));
	    }
      
	    // waits for all objects to be uploaded, starting additional uploads as needed
	    while ( std::atomic_load(&finished_g) < object_names.size() )
	    {
	      cv_g.wait(lck);
	      if ( started < object_names.size() )
	      {
		threads_g.push_back(std::thread(put_concurrent,
						buckets[started%buckets.size()],
						object_names[started]+std::to_string(net_loop),
						(char *)frags[started].c_str(),
						frags[started].length()));
		++started;
	      }
	    }
	    // joins all threads, just to be safe
	    for ( int i=0; i<threads_g.size(); i++ )
	      if ( threads_g[i].joinable() )
		threads_g[i].join();
	  }
	  clock_end("SET network latency " + std::to_string(net_loop));
	}
      }
      clock_end("SET total latency");
      for ( size_t i=0; i<clock_size(); i++ )
      {
	CLOCK_VAL elapsed_time = clock_get(i);
	std::cout << req_type_ << ","
		  << enc_type_ << ","
		  << object_size << ","
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
      {
	for ( int net_loop=0; net_loop<network_loops; net_loop++ )
	{
	  //	  std::cout << "starting loop " << k << std::endl;
	  threads_g.clear();
	  clock_start("GET network latency " + std::to_string(net_loop));
	  {
	    frags.clear();
	    std::atomic_store(&finished_g,0);
	    frags.resize(object_names.size());
	    // concurrent gets, waits for enc_type_.threshold threads to finish,
	    //   uses same buckets/regions every time, uses same object name for each fragment since they are each in a different bucket
	    // can only have MAX_PARALLEL_REQ requests at the same time
	    // TODO: throw away object size var, since it may be incorrect when using AES-SSS
	    size_t object_size_trash;
	    for ( int i=0; i<object_names.size() && i<MAX_PARALLEL_REQ ; ++i )
	    {
	      threads_g.push_back(std::thread(get_concurrent,
					      buckets[i%buckets.size()],
					      object_names[i]+std::to_string(net_loop),
					      std::ref(frags[i]),
					      &object_size_trash));
	    }

	    // TODO: since the ciphertext of AES-SSS is always the first object, must wait for that
	    //   as well as the shares needed to recover the AES key
	    if ( enc_type_.type == ENCODING_TYPE_AES_SSS )
	    {
	      threads_g[0].join();
	      while ( std::atomic_load(&finished_g) < enc_type_.threshold+1 )
		cv_g.wait(lck);
	    }
	    else
	    {
	      while ( std::atomic_load(&finished_g) < enc_type_.threshold )
		cv_g.wait(lck);
	    }
	    for ( int i=0; i<threads_g.size(); ++i )
	      if ( threads_g[i].joinable() )
		threads_g[i].detach();
	  }
	  clock_end("GET network latency " + std::to_string(net_loop));
	}

	clock_start("GET decoding latency");
	{
	  if ( !decode_object(frags, enc_type_, object) )
	    return 1;
	}
	clock_end("GET decoding latency");
      }
      clock_end("GET total latency");

      for ( size_t i=0; i<clock_size(); i++ )
      {
	CLOCK_VAL elapsed_time = clock_get(i);
	std::cout << req_type_ << ","
		  << enc_type_ << ","
		  << object_size << ","
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
