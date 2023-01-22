#include "rg_system.h"
#include "gui.h"

#ifdef RG_ENABLE_NETWORKING
#include <esp_http_server.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <cJSON.h>
#include <ctype.h>

// static const char webui_html[];
#include "webui.html.h"

static httpd_handle_t server;
static char *http_buffer;

static bool wifi_state = true;
static bool webui_state = true;

static const char *SETTING_WEBUI = "HTTPFileServer";
static const char *SETTING_WIFI = "WiFi";

static char *urldecode(const char *str)
{
    char *new_string = strdup(str);
    char *ptr = new_string;
    while (*ptr && *(ptr + 1) && *(ptr + 2))
    {
        if (*ptr == '%' && isxdigit(*(ptr + 1)) && isxdigit(*(ptr + 2)))
        {
            char hex[] = {*(ptr + 1), *(ptr + 2), 0};
            *ptr = strtol(hex, NULL, 16);
            memmove(ptr + 1, ptr + 3, strlen(ptr + 3) + 1);
        }
        ptr++;
    }
    return new_string;
}

static esp_err_t http_api_handler(httpd_req_t *req)
{
    char http_buffer[1024] = {0};
    bool success = false;
    FILE *fp;

    if (httpd_req_recv(req, http_buffer, sizeof(http_buffer)) < 2)
        return ESP_FAIL;

    cJSON *content = cJSON_Parse(http_buffer);
    if (!content)
        return ESP_FAIL;

    const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(content, "cmd")) ?: "-";
    const char *arg1 = cJSON_GetStringValue(cJSON_GetObjectItem(content, "arg1")) ?: "";
    const char *arg2 = cJSON_GetStringValue(cJSON_GetObjectItem(content, "arg2")) ?: "";

    cJSON *response = cJSON_CreateObject();

    gui.http_lock = true;

    if (strcmp(cmd, "list") == 0)
    {
        cJSON *array = cJSON_AddArrayToObject(response, "files");
        rg_scandir_t *files = rg_storage_scandir(arg1, NULL, RG_SCANDIR_SORT | RG_SCANDIR_STAT);
        for (rg_scandir_t *entry = files; entry && entry->is_valid; ++entry)
        {
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj, "name", entry->name);
            cJSON_AddNumberToObject(obj, "size", entry->size);
            cJSON_AddNumberToObject(obj, "mtime", entry->mtime);
            // cJSON_AddBoolToObject(obj, "is_file", entry->is_file);
            cJSON_AddBoolToObject(obj, "is_dir", entry->is_dir);
            cJSON_AddItemToArray(array, obj);
        }
        success = array && files;
        free(files);
    }
    else if (strcmp(cmd, "rename") == 0)
    {
        success = rename(arg1, arg2) == 0;
        gui_invalidate();
    }
    else if (strcmp(cmd, "delete") == 0)
    {
        success = rg_storage_delete(arg1);
        gui_invalidate();
    }
    else if (strcmp(cmd, "mkdir") == 0)
    {
        success = rg_storage_mkdir(arg1);
        gui_invalidate();
    }
    else if (strcmp(cmd, "touch") == 0)
    {
        success = (fp = fopen(arg1, "wb")) && fclose(fp) == 0;
        gui_invalidate();
    }

    gui.http_lock = false;

    cJSON_AddBoolToObject(response, "success", success);

    char *response_text = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response_text);
    free(response_text);

    cJSON_Delete(response);
    cJSON_Delete(content);

    return ESP_OK;
}

static esp_err_t http_upload_handler(httpd_req_t *req)
{
    char *filename = urldecode(req->uri);

    RG_LOGI("Receiving file: %s", filename);

    gui.http_lock = true;
    rg_task_delay(100);

    FILE *fp = fopen(filename, "wb");
    if (!fp)
        return ESP_FAIL;

    size_t received = 0;

    while (received < req->content_len)
    {
        int length = httpd_req_recv(req, http_buffer, 0x8000);
        if (length <= 0)
            break;
        if (!fwrite(http_buffer, length, 1, fp))
        {
            RG_LOGI("Write failure at %d bytes", received);
            break;
        }
        rg_task_delay(0);
        received += length;
    }

    fclose(fp);
    free(filename);

    gui.http_lock = false;
    gui_invalidate();

    if (received < req->content_len)
    {
        RG_LOGE("Received %d/%d bytes", received, req->content_len);
        httpd_resp_sendstr(req, "ERROR");
        unlink(filename);
        return ESP_FAIL;
    }

    RG_LOGI("Received %d/%d bytes", received, req->content_len);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t http_download_handler(httpd_req_t *req)
{
    char *filename = urldecode(req->uri);
    const char *ext = rg_extension(filename);
    FILE *fp;

    RG_LOGI("Serving file: %s", filename);

    gui.http_lock = true;

    if ((fp = fopen(filename, "rb")))
    {
        if (ext && (strcmp(ext, "json") == 0 || strcmp(ext, "log") == 0 || strcmp(ext, "txt") == 0))
            httpd_resp_set_type(req, "text/plain");
        else if (ext && (strcmp(ext, "png") == 0))
            httpd_resp_set_type(req, "image/png");
        else if (ext && (strcmp(ext, "jpg") == 0))
            httpd_resp_set_type(req, "image/jpg");
        else
            httpd_resp_set_type(req, "application/binary");

        for (size_t len; (len = fread(http_buffer, 1, 0x8000, fp));)
            httpd_resp_send_chunk(req, http_buffer, len);

        httpd_resp_send_chunk(req, NULL, 0);
        fclose(fp);
    }
    else
    {
        httpd_resp_send_404(req);
    }
    free(filename);

    gui.http_lock = false;

    return ESP_OK;
}

static esp_err_t http_get_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, webui_html);
    return ESP_OK;
}

void webui_stop(void)
{
    if (!server) // Already stopped
        return;

    httpd_stop(server);
    server = NULL;

    free(http_buffer);
    http_buffer = NULL;
}

void webui_start(void)
{
    if (server) // Already started
        return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        RG_LOGE("Failed to start webserver: 0x%03X", err);
        return;
    }

    http_buffer = malloc(0x10000);

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = http_get_handler,
    });

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri       = "/api",
        .method    = HTTP_POST,
        .handler   = http_api_handler,
    });

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri       = "/*",
        .method    = HTTP_GET,
        .handler   = http_download_handler,
    });

    httpd_register_uri_handler(server, &(httpd_uri_t){
        .uri       = "/*",
        .method    = HTTP_PUT,
        .handler   = http_upload_handler,
    });

    RG_ASSERT(http_buffer && server, "Something went wrong starting server");
    RG_LOGI("Web server started");
}

void wifi_set_switch(bool enable)
{
    rg_settings_set_number(NS_APP, SETTING_WIFI, enable);
    wifi_state = enable;

    if (wifi_state)
        rg_network_wifi_start();
    else
        rg_network_wifi_stop();
}

bool wifi_get_switch(void)
{
    return rg_settings_get_number(NS_APP, SETTING_WIFI, wifi_state);
}

void webui_set_switch(bool enable)
{
    rg_settings_set_number(NS_APP, SETTING_WEBUI, enable);
    webui_state = enable;

    if (webui_state)
        webui_start();
    else
        webui_stop();
}

bool webui_get_switch(void)
{
    return rg_settings_get_number(NS_APP, SETTING_WEBUI, webui_state);
}
#endif
