#include "counter.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>

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

void rate_limit_wait();
CURLcode get(CURL *curl, struct StringWSize *s, char const *url) {
	s->mem = malloc(1);
	s->size = 0;
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)s);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_chunk_callback);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	rate_limit_wait();
	return curl_easy_perform(curl);
}

struct timespec last_requested_time = {0, 0};
bool first_request = true;
void rate_limit_wait() {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	if (first_request) {
		first_request = false;
		last_requested_time = now;
		return;
	}


	double elapsed = (now.tv_sec - last_requested_time.tv_sec)
		+ (now.tv_nsec - last_requested_time.tv_nsec) / 1e9;

	if (elapsed < 1.0) {
		double remaining = 1.0 - elapsed;
		struct timespec sleep_ts;
		sleep_ts.tv_sec = (time_t)remaining;
		sleep_ts.tv_nsec = (long)((remaining - (time_t)remaining) * 1e9);

		while (nanosleep(&sleep_ts, &sleep_ts) == -1 && errno == EINTR) {
		}

		clock_gettime(CLOCK_MONOTONIC, &now);
	}

	last_requested_time = now;
}



struct Dependency {
	struct Crate *crate;
	char *crate_id;
	char *required_version;
	bool optional;
};
struct Crate {
	char *name;
	char *version;
	struct Dependency *dependencies;
	size_t dependencies_len;
};
void free_crate(struct Crate *crate) {
	if (crate == NULL)
		return;

	for (size_t i = 0; i < crate->dependencies_len; i++) {
		struct Dependency *d = crate->dependencies + i;

		free_crate(d->crate);
		free(d->crate_id);
		free(d->required_version);
	}
	free(crate->dependencies);

	free(crate->version);
	free(crate->name);
}
void print_crate(struct Crate *crate) {
	if (crate == NULL)
		return;

	printf("Name: %s, version: %s\n\n", crate->name, crate->version);

	printf("Dependencies: (%zd)\n", crate->dependencies_len);
	for (size_t i = 0; i< crate->dependencies_len; i++) {
		struct Dependency *d = crate->dependencies + i;

		printf("Dep-name: %s, req-version: %s, optional: %s\n",
			d->crate_id,
			d->required_version,
			d->optional ? "true" : "false"
		);

		print_crate(d->crate);
	}
}

const char *BASE_URL = "https://crates.io";
const char *CRATES_API_URL = "api/v1/crates";

void build_dep_url(char **url, char const *crate_name, char const *version);
int get_crate(CURL *curl, struct Crate **crate, char const *crate_name, int depth);
void get_version(char **version, CURL *curl, char const *crate_name);
int get_dependencies(CURL *curl, char const *url, struct Dependency **deps, size_t *deps_len);


int count(char const *crate_name, int depth) { // depth - how deep count dependencies
	curl_global_init(CURL_GLOBAL_ALL);

	CURL *curl = curl_easy_init();
	if (curl == NULL) {
		fprintf(stderr, "Cannot initialize libcurl!\n");
		return 1;
	}

	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "rusty-dep-counter");


	struct Crate *crate = NULL;
	if ( get_crate(curl, &crate, crate_name, depth) ) {
		goto err;
	}
	print_crate(crate);


	free_crate(crate);
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	return 0;


err:
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	return 1;
}


int get_crate(CURL *curl, struct Crate **crate, char const *crate_name, int depth) {
	*crate = malloc(sizeof(struct Crate));
	char *name = strdup(crate_name);

	char *version = NULL;
	get_version(&version, curl, name);
	if (version == NULL) {
		fprintf(stderr, "Cannot get version for %s!\n", name);
		return 1;
	}

	char *url = NULL;
	build_dep_url(&url, name, version);
	if (url == NULL) {
		fprintf(stderr, "Cannot build deps url for %s!\n", name);
		free(version);
		return 1;
	}

	struct Dependency *deps = NULL;
	size_t deps_len = 0;
	if ( get_dependencies(curl, url, &deps, &deps_len) ) {
		free(url);
		free(version);
		return 1;
	}
	free(url);

	(*crate)->name = name;
	(*crate)->version = version;
	(*crate)->dependencies = deps;
	(*crate)->dependencies_len = deps_len;

	if (depth > 0) {
		for (size_t i = 0; i < deps_len; i++) {
			struct Dependency *d = deps + i;
			get_crate(curl, &d->crate, d->crate_id, depth - 1);
		}
	}


	return 0;
}


int get_dependencies(CURL *curl, char const *url, struct Dependency **deps, size_t *deps_len) {
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
	int total_deps = cJSON_GetArraySize(deps_obj);
	struct Dependency *dependencies = malloc( total_deps * sizeof(struct Dependency) );

	int current_dep = 0;
	cJSON_ArrayForEach(dep_obj, deps_obj) {
		struct Dependency *dependency = dependencies + current_dep;
		current_dep++;

		dependency->crate = NULL;

		cJSON *name_obj = cJSON_GetObjectItem(dep_obj, "crate_id");
		char const *n = cJSON_GetStringValue(name_obj);
		dependency->crate_id = strdup(n);

		cJSON *version_obj = cJSON_GetObjectItem(dep_obj, "req");
		char const *v = cJSON_GetStringValue(version_obj);
		dependency->required_version = strdup(v);

		cJSON *optional_obj = cJSON_GetObjectItem(dep_obj, "optional");
		bool opt = cJSON_IsTrue(optional_obj);
		dependency->optional = opt;

	}

	*deps = dependencies;
	*deps_len = total_deps;


	cJSON_Delete(json);
	free(s.mem);
	return 0;

err:
	free(s.mem);
	return 1;
}


void get_version(char **version, CURL *curl, char const *crate_name) {
	char *url;
	int r = asprintf(&url, "%s/%s/%s", BASE_URL, CRATES_API_URL, crate_name);
	if (r == -1) {
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
	int result = asprintf(url, "%s/%s/%s/%s/dependencies", BASE_URL, CRATES_API_URL, crate_name, version);
	if (result == -1)
		*url = NULL;
}
