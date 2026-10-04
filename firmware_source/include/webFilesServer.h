#pragma once

#include <atomic>
#include <string>

#include "esp_http_server.h"
#include "webFilesStorage.h"

class WebFilesServer {
public:
    WebFilesServer() = default;
    ~WebFilesServer();

    bool start();
    void stop();

    bool stopRequested() const { return stopRequested_.load(); }
    bool filesChanged() const { return filesChanged_.load(); }
    httpd_handle_t handle() const { return server_; }

private:
    static esp_err_t indexHandler(httpd_req_t* req);
    static esp_err_t listHandler(httpd_req_t* req);
    static esp_err_t storageHandler(httpd_req_t* req);
    static esp_err_t bookHandler(httpd_req_t* req);
    static esp_err_t stopHandler(httpd_req_t* req);

    esp_err_t handleIndex(httpd_req_t* req);
    esp_err_t handleList(httpd_req_t* req);
    esp_err_t handleStorage(httpd_req_t* req);
    esp_err_t handleBook(httpd_req_t* req);
    esp_err_t handleStop(httpd_req_t* req);

    bool decodeBookName(const char* uri, std::string& name) const;
    bool queryHasReplace(httpd_req_t* req) const;
    static std::string urlEncode(const std::string& value);
    static void jsonError(httpd_req_t* req, const char* status, const char* message);
    static void noCache(httpd_req_t* req);

    httpd_handle_t server_ = nullptr;
    WebFilesStorage storage_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool filesChanged_{false};
};
