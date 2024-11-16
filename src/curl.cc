#include "options.h"

#ifdef CURL_FOUND

#include <curl/curl.h>

#include "curl.h"
#include "functions.h"
#include "utils.h"
#include "log.h"
#include "background.h"
#include "map.h"

static CURL *curl_handle = nullptr;

typedef struct CurlMemoryStruct {
    char *result;
    size_t size;
} CurlMemoryStruct;

/**
 * Set or overwrite a header in the curl_slist.
 *
 * @param headers Pointer to the curl_slist containing the headers.
 * @param header_name Name of the header to set or overwrite (e.g., "Content-Type").
 * @param header_value Value of the header to set (e.g., "application/json").
 * @return Updated curl_slist with the header set.
 */
struct curl_slist *set_or_overwrite_header(struct curl_slist *headers, const char *header_name, const char *header_value) {
    size_t header_name_len = strlen(header_name);
    char *new_header = NULL;

    // Allocate memory for the new header
    asprintf(&new_header, "%s: %s", header_name, header_value);

    // Check if the header exists
    struct curl_slist *current = headers, *prev = NULL;
    while (current) {
        if (strncasecmp(current->data, header_name, header_name_len) == 0 &&
            current->data[header_name_len] == ':') {
            // Header exists, replace it
            struct curl_slist *to_delete = current;
            if (prev) {
                prev->next = current->next; // Remove from the middle
            } else {
                headers = current->next; // Remove the first header
            }
            current = current->next;
            curl_slist_free_all(to_delete); // Free the removed header
        } else {
            prev = current;
            current = current->next;
        }
    }

    // Append the new header
    headers = curl_slist_append(headers, new_header);
    free(new_header); // Free temporary memory

    return headers;
}

static size_t
CurlWriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    CurlMemoryStruct *mem = (CurlMemoryStruct *)userp;

    char *ptr = (char*)realloc(mem->result, mem->size + realsize + 1);
    if (ptr == nullptr) {
        /* out of memory! */
        errlog("not enough memory for curl (realloc returned NULL)\n");
        return 0;
    }

    mem->result = ptr;
    memcpy(&(mem->result[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->result[mem->size] = 0;

    return realsize;
}

static void curl_thread_callback(Var arglist, Var *ret, void *extra_data)
{
    CURL *curl_handle;
    CURLcode res;
    CurlMemoryStruct chunk;
    struct curl_slist *headers = NULL;
    long timeout = CURL_TIMEOUT;

    // Get out total list of arguments
    int nargs = arglist.v.list[0].v.num;

    // Default value ternary; if we don't have an arg then the default is GET.
    const char *method = nargs <= 1 ? "GET" : arglist.v.list[2].v.str;
    
    // Set up the basic universals of the CURL handle.
    curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, arglist.v.list[1].v.str);
    curl_easy_setopt(curl_handle, CURLOPT_PROTOCOLS_STR, "http,https,dict");
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, CurlWriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

    // This block of code reads our headers argument and writes those into the request here.
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (nargs >= 3) {
        Var key, value;
        FOR_EACH_MAP(key, value, arglist.v.list[3]) {
            if (key.type != TYPE_STR) {
                make_error_map(E_INVARG, "Header key type was not a string", ret);
                curl_easy_cleanup(curl_handle);
                return;
            }
            if (value.type != TYPE_STR) {
                make_error_map(E_INVARG, "Header value type was not a string", ret);
                curl_easy_cleanup(curl_handle);
                return;
            }
            headers = set_or_overwrite_header(headers, str_dup(key.v.str), str_dup(value.v.str));
        }
    }
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

    // Set body for request if necessary.
    if (nargs >= 4) {
      const char *request_data = str_dup(arglist.v.list[4].v.str);
      curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, request_data);
    }

    // Specific method handling.
    if (!strcasecmp(method, "POST")) {
      curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
    } else if (!strcasecmp(method, "PUT")) {
      // https://curl.se/libcurl/c/CURLOPT_CUSTOMREQUEST.html
      curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (!strcasecmp(method, "DELETE")) {
      // https://curl.se/libcurl/c/CURLOPT_CUSTOMREQUEST.html
      curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (!strcasecmp(method, "GET")) {
      // Nothing needed here; GET is the default.
    } else {
      // This was an invalid method.
      make_error_map(E_INVARG, "Invalid HTTP Method Provided", ret);
      curl_easy_cleanup(curl_handle);
      // Return control early, so the call doesn't actually fall-thru and call the curl handle.
      return;
    }

    chunk.result = (char*)malloc(1);
    chunk.size = 0;
    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
      make_error_map(E_INVARG, curl_easy_strerror(res), ret);
    } else {
      *ret = str_dup_to_var(raw_bytes_to_binary(chunk.result, chunk.size));
      oklog("CURL [%s]: %lu bytes retrieved from: %s\n", method, (unsigned long)chunk.size, arglist.v.list[1].v.str);
    }

    curl_easy_cleanup(curl_handle);
    free(chunk.result);
}

static package
bf_curl(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    return background_thread(curl_thread_callback, &arglist);
}

static package
bf_url_encode(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    const char *url = arglist.v.list[1].v.str;

    char *encoded = curl_easy_escape(curl_handle, url, memo_strlen(url));

    if (encoded == nullptr) {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    r.type = TYPE_STR;
    r.v.str = str_dup(encoded);

    free_var(arglist);
    curl_free(encoded);

    return make_var_pack(r);
}

static package
bf_url_decode(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var r;
    const char *url = arglist.v.list[1].v.str;

    char *decoded = curl_easy_unescape(curl_handle, url, memo_strlen(url), nullptr);

    if (decoded == nullptr) {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    r.type = TYPE_STR;
    r.v.str = str_dup(decoded);

    free_var(arglist);
    curl_free(decoded);

    return make_var_pack(r);
}

void curl_shutdown(void)
{
    curl_global_cleanup();
    
    if (curl_handle != nullptr)
        curl_easy_cleanup(curl_handle);
}

void
register_curl(void)
{
    oklog("REGISTER_CURL: Using libcurl version %s\n", curl_version());
    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();
 
    /** curl(STR url[, STR method = "GET", MAP headers, STR body]) */
    register_function("curl", 1, 4, bf_curl, TYPE_STR, TYPE_STR, TYPE_MAP, TYPE_STR);
    register_function("url_encode", 1, 1, bf_url_encode, TYPE_STR);
    register_function("url_decode", 1, 1, bf_url_decode, TYPE_STR);
}

#else /* CURL_FOUND */
void register_curl(void) { }
void curl_shutdown(void) { }
#endif /* CURL_FOUND */
