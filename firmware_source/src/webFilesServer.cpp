#include "webFilesServer.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <vector>

#include "cJSON.h"
#include "esp_log.h"
#include "webFilesUi.h"

static const char* TAG = "WEB_FILES_SERVER";

namespace {
constexpr size_t kTransferBufferSize = 8192;

bool hexValue(char c, uint8_t& out) {
    if (c >= '0' && c <= '9') { out = static_cast<uint8_t>(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { out = static_cast<uint8_t>(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { out = static_cast<uint8_t>(c - 'A' + 10); return true; }
    return false;
}

bool urlDecodePath(const std::string& input, std::string& output) {
    output.clear();
    output.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '%') {
            if (i + 2 >= input.size()) return false;
            uint8_t hi = 0, lo = 0;
            if (!hexValue(input[i + 1], hi) || !hexValue(input[i + 2], lo)) return false;
            output.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
        } else if (c == '+') {
            output.push_back(' ');
        } else {
            output.push_back(c);
        }
    }

    return true;
}
}

WebFilesServer::~WebFilesServer() {
    stop();
}

bool WebFilesServer::start() {
    if (server_ != nullptr) return true;

    stopRequested_ = false;
    filesChanged_ = false;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 12;
    config.max_open_sockets = 4;
    config.stack_size = 8192;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;

    if (httpd_start(&server_, &config) != ESP_OK) {
        server_ = nullptr;
        return false;
    }

    httpd_uri_t index = {};
    index.uri = "/";
    index.method = HTTP_GET;
    index.handler = &WebFilesServer::indexHandler;
    index.user_ctx = this;

    httpd_uri_t list = {};
    list.uri = "/api/files";
    list.method = HTTP_GET;
    list.handler = &WebFilesServer::listHandler;
    list.user_ctx = this;

    httpd_uri_t storage = {};
    storage.uri = "/api/storage";
    storage.method = HTTP_GET;
    storage.handler = &WebFilesServer::storageHandler;
    storage.user_ctx = this;

    httpd_uri_t getBook = {};
    getBook.uri = "/api/files/*";
    getBook.method = HTTP_GET;
    getBook.handler = &WebFilesServer::bookHandler;
    getBook.user_ctx = this;

    httpd_uri_t putBook = {};
    putBook.uri = "/api/files/*";
    putBook.method = HTTP_PUT;
    putBook.handler = &WebFilesServer::bookHandler;
    putBook.user_ctx = this;

    httpd_uri_t deleteBook = {};
    deleteBook.uri = "/api/files/*";
    deleteBook.method = HTTP_DELETE;
    deleteBook.handler = &WebFilesServer::bookHandler;
    deleteBook.user_ctx = this;

    httpd_uri_t stop = {};
    stop.uri = "/api/stop";
    stop.method = HTTP_POST;
    stop.handler = &WebFilesServer::stopHandler;
    stop.user_ctx = this;

    const httpd_uri_t* handlers[] = {&index, &list, &storage, &getBook, &putBook, &deleteBook, &stop};
    for (const auto* handler : handlers) {
        if (httpd_register_uri_handler(server_, handler) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register URI handler: %s", handler->uri);
            httpd_stop(server_);
            server_ = nullptr;
            return false;
        }
    }

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return true;
}

void WebFilesServer::stop() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    storage_.abortUpload();
}

esp_err_t WebFilesServer::indexHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFilesServer*>(req->user_ctx);
    return self->handleIndex(req);
}

esp_err_t WebFilesServer::listHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFilesServer*>(req->user_ctx);
    return self->handleList(req);
}

esp_err_t WebFilesServer::storageHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFilesServer*>(req->user_ctx);
    return self->handleStorage(req);
}

esp_err_t WebFilesServer::bookHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFilesServer*>(req->user_ctx);
    return self->handleBook(req);
}

esp_err_t WebFilesServer::stopHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFilesServer*>(req->user_ctx);
    return self->handleStop(req);
}

void WebFilesServer::noCache(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
}

void WebFilesServer::jsonError(httpd_req_t* req, const char* status, const char* message) {
    noCache(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    char buffer[256] = {};
    std::snprintf(buffer, sizeof(buffer), "{\"error\":\"%s\"}", message);
    httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebFilesServer::handleIndex(httpd_req_t* req) {
    noCache(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, WebFilesUi::indexHtml(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebFilesServer::handleList(httpd_req_t* req) {
    std::vector<WebFilesStorage::FileInfo> files;
    if (!storage_.list(files)) {
        jsonError(req, "500 Internal Server Error", "Unable to list books");
        return ESP_OK;
    }

    cJSON* root = cJSON_CreateArray();
    if (!root) {
        jsonError(req, "500 Internal Server Error", "Out of memory");
        return ESP_OK;
    }

    for (const auto& file : files) {
        cJSON* obj = cJSON_CreateObject();
        if (!obj) continue;
        cJSON_AddStringToObject(obj, "name", file.name.c_str());
        cJSON_AddNumberToObject(obj, "size", static_cast<double>(file.size));
        cJSON_AddItemToArray(root, obj);
    }

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        jsonError(req, "500 Internal Server Error", "Out of memory");
        return ESP_OK;
    }

    noCache(req);
    httpd_resp_set_type(req, "application/json");
    const esp_err_t result = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    return result;
}

esp_err_t WebFilesServer::handleStorage(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        jsonError(req, "500 Internal Server Error", "Out of memory");
        return ESP_OK;
    }

    cJSON_AddNumberToObject(root, "total", static_cast<double>(storage_.totalBytes()));
    cJSON_AddNumberToObject(root, "free", static_cast<double>(storage_.freeBytes()));
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        jsonError(req, "500 Internal Server Error", "Out of memory");
        return ESP_OK;
    }

    noCache(req);
    httpd_resp_set_type(req, "application/json");
    const esp_err_t result = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    return result;
}

bool WebFilesServer::decodeBookName(const char* uri, std::string& name) const {
    if (!uri) return false;
    const std::string prefix = "/api/files/";
    const std::string raw(uri);
    if (raw.rfind(prefix, 0) != 0) return false;

    std::string pathPart = raw.substr(prefix.size());
    const size_t queryPos = pathPart.find('?');
    if (queryPos != std::string::npos) pathPart.resize(queryPos);

    if (!urlDecodePath(pathPart, name)) return false;
    return storage_.isValidBookName(name);
}

bool WebFilesServer::queryHasReplace(httpd_req_t* req) const {
    size_t len = 0;
    if (httpd_req_get_url_query_len(req) == 0) return false;
    len = httpd_req_get_url_query_len(req) + 1;

    std::vector<char> query(len, 0);
    if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) return false;

    char value[8] = {};
    if (httpd_query_key_value(query.data(), "replace", value, sizeof(value)) != ESP_OK) return false;
    return std::strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0;
}

std::string WebFilesServer::urlEncode(const std::string& value) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

esp_err_t WebFilesServer::handleBook(httpd_req_t* req) {
    std::string name;
    if (!decodeBookName(req->uri, name)) {
        jsonError(req, "400 Bad Request", "Invalid EPUB filename");
        return ESP_OK;
    }

    if (req->method == HTTP_GET) {
        void* file = nullptr;
        uint64_t size = 0;
        if (!storage_.openBookForRead(name, file, size)) {
            jsonError(req, "404 Not Found", "Book not found");
            return ESP_OK;
        }

        noCache(req);
        httpd_resp_set_type(req, "application/epub+zip");
        const std::string disposition = "attachment; filename*=UTF-8''" + urlEncode(name);
        httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());

        char buffer[kTransferBufferSize];
        esp_err_t result = ESP_OK;
        size_t bytesRead = 0;
        do {
            bytesRead = storage_.readBook(file, buffer, sizeof(buffer));
            if (bytesRead == 0) break;
            result = httpd_resp_send_chunk(req, buffer, bytesRead);
            if (result != ESP_OK) break;
        } while (bytesRead > 0);

        if (result == ESP_OK) result = httpd_resp_send_chunk(req, nullptr, 0);
        storage_.closeBook(file);
        return result;
    }

    if (req->method == HTTP_DELETE) {
        if (!storage_.deleteBook(name)) {
            jsonError(req, "404 Not Found", "Book not found or delete failed");
            return ESP_OK;
        }
        filesChanged_ = true;
        noCache(req);
        return httpd_resp_sendstr(req, "deleted");
    }

    if (req->method == HTTP_PUT) {
        const bool replaceExisting = queryHasReplace(req);
        if (storage_.exists(name) && !replaceExisting) {
            jsonError(req, "409 Conflict", "Book already exists");
            return ESP_OK;
        }

        const int64_t expectedSize = req->content_len;
        if (expectedSize < 0) {
            jsonError(req, "411 Length Required", "Content-Length is required");
            return ESP_OK;
        }

        if (!storage_.beginUpload(name, static_cast<uint64_t>(expectedSize))) {
            jsonError(req, "507 Insufficient Storage", "Not enough storage or upload busy");
            return ESP_OK;
        }

        char buffer[kTransferBufferSize];
        int64_t remaining = expectedSize;
        bool success = true;

        while (remaining > 0) {
            const size_t wanted = static_cast<size_t>(std::min<int64_t>(remaining, sizeof(buffer)));
            const int received = httpd_req_recv(req, buffer, wanted);
            if (received <= 0) {
                success = false;
                break;
            }
            if (!storage_.writeUpload(buffer, static_cast<size_t>(received))) {
                success = false;
                break;
            }
            remaining -= received;
        }

        if (!success || remaining != 0 || !storage_.finishUpload(name, replaceExisting)) {
            storage_.abortUpload();
            jsonError(req, "500 Internal Server Error", "Upload failed");
            return ESP_OK;
        }

        filesChanged_ = true;
        noCache(req);
        httpd_resp_set_status(req, "201 Created");
        return httpd_resp_sendstr(req, "uploaded");
    }

    jsonError(req, "405 Method Not Allowed", "Unsupported method");
    return ESP_OK;
}

esp_err_t WebFilesServer::handleStop(httpd_req_t* req) {
    stopRequested_ = true;
    noCache(req);
    return httpd_resp_sendstr(req, "stopping");
}
