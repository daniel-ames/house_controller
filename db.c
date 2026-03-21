#include <stdio.h>
#include <string.h>
#include <curl/curl.h>


void write_to_db(char *influxdb_line_protocol)
{
  const char *db_url = "http://optiplex:8086";
  const char *org = "house";
  const char *bucket = "house_controller";
  const char *api_token = "nHh-S1f5Wlz_4LvsS0q9sBxsFR3u5UsTEnZDEgXuTn4JWn-8VESGJ-aW5yvYRY4vEXWb8ApZFW6qA9G3dicpWA==";

  char url[512];
  snprintf(url, sizeof(url), "%s/api/v2/write?org=%s&bucket=%s&precision=s", db_url, org, bucket);

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
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_ret);
    printf("HTTP response: %ld\n", http_ret);
  }

  curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);
}
