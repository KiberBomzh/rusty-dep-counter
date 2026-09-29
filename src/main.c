#include <stdio.h>
#include <curl/curl.h>


int main(void) {
	curl_global_init(CURL_GLOBAL_ALL);

	CURL *curl = curl_easy_init();
	if (curl == NULL) {
		fprintf(stderr, "Cannot initialize libcurl!\n");
		return 1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, "https://flibusta.is");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	CURLcode result = curl_easy_perform(curl);
	if (result != CURLE_OK) {
		fprintf(stderr, "Curl error: %s\n", curl_easy_strerror(result));
		goto clean_err;
	}


	curl_easy_cleanup(curl);
	curl_global_cleanup();
	return 0;


clean_err:
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	return 1;
}
