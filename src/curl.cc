#include "options.h"

#ifdef CURL_FOUND

#include <curl/curl.h>

#include "curl.h"
#include "functions.h"
#include "utils.h"
#include "log.h"
#include "background.h"
#include "map.h"
#include "list.h"

static CURL *curl_handle = nullptr;

typedef struct CurlMemoryStruct {
    char *result;
    size_t size;
} CurlMemoryStruct;

typedef struct {
    char **headers;
    size_t num_headers;
} CurlHeaderList;

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
    struct curl_slist *new_headers = NULL;
    struct curl_slist *temp;

    // Allocate memory for the new header
    asprintf(&new_header, "%s: %s", header_name, header_value);

    // Rebuild the header list without the header to be replaced
    for (temp = headers; temp; temp = temp->next) {
        if (strncasecmp(temp->data, header_name, header_name_len) == 0 &&
            temp->data[header_name_len] == ':') {
            continue; // Skip the header to be replaced
        }
        new_headers = curl_slist_append(new_headers, temp->data);
    }

    // Append the new header
    new_headers = curl_slist_append(new_headers, new_header);
    free(new_header);

    // Free the old header list
    curl_slist_free_all(headers);

    return new_headers;
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

static size_t CurlHeaderCallback(char *buffer, size_t size, size_t nitems, void *userdata) {
    size_t realsize = size * nitems;
    CurlHeaderList *header_list = (CurlHeaderList *)userdata;

    char *header_line = (char *)malloc(realsize + 1);
    if(header_line == NULL) {
        errlog("Not enough memory for header line\n");
        return 0;
    }
    memcpy(header_line, buffer, realsize);
    header_line[realsize] = '\0';

    char **new_headers = (char **)realloc(header_list->headers, (header_list->num_headers + 1) * sizeof(char *));
    if(new_headers == NULL) {
        free(header_line);
        errlog("Not enough memory for header list\n");
        return 0;
    }

    header_list->headers = new_headers;
    header_list->headers[header_list->num_headers] = header_line;
    header_list->num_headers++;

    return realsize;
}

static void curl_thread_callback(Var arglist, Var *ret, void *extra_data)
{
    CURL *curl_handle;
    CURLcode res;
    CurlMemoryStruct chunk;
    CurlHeaderList header_list;
    struct curl_slist *headers = NULL;
    struct curl_slist *cookies;
    struct curl_slist *nc;
    long timeout = CURL_TIMEOUT;

    header_list.headers = NULL;
    header_list.num_headers = 0;

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
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, CurlHeaderCallback);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&header_list);

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
            headers = set_or_overwrite_header(headers, key.v.str, value.v.str);
        }
    }
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

    // Set body for request if necessary.
    if (nargs >= 4) {
      curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, arglist.v.list[4].v.str);
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
      curl_slist_free_all(headers);
      curl_slist_free_all(cookies);
      for (size_t i = 0; i < header_list.num_headers; i++) {
          free(header_list.headers[i]);
      }
      free(header_list.headers);
      // Return control early, so the call doesn't actually fall-thru and call the curl handle.
      return;
    }

    chunk.result = (char*)malloc(1);
    chunk.size = 0;
    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
      make_error_map(E_INVARG, curl_easy_strerror(res), ret);
    } else {
        // Response keys
        static const Var status_key = str_dup_to_var("status");
        static const Var body_key = str_dup_to_var("body");
        static const Var headers_key = str_dup_to_var("headers");
        static const Var cookies_key = str_dup_to_var("cookies");

        // Response values
        Var statusCode;
        statusCode.type = TYPE_INT;

        // Assigning values from response
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &statusCode.v.num);

        // Process headers
        Var headers_var = new_map();
        for (size_t i = 0; i < header_list.num_headers; i++) {
            char *header_line = header_list.headers[i];
            // Skip leading whitespace
            while (isspace((unsigned char)*header_line)) header_line++;

            if (strlen(header_line) == 0) {
                continue;
            }

            char *colon = strchr(header_line, ':');
            if (colon) {
                *colon = '\0';
                char *key = header_line;
                char *value = colon + 1;
                // Skip leading whitespace in value
                while (isspace((unsigned char)*value)) value++;
                // Remove trailing whitespace from key
                char *key_end = colon - 1;
                while (key_end > key && isspace((unsigned char)*key_end)) {
                    *key_end = '\0';
                    key_end--;
                }
                // Remove trailing whitespace from value
                char *value_end = value + strlen(value) - 1;
                while (value_end > value && isspace((unsigned char)*value_end)) {
                    *value_end = '\0';
                    value_end--;
                }
                Var key_var = str_dup_to_var(key);
                Var value_var = str_dup_to_var(value);
                headers_var = mapinsert(headers_var, key_var, value_var);
            } else {
                // Remove trailing whitespace from status line
                char *line_end = header_line + strlen(header_line) - 1;
                while (line_end > header_line && isspace((unsigned char)*line_end)) {
                    *line_end = '\0';
                    line_end--;
                }
                Var key_var = str_dup_to_var(":status-line");
                Var value_var = str_dup_to_var(header_line);
                headers_var = mapinsert(headers_var, key_var, value_var);
            }
        }


        // Process cookies
        res = curl_easy_getinfo(curl_handle, CURLINFO_COOKIELIST, &cookies);
        Var cookie_list_var = new_list(0);
        nc = cookies;
        while(nc) {
            cookie_list_var = listappend(cookie_list_var, str_dup_to_var(nc->data));
            nc = nc->next;
        }

        *ret = new_map();
        *ret = mapinsert(*ret, var_ref(status_key), statusCode);
        *ret = mapinsert(*ret, var_ref(body_key), str_dup_to_var(raw_bytes_to_binary(chunk.result, chunk.size)));
        *ret = mapinsert(*ret, var_ref(headers_key), headers_var);
        *ret = mapinsert(*ret, var_ref(cookies_key), cookie_list_var);

        oklog("CURL [%s]: %lu bytes retrieved from: %s\n", method, (unsigned long)chunk.size, arglist.v.list[1].v.str);
    }

    curl_slist_free_all(headers);
    for (size_t i = 0; i < header_list.num_headers; i++) {
        free(header_list.headers[i]);
    }
    free(header_list.headers);
    curl_slist_free_all(cookies);
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
    if (curl_handle != nullptr)
        curl_easy_cleanup(curl_handle);

    curl_global_cleanup();
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
