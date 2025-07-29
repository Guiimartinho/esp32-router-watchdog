/*
 * TinyUPnP.h - Library for creating UPnP rules automatically in your router.
 * Created by Ofek Pearl, September 2017.
*/

#if defined(ESP8266)
    #include <ESP8266WiFi.h>
#else
    #include <WiFi.h>
#endif

#include "TinyUPnP.h"
#include "esp_log.h"

static const char* TAG = "TinyUPnP";

IPAddress ipMulti(239, 255, 255, 250);  // multicast address for SSDP
IPAddress connectivityTestIp(64, 233, 187, 99);  // Google
IPAddress ipNull(0, 0, 0, 0);  // indication to update rules when the IP of the device changes

char packetBuffer[UPNP_UDP_TX_PACKET_MAX_SIZE];  // buffer to hold incoming packet
char responseBuffer[UPNP_UDP_TX_RESPONSE_MAX_SIZE];

char body_tmp[1200];
char integer_string[32];

SOAPAction SOAPActionGetSpecificPortMappingEntry = {.name = "GetSpecificPortMappingEntry"};
SOAPAction SOAPActionDeletePortMapping = {.name = "DeletePortMapping"};

// timeoutMs - timeout in milli seconds for the operations of this class, 0 for blocking operation
TinyUPnP::TinyUPnP(unsigned long timeoutMs = 20000) {
    _timeoutMs = timeoutMs;
    _lastUpdateTime = 0;
    _consequtiveFails = 0;
    _headRuleNode = NULL;
    clearGatewayInfo(&_gwInfo);

    ESP_LOGD(TAG, "UPNP_UDP_TX_PACKET_MAX_SIZE=%d", UPNP_UDP_TX_PACKET_MAX_SIZE);
    ESP_LOGD(TAG, "UPNP_UDP_TX_RESPONSE_MAX_SIZE=%d", UPNP_UDP_TX_RESPONSE_MAX_SIZE);
}

TinyUPnP::~TinyUPnP() {
}

void TinyUPnP::addPortMappingConfig(IPAddress ruleIP, int rulePort, String ruleProtocol, int ruleLeaseDuration, String ruleFriendlyName) {
	addPortMappingConfig(ruleIP, rulePort, rulePort, ruleProtocol, ruleLeaseDuration, ruleFriendlyName);
}

void TinyUPnP::addPortMappingConfig(IPAddress ruleIP, int ruleInternalPort, int ruleExternalPort, String ruleProtocol, int ruleLeaseDuration, String ruleFriendlyName) {
    static int index = 0;
    upnpRule *newUpnpRule = new upnpRule();
    newUpnpRule->index = index++;
    newUpnpRule->internalAddr = (ruleIP == WiFi.localIP()) ? ipNull : ruleIP;  // for automatic IP change handling
    newUpnpRule->internalPort = ruleInternalPort;
    newUpnpRule->externalPort = ruleExternalPort;
    newUpnpRule->leaseDuration = ruleLeaseDuration;
    newUpnpRule->protocol = ruleProtocol;
    newUpnpRule->devFriendlyName = ruleFriendlyName;

    // linked list insert
    upnpRuleNode *newUpnpRuleNode = new upnpRuleNode();
    newUpnpRuleNode->upnpRule = newUpnpRule;
    newUpnpRuleNode->next = NULL;
    
    if (_headRuleNode == NULL) {
        _headRuleNode = newUpnpRuleNode;
    } else {
        upnpRuleNode *currNode = _headRuleNode;
        while (currNode->next != NULL) {
            currNode = currNode->next;
        }
        currNode->next = newUpnpRuleNode;
    }
}

portMappingResult TinyUPnP::commitPortMappings() {
    if (!_headRuleNode) {
        ESP_LOGE(TAG, "No UPnP port mapping was set.");
        return EMPTY_PORT_MAPPING_CONFIG;
    }

    unsigned long startTime = millis();

    // verify WiFi is connected
    if (!testConnectivity(startTime)) {
        ESP_LOGE(TAG, "Not connected to WiFi, cannot continue.");
        return NETWORK_ERROR;
    }

    // get all the needed IGD information using SSDP if we don't have it already
    if (!isGatewayInfoValid(&_gwInfo)) {
        getGatewayInfo(&_gwInfo, startTime);
        if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
            ESP_LOGE(TAG, "Invalid router info, cannot continue.");
            _wifiClient.stop();
            return NETWORK_ERROR;
        }
        delay(1000);  // longer delay to allow more time for the router to update its rules
    }

    ESP_LOGD(TAG, "port [%d] actionPort [%d]", _gwInfo.port, _gwInfo.actionPort);

    // double verify gateway information is valid
    if (!isGatewayInfoValid(&_gwInfo)) {
        ESP_LOGE(TAG, "Invalid router info, cannot continue.");
        return NETWORK_ERROR;
    }

    if (_gwInfo.port != _gwInfo.actionPort) {
        // in this case we need to connect to a different port
        ESP_LOGD(TAG, "Connection port changed, disconnecting from IGD.");
        _wifiClient.stop();
    }

    bool allPortMappingsAlreadyExist = true;  // for debug
    int addedPortMappings = 0;  // for debug
    upnpRuleNode *currNode = _headRuleNode;
    while (currNode != NULL) {
        ESP_LOGD(TAG, "Verify port mapping for rule [%s]", currNode->upnpRule->devFriendlyName.c_str());
        bool currPortMappingAlreadyExists = true;  // for debug
        // TODO: since verifyPortMapping connects to the IGD then addPortMappingEntry can skip it
        if (!verifyPortMapping(&_gwInfo, currNode->upnpRule)) {
            // need to add the port mapping
            currPortMappingAlreadyExists = false;
            allPortMappingsAlreadyExist = false;
            if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
                ESP_LOGE(TAG, "Timeout expired while trying to add a port mapping.");
                _wifiClient.stop();
                return TIMEOUT;
            }

            addPortMappingEntry(&_gwInfo, currNode->upnpRule);

            int tries = 0;
            while (tries <= 3) {
                delay(2000);  // longer delay to allow more time for the router to update its rules
                if (verifyPortMapping(&_gwInfo, currNode->upnpRule)) {
                    break;
                }
                tries++;
            }

            if (tries > 3) {
                _wifiClient.stop();
                return VERIFICATION_FAILED;
            }
        }

        if (!currPortMappingAlreadyExists) {
            addedPortMappings++;
            ESP_LOGD(TAG, "Port mapping [%s] was added.", currNode->upnpRule->devFriendlyName.c_str());
        }

        currNode = currNode->next;
    }

    _wifiClient.stop();
    
    if (allPortMappingsAlreadyExist) {
        ESP_LOGD(TAG, "All port mappings were already found in the IGD, not doing anything.");
        return ALREADY_MAPPED;
    } else {
        // addedPortMappings is at least 1 here
        if (addedPortMappings > 1) {
            ESP_LOGD(TAG, "%d UPnP port mappings were added.", addedPortMappings);
        } else {
            ESP_LOGD(TAG, "One UPnP port mapping was added.");
        }
    }

    return SUCCESS;
}

boolean TinyUPnP::getGatewayInfo(gatewayInfo *deviceInfo, long startTime) {
    while (!connectUDP()) {
        if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
            ESP_LOGE(TAG, "Timeout expired while connecting UDP.");
            _udpClient.stop();
            return false;
        }
        delay(500);

    }
    
    broadcastMSearch();
    IPAddress gatewayIP = WiFi.gatewayIP();

    ESP_LOGD(TAG, "Gateway IP [%s]", gatewayIP.toString().c_str());

    ssdpDevice* ssdpDevice_ptr = NULL;
    while ((ssdpDevice_ptr = waitForUnicastResponseToMSearch(gatewayIP)) == NULL) {
        if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
            ESP_LOGE(TAG, "Timeout expired waiting for gateway response to M-SEARCH.");
            _udpClient.stop();
            return false;
        }
        delay(1);
    }

    deviceInfo->host = ssdpDevice_ptr->host;
    deviceInfo->port = ssdpDevice_ptr->port;
    deviceInfo->path = ssdpDevice_ptr->path;
    // the following is the default and may be overridden if URLBase tag is specified
    deviceInfo->actionPort = ssdpDevice_ptr->port;

    delete ssdpDevice_ptr;

    // close the UDP connection
    _udpClient.stop();

    // connect to IGD (TCP connection)
    while (!connectToIGD(deviceInfo->host, deviceInfo->port)) {
        if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
            ESP_LOGE(TAG, "Timeout expired while trying to connect to the IGD.");
            _wifiClient.stop();
            return false;
        }
        delay(500);
    }
    
    // get event urls from the gateway IGD
    while (!getIGDEventURLs(deviceInfo)) {
        if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
            ESP_LOGE(TAG, "Timeout expired while adding a new port mapping.");
            _wifiClient.stop();
            return false;
        }
        delay(500);
    }

    return true;
}

void TinyUPnP::clearGatewayInfo(gatewayInfo *deviceInfo) {
    deviceInfo->host = IPAddress(0, 0, 0, 0);
    deviceInfo->port = 0;
    deviceInfo->path = "";
    deviceInfo->actionPort = 0;
    deviceInfo->actionPath = "";
    deviceInfo->serviceTypeName = "";
}

boolean TinyUPnP::isGatewayInfoValid(gatewayInfo *deviceInfo) {
    ESP_LOGD(TAG, "isGatewayInfoValid host[%s] port[%d] path[%s] actionPort[%d] actionPath[%s] serviceTypeName[%s]",
             deviceInfo->host.toString().c_str(),
             deviceInfo->port,
             deviceInfo->path.c_str(),
             deviceInfo->actionPort,
             deviceInfo->actionPath.c_str(),
             deviceInfo->serviceTypeName.c_str());

    if (deviceInfo->host == IPAddress(0, 0, 0, 0)
        || deviceInfo->port == 0
        || deviceInfo->path.length() == 0
        || deviceInfo->actionPort == 0) {
        ESP_LOGD(TAG, "Gateway info is not valid");
        return false;
    }

    ESP_LOGD(TAG, "Gateway info is valid");
    return true;
}

portMappingResult TinyUPnP::updatePortMappings(unsigned long intervalMs, callback_function fallback) {
    if (millis() - _lastUpdateTime >= intervalMs) {
        ESP_LOGD(TAG, "Updating port mapping");

        // fallback
        if (_consequtiveFails >= MAX_NUM_OF_UPDATES_WITH_NO_EFFECT) {
            ESP_LOGE(TAG, "Too many times with no effect on updatePortMappings. Current number of fallbacks: [%lu]", _consequtiveFails);

            _consequtiveFails = 0;
            clearGatewayInfo(&_gwInfo);
            if (fallback != NULL) {
                ESP_LOGD(TAG, "Executing fallback method");
                fallback();
            }

            return TIMEOUT;
        }

        portMappingResult result = commitPortMappings();

        if (result == SUCCESS || result == ALREADY_MAPPED) {
            _lastUpdateTime = millis();
            _wifiClient.stop();
            _consequtiveFails = 0;
            return result;
        } else {
            _lastUpdateTime += intervalMs / 2;  // delay next try
            ESP_LOGE(TAG, "While updating UPnP port mapping. Failed with error code [%d]", result);
            _wifiClient.stop();
            _consequtiveFails++;
            return result;
        }
    }

    _wifiClient.stop();
    return NOP;  // no need to check yet
}

boolean TinyUPnP::testConnectivity(unsigned long startTime) {
    ESP_LOGD(TAG, "Testing WiFi connection for [%s]", WiFi.localIP().toString().c_str());
    
    while (WiFi.status() != WL_CONNECTED) {
        if (_timeoutMs > 0 && startTime > 0 && (millis() - startTime > _timeoutMs)) {
            ESP_LOGE(TAG, "Timeout expired while verifying WiFi connection.");
            _wifiClient.stop();
            return false;
        }
        delay(200);
        // O log contínuo de "." foi removido para um output mais limpo.
    }
    ESP_LOGD(TAG, "WiFi connection ==> GOOD");

    ESP_LOGD(TAG, "Testing internet connection...");
    _wifiClient.connect(connectivityTestIp, 80);
    while (!_wifiClient.connected()) {
        if (startTime + TCP_CONNECTION_TIMEOUT_MS > millis()) {
            ESP_LOGE(TAG, "Internet connection test ==> BAD");
            _wifiClient.stop();
            return false;
        }
    }

    ESP_LOGD(TAG, "Internet connection test ==> GOOD");
    _wifiClient.stop();
    return true;
}

boolean TinyUPnP::verifyPortMapping(gatewayInfo *deviceInfo, upnpRule *rule_ptr) {
    if (!applyActionOnSpecificPortMapping(&SOAPActionGetSpecificPortMappingEntry ,deviceInfo, rule_ptr)) {
        return false;
    }

    ESP_LOGD(TAG, "verifyPortMapping called");
    
    boolean isSuccess = false;
    boolean detectedChangedIP = false;
    while (_wifiClient.available()) {
        String line = _wifiClient.readStringUntil('\r');
        ESP_LOGD(TAG, "Response line: %s", line.c_str());

        if (line.indexOf("errorCode") >= 0) {
            isSuccess = false;
            // Apenas esvazia o buffer sem logar cada linha para evitar poluição
            while (_wifiClient.available()) {
                _wifiClient.read();
            }
            continue;
        }

        if (line.indexOf("NewInternalClient") >= 0) {
            String content = getTagContent(line, "NewInternalClient");
            if (content.length() > 0) {
                IPAddress ipAddressToVerify = (rule_ptr->internalAddr == ipNull) ? WiFi.localIP() : rule_ptr->internalAddr;
                if (content == ipAddressToVerify.toString()) {
                    isSuccess = true;
                } else {
                    detectedChangedIP = true;
                }
            }
        }
    }

    _wifiClient.stop();

    if (isSuccess) {
        ESP_LOGD(TAG, "Port mapping found in IGD");
    } else if (detectedChangedIP) {
        ESP_LOGW(TAG, "Detected a change in IP, removing all old port mappings.");
        removeAllPortMappingsFromIGD();
    } else {
        ESP_LOGD(TAG, "Could not find port mapping in IGD");
    }

    return isSuccess;
}

boolean TinyUPnP::deletePortMapping(gatewayInfo *deviceInfo, upnpRule *rule_ptr) {
    if (!applyActionOnSpecificPortMapping(&SOAPActionDeletePortMapping ,deviceInfo, rule_ptr)) {
        return false;
    }
    
    boolean isSuccess = false;
    while (_wifiClient.available()) {
        String line = _wifiClient.readStringUntil('\r');
        ESP_LOGD(TAG, "Response line: %s", line.c_str());

        if (line.indexOf("errorCode") >= 0) {
            isSuccess = false;
            // Apenas esvazia o buffer sem logar para evitar poluição
            while (_wifiClient.available()) {
                _wifiClient.read();
            }
            continue;
        }
        if (line.indexOf("DeletePortMappingResponse") >= 0) { 
            isSuccess = true;
        }
    }

    return isSuccess;
}

boolean TinyUPnP::applyActionOnSpecificPortMapping(SOAPAction *soapAction, gatewayInfo *deviceInfo, upnpRule *rule_ptr) {
    ESP_LOGD(TAG, "Apply action [%s] on port mapping [%s]", soapAction->name, rule_ptr->devFriendlyName.c_str());

    // Conecta ao IGD (conexão TCP) novamente, se necessário
    unsigned long timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
    if (!_wifiClient.connected()) {
        while (!connectToIGD(deviceInfo->host, deviceInfo->actionPort)) {
            if (millis() > timeout) {
                ESP_LOGE(TAG, "Timeout expired while trying to connect to the IGD");
                _wifiClient.stop();
                return false;
            }
            delay(500);
        }
    }

    strcpy_P(body_tmp, PSTR("<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n<s:Body>\r\n<u:"));
    strcat_P(body_tmp, soapAction->name);
    strcat_P(body_tmp, PSTR(" xmlns:u=\""));
    strcat_P(body_tmp, deviceInfo->serviceTypeName.c_str());
    strcat_P(body_tmp, PSTR("\">\r\n<NewRemoteHost></NewRemoteHost>\r\n<NewExternalPort>"));
    sprintf(integer_string, "%d", rule_ptr->externalPort);
    strcat_P(body_tmp, integer_string);
    strcat_P(body_tmp, PSTR("</NewExternalPort>\r\n<NewProtocol>"));
    strcat_P(body_tmp, rule_ptr->protocol.c_str());
    strcat_P(body_tmp, PSTR("</NewProtocol>\r\n</u:"));
    strcat_P(body_tmp, soapAction->name);
    strcat_P(body_tmp, PSTR(">\r\n</s:Body>\r\n</s:Envelope>\r\n"));

    sprintf(integer_string, "%d", strlen(body_tmp));

    _wifiClient.print(F("POST "));
    _wifiClient.print(deviceInfo->actionPath);
    _wifiClient.println(F(" HTTP/1.1"));
    _wifiClient.println(F("Connection: close"));
    _wifiClient.println(F("Content-Type: text/xml; charset=\"utf-8\""));
    _wifiClient.println("Host: " + deviceInfo->host.toString() + ":" + String(deviceInfo->actionPort));
    _wifiClient.print(F("SOAPAction: \""));
    _wifiClient.print(deviceInfo->serviceTypeName);
    _wifiClient.print(F("#"));
    _wifiClient.print(soapAction->name);
    _wifiClient.println(F("\""));
    _wifiClient.print(F("Content-Length: "));
    _wifiClient.println(integer_string);
    _wifiClient.println();

    _wifiClient.println(body_tmp);
    _wifiClient.println();

    ESP_LOGD(TAG, "SOAP Request Body:\n%s", body_tmp);

    timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
    while (_wifiClient.available() == 0) {
        if (millis() > timeout) {
            ESP_LOGE(TAG, "TCP connection timeout while waiting for SOAP response");
            _wifiClient.stop();
            return false;
        }
    }
    return true;
}

void TinyUPnP::removeAllPortMappingsFromIGD() {
    upnpRuleNode *currNode = _headRuleNode;
    while (currNode != NULL) {
        deletePortMapping(&_gwInfo, currNode->upnpRule);
        currNode = currNode->next;
    }
}

// a single try to connect UDP multicast address and port of UPnP (239.255.255.250 and 1900 respectively)
// this will enable receiving SSDP packets after the M-SEARCH multicast message will be broadcasted
boolean TinyUPnP::connectUDP() {
#if defined(ESP8266)
    if (_udpClient.beginMulticast(WiFi.localIP(), ipMulti, 0)) {
        return true;
    }
#else
    if (_udpClient.beginMulticast(ipMulti, UPNP_SSDP_PORT)) {
        return true;
    }
#endif

    ESP_LOGE(TAG, "UDP multicast connection failed");
    return false;
}

void TinyUPnP::broadcastMSearch(bool isSsdpAll /*=false*/) {
    ESP_LOGD(TAG, "Sending M-SEARCH to [%s] Port [%d]", ipMulti.toString().c_str(), UPNP_SSDP_PORT);

#if defined(ESP8266)
    _udpClient.beginPacketMulticast(ipMulti, UPNP_SSDP_PORT, WiFi.localIP());
#else
    uint8_t beginMulticastPacketRes = _udpClient.beginMulticastPacket();
    ESP_LOGD(TAG, "beginMulticastPacketRes [%u]", beginMulticastPacketRes);
#endif

    const char * const * serviceList = serviceListUpnp;
    if (isSsdpAll) {
        serviceList = serviceListSsdpAll;
    }

    for (int i = 0; serviceList[i]; i++) {
        strcpy_P(body_tmp, PSTR("M-SEARCH * HTTP/1.1\r\n"));
        strcat_P(body_tmp, PSTR("HOST: 239.255.255.250:"));
        sprintf(integer_string, "%d", UPNP_SSDP_PORT);
        strcat_P(body_tmp, integer_string);
        strcat_P(body_tmp, PSTR("\r\n"));
        strcat_P(body_tmp, PSTR("MAN: \"ssdp:discover\"\r\n"));
        strcat_P(body_tmp, PSTR("MX: 2\r\n"));
        strcat_P(body_tmp, PSTR("ST: "));
        strcat_P(body_tmp, serviceList[i]);
        strcat_P(body_tmp, PSTR("\r\n"));
        strcat_P(body_tmp, PSTR("USER-AGENT: unix/5.1 UPnP/2.0 TinyUPnP/1.0\r\n"));
        strcat_P(body_tmp, PSTR("\r\n"));

        ESP_LOGD(TAG, "M-SEARCH packet content:\n%s", body_tmp);
        size_t len = strlen(body_tmp);
        ESP_LOGD(TAG, "M-SEARCH packet length is [%d]", len);

#if defined(ESP8266)
        _udpClient.write(body_tmp);
#else
        _udpClient.print(body_tmp);
#endif
    
        int endPacketRes = _udpClient.endPacket();
        ESP_LOGD(TAG, "endPacketRes [%d]", endPacketRes);
    }

    ESP_LOGD(TAG, "M-SEARCH packets sent");
}

ssdpDeviceNode* TinyUPnP::listSsdpDevices() {
    if (_timeoutMs <= 0) {
        ESP_LOGE(TAG, "Timeout must be set to use this method, exiting.");
        return NULL;
    }

    unsigned long startTime = millis();
    while (!connectUDP()) {
        if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
            ESP_LOGE(TAG, "Timeout expired while connecting UDP");
            _udpClient.stop();
            return NULL;
        }
        delay(500);
    }
    
    broadcastMSearch(true);
    IPAddress gatewayIP = WiFi.gatewayIP();
    ESP_LOGD(TAG, "Gateway IP [%s]", gatewayIP.toString().c_str());

    ssdpDeviceNode *ssdpDeviceNode_head = NULL;
    ssdpDeviceNode *ssdpDeviceNode_tail = NULL;

    while (true) {
        ssdpDevice *ssdpDevice_ptr = waitForUnicastResponseToMSearch(ipNull);
        if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
            ESP_LOGD(TAG, "Timeout expired while waiting for M-SEARCH responses.");
            _udpClient.stop();
            delete ssdpDevice_ptr; // Limpa o último ponteiro se houver um
            break;
        }

        if (ssdpDevice_ptr != NULL) {
            ssdpDeviceNode *newNode = new ssdpDeviceNode();
            newNode->ssdpDevice = ssdpDevice_ptr;
            newNode->next = NULL;

            if (ssdpDeviceNode_head == NULL) {
                ssdpDeviceNode_head = newNode;
                ssdpDeviceNode_tail = newNode;
            } else {
                ssdpDeviceNode_tail->next = newNode;
                ssdpDeviceNode_tail = newNode;
            }
        }
        delay(5);
    }

    _udpClient.stop();

    // Dedup SSDP devices from the list - O(n^2)
    ssdpDeviceNode *ptr = ssdpDeviceNode_head;
    while (ptr != NULL) {
        ssdpDeviceNode *prev = ptr;
        ssdpDeviceNode *curr = ptr->next;
        while (curr != NULL) {
            if (curr->ssdpDevice->host == ptr->ssdpDevice->host
                && curr->ssdpDevice->port == ptr->ssdpDevice->port
                && curr->ssdpDevice->path == ptr->ssdpDevice->path) {
                prev->next = curr->next;
                delete curr->ssdpDevice;
                delete curr;
                curr = prev->next;
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
        ptr = ptr->next;
    }

    return ssdpDeviceNode_head;
}


// Assuming an M-SEARCH message was broadcaseted, wait for the response from the IGD (Internet Gateway Device)
// Note: the response from the IGD is sent back as unicast to this device
// Note: only gateway defined IGD response will be considered, the rest will be ignored
ssdpDevice* TinyUPnP::waitForUnicastResponseToMSearch(IPAddress gatewayIP) {
    // Limpa o buffer UDP para garantir que estamos lendo uma resposta nova
    _udpClient.flush();
    int packetSize = _udpClient.parsePacket();

    if (packetSize <= 0) {
        return NULL;
    }

    IPAddress remoteIP = _udpClient.remoteIP();
    
    // Se um gatewayIP específico for fornecido, filtra pacotes de outros IPs
    if (gatewayIP != ipNull && remoteIP != gatewayIP) {
        ESP_LOGD(TAG, "Discarded packet not from IGD. Gateway: %s, Remote: %s", 
                 gatewayIP.toString().c_str(), remoteIP.toString().c_str());
        return NULL;
    }

    ESP_LOGD(TAG, "Received packet size: %d from IP: %s Port: %d", 
             packetSize, remoteIP.toString().c_str(), _udpClient.remotePort());

    if (packetSize > UPNP_UDP_TX_RESPONSE_MAX_SIZE) {
        ESP_LOGE(TAG, "Received packet larger than response buffer (%d > %d)", 
                 packetSize, UPNP_UDP_TX_RESPONSE_MAX_SIZE);
        return NULL;
    }
  
    int idx = 0;
    while (idx < packetSize) {
        memset(packetBuffer, 0, UPNP_UDP_TX_PACKET_MAX_SIZE);
        int len = _udpClient.read(packetBuffer, UPNP_UDP_TX_PACKET_MAX_SIZE);
        if (len <= 0) {
            break;
        }
        ESP_LOGD(TAG, "UDP packet read bytes [%d] out of [%d]", len, packetSize);
        memcpy(responseBuffer + idx, packetBuffer, len);
        idx += len;
    }
    responseBuffer[idx] = '\0';

    ESP_LOGD(TAG, "Gateway packet content:\n%s", responseBuffer);

    const char * const * serviceList = serviceListUpnp;
    if (gatewayIP == ipNull) {
        serviceList = serviceListSsdpAll;
    }

    if (gatewayIP != ipNull) {
        boolean foundIGD = false;
        for (int i = 0; serviceList[i]; i++) {
            if (strstr(responseBuffer, serviceList[i]) != NULL) {
                foundIGD = true;
                ESP_LOGD(TAG, "IGD of type [%s] found", serviceList[i]);
                break;
            }
        }

        if (!foundIGD) {
            ESP_LOGD(TAG, "IGD service type not found in response");
            return NULL;
        }
    }

    String location = "";
    char* location_indexStart = strstr(responseBuffer, "LOCATION:");
    if (location_indexStart == NULL) { // Tenta com 'L' minúsculo
        location_indexStart = strstr(responseBuffer, "Location:");
    }
    if (location_indexStart == NULL) { // Tenta com 'l' minúsculo
        location_indexStart = strstr(responseBuffer, "location:");
    }

    if (location_indexStart != NULL) {
        location_indexStart += strlen("location:"); // Avança o ponteiro
        char* location_indexEnd = strstr(location_indexStart, "\r\n");
        if (location_indexEnd != NULL) {
            int urlLength = location_indexEnd - location_indexStart;
            char locationCharArr[urlLength + 1];
            memcpy(locationCharArr, location_indexStart, urlLength);
            locationCharArr[urlLength] = '\0';
            location = String(locationCharArr);
            location.trim();
        } else {
            ESP_LOGE(TAG, "Could not extract value from LOCATION param");
            return NULL;
        }
    } else {
        ESP_LOGE(TAG, "LOCATION param was not found in SSDP response");
        return NULL;
    }
    
    ESP_LOGD(TAG, "Device location found [%s]", location.c_str());
 
    IPAddress host = getHost(location);
    int port = getPort(location);
    String path = getPath(location);

    ssdpDevice *newSsdpDevice_ptr = new ssdpDevice();
    
    newSsdpDevice_ptr->host = host;
    newSsdpDevice_ptr->port = port;
    newSsdpDevice_ptr->path = path;
    
    return newSsdpDevice_ptr;
}

// a single trial to connect to the IGD (with TCP)
boolean TinyUPnP::connectToIGD(IPAddress host, int port) {
    ESP_LOGD(TAG, "Connecting to IGD with host [%s] port [%d]", host.toString().c_str(), port);
    
    if (_wifiClient.connect(host, port)) {
        ESP_LOGD(TAG, "Connected to IGD");
        return true;
    }
    return false;
}

boolean TinyUPnP::getIGDEventURLs(gatewayInfo *deviceInfo) {
    ESP_LOGD(TAG, "called getIGDEventURLs. ActionPath: [%s], Path: [%s]", 
             deviceInfo->actionPath.c_str(), deviceInfo->path.c_str());

    // Monta e envia a requisição HTTP GET
    _wifiClient.print(F("GET "));
    _wifiClient.print(deviceInfo->path);
    _wifiClient.println(F(" HTTP/1.1"));
    _wifiClient.println(F("Content-Type: text/xml; charset=\"utf-8\""));
    _wifiClient.println("Host: " + deviceInfo->host.toString() + ":" + String(deviceInfo->actionPort));
    _wifiClient.println(F("Content-Length: 0"));
    _wifiClient.println();
    
    // Aguarda pela resposta
    unsigned long timeout = millis();
    while (_wifiClient.available() == 0) {
        if (millis() - timeout > TCP_CONNECTION_TIMEOUT_MS) {
            ESP_LOGE(TAG, "TCP connection timeout while executing getIGDEventURLs");
            _wifiClient.stop();
            return false;
        }
    }
    
    // Lê e processa a resposta do servidor
    boolean upnpServiceFound = false;
    boolean urlBaseFound = false;
    while (_wifiClient.available()) {
        String line = _wifiClient.readStringUntil('\r');
        int index_in_line = 0;
        ESP_LOGD(TAG, "Response line: %s", line.c_str());

        if (!urlBaseFound && line.indexOf("<URLBase>") >= 0) {
            String baseUrl = getTagContent(line, "URLBase");
            if (baseUrl.length() > 0) {
                baseUrl.trim();
                IPAddress host = getHost(baseUrl);
                int port = getPort(baseUrl);
                deviceInfo->actionPort = port;
                ESP_LOGD(TAG, "URLBase found: [%s] -> host: [%s], port: [%d]", 
                         baseUrl.c_str(), host.toString().c_str(), port);
                urlBaseFound = true;
            }
        }

        int service_type_index_start = 0;
        for (int i = 0; serviceListUpnp[i]; i++) {
            int service_type_index = line.indexOf(UPNP_SERVICE_TYPE_TAG_START + serviceListUpnp[i]);
            if (!upnpServiceFound && service_type_index >= 0) {
                upnpServiceFound = true;
                deviceInfo->serviceTypeName = getTagContent(line.substring(service_type_index), UPNP_SERVICE_TYPE_TAG_NAME);
                ESP_LOGD(TAG, "Service found: [%s] for deviceType [%s]", 
                         deviceInfo->serviceTypeName.c_str(), serviceListUpnp[i]);
                index_in_line = line.indexOf(UPNP_SERVICE_TYPE_TAG_END, service_type_index);
                break;
            }
        }
        
        if (upnpServiceFound && (index_in_line = line.indexOf("<controlURL>", index_in_line)) >= 0) {
            String controlURLContent = getTagContent(line.substring(index_in_line), "controlURL");
            if (controlURLContent.length() > 0) {
                deviceInfo->actionPath = controlURLContent;
                ESP_LOGD(TAG, "controlURL tag found! Setting actionPath to [%s]", controlURLContent.c_str());
                
                // Limpa o buffer e retorna sucesso
                while (_wifiClient.available()) {
                    _wifiClient.read();
                }
                return true;
            }
        }
    }

    return false;
}

boolean TinyUPnP::addPortMappingEntry(gatewayInfo *deviceInfo, upnpRule *rule_ptr) {
    ESP_LOGD(TAG, "called addPortMappingEntry");

    // Conecta ao IGD se necessário
    unsigned long timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
    if (!_wifiClient.connected()) {
        while (!connectToIGD(_gwInfo.host, _gwInfo.actionPort)) {
            if (millis() > timeout) {
                ESP_LOGE(TAG, "Timeout expired while trying to connect to the IGD");
                _wifiClient.stop();
                return false;
            }
            delay(500);
        }
    }

    ESP_LOGD(TAG, "deviceInfo->actionPath [%s]", deviceInfo->actionPath.c_str());
    ESP_LOGD(TAG, "deviceInfo->serviceTypeName [%s]", deviceInfo->serviceTypeName.c_str());

    strcpy_P(body_tmp, PSTR("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:AddPortMapping xmlns:u=\""));
    strcat_P(body_tmp, deviceInfo->serviceTypeName.c_str());
    strcat_P(body_tmp, PSTR("\"><NewRemoteHost></NewRemoteHost><NewExternalPort>"));
    sprintf(integer_string, "%d", rule_ptr->externalPort);
    strcat_P(body_tmp, integer_string);
    strcat_P(body_tmp, PSTR("</NewExternalPort><NewProtocol>"));
    strcat_P(body_tmp, rule_ptr->protocol.c_str());
    strcat_P(body_tmp, PSTR("</NewProtocol><NewInternalPort>"));
    sprintf(integer_string, "%d", rule_ptr->internalPort);
    strcat_P(body_tmp, integer_string);
    strcat_P(body_tmp, PSTR("</NewInternalPort><NewInternalClient>"));
    IPAddress ipAddress = (rule_ptr->internalAddr == ipNull) ? WiFi.localIP() : rule_ptr->internalAddr;
    strcat_P(body_tmp, ipAddress.toString().c_str());
    strcat_P(body_tmp, PSTR("</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>"));
    strcat_P(body_tmp, rule_ptr->devFriendlyName.c_str());
    strcat_P(body_tmp, PSTR("</NewPortMappingDescription><NewLeaseDuration>"));
    sprintf(integer_string, "%d", rule_ptr->leaseDuration);
    strcat_P(body_tmp, integer_string);
    strcat_P(body_tmp, PSTR("</NewLeaseDuration></u:AddPortMapping></s:Body></s:Envelope>"));

    sprintf(integer_string, "%d", strlen(body_tmp));
    
    _wifiClient.print(F("POST "));
    _wifiClient.print(deviceInfo->actionPath);
    _wifiClient.println(F(" HTTP/1.1"));
    _wifiClient.println(F("Content-Type: text/xml; charset=\"utf-8\""));
    _wifiClient.println("Host: " + deviceInfo->host.toString() + ":" + String(deviceInfo->actionPort));
    _wifiClient.print(F("SOAPAction: \""));
    _wifiClient.print(deviceInfo->serviceTypeName);
    _wifiClient.println(F("#AddPortMapping\""));
    _wifiClient.print(F("Content-Length: "));
    _wifiClient.println(integer_string);
    _wifiClient.println();
    _wifiClient.println(body_tmp);
    _wifiClient.println();
    
    ESP_LOGD(TAG, "SOAP Request (AddPortMapping), Content-Length: %s\n%s", integer_string, body_tmp);
  
    timeout = millis();
    while (_wifiClient.available() == 0) {
        if (millis() - timeout > TCP_CONNECTION_TIMEOUT_MS) {
            ESP_LOGE(TAG, "TCP connection timeout while adding a port mapping");
            _wifiClient.stop();
            return false;
        }
    }

    boolean isSuccess = true;
    while (_wifiClient.available()) {
        String line = _wifiClient.readStringUntil('\r');
        if (line.indexOf("errorCode") >= 0) {
            isSuccess = false;
        }
        ESP_LOGD(TAG, "Response line: %s", line.c_str());
    }
    
    if (!isSuccess) {
        _wifiClient.stop();
    }

    return isSuccess;
}

boolean TinyUPnP::printAllPortMappings() {
    if (!isGatewayInfoValid(&_gwInfo)) {
        ESP_LOGE(TAG, "Invalid router info, cannot continue");
        return false;
    }
    
    upnpRuleNode *ruleNodeHead_ptr = NULL;
    upnpRuleNode *ruleNodeTail_ptr = NULL;
    auto cleanup_rule_nodes_fn = [&ruleNodeHead_ptr] () {
        upnpRuleNode *curr_ptr = ruleNodeHead_ptr;
        while (curr_ptr != NULL) {
            upnpRuleNode *del_prt = curr_ptr;
            curr_ptr = curr_ptr->next;
            delete del_prt->upnpRule;
            delete del_prt;
        }
    };

    boolean reachedEnd = false;
    int index = 0;
    while (!reachedEnd) {
        unsigned long timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
        if (!_wifiClient.connected()) {
            while (!connectToIGD(_gwInfo.host, _gwInfo.actionPort)) {
                if (millis() > timeout) {
                    ESP_LOGE(TAG, "Timeout expired while trying to connect to the IGD");
                    _wifiClient.stop();
                    cleanup_rule_nodes_fn();
                    return false;
                }
                delay(1000);
            }
        }
        
        ESP_LOGD(TAG, "Sending query for port mapping index [%d]", index);

        strcpy_P(body_tmp, PSTR("<?xml version=\"1.0\"?>"
            "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
            "<s:Body>"
            "<u:GetGenericPortMappingEntry xmlns:u=\""));
        strcat_P(body_tmp, _gwInfo.serviceTypeName.c_str());
        strcat_P(body_tmp, PSTR("\">"
            "  <NewPortMappingIndex>"));
        sprintf(integer_string, "%d", index);
        strcat_P(body_tmp, integer_string);
        strcat_P(body_tmp, PSTR("</NewPortMappingIndex>"
            "</u:GetGenericPortMappingEntry>"
            "</s:Body>"
            "</s:Envelope>"));
        
        sprintf(integer_string, "%d", strlen(body_tmp));
        
        _wifiClient.print(F("POST "));
        _wifiClient.print(_gwInfo.actionPath);
        _wifiClient.println(F(" HTTP/1.1"));
        _wifiClient.println(F("Connection: keep-alive"));
        _wifiClient.println(F("Content-Type: text/xml; charset=\"utf-8\""));
        _wifiClient.println("Host: " + _gwInfo.host.toString() + ":" + String(_gwInfo.actionPort));
        _wifiClient.print(F("SOAPAction: \""));
        _wifiClient.print(_gwInfo.serviceTypeName);
        _wifiClient.println(F("#GetGenericPortMappingEntry\""));
        _wifiClient.print(F("Content-Length: "));
        _wifiClient.println(integer_string);
        _wifiClient.println();
        _wifiClient.println(body_tmp);
        _wifiClient.println();
  
        timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
        while (_wifiClient.available() == 0) {
            if (millis() > timeout) {
                ESP_LOGE(TAG, "TCP connection timeout while retrieving port mappings");
                _wifiClient.stop();
                cleanup_rule_nodes_fn();
                return false;
            }
        }
        
        while (_wifiClient.available()) {
            String line = _wifiClient.readStringUntil('\r');
            ESP_LOGD(TAG, "Response line: %s", line.c_str());

            if (line.indexOf(PORT_MAPPING_INVALID_INDEX) >= 0) {
                reachedEnd = true;
            } else if (line.indexOf(PORT_MAPPING_INVALID_ACTION) >= 0) {
                ESP_LOGW(TAG, "Invalid action while reading port mappings");
                reachedEnd = true;
            } else if (line.indexOf(F("HTTP/1.1 500 ")) >= 0) {
                ESP_LOGW(TAG, "Internal server error, likely because all mappings have been shown");
                reachedEnd = true;
            } else if (line.indexOf(F("GetGenericPortMappingEntryResponse")) >= 0) {
                upnpRule *rule_ptr = new upnpRule();
                rule_ptr->index = index;
                rule_ptr->devFriendlyName = getTagContent(line, "NewPortMappingDescription");
                String newInternalClient = getTagContent(line, "NewInternalClient");
                if (newInternalClient == "") {
                    delete rule_ptr;
                    continue;
                }
                rule_ptr->internalAddr.fromString(newInternalClient);
                rule_ptr->internalPort = getTagContent(line, "NewInternalPort").toInt();
                rule_ptr->externalPort = getTagContent(line, "NewExternalPort").toInt();
                rule_ptr->protocol = getTagContent(line, "NewProtocol");
                rule_ptr->leaseDuration = getTagContent(line, "NewLeaseDuration").toInt();
                        
                upnpRuleNode *currRuleNode_ptr = new upnpRuleNode();
                currRuleNode_ptr->upnpRule = rule_ptr;
                currRuleNode_ptr->next = NULL;
                if (ruleNodeHead_ptr == NULL) {
                    ruleNodeHead_ptr = currRuleNode_ptr;
                    ruleNodeTail_ptr = currRuleNode_ptr;
                } else {
                    ruleNodeTail_ptr->next = currRuleNode_ptr;
                    ruleNodeTail_ptr = currRuleNode_ptr;
                }
            }
        }
        
        index++;
        delay(250);
    }
    
    ESP_LOGI(TAG, "IGD current port mappings:");
    upnpRuleNode *curr_ptr = ruleNodeHead_ptr;
    while (curr_ptr != NULL) {
        upnpRuleToString(curr_ptr->upnpRule);
        upnpRuleNode *del_prt = curr_ptr;
        curr_ptr = curr_ptr->next;
        delete del_prt->upnpRule;
        delete del_prt;
    }
    
    _wifiClient.stop();
    return true;
}

void TinyUPnP::printPortMappingConfig() {
    ESP_LOGI(TAG, "TinyUPnP configured port mappings:");
    upnpRuleNode *currRuleNode = _headRuleNode;
    while (currRuleNode != NULL) {
        upnpRuleToString(currRuleNode->upnpRule);
        currRuleNode = currRuleNode->next;
    }
}

void TinyUPnP::upnpRuleToString(upnpRule *rule_ptr) {
    IPAddress ipAddress = (rule_ptr->internalAddr == ipNull) ? WiFi.localIP() : rule_ptr->internalAddr;
    
    // Usa um buffer de caracteres para formatar a string de forma eficiente
    char buffer[200];
    snprintf(buffer, sizeof(buffer),
             "%-5d %-30s %-18s %-7d %-7d %-7s %-7d",
             rule_ptr->index,
             rule_ptr->devFriendlyName.c_str(),
             ipAddress.toString().c_str(),
             rule_ptr->internalPort,
             rule_ptr->externalPort,
             rule_ptr->protocol.c_str(),
             rule_ptr->leaseDuration
    );

    ESP_LOGI(TAG, "%s", buffer);
}

void TinyUPnP::printSsdpDevices(ssdpDeviceNode* ssdpDeviceNode_head) {
    // Adiciona um cabeçalho para a lista no log
    ESP_LOGI(TAG, "Lista de Dispositivos SSDP Encontrados:");
    
    ssdpDeviceNode *ssdpDeviceNodeCurr = ssdpDeviceNode_head;
    if (ssdpDeviceNodeCurr == NULL) {
        ESP_LOGI(TAG, "-> Nenhum dispositivo encontrado.");
    }
    
    while (ssdpDeviceNodeCurr != NULL) {
        ssdpDeviceToString(ssdpDeviceNodeCurr->ssdpDevice);
        ssdpDeviceNodeCurr = ssdpDeviceNodeCurr->next;
    }
}

void TinyUPnP::ssdpDeviceToString(ssdpDevice* ssdpDevice) {
    ESP_LOGI(TAG, "-> host: [%s], port: [%d], path: [%s]", 
             ssdpDevice->host.toString().c_str(), 
             ssdpDevice->port, 
             ssdpDevice->path.c_str());
}


// String TinyUPnP::getSpacesString(int num) {
//     if (num < 0) {
//         num = 1;
//     }
//     String spaces = "";
//     for (int i = 0; i < num; i++) {
//         spaces += " ";
//     }
//     return spaces;
// }

/*
char* TinyUPnP::ipAddressToCharArr(IPAddress ipAddress) {
    char s[17];
    sprintf(s, "%d.%d.%d.%d", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
    s[16] = '\0';
    return s;
}*/

IPAddress TinyUPnP::getHost(String url) {
    IPAddress result(0,0,0,0);
    if (url.indexOf(F("https://")) != -1) {
        url.replace("https://", "");
    }
    if (url.indexOf(F("http://")) != -1) {
        url.replace("http://", "");
    }
    int endIndex = url.indexOf('/');
    if (endIndex != -1) {
        url = url.substring(0, endIndex);
    }
    int colonsIndex = url.indexOf(':');
    if (colonsIndex != -1) {
        url = url.substring(0, colonsIndex);
    }
    result.fromString(url);
    return result;
}

int TinyUPnP::getPort(String url) {
    int port = -1;
    if (url.indexOf(F("https://")) != -1) {
        url.replace("https://", "");
    }
    if (url.indexOf(F("http://")) != -1) {
        url.replace("http://", "");
    }
    int portEndIndex = url.indexOf("/");
    if (portEndIndex == -1) {
        portEndIndex = url.length();
    }
    url = url.substring(0, portEndIndex);
    int colonsIndex = url.indexOf(":");
    if (colonsIndex != -1) {
        url = url.substring(colonsIndex + 1, portEndIndex);
        port = url.toInt();
    } else {
        port = 80;
    }
    return port;
}

String TinyUPnP::getPath(String url) {
    if (url.indexOf("https://") != -1) {
        url.replace("https://", "");
    }
    if (url.indexOf("http://") != -1) {
        url.replace("http://", "");
    }
    int firstSlashIndex = url.indexOf("/");
    if (firstSlashIndex == -1) {
        ESP_LOGE(TAG, "Cannot find path in url [%s]", url.c_str());
        return "";
    }
    return url.substring(firstSlashIndex, url.length());
}

String TinyUPnP::getTagContent(const String &line, String tagName) {
    int startIndex = line.indexOf("<" + tagName + ">");
    if (startIndex == -1) {
        // Não logamos um erro aqui, pois é comum procurar por uma tag que não existe na linha atual.
        return "";
    }
    startIndex += tagName.length() + 2;
    int endIndex = line.indexOf("</" + tagName + ">", startIndex);
    if (endIndex == -1) {
        ESP_LOGE(TAG, "Found start tag <%s> but no end tag in line: %s", tagName.c_str(), line.c_str());
        return "";
    }
    return line.substring(startIndex, endIndex);
}