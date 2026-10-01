#include "counter.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cJSON.h"
#include <curl/curl.h>


struct StringWSize {
	char *mem;
	size_t size;
};

size_t write_chunk_callback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct StringWSize *s = (struct StringWSize * )userp;

	char *ptr = realloc(s->mem, s->size + realsize + 1);
	if (ptr == NULL) {
		fprintf(stderr, "Not enough memory (realloc returned NULL)!\n");
		return 0;
	}

	s->mem = ptr;
	memcpy( &(s->mem[s->size]), contents, realsize);
	s->size += realsize;
	s->mem[s->size] = '\0';


	return realsize;
}


const char *BASE_URL = "https://crates.io";
const char *CRATES_API_URL = "api/v1/crates";

void build_dep_url(char **url, char const *crate_name, char const *version);
void get_version(char **version, CURL *curl, char const *crate_name);
int check_crate(CURL *curl, char const *url, int dep);


int count(char const *crate_name, int depth) { // depth - how deep count dependencies
	curl_global_init(CURL_GLOBAL_ALL);

	CURL *curl = curl_easy_init();
	if (curl == NULL) {
		fprintf(stderr, "Cannot initialize libcurl!\n");
		return 1;
	}

	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "rusty-dep-counter");


	char *version = NULL;
	get_version(&version, curl, crate_name);
	if (version == NULL) {
		fprintf(stderr, "Cannot get version for %s!\n", crate_name);
		goto err;
	}

	char *url = NULL;
	build_dep_url(&url, crate_name, version);
	if (url == NULL) {
		fprintf(stderr, "Cannot build deps url for %s!\n", crate_name);
		free(version);
		goto err;
	}
	if ( check_crate(curl, url, depth) ) {
		free(version);
		free(url);
		goto err;
	}


	curl_easy_cleanup(curl);
	curl_global_cleanup();
	return 0;


err:
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	return 1;
}


CURLcode get(CURL *curl, struct StringWSize *s, char const *url) {
	s->mem = malloc(1);
	s->size = 0;
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)s);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_chunk_callback);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	return curl_easy_perform(curl);
}


int check_crate(CURL *curl, char const *url, int depth) {
	struct StringWSize s;
	CURLcode result = get(curl, &s, url);
	if (result != CURLE_OK) {
		fprintf(stderr, "Curl error: %s\n", curl_easy_strerror(result));
		goto err;
	}

	cJSON *json = cJSON_Parse(s.mem);
	if (json == NULL) {
		goto err;
	}

	cJSON *deps_obj = cJSON_GetObjectItem(json, "dependencies");
	if (deps_obj == NULL) {
		cJSON_Delete(json);
		goto err;
	}

	cJSON *dep_obj = NULL;
	unsigned int total = 0;
	cJSON_ArrayForEach(dep_obj, deps_obj) {
		cJSON *name_obj = cJSON_GetObjectItem(dep_obj, "crate_id");
		char const *name = cJSON_GetStringValue(name_obj);
		printf("%s\n", name);
		total++;
	}
	printf("Depth: %d, Total: %d\n", depth, total);


	cJSON_Delete(json);
	free(s.mem);
	return 0;

err:
	free(s.mem);
	return 1;
}


void get_version(char **version, CURL *curl, char const *crate_name) {
	char *url;
	asprintf(&url, "%s/%s/%s", BASE_URL, CRATES_API_URL, crate_name);
	if (url == NULL) {
		return;
	}


	struct StringWSize s;
	CURLcode result = get(curl, &s, url);
	if (result != CURLE_OK) {
		fprintf(stderr, "Curl error: %s\n", curl_easy_strerror(result));
		free(s.mem);
		free(url);
		return;
	}

	cJSON *json = cJSON_Parse(s.mem);
	if (json == NULL) {
		free(url);
		goto end;
	}

	cJSON *crate_obj = cJSON_GetObjectItem(json, "crate");
	cJSON *version_obj = cJSON_GetObjectItem(crate_obj, "default_version");
	if (version_obj == NULL) {
		free(url);
		goto end;
	}

	char const *tmp = cJSON_GetStringValue(version_obj);
	if (tmp == NULL) {
		goto end;
	}
	
	*version = strdup(tmp);


end:
	cJSON_Delete(json);
	free(s.mem);
}


void build_dep_url(char **url, char const *crate_name, char const *version) {
	asprintf(url, "%s/%s/%s/%s/dependencies", BASE_URL, CRATES_API_URL, crate_name, version);
}
