#include <stdio.h>
#include <string.h>
#include <curl/curl.h>

#include "street_cred.h"


void write_to_db(char *influxdb_line_protocol)
{
  const char *db_url = "http://optiplex:8086";
  const char *org = "house";
  const char *bucket = "house_controller";
  // const char *api_token = "nope";  <--- defined in unversioned street_cred.h

  char url[512];
  snprintf(url, sizeof(url), "%s/api/v2/write?org=%s&bucket=%s&precision=ns", db_url, org, bucket);

  CURL *curl = curl_easy_init();
  if (!curl) {
    printf("Failed to init curl\n");
    return;
  }

  // now make the headers. there are a few
  struct curl_slist * hdrs = NULL;
  char auth_hdr[1024];
  snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Token %s", api_token);

  hdrs = curl_slist_append(hdrs, auth_hdr);
  hdrs = curl_slist_append(hdrs, "Content-Type: text/plain; charset=utf-8");
  hdrs = curl_slist_append(hdrs, "Accept: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, influxdb_line_protocol);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(influxdb_line_protocol));

  CURLcode result = curl_easy_perform(curl);
  if (result != CURLE_OK)
    fprintf(stderr, "curl perform failed: %s\n", curl_easy_strerror(result));
  else {
    long http_ret = 0;
    result = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_ret);
    if (result != CURLE_OK)
      fprintf(stderr, "curl_easy_getinfo failed: %s\n", curl_easy_strerror(result));
    else {
      if(http_ret < 200 || http_ret >= 300) {
        fprintf(stderr, "Influx write failed: HTTP %ld\n", http_ret);
      }
    }
  }

  curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);
}
