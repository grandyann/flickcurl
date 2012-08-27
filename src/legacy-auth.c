/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * legacy-auth.c - Flickr Legacy authentication
 *
 * Copyright (C) 2007-2012, David Beckett http://www.dajobe.org/
 *
 * This file is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License (LGPL) V2.1 or any newer version
 *   2. GNU General Public License (GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 *
 * You may not use this file except in compliance with at least one of
 * the above three licenses.
 *
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * complete terms and further detail along with the license texts for
 * the licenses in COPYING.LIB, COPYING and LICENSE-2.0.txt respectively.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WIN32
#include <win32_flickcurl_config.h>
#endif

#include <flickcurl.h>
#include <flickcurl_internal.h>



static int
compare_args(const void *a, const void *b)
{
  return strcmp(*(char**)a, *(char**)b);
}


static void
flickcurl_sort_args(flickcurl *fc)
{
  qsort((void*)fc->parameters, fc->count, sizeof(char*[2]), compare_args);
}


int
flickcurl_legacy_prepare_common(flickcurl *fc,
                                const char* url,
                                const char* method,
                                const char* upload_field,
                                const char* upload_value,
                                int parameters_in_url, int need_auth)
{
  int i;
  char *md5_string = NULL;
  size_t* values_len = NULL;
  unsigned int fc_uri_len = 0;
  
  if(!url)
    return 1;
 
  /* If one is given, both are required */
  if((upload_field || upload_value) && (!upload_field || !upload_value))
    return 1;
 
  fc->failed = 0;
  fc->error_code = 0;
  if(fc->error_msg) {
    free(fc->error_msg);
    fc->error_msg = NULL;
  }
  /* Default to read */
  fc->is_write = 0;
  /* Default to no data */
  if(fc->data) {
    if(fc->data_is_xml)
      xmlFree(fc->data);
    fc->data = NULL;
    fc->data_length = 0;
    fc->data_is_xml = 0;
  }
  if(fc->param_fields) {
    for(i = 0; fc->param_fields[i]; i++) {
      free(fc->param_fields[i]);
      free(fc->param_values[i]);
    }
    free(fc->param_fields);
    free(fc->param_values);
    fc->param_fields = NULL;
    fc->param_values = NULL;
    fc->parameter_count = 0;
  }
  if(fc->upload_field) {
    free(fc->upload_field);
    fc->upload_field = NULL;
  }
  if(fc->upload_value) {
    free(fc->upload_value);
    fc->upload_value = NULL;
  }
 
  if(!fc->secret) {
    flickcurl_error(fc, "No legacy Flickr auth secret");
    return 1;
  }
  if(!fc->api_key) {
    flickcurl_error(fc, "No API Key (OAuth Client Key)");
    return 1;
  }

 
  if(fc->method)
    free(fc->method);
  if(method)
    fc->method = strdup(method);
  else
    fc->method = NULL;

  if(fc->method)
    flickcurl_add_param(fc, "method", fc->method);

  flickcurl_add_param(fc, "api_key", fc->api_key);

  if(need_auth && fc->auth_token)
    flickcurl_add_param(fc, "auth_token", fc->auth_token);

  flickcurl_end_params(fc);

  /* +1 for api_sig +1 for NULL terminating pointer */
  fc->param_fields = (char**)calloc(fc->count + 2, sizeof(char*));
  fc->param_values = (char**)calloc(fc->count + 2, sizeof(char*));
  values_len = (size_t*)calloc(fc->count + 2, sizeof(size_t));

  if((need_auth && fc->auth_token) || fc->sign)
    flickcurl_sort_args(fc);

  fc_uri_len = strlen(url);
 
  /* Save away the parameters and calculate the value lengths */
  for(i = 0; fc->parameters[i][0]; i++) {
    size_t param_len = strlen(fc->parameters[i][0]);

    if(fc->parameters[i][1])
      values_len[i] = strlen(fc->parameters[i][1]);
    else {
      values_len[i] = 0;
      fc->parameters[i][1] = "";
    }
    fc->param_fields[i] = (char*)malloc(param_len+1);
    strcpy(fc->param_fields[i], fc->parameters[i][0]);
    fc->param_values[i] = (char*)malloc(values_len[i]+1);
    strcpy(fc->param_values[i], fc->parameters[i][1]);

    /* 3x value len is conservative URI %XX escaping on every char */
    fc_uri_len += param_len + 1 /* = */ + 3 * values_len[i];
  }

  if(upload_field) {
    fc->upload_field = (char*)malloc(strlen(upload_field)+1);
    strcpy(fc->upload_field, upload_field);

    fc->upload_value = (char*)malloc(strlen(upload_value)+1);
    strcpy(fc->upload_value, upload_value);
  }

  if((need_auth && fc->auth_token) || fc->sign) {
    size_t buf_len = 0;
    char *buf;
   
    buf_len = strlen(fc->secret);
    for(i = 0; fc->parameters[i][0]; i++)
      buf_len += strlen(fc->parameters[i][0]) + values_len[i];

    buf = (char*)malloc(buf_len+1);
    strcpy(buf, fc->secret);
    for(i = 0; fc->parameters[i][0]; i++) {
      strcat(buf, fc->parameters[i][0]);
      strcat(buf, fc->parameters[i][1]);
    }
   
#ifdef FLICKCURL_DEBUG
    fprintf(stderr, "MD5 Buffer '%s'\n", buf);
#endif
    md5_string = MD5_string(buf);
   
    flickcurl_add_param(fc, "api_sig", md5_string);
    fc->count--;

    /* Add a new parameter pair */
    values_len[fc->count] = 32; /* MD5 is always 32 */
    fc->param_fields[fc->count] = (char*)malloc(7+1); /* 7 = strlen(api_sig) */
    strcpy(fc->param_fields[fc->count], fc->parameters[fc->count][0]);
    fc->param_values[fc->count] = (char*)malloc(32+1); /* 32 = MD5 */
    strcpy(fc->param_values[fc->count], fc->parameters[fc->count][1]);

    fc_uri_len += 7 /* "api_sig" */ + 1 /* = */ + 32 /* MD5 value: never escaped */;
    
    fc->count++;
   
#ifdef FLICKCURL_DEBUG
    fprintf(stderr, "Signature: '%s'\n", fc->parameters[fc->count - 1][1]);
#endif
   
    free(buf);
   
    flickcurl_end_params(fc);
  }

  /* add &s between parameters */
  fc_uri_len += fc->count - 1;

  /* reuse or grow uri buffer */
  if(fc->uri_len < fc_uri_len) {
    free(fc->uri);
    fc->uri = (char*)malloc(fc_uri_len+1);
    fc->uri_len = fc_uri_len;
  }
  strcpy(fc->uri, url);

  if(parameters_in_url) {
    for(i = 0; fc->parameters[i][0]; i++) {
      char *value = (char*)fc->parameters[i][1];
      char *escaped_value = NULL;

      if(!fc->parameters[i][1])
        continue;

      strcat(fc->uri, fc->parameters[i][0]);
      strcat(fc->uri, "=");
      if(!strcmp(fc->parameters[i][0], "method")) {
        /* do not touch method name */
      } else
        escaped_value = curl_escape(value, values_len[i]);

      if(escaped_value) {
        strcat(fc->uri, escaped_value);
        curl_free(escaped_value);
      } else
        strcat(fc->uri, value);
      strcat(fc->uri, "&");
    }

    /* zap last & */
    fc->uri[strlen(fc->uri)-1]= '\0';
  }

#ifdef FLICKCURL_DEBUG
  fprintf(stderr, "URI is '%s'\n", fc->uri);

  FLICKCURL_ASSERT((strlen(fc->uri) == fc_uri_len),
                   "Final URI does not match expected length");
#endif

  if(md5_string)
    free(md5_string);

  if(values_len)
    free(values_len);

  return 0;
}


