// #include <functional>
// #include <string>

#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "libs3.h"

typedef struct growbuffer
{
  // The total number of bytes, and the start byte
  int size;
  // The start byte
  int start;
  // The blocks
  char data[64 * 1024];
  struct growbuffer *prev, *next;
} growbuffer;

// returns nonzero on success, zero on out of memory
static int growbuffer_append(growbuffer **gb, const char *data, int dataLen)
{
  while (dataLen) {
    growbuffer *buf = *gb ? (*gb)->prev : 0;
    if (!buf || (buf->size == sizeof(buf->data))) {
      buf = (growbuffer *) malloc(sizeof(growbuffer));
      if (!buf) {
	return 0;
      }
      buf->size = 0;
      buf->start = 0;
      if (*gb) {
	buf->prev = (*gb)->prev;
	buf->next = *gb;
	(*gb)->prev->next = buf;
	(*gb)->prev = buf;
      }
      else {
	buf->prev = buf->next = buf;
	*gb = buf;
      }
    }

    int toCopy = (sizeof(buf->data) - buf->size);
    if (toCopy > dataLen) {
      toCopy = dataLen;
    }

    memcpy(&(buf->data[buf->size]), data, toCopy);

    buf->size += toCopy, data += toCopy, dataLen -= toCopy;
  }

  return 1;
}




typedef struct put_object_callback_data
{
  FILE *infile;
  growbuffer *gb;
  uint64_t contentLength, originalContentLength;
  int noStatus;
} put_object_callback_data;

// ==================
static void growbuffer_read(growbuffer **gb, int amt, int *amtReturn,
                            char *buffer)
{
  *amtReturn = 0;

  growbuffer *buf = *gb;

  if (!buf) {
    return;
  }

  *amtReturn = (buf->size > amt) ? amt : buf->size;

  memcpy(buffer, &(buf->data[buf->start]), *amtReturn);

  buf->start += *amtReturn, buf->size -= *amtReturn;

  if (buf->size == 0) {
    if (buf->next == buf) {
      *gb = 0;
    }
    else {
      *gb = buf->next;
      buf->prev->next = buf->next;
      buf->next->prev = buf->prev;
    }
    free(buf);
  }
}





// TODO: sigh... more fixing
static int putObjectDataCallback(int bufferSize, char *buffer,
                                 void *callbackData)
{
    put_object_callback_data *data =
      (put_object_callback_data *) callbackData;

    int ret = 0;

    if (data->contentLength) {
      int toRead = ((data->contentLength > (unsigned) bufferSize) ?
		    (unsigned) bufferSize : data->contentLength);
      if (data->gb)
	growbuffer_read(&(data->gb), toRead, &ret, buffer);
      else if (data->infile)
	ret = fread(buffer, 1, toRead, data->infile);
    }

    data->contentLength -= ret;

    if (data->contentLength && !data->noStatus) {
      // Avoid a weird bug in MingW, which won't print the second integer
      // value properly when it's in the same call, so print separately
      printf("%llu bytes remaining ",
	     (unsigned long long) data->contentLength);
      printf("(%d%% complete) ...\n",
	     (int) (((data->originalContentLength -
		      data->contentLength) * 100) /
		    data->originalContentLength));
    }

    return ret;
}

// Command-line options, saved as globals ------------------------------------

static int showResponsePropertiesG = 0;
static S3Protocol protocolG = S3ProtocolHTTPS;
S3UriStyle uriStyleG = S3UriStylePath;
static int retriesG = 5;

// Environment variables, saved as globals ----------------------------------

const char *accessKeyIdG = 0;
const char *secretAccessKeyG = 0;

// Request results, saved as globals -----------------------------------------

static S3Status statusG = S3StatusOK;
static char errorDetailsG[4096] = { 0 };

// response properties callback ----------------------------------------------

// This callback does the same thing for every request type: prints out the
// properties if the user has requested them to be so
static S3Status responsePropertiesCallback
(const S3ResponseProperties *properties, void *callbackData)
{
  (void) callbackData;

  if (!showResponsePropertiesG) {
    return S3StatusOK;
  }

#define print_nonnull(name, field)                                 \
  do {								   \
    if (properties-> field) {					   \
      printf("%s: %s\n", name, properties-> field);		   \
    }								   \
  } while (0)

  print_nonnull("Content-Type", contentType);
  print_nonnull("Request-Id", requestId);
  print_nonnull("Request-Id-2", requestId2);
  if (properties->contentLength > 0) {
    printf("Content-Length: %lld\n",
	   (unsigned long long) properties->contentLength);
  }
  print_nonnull("Server", server);
  print_nonnull("ETag", eTag);
  if (properties->lastModified > 0) {
    char timebuf[256];
    time_t t = (time_t) properties->lastModified;
    // gmtime is not thread-safe but we don't care here.
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
    printf("Last-Modified: %s\n", timebuf);
  }
  int i;
  for (i = 0; i < properties->metaDataCount; i++) {
    printf("x-amz-meta-%s: %s\n", properties->metaData[i].name,
	   properties->metaData[i].value);
  }

  return S3StatusOK;
}


// response complete callback ------------------------------------------------

// This callback does the same thing for every request type: saves the status
// and error stuff in global variables
static void responseCompleteCallback(S3Status status,
                                     const S3ErrorDetails *error,
                                     void *callbackData)
{
  (void) callbackData;

  statusG = status;
  // Compose the error details message now, although we might not use it.
  // Can't just save a pointer to [error] since it's not guaranteed to last
  // beyond this callback
  int len = 0;
  if (error && error->message) {
    len += snprintf(&(errorDetailsG[len]), sizeof(errorDetailsG) - len,
		    "  Message: %s\n", error->message);
  }
  if (error && error->resource) {
    len += snprintf(&(errorDetailsG[len]), sizeof(errorDetailsG) - len,
		    "  Resource: %s\n", error->resource);
  }
  if (error && error->furtherDetails) {
    len += snprintf(&(errorDetailsG[len]), sizeof(errorDetailsG) - len,
		    "  Further Details: %s\n", error->furtherDetails);
  }
  if (error && error->extraDetailsCount) {
    len += snprintf(&(errorDetailsG[len]), sizeof(errorDetailsG) - len,
		    "%s", "  Extra Details:\n");
    int i;
    for (i = 0; i < error->extraDetailsCount; i++) {
      len += snprintf(&(errorDetailsG[len]),
		      sizeof(errorDetailsG) - len, "    %s: %s\n",
		      error->extraDetails[i].name,
		      error->extraDetails[i].value);
    }
  }
}







// try 1
// Some Windows stuff
#ifndef FOPEN_EXTRA_FLAGS
#define FOPEN_EXTRA_FLAGS ""
#endif

// Some Unix stuff (to work around Windows issues)
#ifndef SLEEP_UNITS_PER_SECOND
#define SLEEP_UNITS_PER_SECOND 1
#endif

// Also needed for Windows, because somehow MinGW doesn't define this
extern int putenv(char *);






// try 1 end



#define LOCATION_PREFIX "location="
#define LOCATION_PREFIX_LEN (sizeof(LOCATION_PREFIX) - 1)
#define CANNED_ACL_PREFIX "cannedAcl="
#define CANNED_ACL_PREFIX_LEN (sizeof(CANNED_ACL_PREFIX) - 1)
#define PREFIX_PREFIX "prefix="
#define PREFIX_PREFIX_LEN (sizeof(PREFIX_PREFIX) - 1)
#define MARKER_PREFIX "marker="
#define MARKER_PREFIX_LEN (sizeof(MARKER_PREFIX) - 1)
#define DELIMITER_PREFIX "delimiter="
#define DELIMITER_PREFIX_LEN (sizeof(DELIMITER_PREFIX) - 1)
#define MAXKEYS_PREFIX "maxkeys="
#define MAXKEYS_PREFIX_LEN (sizeof(MAXKEYS_PREFIX) - 1)
#define FILENAME_PREFIX "filename="
#define FILENAME_PREFIX_LEN (sizeof(FILENAME_PREFIX) - 1)
#define CONTENT_LENGTH_PREFIX "contentLength="
#define CONTENT_LENGTH_PREFIX_LEN (sizeof(CONTENT_LENGTH_PREFIX) - 1)
#define CACHE_CONTROL_PREFIX "cacheControl="
#define CACHE_CONTROL_PREFIX_LEN (sizeof(CACHE_CONTROL_PREFIX) - 1)
#define CONTENT_TYPE_PREFIX "contentType="
#define CONTENT_TYPE_PREFIX_LEN (sizeof(CONTENT_TYPE_PREFIX) - 1)
#define MD5_PREFIX "md5="
#define MD5_PREFIX_LEN (sizeof(MD5_PREFIX) - 1)
#define CONTENT_DISPOSITION_FILENAME_PREFIX "contentDispositionFilename="
#define CONTENT_DISPOSITION_FILENAME_PREFIX_LEN \
  (sizeof(CONTENT_DISPOSITION_FILENAME_PREFIX) - 1)
#define CONTENT_ENCODING_PREFIX "contentEncoding="
#define CONTENT_ENCODING_PREFIX_LEN (sizeof(CONTENT_ENCODING_PREFIX) - 1)
#define EXPIRES_PREFIX "expires="
#define EXPIRES_PREFIX_LEN (sizeof(EXPIRES_PREFIX) - 1)
#define X_AMZ_META_PREFIX "x-amz-meta-"
#define X_AMZ_META_PREFIX_LEN (sizeof(X_AMZ_META_PREFIX) - 1)
#define IF_MODIFIED_SINCE_PREFIX "ifModifiedSince="
#define IF_MODIFIED_SINCE_PREFIX_LEN (sizeof(IF_MODIFIED_SINCE_PREFIX) - 1)
#define IF_NOT_MODIFIED_SINCE_PREFIX "ifNotmodifiedSince="
#define IF_NOT_MODIFIED_SINCE_PREFIX_LEN \
  (sizeof(IF_NOT_MODIFIED_SINCE_PREFIX) - 1)
#define IF_MATCH_PREFIX "ifMatch="
#define IF_MATCH_PREFIX_LEN (sizeof(IF_MATCH_PREFIX) - 1)
#define IF_NOT_MATCH_PREFIX "ifNotMatch="
#define IF_NOT_MATCH_PREFIX_LEN (sizeof(IF_NOT_MATCH_PREFIX) - 1)
#define START_BYTE_PREFIX "startByte="
#define START_BYTE_PREFIX_LEN (sizeof(START_BYTE_PREFIX) - 1)
#define BYTE_COUNT_PREFIX "byteCount="
#define BYTE_COUNT_PREFIX_LEN (sizeof(BYTE_COUNT_PREFIX) - 1)
#define ALL_DETAILS_PREFIX "allDetails="
#define ALL_DETAILS_PREFIX_LEN (sizeof(ALL_DETAILS_PREFIX) - 1)
#define NO_STATUS_PREFIX "noStatus="
#define NO_STATUS_PREFIX_LEN (sizeof(NO_STATUS_PREFIX) - 1)
#define RESOURCE_PREFIX "resource="
#define RESOURCE_PREFIX_LEN (sizeof(RESOURCE_PREFIX) - 1)
#define TARGET_BUCKET_PREFIX "targetBucket="
#define TARGET_BUCKET_PREFIX_LEN (sizeof(TARGET_BUCKET_PREFIX) - 1)
#define TARGET_PREFIX_PREFIX "targetPrefix="
#define TARGET_PREFIX_PREFIX_LEN (sizeof(TARGET_PREFIX_PREFIX) - 1)


// util ----------------------------------------------------------------------

static void S3_init()
{
  S3Status status;
  const char *hostname = getenv("S3_HOSTNAME");

  if ((status = S3_initialize("s3", S3_INIT_ALL, hostname))
      != S3StatusOK) {
    fprintf(stderr, "Failed to initialize libs3: %s\n",
	    S3_get_status_name(status));
    exit(-1);
  }
}

static void printError()
{
  if (statusG < S3StatusErrorAccessDenied) {
    fprintf(stderr, "\nERROR: %s\n", S3_get_status_name(statusG));
  }
  else {
    fprintf(stderr, "\nERROR: %s\n", S3_get_status_name(statusG));
    fprintf(stderr, "%s\n", errorDetailsG);
  }
}



static void usageExit(FILE *out)
{
  fprintf(out,
"\n Options:\n"
"\n"
"   Command Line:\n"
"\n"
"   -f/--force           : force operation despite warnings\n"
"   -h/--vhost-style     : use virtual-host-style URIs (default is "
                          "path-style)\n"
"   -u/--unencrypted     : unencrypted (use HTTP instead of HTTPS)\n"
"   -s/--show-properties : show response properties on stdout\n"
"   -r/--retries         : retry retryable failures this number of times\n"
"                          (default is 5)\n"
"\n"
"   Environment:\n"
"\n"
"   S3_ACCESS_KEY_ID     : S3 access key ID (required)\n"
"   S3_SECRET_ACCESS_KEY : S3 secret access key (required)\n"
"   S3_HOSTNAME          : specify alternative S3 host (optional)\n"
"\n"
" Commands (with <required parameters> and [optional parameters]) :\n"
"\n"
"   (NOTE: all command parameters take a value and are specified using the\n"
"          pattern parameter=value)\n"
"\n"
"   help                 : Prints this help text\n"
"\n"
"   list                 : Lists owned buckets\n"
"     [allDetails]       : Show full details\n"
"\n"
"   test                 : Tests a bucket for existence and accessibility\n"
"     <bucket>           : Bucket to test\n"
"\n"
"   create               : Create a new bucket\n"
"     <bucket>           : Bucket to create\n"
"     [cannedAcl]        : Canned ACL for the bucket (see Canned ACLs)\n"
"     [location]         : Location for bucket (for example, EU)\n"
"\n"
"   delete               : Delete a bucket or key\n"
"     <bucket>[/<key>]   : Bucket or bucket/key to delete\n"
"\n"
"   list                 : List bucket contents\n"
"     <bucket>           : Bucket to list\n"
"     [prefix]           : Prefix for results set\n"
"     [marker]           : Where in results set to start listing\n"
"     [delimiter]        : Delimiter for rolling up results set\n"
"     [maxkeys]          : Maximum number of keys to return in results set\n"
"     [allDetails]       : Show full details for each key\n"
"\n"
"   getacl               : Get the ACL of a bucket or key\n"
"     <bucket>[/<key>]   : Bucket or bucket/key to get the ACL of\n"
"     [filename]         : Output filename for ACL (default is stdout)\n"
"\n"
"   setacl               : Set the ACL of a bucket or key\n"
"     <bucket>[/<key>]   : Bucket or bucket/key to set the ACL of\n"
"     [filename]         : Input filename for ACL (default is stdin)\n"
"\n"
"   getlogging           : Get the logging status of a bucket\n"
"     <bucket>           : Bucket to get the logging status of\n"
"     [filename]         : Output filename for ACL (default is stdout)\n"
"\n"
"   setlogging           : Set the logging status of a bucket\n"
"     <bucket>           : Bucket to set the logging status of\n"
"     [targetBucket]     : Target bucket to log to; if not present, disables\n"
"                          logging\n"
"     [targetPrefix]     : Key prefix to use for logs\n"
"     [filename]         : Input filename for ACL (default is stdin)\n"
"\n"
"   put                  : Puts an object\n"
"     <bucket>/<key>     : Bucket/key to put object to\n"
"     [filename]         : Filename to read source data from "
                          "(default is stdin)\n"
"     [contentLength]    : How many bytes of source data to put (required if\n"
"                          source file is stdin)\n"
"     [cacheControl]     : Cache-Control HTTP header string to associate with\n"
"                          object\n"
"     [contentType]      : Content-Type HTTP header string to associate with\n"
"                          object\n"
"     [md5]              : MD5 for validating source data\n"
"     [contentDispositionFilename] : Content-Disposition filename string to\n"
"                          associate with object\n"
"     [contentEncoding]  : Content-Encoding HTTP header string to associate\n"
"                          with object\n"
"     [expires]          : Expiration date to associate with object\n"
"     [cannedAcl]        : Canned ACL for the object (see Canned ACLs)\n"
"     [x-amz-meta-...]]  : Metadata headers to associate with the object\n"
"\n"
"   copy                 : Copies an object; if any options are set, the "
                          "entire\n"
"                          metadata of the object is replaced\n"
"     <sourcebucket>/<sourcekey> : Source bucket/key\n"
"     <destbucket>/<destkey> : Destination bucket/key\n"
"     [cacheControl]     : Cache-Control HTTP header string to associate with\n"
"                          object\n"
"     [contentType]      : Content-Type HTTP header string to associate with\n"
"                          object\n"
"     [contentDispositionFilename] : Content-Disposition filename string to\n"
"                          associate with object\n"
"     [contentEncoding]  : Content-Encoding HTTP header string to associate\n"
"                          with object\n"
"     [expires]          : Expiration date to associate with object\n"
"     [cannedAcl]        : Canned ACL for the object (see Canned ACLs)\n"
"     [x-amz-meta-...]]  : Metadata headers to associate with the object\n"
"\n"
"   get                  : Gets an object\n"
"     <buckey>/<key>     : Bucket/key of object to get\n"
"     [filename]         : Filename to write object data to (required if -s\n"
"                          command line parameter was used)\n"
"     [ifModifiedSince]  : Only return the object if it has been modified "
                          "since\n"
"                          this date\n"
"     [ifNotmodifiedSince] : Only return the object if it has not been "
                          "modified\n"
"                          since this date\n"
"     [ifMatch]          : Only return the object if its ETag header matches\n"
"                          this string\n"
"     [ifNotMatch]       : Only return the object if its ETag header does "
                          "not\n"
"                          match this string\n"
"     [startByte]        : First byte of byte range to return\n"
"     [byteCount]        : Number of bytes of byte range to return\n"
"\n"
"   head                 : Gets only the headers of an object, implies -s\n"
"     <bucket>/<key>     : Bucket/key of object to get headers of\n"
"\n"
"   gqs                  : Generates an authenticated query string\n"
"     <bucket>[/<key>]   : Bucket or bucket/key to generate query string for\n"
"     [expires]          : Expiration date for query string\n"
"     [resource]         : Sub-resource of key for query string, without a\n"
"                          leading '?', for example, \"torrent\"\n"
"\n"
" Canned ACLs:\n"
"\n"
"  The following canned ACLs are supported:\n"
"    private (default), public-read, public-read-write, authenticated-read\n"
"\n"
" ACL Format:\n"
"\n"
"  For the getacl and setacl commands, the format of the ACL list is:\n"
"  1) An initial line giving the owner id in this format:\n"
"       OwnerID <Owner ID> <Owner Display Name>\n"
"  2) Optional header lines, giving column headers, starting with the\n"
"     word \"Type\", or with some number of dashes\n"
"  3) Grant lines, of the form:\n"
"       <Grant Type> (whitespace) <Grantee> (whitespace) <Permission>\n"
"     where Grant Type is one of: Email, UserID, or Group, and\n"
"     Grantee is the identification of the grantee based on this type,\n"
"     and Permission is one of: READ, WRITE, READ_ACP, or FULL_CONTROL.\n"
"\n"
"  Note that the easiest way to modify an ACL is to first get it, saving it\n"
"  into a file, then modifying the file, and then setting the modified file\n"
"  back as the new ACL for the bucket/object.\n"
"\n"
" Date Format:\n"
"\n"
"  The format for dates used in parameters is as ISO 8601 dates, i.e.\n"
"  YYYY-MM-DDTHH:MM:SS[+/-dd:dd].  Examples:\n"
"      2008-07-29T20:36:14\n"
"      2008-07-29T20:36:14-06:00\n"
"      2008-07-29T20:36:14+11:30\n"
	  "\n");

  exit(-1);
}



static int should_retry()
{
  if (retriesG--) {
    // Sleep before next retry; start out with a 1 second sleep
    static int retrySleepInterval = 1 * SLEEP_UNITS_PER_SECOND;
    sleep(retrySleepInterval);
    // Next sleep 1 second longer
    retrySleepInterval++;
    return 1;
  }

  return 0;
}

static void growbuffer_destroy(growbuffer *gb)
{
  growbuffer *start = gb;

  while (gb) {
    growbuffer *next = gb->next;
    free(gb);
    gb = (next == start) ? 0 : next;
  }
}


//----EDIT----
int set_access_key_id(const char *access_key_id)
{
  if ( !access_key_id )
    return 1;
  if ( !(accessKeyIdG = (char *)malloc(strlen(access_key_id))) )
    return 2;
  strcpy((char *)accessKeyIdG,access_key_id);
  return 0;
}

int set_secret_access_key(const char *secret_access_key)
{
  if ( !secret_access_key )
    return 1;
  if ( !(secretAccessKeyG = (char *)malloc(strlen(secret_access_key))) )
    return 2;
  strcpy((char *)secretAccessKeyG,secret_access_key);
  return 0;
}
int set_uri_style(S3UriStyle uri_style)
{
  uriStyleG = uri_style;
  return 0;
}
//------------

// TODO: this is terribad
//   bucket: name of the bucket
//   object: name of the object (without the bucket prefix)
//   obj_buf: a pointer to the object being stored
//   obj_len: the length of the object being stored
//   return: error value
int put_object (const char *bucket, const char *object, const char *obj_buf, const int obj_len)
{
  //----EDIT----
  const char *bucket_name = bucket;
  const char *obj_name = object;
  //------------

  // a bunch of default parameters that are used later, add them in as valid options later on
  uint64_t contentLength = 0;
  const char *cacheControl = 0, *contentType = 0, *md5 = 0;
  const char *contentDispositionFilename = 0, *contentEncoding = 0;
  int64_t expires = -1;
  S3CannedAcl cannedAcl = S3CannedAclPrivate;
  int metaPropertiesCount = 0;
  S3NameValue metaProperties[S3_MAX_METADATA_COUNT];
  int noStatus = 0;

  put_object_callback_data data;
  data.infile = 0;
  data.gb = 0;
  data.noStatus = noStatus;

  // Read from stdin.  If contentLength is not provided, we have
  // to read it all in to get contentLength.
  if (!contentLength) {
    // Read all if stdin to get the data
    int len = obj_len;
    const char *buffer = obj_buf;
    int max_len = 64 * 1024;
    while (1) {
      int amtRead = len < max_len ? len : max_len;
      if (amtRead == 0) {
	break;
      }

      if (!growbuffer_append(&(data.gb), buffer, amtRead)) {
	// this function call appends buffer to structure...NN
	fprintf(stderr, "\nERROR: Out of memory while reading "
		"stdin\n");
	return 1;
      }
      contentLength += amtRead;
      if (amtRead < max_len) {
	break;
      }
      buffer += max_len;
      len -= max_len;
    }
  }
  else
  {
    data.infile = stdin;
  }

  data.contentLength = data.originalContentLength = contentLength;

  S3_init();

  S3BucketContext bucketContext =
    {
      0,
      bucket_name,
      protocolG,
      uriStyleG,
      accessKeyIdG,
      secretAccessKeyG
    };
  S3PutProperties putProperties =
    {
      contentType,
      md5,
      cacheControl,
      contentDispositionFilename,
      contentEncoding,
      expires,
      cannedAcl,
      metaPropertiesCount,
      metaProperties
    };
  S3PutObjectHandler putObjectHandler =
    {
      { &responsePropertiesCallback, &responseCompleteCallback },
      &putObjectDataCallback
    };

  //----EDIT----
  if ( !accessKeyIdG )
  {
    fprintf(stderr,"ERROR: access key ID is not set (probably need to set environment variable)\n");
    return 2;
  }
  if ( !secretAccessKeyG )
  {
    fprintf(stderr,"ERROR: secret access key is not set (probably need to set environment variable)\n");
    return 3;
  }
  if ( uriStyleG != S3UriStyleVirtualHost )
  {
    fprintf(stderr,"WARNING: URI style is not set to S3UriStyleVirtualHost. You may get an invalid signature error if you access a bucket outside the default region\n");
  }
  //------------

  do {
    S3_put_object(&bucketContext, obj_name, contentLength, &putProperties, 0,
		  &putObjectHandler, &data);
  }
  while (S3_status_is_retryable(statusG) && should_retry());

  // cleanup
  if (data.infile) {
    fclose(data.infile);
  }
  else if (data.gb) {
    growbuffer_destroy(data.gb);
  }

  if (statusG != S3StatusOK) {
    printError();
    //----EDIT----
    return 4;
    //------------
  }
  else if (data.contentLength) {
    fprintf(stderr, "\nERROR: Failed to read remaining %llu bytes from "
	    "input\n", (unsigned long long) data.contentLength);
  }

  S3_deinitialize();
  return 0;
}



// TODO: aaaaaaugh, naveeeeed sigh.... fix it
static S3Status getObjectDataCallback(int bufferSize, const char *buffer,
                                      void *callbackData)
{
  //FILE *outfile = (FILE *) callbackData;
  //
  //  size_t wrote = fwrite(buffer, 1, bufferSize, outfile);

  char **buffer_p = (char **)callbackData;
  int oldbufsize = *(int *)buffer_p[0];
  if ( oldbufsize == 0 )
    buffer_p[1] = (char *)malloc(bufferSize);
  else
    buffer_p[1] = (char *)realloc(buffer_p[1],oldbufsize+bufferSize);
  *(int *)buffer_p[0] += bufferSize;
  memcpy(buffer_p[1] + oldbufsize,buffer,bufferSize);



  //char* ptr1; int * ptr2;
  //    ptr1 = * outfile; ptr2 =  outfile[1];
  //    * ptr2 += bufferSize;
  //    * ptr1


  // =================================================================
  /*
     int size_new_buffer = bufferSize + 5 +1;
    char cc = *buffer; char cc2[60]; //= *buffer [1:5 ];
    char * cc3[bufferSize];
 //    printf ("buffer first character: %c \n", cc);
 //cc2 = *buffer;
 //   printf ( "char is %c  s\n", (cc2) [2] );
 //strcopy (cc2, "" );
    strcpy (cc2, buffer );  //"abcLpqr");
//    strcat (cc2, ".TXTT");

 //    printf ("char is %c t\n", cc2 [2]);

     int i; printf (" << ");
    for (i=0; i<30; i++)
    printf ("%c", cc2[i]); // Buffer printing here
                printf(" >>\n");
  //


//    char* Aargv[3];
//    Aargv[0] =  (char *)malloc(strlen("get")+1);
//    strcpy(Aargv[0],"get");

// */
  // =================================================================
  return ((*(int *)buffer_p[0] < oldbufsize+bufferSize) ?
	  S3StatusAbortedByCallback : S3StatusOK);

  //    return ((wrote < (size_t) bufferSize) ?
  //            S3StatusAbortedByCallback : S3StatusOK);
}

// TODO: this is terribad as well, do something similar to how you fix put_object
//   argv: "<bucket>/<object>"
//   data_length: a pointer to an integer where the length of the retrieved object will be stored
//   returns a pointer to the retrieved object
char* get_object (char *argv, int *data_length)
//(int argc, char **argv, int optindex, char **buffer_p)
{
  int argc = 1; int optindex=0;

  char **buffer_p = (char **)malloc(2*sizeof(char *));
  //     buffer_p[0] = (char *)malloc(sizeof(int));
  buffer_p[0] = (char *) data_length;
  buffer_p[1] = NULL;
  *((int *)buffer_p[0]) = 0;

  if (optindex == argc) {
    fprintf(stderr, "\nERROR: Missing parameter: bucket/key\n");
    usageExit(stderr); // printf("Ssss\n");
  }

  // Split bucket/key
  char *slash = argv  ; // [optindex];    // (581)
  while (*slash && (*slash != '/')) {
    slash++;
  }

  if (!*slash || !*(slash + 1)) {
    fprintf(stderr, "\nERROR: Invalid bucket/key name: %s\n", argv);
    //  argv[optindex]);   // (581)
    usageExit(stderr);
  }
  *slash++ = 0;


  const char *bucket_name = argv ; optindex++; //[optindex++];    // (581)
  const char *obj_name = slash;

  int64_t ifModifiedSince = -1, ifNotModifiedSince = -1;
  const char *ifMatch = 0, *ifNotMatch = 0;
  uint64_t startByte = 0, byteCount = 0;

  /*    // comment block (878)
    while (optindex < argc) {
        char *param = argv [optindex++];
        printf ( "argc is = %d, optindex is = %d \n", argc, optindex);
        if (!strncmp(param, FILENAME_PREFIX, FILENAME_PREFIX_LEN)) {
        printf("\nY\n");
            filename = &(param[FILENAME_PREFIX_LEN]);
            printf( filename);
        }
        else if (!strncmp(param, IF_MODIFIED_SINCE_PREFIX,
                     IF_MODIFIED_SINCE_PREFIX_LEN)) {
            // Parse ifModifiedSince
            ifModifiedSince = parseIso8601Time
                (&(param[IF_MODIFIED_SINCE_PREFIX_LEN]));
            if (ifModifiedSince < 0) {
                fprintf(stderr, "\nERROR: Invalid ifModifiedSince time "
                        "value; ISO 8601 time format required\n");
                usageExit(stderr);
            }
        }
        else if (!strncmp(param, IF_NOT_MODIFIED_SINCE_PREFIX,
                          IF_NOT_MODIFIED_SINCE_PREFIX_LEN)) {
            // Parse ifModifiedSince
            ifNotModifiedSince = parseIso8601Time
                (&(param[IF_NOT_MODIFIED_SINCE_PREFIX_LEN]));
            if (ifNotModifiedSince < 0) {
                fprintf(stderr, "\nERROR: Invalid ifNotModifiedSince time "
                        "value; ISO 8601 time format required\n");
                usageExit(stderr);
            }
        }
        else if (!strncmp(param, IF_MATCH_PREFIX, IF_MATCH_PREFIX_LEN)) {
            ifMatch = &(param[IF_MATCH_PREFIX_LEN]);
        }
        else if (!strncmp(param, IF_NOT_MATCH_PREFIX,
                          IF_NOT_MATCH_PREFIX_LEN)) {
            ifNotMatch = &(param[IF_NOT_MATCH_PREFIX_LEN]);
        }
        else if (!strncmp(param, START_BYTE_PREFIX, START_BYTE_PREFIX_LEN)) {
            startByte = convertInt
                (&(param[START_BYTE_PREFIX_LEN]), "startByte");
        }
        else if (!strncmp(param, BYTE_COUNT_PREFIX, BYTE_COUNT_PREFIX_LEN)) {
            byteCount = convertInt
                (&(param[BYTE_COUNT_PREFIX_LEN]), "byteCount");
        }
        else {
            fprintf(stderr, "\nERROR: Unknown param: %s\n", param);
            usageExit(stderr);
        }
    }
  */  // comment block (878)

  /*  // comment out block of outfile  (877)
    FILE *outfile = 0;

    if (filename) {
        // Stat the file, and if it doesn't exist, open it in w mode
        struct stat buf;
        if (stat(filename, &buf) == -1) {
            outfile = fopen(filename, "w" FOPEN_EXTRA_FLAGS);
            printf ("write mode\n");
        }
        else {
            // Open in r+ so that we don't truncate the file, just in case
            // there is an error and we write no bytes, we leave the file
            // unmodified
            outfile = fopen(filename, "r+" FOPEN_EXTRA_FLAGS);

        }

        if (!outfile) {
            fprintf(stderr, "\nERROR: Failed to open output file %s: ",
                    filename);
            perror(0);
            exit(-1);
        }
    }
    else if (showResponsePropertiesG) {
        fprintf(stderr, "\nERROR: get -s requires a filename parameter\n");
        usageExit(stderr);
    }
    else {
        outfile = stdout;
    }
  */   // (877)

  // char ** outfile = (char** ) malloc(sizeof(char *) * 2); //NN
  // new modification here:
  /*
    int bufsize, i;

      char *buffer = "Hello, world";

      char **buffer_p = (char **)malloc(2*sizeof(char *));
      buffer_p[0] = (char *)malloc(sizeof(int));
      buffer_p[1] = NULL;
      *((int *)buffer_p[0]) = 0;


      printf("bufsize = %d\n",((int *)buffer_p[0])[0]);

      func(strlen(buffer)+1,buffer,buffer_p);

      bufsize = ((int *)buffer_p[0])[0];
      printf("bufsize = %d\n",bufsize);
      printf("buffer = ");
      for ( i=0; i<bufsize; i++ )
        printf("%c",buffer_p[1][i]);
      printf("\n");

      func(strlen(buffer)+1,buffer,buffer_p);
      bufsize = ((int *)buffer_p[0])[0];
      printf("bufsize = %d\n",bufsize);
      printf("buffer = ");
      for ( i=0; i<bufsize; i++ )
        printf("%c",buffer_p[1][i]);
      printf("\n");

  */

  // modification stop


  S3_init();

  S3BucketContext bucketContext =
    {
      0,
      bucket_name,
      protocolG,
      uriStyleG,
      accessKeyIdG,
      secretAccessKeyG
    };

  S3GetConditions getConditions =
    {
      ifModifiedSince,
      ifNotModifiedSince,
      ifMatch,
      ifNotMatch
    };

  S3GetObjectHandler getObjectHandler =
    {
      { &responsePropertiesCallback, &responseCompleteCallback },
      &getObjectDataCallback
    };

  //----EDIT----
  if ( !accessKeyIdG )
  {
    fprintf(stderr,"ERROR: access key ID is not set (probably need to set environment variable)\n");
    exit(0);
  }
  if ( !secretAccessKeyG )
  {
    fprintf(stderr,"secret access key is not set (probably need to set environment variable)\n");
    exit(0);
  }
  //------------

  do {
    // TODO: awkward
    S3_get_object(&bucketContext, obj_name, &getConditions, startByte,
		  byteCount, 0, &getObjectHandler, buffer_p);
		  //           byteCount, 0, &getObjectHandler, outfile);
  }
  while (S3_status_is_retryable(statusG) && should_retry());

  if (statusG != S3StatusOK) {
    printError();
    //----EDIT----
    exit(0);
    //------------
  }

  // TODO: add back in once figured out
//  if ( outfile )
//    fclose(outfile);

  S3_deinitialize();

  // TODO: baaaad
  //   * data_length= *(int *)buffer_p[0];
  // data_length = buff
  return buffer_p[1];
}




//int main () // ( int argc, char * argv[]  )
//// now this can be called without passing command-line arguments
//
//// int main ( int argc, char * argv[] )
//// // call using this in cygwin:
//// // ./a.exe bucket_one_n/sanafile.txt
//
//{
//  int num_fragments = 5, threshold = 3;
//  int fragment_length = 4;
//  char **fragments;
//
//  accessKeyIdG = getenv("S3_ACCESS_KEY_ID");
//  secretAccessKeyG = getenv("S3_SECRET_ACCESS_KEY");
//
//  // / *
//  char * Aargv ;  // [1];//[1];
//  char * AAargv;//[1];
//  char * Argv;
//  //    Aargv[0] =  (char *)malloc(strlen("get")+1);
//  //   strcpy(Aargv[0],"get");
//
//  Aargv  =  (char *)malloc(strlen("bucket_one_n/s3_func_3.c")+1);
//  strcpy(Aargv  ,"bucket_one_n/s3_func_3.c");
//
//  AAargv  =  (char *)malloc(strlen("bucket_one_n/file_put987.txt")+1);
//  strcpy(AAargv ,"bucket_one_n/file_put987.txt");
//
//  //     AAargv[1] =  (char *)malloc(strlen("filename=foob4.txt")+1);
//  //     strcpy(AAargv[1],"filename=foob4.txt");
//
//
//  //   argv[2] =  (char *)malloc(strlen("filename=COPYING")+1);
//  //   strcpy(argv[2],"filename=COPYING");
//
//
//  //argv[1] =  "bucket_one_n/file_put";
//  //argv[2] =  "filename=LICENSE";
//  //   return;
//  // */
//
//  //   put_object ( 3,  argv,1 );
//  int argc = 2;
//  char *rit_buf = "Sentence 1 of file uploaded \n  LIne 2 \n LIne 3 \nL4))";
//  int rit_len = strlen(rit_buf)-2;
//
//  //      put_object (argc, AAargv, 1 , rit_buf);
//
//
//  put_object (AAargv, rit_buf,   rit_len  );
//
//
//
//
//  //  put_object ( "bucket_one_n/testfileAA.c", rit_buf);
//  printf ("done\n ===========\n===\n"); //return;
//  int bufsize, i;
//
//
//  printf("ss\n"); char * buffer_retrieved;
//  int * data_length = (int *) malloc(sizeof(int));
//  buffer_retrieved = get_object(Aargv ,  data_length);
//
//  printf ("data_length is: %d\n", *data_length);
//  //  get_object(2, Aargv, 0, buffer_p);
//  printf ("\n|----------------|;");
//  char * bucketfilenamePath ; // = "bucket_one_n/s3_func_3.c";
//
//  bucketfilenamePath  =  (char *)malloc(strlen("bucket_one_n/s3_func_3.c")+1);
//  strcpy(bucketfilenamePath ,"bucket_one_n/s3_func_3.c");
//  printf ("Try two ! ------------\n------\n" );
//  //      get_object(bucketfilenamePath, buffer_p);
//
//  // / *
//  bufsize = * data_length; //((int *)buffer_p[0])[0];
//  printf("bufsize = %d\n",bufsize);
//
//  printf("buffer = <");
//  for ( i=0; i<bufsize; i++ )
//    printf("%c",buffer_retrieved[i]);
//  printf(">\n");
//  //*/
//
//
//  /*
//       bufsize = ((int *)buffer_p[0])[0];
//       printf("bufsize = %d\n",bufsize);
//
//       printf("buffer = <");
//       for ( i=0; i<bufsize; i++ )
//         printf("%c",buffer_p[1][i]);
//       printf(">\n");
//  */
//
//  printf (" Done it all. \n");
//  //  S3_init();
//  //  put_object(fragment_name,fragments,num_fragments,fragment_length,threshold);
//  //  get_object(fragment_name,num_fragments,threshold);
//
//}
