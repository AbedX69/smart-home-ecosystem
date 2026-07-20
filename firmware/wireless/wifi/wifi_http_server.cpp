/*
 * =============================================================================
 * FILE:        wifi_http_server.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-13
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "wifi_http_server.h"
#include "wifi_manager.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "WiFiHttpServer";

/* =============================================================================
 * CAPTIVE PORTAL HTML
 * =============================================================================
 * Embedded HTML for WiFi setup page. Kept minimal to save flash space.
 * In production, you'd serve this from SPIFFS/LittleFS.
 * ========================================================================== */

static const char CAPTIVE_PORTAL_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>ESP32 WiFi Setup</title>
    <style>
        *{box-sizing:border-box;margin:0;padding:0}
        body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#e0e0e0;padding:20px}
        .card{background:#16213e;border-radius:12px;padding:24px;max-width:400px;margin:0 auto}
        h1{color:#0f3460;font-size:1.4em;margin-bottom:16px;text-align:center;color:#e94560}
        label{display:block;margin:12px 0 4px;font-size:0.9em;color:#a0a0a0}
        input,select{width:100%;padding:10px;border:1px solid #333;border-radius:6px;
               background:#0f3460;color:#fff;font-size:1em}
        button{width:100%;padding:12px;background:#e94560;color:#fff;border:none;
               border-radius:6px;font-size:1em;cursor:pointer;margin-top:16px}
        button:hover{background:#c73650}
        .status{padding:8px;border-radius:6px;margin-top:12px;text-align:center;font-size:0.9em}
        .ok{background:#0a3d0a;color:#4caf50}
        .err{background:#3d0a0a;color:#f44336}
        .nets{max-height:200px;overflow-y:auto;margin:8px 0}
        .net{padding:8px;cursor:pointer;border-bottom:1px solid #333}
        .net:hover{background:#0f3460}
        .rssi{float:right;color:#888;font-size:0.85em}
    </style>
</head>
<body>
<div class="card">
    <h1>&#128225; WiFi Setup</h1>
    <div id="status"></div>
    <button onclick="scan()">Scan Networks</button>
    <div id="nets" class="nets"></div>
    <label for="ssid">SSID</label>
    <input id="ssid" placeholder="Network name">
    <label for="pass">Password</label>
    <input id="pass" type="password" placeholder="Password">
    <button onclick="save()">Connect</button>
</div>
<script>
function scan(){
    document.getElementById('nets').innerHTML='Scanning...';
    fetch('/api/scan').then(r=>r.json()).then(d=>{
        let h='';
        d.networks.forEach(n=>{
            h+='<div class="net" onclick="document.getElementById(\'ssid\').value=\''+n.ssid+'\'">'+
               n.ssid+'<span class="rssi">'+n.rssi+' dBm</span></div>';
        });
        document.getElementById('nets').innerHTML=h||'No networks found';
    }).catch(e=>{document.getElementById('nets').innerHTML='Scan failed';});
}
function save(){
    let s=document.getElementById('ssid').value;
    let p=document.getElementById('pass').value;
    if(!s){alert('Enter SSID');return;}
    fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},
        body:JSON.stringify({ssid:s,password:p})
    }).then(r=>r.json()).then(d=>{
        let st=document.getElementById('status');
        if(d.status==='ok'){
            st.className='status ok';st.textContent='Connecting to '+s+'...';
        }else{
            st.className='status err';st.textContent='Error: '+d.message;
        }
    }).catch(e=>{
        let st=document.getElementById('status');
        st.className='status err';st.textContent='Request failed';
    });
}
fetch('/api/status').then(r=>r.json()).then(d=>{
    if(d.connected){
        let st=document.getElementById('status');
        st.className='status ok';
        st.textContent='Connected to '+d.ssid+' ('+d.ip+')';
    }
});
</script>
</body>
</html>
)rawliteral";

/* =============================================================================
 * SINGLETON
 * ========================================================================== */

WiFiHttpServer& WiFiHttpServer::instance() {
    static WiFiHttpServer inst;
    return inst;
}

WiFiHttpServer::WiFiHttpServer()
    : _server(nullptr)
    , _running(false)
    , _captive_portal(false)
    , _dns_task(nullptr)
    , _dns_socket(-1)
{
    memset(_pending, 0, sizeof(_pending));
}

WiFiHttpServer::~WiFiHttpServer() {
    stop();
}

/* =============================================================================
 * LIFECYCLE
 * ========================================================================== */

esp_err_t WiFiHttpServer::begin(uint16_t port) {
    if (_running) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = HTTP_SERVER_MAX_ROUTES;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    /* Allow wildcard URI matching for captive portal catch-all */
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Server start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register any pending routes */
    for (int i = 0; i < HTTP_SERVER_MAX_ROUTES; i++) {
        if (_pending[i].used) {
            httpd_register_uri_handler(_server, &_pending[i].uri_handler);
        }
    }

    _running = true;
    ESP_LOGI(TAG, "HTTP server started on port %d", port);
    return ESP_OK;
}

esp_err_t WiFiHttpServer::beginCaptivePortal() {
    _captive_portal = true;

    /* Register built-in captive portal routes */
    addRoute("/", HTTP_GET, captiveRootHandler);
    addRoute("/api/scan", HTTP_GET, captiveScanHandler);
    addRoute("/api/wifi", HTTP_POST, captiveWifiHandler);
    addRoute("/api/status", HTTP_GET, captiveStatusHandler);

    esp_err_t ret = begin();
    if (ret != ESP_OK) return ret;

    /* Add catch-all LAST (wildcard routes must be registered after specific ones) */
    httpd_uri_t catch_all = {};
    catch_all.uri = "/*";
    catch_all.method = HTTP_GET;
    catch_all.handler = captiveCatchAllHandler;
    httpd_register_uri_handler(_server, &catch_all);

    /* Start DNS server for captive portal redirect */
    startDNS();

    ESP_LOGI(TAG, "Captive portal active at http://192.168.4.1");
    return ESP_OK;
}

esp_err_t WiFiHttpServer::stop() {
    if (!_running) return ESP_OK;

    stopDNS();

    if (_server) {
        httpd_stop(_server);
        _server = nullptr;
    }

    _running = false;
    _captive_portal = false;
    ESP_LOGI(TAG, "HTTP server stopped");
    return ESP_OK;
}

bool WiFiHttpServer::isRunning() const { return _running; }

/* =============================================================================
 * ROUTE REGISTRATION
 * ========================================================================== */

esp_err_t WiFiHttpServer::addRoute(const char* uri, httpd_method_t method,
                                    esp_err_t (*handler)(httpd_req_t*),
                                    void* user_ctx) {
    /* If server is already running, register immediately */
    if (_running && _server) {
        httpd_uri_t uri_handler = {};
        uri_handler.uri = uri;
        uri_handler.method = method;
        uri_handler.handler = handler;
        uri_handler.user_ctx = user_ctx;
        return httpd_register_uri_handler(_server, &uri_handler);
    }

    /* Otherwise, store as pending */
    for (int i = 0; i < HTTP_SERVER_MAX_ROUTES; i++) {
        if (!_pending[i].used) {
            _pending[i].uri_handler.uri = uri;
            _pending[i].uri_handler.method = method;
            _pending[i].uri_handler.handler = handler;
            _pending[i].uri_handler.user_ctx = user_ctx;
            _pending[i].used = true;
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "Max routes (%d) reached!", HTTP_SERVER_MAX_ROUTES);
    return ESP_ERR_NO_MEM;
}

/* =============================================================================
 * RESPONSE HELPERS
 * ========================================================================== */

esp_err_t WiFiHttpServer::sendJSON(httpd_req_t* req, const char* json) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WiFiHttpServer::sendHTML(httpd_req_t* req, const char* html) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WiFiHttpServer::sendRedirect(httpd_req_t* req, const char* location) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    return httpd_resp_send(req, "Redirecting...", HTTPD_RESP_USE_STRLEN);
}

esp_err_t WiFiHttpServer::sendText(httpd_req_t* req, const char* text) {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WiFiHttpServer::sendError(httpd_req_t* req, httpd_err_code_t code, const char* msg) {
    return httpd_resp_send_err(req, code, msg);
}

int WiFiHttpServer::readBody(httpd_req_t* req, char* buf, size_t buf_len) {
    int total = req->content_len;
    if (total <= 0) return 0;
    if ((size_t)total >= buf_len) total = buf_len - 1;

    int received = httpd_req_recv(req, buf, total);
    if (received > 0) {
        buf[received] = '\0';
    }
    return received;
}

bool WiFiHttpServer::getQueryParam(httpd_req_t* req, const char* key,
                                    char* val, size_t val_len) {
    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    if (query_len <= 1) return false;

    char* query = (char*)malloc(query_len);
    if (!query) return false;

    bool found = false;
    if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
        if (httpd_query_key_value(query, key, val, val_len) == ESP_OK) {
            found = true;
        }
    }

    free(query);
    return found;
}

httpd_handle_t WiFiHttpServer::getHandle() const { return _server; }

/* =============================================================================
 * CAPTIVE PORTAL HANDLERS
 * ========================================================================== */

esp_err_t WiFiHttpServer::captiveRootHandler(httpd_req_t* req) {
    return sendHTML(req, CAPTIVE_PORTAL_HTML);
}

esp_err_t WiFiHttpServer::captiveScanHandler(httpd_req_t* req) {
    WiFiManager& wifi = WiFiManager::instance();

    wifi_ap_record_t results[20];
    uint16_t found = 0;
    esp_err_t ret = wifi.scan(results, 20, found);

    if (ret != ESP_OK) {
        return sendJSON(req, "{\"networks\":[],\"error\":\"scan failed\"}");
    }

    /* Build JSON manually (no external JSON lib needed).
     * This is ugly but keeps dependencies at zero. */
    char json[2048] = "{\"networks\":[";
    int pos = strlen(json);

    for (int i = 0; i < found && pos < 1900; i++) {
        if (i > 0) json[pos++] = ',';
        pos += snprintf(json + pos, sizeof(json) - pos,
                        "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%d}",
                        results[i].ssid, results[i].rssi, results[i].authmode);
    }
    snprintf(json + pos, sizeof(json) - pos, "]}");

    return sendJSON(req, json);
}

esp_err_t WiFiHttpServer::captiveWifiHandler(httpd_req_t* req) {
    char body[256] = {};
    int len = readBody(req, body, sizeof(body));
    if (len <= 0) {
        return sendJSON(req, "{\"status\":\"error\",\"message\":\"empty body\"}");
    }

    /* Extremely basic JSON parsing. In production, use cJSON.
     * Looking for: {"ssid":"...", "password":"..."} */
    char ssid[33] = {};
    char pass[65] = {};

    auto extractField = [](const char* json, const char* key, char* out, size_t out_len) {
        char search[64];
        snprintf(search, sizeof(search), "\"%s\":\"", key);
        const char* start = strstr(json, search);
        if (!start) return false;
        start += strlen(search);
        const char* end = strchr(start, '"');
        if (!end) return false;
        size_t copy_len = end - start;
        if (copy_len >= out_len) copy_len = out_len - 1;
        strncpy(out, start, copy_len);
        out[copy_len] = '\0';
        return true;
    };

    if (!extractField(body, "ssid", ssid, sizeof(ssid))) {
        return sendJSON(req, "{\"status\":\"error\",\"message\":\"missing ssid\"}");
    }
    extractField(body, "password", pass, sizeof(pass));

    ESP_LOGI(TAG, "Received WiFi credentials: SSID=%s", ssid);

    /* Save and attempt connection */
    WiFiManager& wifi = WiFiManager::instance();
    wifi.saveCredentials(ssid, pass);

    /* Start connection in STA+AP mode so the portal stays up during connect */
    WiFiAPConfig ap_cfg;
    wifi.beginSTAAP(ssid, pass, ap_cfg.ssid);

    return sendJSON(req, "{\"status\":\"ok\",\"message\":\"connecting\"}");
}

esp_err_t WiFiHttpServer::captiveStatusHandler(httpd_req_t* req) {
    WiFiManager& wifi = WiFiManager::instance();

    char ip[16] = {};
    wifi.getIP(ip, sizeof(ip));

    char json[256];
    snprintf(json, sizeof(json),
             "{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"channel\":%d}",
             wifi.isConnected() ? "true" : "false",
             wifi.getSSID(),
             ip,
             wifi.getRSSI(),
             wifi.getChannel());

    return sendJSON(req, json);
}

esp_err_t WiFiHttpServer::captiveCatchAllHandler(httpd_req_t* req) {
    /* Redirect everything to the root page.
     * This is what makes the captive portal "pop up" on phones. */
    return sendRedirect(req, "http://192.168.4.1/");
}

/* =============================================================================
 * DNS SERVER FOR CAPTIVE PORTAL
 * =============================================================================
 * 
 * A simple DNS server that responds to ALL queries with our IP (192.168.4.1).
 * This is what forces phones to show the captive portal.
 * 
 * DNS is UDP on port 53. We parse just enough of the query to build a valid
 * response - this is NOT a full DNS implementation.
 * ========================================================================== */

esp_err_t WiFiHttpServer::startDNS() {
    if (_dns_task) return ESP_OK;

    xTaskCreate(dnsTask, "captive_dns", 4096, this, 3, &_dns_task);
    ESP_LOGI(TAG, "Captive portal DNS started");
    return ESP_OK;
}

esp_err_t WiFiHttpServer::stopDNS() {
    if (_dns_task) {
        vTaskDelete(_dns_task);
        _dns_task = nullptr;
    }
    if (_dns_socket >= 0) {
        close(_dns_socket);
        _dns_socket = -1;
    }
    return ESP_OK;
}

void WiFiHttpServer::dnsTask(void* arg) {
    WiFiHttpServer* self = static_cast<WiFiHttpServer*>(arg);

    /* Create UDP socket on port 53 */
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket creation failed");
        vTaskDelete(nullptr);
        return;
    }
    self->_dns_socket = sock;

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS socket bind failed");
        close(sock);
        self->_dns_socket = -1;
        vTaskDelete(nullptr);
        return;
    }

    /* Set receive timeout so we can cleanly exit */
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[512];
    struct sockaddr_in client_addr;
    socklen_t client_len;

    /* Our IP: 192.168.4.1 (default AP gateway) */
    uint8_t our_ip[4] = {192, 168, 4, 1};

    while (true) {
        client_len = sizeof(client_addr);
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                          (struct sockaddr*)&client_addr, &client_len);

        if (len < 12) continue;  // Too short to be DNS

        /* Build DNS response:
         * - Copy the transaction ID from the query
         * - Set response flags
         * - Copy the question section
         * - Add an answer section pointing to our IP */

        uint8_t resp[512];
        int resp_len = 0;

        /* Header: copy ID, set flags to "standard response, no error" */
        resp[0] = buf[0];  // Transaction ID (2 bytes)
        resp[1] = buf[1];
        resp[2] = 0x81;    // Flags: response, recursion available
        resp[3] = 0x80;
        resp[4] = buf[4];  // Questions count (copy from query)
        resp[5] = buf[5];
        resp[6] = 0x00;    // Answers count = 1
        resp[7] = 0x01;
        resp[8] = 0x00;    // Authority RRs = 0
        resp[9] = 0x00;
        resp[10] = 0x00;   // Additional RRs = 0
        resp[11] = 0x00;
        resp_len = 12;

        /* Copy the question section from the query */
        int q_start = 12;
        int q_end = q_start;
        /* Walk past the QNAME (series of labels ending with 0x00) */
        while (q_end < len && buf[q_end] != 0x00) {
            q_end += buf[q_end] + 1;
        }
        q_end += 1;  // Skip the 0x00 terminator
        q_end += 4;  // Skip QTYPE (2) and QCLASS (2)

        if (q_end > len) {
            continue;  // Malformed query
        }

        memcpy(resp + resp_len, buf + q_start, q_end - q_start);
        resp_len += (q_end - q_start);

        /* Answer section: pointer to name + A record with our IP */
        resp[resp_len++] = 0xC0;  // Name pointer
        resp[resp_len++] = 0x0C;  // Points to offset 12 (the question name)
        resp[resp_len++] = 0x00;  // Type: A (IPv4)
        resp[resp_len++] = 0x01;
        resp[resp_len++] = 0x00;  // Class: IN
        resp[resp_len++] = 0x01;
        resp[resp_len++] = 0x00;  // TTL: 60 seconds
        resp[resp_len++] = 0x00;
        resp[resp_len++] = 0x00;
        resp[resp_len++] = 0x3C;
        resp[resp_len++] = 0x00;  // Data length: 4 bytes
        resp[resp_len++] = 0x04;
        resp[resp_len++] = our_ip[0];  // IP address
        resp[resp_len++] = our_ip[1];
        resp[resp_len++] = our_ip[2];
        resp[resp_len++] = our_ip[3];

        sendto(sock, resp, resp_len, 0,
               (struct sockaddr*)&client_addr, client_len);
    }

    close(sock);
    self->_dns_socket = -1;
    vTaskDelete(nullptr);
}
