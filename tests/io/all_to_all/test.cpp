#include <sstream>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <fstream>

#include <sys/poll.h>

#include "s3.h"
#include "clock.h"
#include <cstdlib>
#include <cstring>

//#define DEBUG
#define VALID_OBJECT_NAME_CHARS  "abcdefghijklmnopqrstuvwxyz-0123456789"
#define VALID_BUCKET_NAME_CHARS  "abcdefghijklmnopqrstuvwxyz-0123456789"

enum request_type_t {
  REQUEST_TYPE_SET,
  REQUEST_TYPE_GET,
  REQUEST_TYPE_BOTH,
  REQUEST_TYPE_MAX
};
  
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

// TODO
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

  // process command line params
  request_type_t req_type;
  std::string object_name, bucket_name;
  size_t object_size;
  int opt_idx = 1;

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
  if ( (req_type == REQUEST_TYPE_SET) || (req_type == REQUEST_TYPE_BOTH) )
  {
    if ( !(object_size  = atoi(argv[opt_idx++])) )
    {
      std::cerr << "Invalid object size: " << argv[opt_idx-1] << std::endl;
      return opt_idx-1;
    }
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
  switch (req_type)
  {
    case REQUEST_TYPE_SET:
    {
      clock_start("SET latency");
      if ( (ret = put_simple(bucket_name, object_name, object_size)) )
      {
	std::cerr << "Error calling put_simple()" << std::endl;
	return ret;
      }
      clock_end("SET latency");
      for ( size_t i=0; i<clock_size(); i++ )
      {
	CLOCK_VAL time = clock_get(i);
	std::cout << std::setprecision(6);
	std::cout << "SET" << ","
		  << bucket_name << ","
		  << object_size << ","
//		  << time.first << ","
		  << time.second.tv_sec << "."
		  << std::fixed << time.second.tv_usec << std::endl;
      }
      break;
    }
    case REQUEST_TYPE_GET:
    {
      clock_start("GET latency");
      if ( (ret = get_simple(bucket_name, object_name, &object_size)) )
      {
	std::cerr << "Error calling get_simple()" << std::endl;
	return ret;
      }
      clock_end("GET latency");
      for ( size_t i=0; i<clock_size(); i++ )
      {
	CLOCK_VAL time = clock_get(i);
	std::cout << std::setprecision(6);
	std::cout << "GET" << ","
		  << bucket_name << ","
		  << object_size << ","
//		  << time.first << ","
		  << time.second.tv_sec << "."
		  << std::fixed << time.second.tv_usec << std::endl;
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
