#ifndef S3_H
#define S3_H

#include "libs3.h"

extern const char *accessKeyIdG;
extern const char *secretAccessKeyG;
extern S3UriStyle uriStyleG;

int set_access_key_id(const char *access_key_id);
int set_secret_access_key(const char *secret_access_key);
int set_uri_style(S3UriStyle uri_style);

char* get_object (char *argv, int* data_length);
int put_object (const char *bucket, const char *object, const char *obj_buf, const int obj_len);

#endif
