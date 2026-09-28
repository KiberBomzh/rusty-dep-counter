#include <stdio.h>
#include <curl/curl.h>


int main(void) {
	CURL *curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, "https://flibusta.is");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);


	return 0;
}
