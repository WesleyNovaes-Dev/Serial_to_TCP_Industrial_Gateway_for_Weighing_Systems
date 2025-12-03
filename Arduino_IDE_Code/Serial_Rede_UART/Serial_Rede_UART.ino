#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "esp_eap_client.h"
#include <IPAddress.h>
#include <vector>
#include <stdarg.h>
#include <memory>

#include <SPI.h>
#include <Ethernet.h>

// ========== 1. DEFINIÇÕES E PINAGEM ==========
const int ledPin = 2;

// SERIAL RS232 (Balança)
#define RS232_TX 17
#define RS232_RX 16

// W5500 SPI pins
#define W5500_SCLK  18
#define W5500_MISO  19
#define W5500_MOSI  23
#define W5500_CS    5
#define W5500_RST   4

// MAC Address para Ethernet
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x05 };

// Configurações de Tempo
const long SOFTAP_GRACE_PERIOD_MS = 60000;
const long NETWORK_CHECK_INTERVAL = 15000; // 15 segundos para reconexão
const unsigned long WEB_POST_READ_TIMEOUT = 800;
const unsigned long RS232_POLL_INTERVAL = 1;
const unsigned long STATUS_LOG_INTERVAL = 60000; // 1 minuto
const unsigned long BALANCE_CHECK_INTERVAL = 5000; // Verificar balança a cada 5s

// ========== 2. ESTRUTURAS E ENUMS ==========
struct AuthClient {
    std::shared_ptr<Client> client;
    String ip;
    unsigned long lastSentMs = 0;
};

struct WinsConfig {
    String wins1;
    String wins2;
};

enum NetworkMode { 
    NET_NONE, 
    NET_SOFTAP, 
    NET_WIFI, 
    NET_ETHERNET 
};

enum LedStatus {
    LED_OFF,
    LED_ON,
    LED_BLINK_SLOW,    // Modo AP/Configuração
    LED_BLINK_FAST,    // Conectando
    LED_BLINK_DOUBLE   // Erro
};

// ========== 3. OBJETOS GLOBAIS ==========
NetworkMode currentMode = NET_NONE;
LedStatus currentLedStatus = LED_OFF;

HardwareSerial SerialRS232(2);
Preferences prefs;

// Servidores
WiFiServer wifiWeb(80);
WiFiServer wifiData(9000);
WiFiServer wifiLog(5000);

EthernetServer ethWeb(80);
EthernetServer ethData(9000);
EthernetServer ethLog(5000);

// Clientes
std::vector<AuthClient> dataClients;
std::vector<AuthClient> logClients;

// ========== 4. CONFIGURAÇÕES PERSISTENTES ==========
String adminPassword = "";
String deviceHostname = "esp32-balanca-Generic";
long rs232Baud = 9600;
uint16_t dataPort = 9000;
uint16_t logPort = 5000;
String lastSoftApPass = "12345678";
String softApSSID = "ESP32_Balanca_Config_Generic";

// Configurações de Rede
String lastSSID = "", lastPass = "", lastUser = "";
bool lastEnterprise = false;
bool lastUseStaticIP = false;
String lastStaticIP = "", lastSubnet = "", lastGateway = "", lastDns1 = "", lastDns2 = "";
WinsConfig winsConfig = {"", ""};

// Configurações do Sistema
bool lastUseLCD = false;
bool enableLineChangeDetector = false;
int dataSendFormat = 1;
bool sendAlways = true;

// ========== 5. VARIÁVEIS DE ESTADO ==========
bool connectionJustSucceeded = false;
unsigned long connectionSuccessTime = 0;
unsigned long ledPreviousMillis = 0;
const long ledInterval = 500;
bool isRs232Active = false;
unsigned long lastNetworkCheck = 0;
unsigned long softApStartTime = 0;
unsigned long lastStatusLogTime = 0;
unsigned long lastBalanceCheck = 0;
unsigned long lastReconnectAttempt = 0;

// Buffer de Dados
String currentWeightBuffer = "";
String lastSentWeight = "";
String serialUsbBuffer = "";

// Controle LED
unsigned long lastLedChange = 0;
int ledBlinkState = LOW;

// Estado do Sistema
bool balanceConnected = false;
bool networkConnected = false;
String lastWebAlert = "";

// ========== 6. PROTÓTIPOS ==========
void app_log(const char *format, ...);
void saveCredentials();
void loadCredentials();
void clearCredentials();
void startSoftAP();
void stopSoftAP();
void stopNetwork();
bool startEthernet();
bool startWiFi();
void manageNetwork();
void handleDataClient();
void handleLogClients();
void broadcastWeightAndClientStatusToLog(String cleanWeight);
String urlDecode(String str);
String getParam(String req, String key);
template <typename T> void handleWebClientGeneric(T client);

// Novas funções
void setLedStatus(LedStatus status);
void updateLed();
void performFactoryReset();
void processSerialUSBCommands();
void resetW5500();
IPAddress strToIP(String s);
void checkSerialState();
String cleanWeightLine(const String &line);
void broadcastDataToClients(const String &payload);
void processRS232();
void startServicesHibrido();
void checkBalanceConnection();
void attemptReconnect();
String getNetworkStatusJSON();
String scanWiFiNetworks();
String generateWebInterface();

// ========== 7. SISTEMA DE LOG COMPLETO ==========
void app_log(const char *format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    
    // Log na Serial
    Serial.print(buf);
    
    // Log para clientes de LOG apenas se houver conexão de rede
    if (networkConnected && dataClients.empty()) {
        for (int i = (int)logClients.size() - 1; i >= 0; i--) {
            if (logClients[i].client && logClients[i].client->connected()) {
                logClients[i].client->write((uint8_t*)buf, strlen(buf));
            } else {
                if (logClients[i].client) logClients[i].client->stop();
                logClients.erase(logClients.begin() + i);
            }
        }
    }
}

// ========== 8. SISTEMA LED ==========
void setLedStatus(LedStatus status) {
    currentLedStatus = status;
    lastLedChange = millis();
    ledBlinkState = LOW;
    
    switch(status) {
        case LED_OFF:
            digitalWrite(ledPin, LOW);
            break;
        case LED_ON:
            digitalWrite(ledPin, HIGH);
            break;
        case LED_BLINK_SLOW:  // 1Hz - Modo AP
        case LED_BLINK_FAST:  // 2Hz - Conectando
        case LED_BLINK_DOUBLE: // Pisca duplo - Erro
            digitalWrite(ledPin, LOW);
            break;
    }
}

void updateLed() {
    unsigned long currentMillis = millis();
    long interval = 0;
    
    switch(currentLedStatus) {
        case LED_BLINK_SLOW:
            interval = 1000; break;
        case LED_BLINK_FAST:
            interval = 500; break;
        case LED_BLINK_DOUBLE:
            interval = 300; break;
        default:
            return;
    }
    
    if (currentMillis - lastLedChange >= interval) {
        lastLedChange = currentMillis;
        
        if (currentLedStatus == LED_BLINK_DOUBLE) {
            // Pisca duplo especial
            static int doubleBlinkPhase = 0;
            switch(doubleBlinkPhase) {
                case 0: digitalWrite(ledPin, HIGH); break;
                case 1: digitalWrite(ledPin, LOW); break;
                case 2: digitalWrite(ledPin, HIGH); break;
                case 3: digitalWrite(ledPin, LOW); break;
            }
            doubleBlinkPhase = (doubleBlinkPhase + 1) % 4;
        } else {
            // Pisca normal
            ledBlinkState = !ledBlinkState;
            digitalWrite(ledPin, ledBlinkState);
        }
    }
}

// ========== 9. BROADCAST E LOGS ==========
void broadcastWeightAndClientStatusToLog(String cleanWeight) {
    if (logClients.empty() || !networkConnected) return;
    
    String dataClientList = "";
    for (const auto& client : dataClients) {
        if (!dataClientList.isEmpty()) dataClientList += ", ";
        dataClientList += client.ip;
    }
    if (dataClientList.isEmpty()) dataClientList = "Nenhum";

    String logClientList = "";
    for (const auto& client : logClients) {
        if (!logClientList.isEmpty()) logClientList += ", ";
        logClientList += client.ip;
    }

    String balanceStatus = balanceConnected ? "Conectada" : "DESLIGADA";
    
    String logPayload = "[PESO]|" + cleanWeight +
                       "|Balança: " + balanceStatus +
                       "|Clientes Dados (" + String(dataClients.size()) + "): " + dataClientList +
                       "|Clientes Log (" + String(logClients.size()) + "): " + logClientList +
                       "|Rede: " + String(currentMode == NET_ETHERNET ? "CABO" : 
                                       currentMode == NET_WIFI ? "WiFi" : "AP") +
                       "\n";

    for (int i = (int)logClients.size() - 1; i >= 0; i--) {
        if (logClients[i].client && logClients[i].client->connected()) {
            logClients[i].client->write((const uint8_t*)logPayload.c_str(), logPayload.length());
        } else {
            if (logClients[i].client) logClients[i].client->stop();
            logClients.erase(logClients.begin() + i);
        }
    }
}

// ========== 10. GESTÃO DE REDE REFATORADA ==========
IPAddress strToIP(String s) { 
    IPAddress ip; 
    if (s.length() > 0) {
        ip.fromString(s); 
    }
    return ip; 
}

void resetW5500() {
    pinMode(W5500_RST, OUTPUT);
    digitalWrite(W5500_RST, LOW); 
    delay(200);
    digitalWrite(W5500_RST, HIGH); 
    delay(200);
}

void stopNetwork() {
    app_log("[REDE] Parando serviços de rede...\n");
    
    if (currentMode == NET_WIFI || currentMode == NET_SOFTAP) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    
    currentMode = NET_NONE;
    networkConnected = false;
    
    // Limpa clientes
    for (auto &c : dataClients) if (c.client) c.client->stop();
    for (auto &c : logClients) if (c.client) c.client->stop();
    dataClients.clear(); 
    logClients.clear();
    
    setLedStatus(LED_BLINK_SLOW);
}

void startServicesHibrido() {
    app_log("[SERVICOS] Iniciando serviços...\n");
    
    if (currentMode == NET_ETHERNET) {
        ethWeb.begin();
        ethData.begin();
        ethLog.begin();
        app_log("[SERVICOS] Servidores Ethernet iniciados\n");
    } else {
        wifiWeb.begin();
        wifiData.begin(dataPort);
        wifiLog.begin(logPort);
        app_log("[SERVICOS] Servidores WiFi iniciados\n");
    }

    // mDNS
    MDNS.end();
    if (!MDNS.begin(deviceHostname.c_str())) {
        app_log("[SERVICOS] Erro ao iniciar mDNS!\n");
    } else {
        app_log("[SERVICOS] mDNS: http://%s.local\n", deviceHostname.c_str());
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("balanca-data", "tcp", dataPort);
    }

    // OTA apenas para WiFi
    if (currentMode == NET_WIFI || currentMode == NET_SOFTAP) {
        ArduinoOTA.setHostname(deviceHostname.c_str());
        if (adminPassword.length() > 0) {
            ArduinoOTA.setPassword(adminPassword.c_str());
        }
        ArduinoOTA.begin();
        app_log("[SERVICOS] OTA pronto\n");
    }
    
    app_log("[SERVICOS] Serviços online - Web:80, Dados:%d, Log:%d\n", dataPort, logPort);
    connectionJustSucceeded = true;
    connectionSuccessTime = millis();
    networkConnected = true;
}

bool startEthernet() {
    stopNetwork();
    app_log("[ETHERNET] Iniciando conexão por cabo...\n");
    setLedStatus(LED_BLINK_FAST);
    
    resetW5500();
    Ethernet.init(W5500_CS);

    int dhcpStatus = 0;
    if (lastUseStaticIP && lastStaticIP.length() > 0) {
        app_log("[ETHERNET] IP Estático: %s\n", lastStaticIP.c_str());
        IPAddress ip = strToIP(lastStaticIP);
        IPAddress gateway = strToIP(lastGateway);
        IPAddress subnet = strToIP(lastSubnet);
        IPAddress dns1 = strToIP(lastDns1);
        IPAddress dns2 = strToIP(lastDns2);
        
        if (dns1 != INADDR_NONE) {
            Ethernet.begin(mac, ip, dns1, gateway, subnet);
        } else {
            Ethernet.begin(mac, ip, gateway, subnet);
        }
        dhcpStatus = 1;
    } else {
        app_log("[ETHERNET] DHCP: Solicitando...\n");
        dhcpStatus = Ethernet.begin(mac, 10000); // Timeout de 10s
    }

    if (dhcpStatus == 0) {
        app_log("[ETHERNET] Falha - Sem cabo/DHCP\n");
        setLedStatus(LED_BLINK_DOUBLE);
        return false;
    }

    app_log("[ETHERNET] Conectado! IP: %s\n", Ethernet.localIP().toString().c_str());
    currentMode = NET_ETHERNET;
    startServicesHibrido();
    setLedStatus(LED_ON);
    return true;
}

bool startWiFi() {
    stopNetwork();
    
    if (lastSSID == "") { 
        app_log("[WIFI] Nenhuma rede configurada\n"); 
        return false; 
    }

    app_log("[WIFI] Conectando: %s\n", lastSSID.c_str());
    setLedStatus(LED_BLINK_FAST);
    
    WiFi.mode(WIFI_STA);

    // Configuração IP estático para WiFi
    if (lastUseStaticIP && lastStaticIP.length() > 0) {
        app_log("[WIFI] IP Estático: %s\n", lastStaticIP.c_str());
        IPAddress ip = strToIP(lastStaticIP);
        IPAddress gateway = strToIP(lastGateway);
        IPAddress subnet = strToIP(lastSubnet);
        IPAddress dns1 = strToIP(lastDns1);
        IPAddress dns2 = strToIP(lastDns2);
        
        if (dns1 != INADDR_NONE || dns2 != INADDR_NONE) {
            if (dns1 != INADDR_NONE && dns2 != INADDR_NONE) {
                WiFi.config(ip, gateway, subnet, dns1, dns2);
            } else if (dns1 != INADDR_NONE) {
                WiFi.config(ip, gateway, subnet, dns1);
            }
        } else {
            WiFi.config(ip, gateway, subnet);
        }
    }

    // Configuração Enterprise 802.1x
    if (lastEnterprise) {
        app_log("[WIFI] Modo Enterprise 802.1x - Usuário: %s\n", lastUser.c_str());
        esp_eap_client_set_identity((uint8_t*)lastUser.c_str(), lastUser.length());
        esp_eap_client_set_username((uint8_t*)lastUser.c_str(), lastUser.length());
        esp_eap_client_set_password((uint8_t*)lastPass.c_str(), lastPass.length());
        esp_wifi_sta_enterprise_enable();
        WiFi.begin(lastSSID.c_str());
    } else {
        WiFi.begin(lastSSID.c_str(), lastPass.c_str());
    }

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) { // 15s timeout
        delay(500); 
        app_log("."); 
        attempts++;
        if (attempts % 4 == 0) yield();
    }
    app_log("\n");

    if (WiFi.status() == WL_CONNECTED) {
        app_log("[WIFI] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
        currentMode = NET_WIFI;
        startServicesHibrido();
        setLedStatus(LED_ON);
        return true;
    } else {
        app_log("[WIFI] Falha na conexão\n");
        setLedStatus(LED_BLINK_DOUBLE);
        return false;
    }
}

void startSoftAP() {
    stopNetwork();
    app_log("[SOFTAP] Iniciando modo configuração...\n");
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(softApSSID.c_str(), lastSoftApPass.c_str());
    
    currentMode = NET_SOFTAP;
    startServicesHibrido();
    
    IPAddress IP = WiFi.softAPIP();
    app_log("[SOFTAP] Pronto! IP: %s - Senha: %s\n", IP.toString().c_str(), lastSoftApPass.c_str());
    
    softApStartTime = millis();
    setLedStatus(LED_BLINK_SLOW);
}

void stopSoftAP() {
    if (WiFi.getMode() == WIFI_AP_STA) {
        WiFi.softAPdisconnect(true);
        app_log("[SOFTAP] Desativado\n");
    }
}

void attemptReconnect() {
    if (millis() - lastReconnectAttempt < NETWORK_CHECK_INTERVAL) return;
    lastReconnectAttempt = millis();
    
    // Verifica se temos credenciais salvas
    if (lastSSID.length() > 0) {
        app_log("[RECONEXAO] Tentando reconectar à rede salva...\n");
        if (startWiFi()) {
            app_log("[RECONEXAO] Reconexão bem-sucedida!\n");
        } else {
            app_log("[RECONEXAO] Falha na reconexão\n");
        }
    }
}

void manageNetwork() {
    if (millis() - lastNetworkCheck < NETWORK_CHECK_INTERVAL) return;
    lastNetworkCheck = millis();

    // Verifica status do cabo Ethernet
    bool cableConnected = (Ethernet.linkStatus() == LinkON);
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);

    // Lógica de failover inteligente
    if (cableConnected && currentMode != NET_ETHERNET) {
        app_log("[FAILOVER] Cabo detectado - Mudando para Ethernet\n");
        if (!startEthernet()) {
            if (currentMode == NET_NONE && !startWiFi()) {
                startSoftAP();
            }
        }
    } 
    else if (!cableConnected && currentMode == NET_ETHERNET) {
        app_log("[FAILOVER] Cabo desconectado - Tentando WiFi\n");
        if (!startWiFi()) {
            app_log("[FAILOVER] WiFi falhou - Iniciando SoftAP\n");
            startSoftAP();
        }
    }
    // Se desconectou do WiFi, tenta reconectar ou vai para Ethernet
    else if (currentMode == NET_WIFI && !wifiConnected) {
        app_log("[FAILOVER] WiFi desconectado - Tentando reconectar\n");
        attemptReconnect();
    }
    // Se está em modo AP mas há redes salvas, tenta reconectar periodicamente
    else if (currentMode == NET_SOFTAP && lastSSID.length() > 0) {
        attemptReconnect();
    }

    // Timeout do SoftAP se ninguém conectar
    if (currentMode == NET_SOFTAP && WiFi.softAPgetStationNum() == 0) {
        if (millis() - softApStartTime > (SOFTAP_GRACE_PERIOD_MS * 5)) {
            app_log("[SOFTAP] Timeout - Tentando reconexão...\n");
            if (lastSSID.length() > 0 && !startWiFi()) {
                if (cableConnected && !startEthernet()) {
                    app_log("[SOFTAP] Nenhuma conexão disponível - Mantendo AP\n");
                    softApStartTime = millis(); // Reseta o timer
                }
            }
        }
    }
}

// ========== 11. PERSISTÊNCIA ==========
void saveCredentials() {
    prefs.begin("wifiCreds", false);
    prefs.putString("ssid", lastSSID);
    prefs.putString("pass", lastPass);
    prefs.putString("user", lastUser);
    prefs.putBool("enterprise", lastEnterprise);
    prefs.putBool("useStatic", lastUseStaticIP);
    if (lastUseStaticIP) {
        prefs.putString("staticIP", lastStaticIP);
        prefs.putString("subnet", lastSubnet);
        prefs.putString("gateway", lastGateway);
        prefs.putString("dns1", lastDns1);
        prefs.putString("dns2", lastDns2);
        prefs.putString("wins1", winsConfig.wins1);
        prefs.putString("wins2", winsConfig.wins2);
    }
    prefs.end();

    prefs.begin("sysConfig", false);
    prefs.putString("admPass", adminPassword);
    prefs.putString("hostname", deviceHostname);
    prefs.putUShort("dPort", dataPort);
    prefs.putUShort("lPort", logPort);
    prefs.putLong("baud", rs232Baud);
    prefs.putInt("dsf", dataSendFormat);
    prefs.putString("softap", lastSoftApPass);
    prefs.putString("softapssid", softApSSID);
    prefs.putBool("useLCD", lastUseLCD);
    prefs.putBool("sendAlways", sendAlways);
    prefs.end();
    
    app_log("[CONFIG] Credenciais salvas\n");
}

void loadCredentials() {
    prefs.begin("wifiCreds", true);
    lastSSID = prefs.getString("ssid", "");
    lastPass = prefs.getString("pass", "");
    lastUser = prefs.getString("user", "");
    lastEnterprise = prefs.getBool("enterprise", false);
    lastUseStaticIP = prefs.getBool("useStatic", false);
    lastStaticIP = prefs.getString("staticIP", "");
    lastSubnet = prefs.getString("subnet", "");
    lastGateway = prefs.getString("gateway", "");
    lastDns1 = prefs.getString("dns1", "");
    lastDns2 = prefs.getString("dns2", "");
    winsConfig.wins1 = prefs.getString("wins1", "");
    winsConfig.wins2 = prefs.getString("wins2", "");
    prefs.end();

    prefs.begin("sysConfig", true);
    adminPassword = prefs.getString("admPass", "");
    deviceHostname = prefs.getString("hostname", "esp32-balanca-Generic");
    dataPort = prefs.getUShort("dPort", 9000);
    logPort = prefs.getUShort("lPort", 5000);
    rs232Baud = prefs.getLong("baud", 9600);
    dataSendFormat = prefs.getInt("dsf", 1);
    lastSoftApPass = prefs.getString("softap", "12345678");
    softApSSID = prefs.getString("softapssid", "ESP32_Balanca_Config_Generic");
    lastUseLCD = prefs.getBool("useLCD", true);
    sendAlways = prefs.getBool("sendAlways", true);
    prefs.end();

    enableLineChangeDetector = lastUseLCD;
    app_log("[CONFIG] Configurações carregadas\n");
}

void clearCredentials() { 
    prefs.begin("wifiCreds", false); 
    prefs.clear(); 
    prefs.end(); 
    
    prefs.begin("sysConfig", false);
    prefs.clear();
    prefs.end();
    
    lastSSID = ""; 
    lastPass = ""; 
    lastUser = "";
    adminPassword = "";
    app_log("[CONFIG] Credenciais limpas\n");
}

// ========== 12. HARDWARE ==========
void checkSerialState() {
    if (currentMode != NET_NONE) {
        if (!isRs232Active) {
            SerialRS232.begin(rs232Baud, SERIAL_8N1, RS232_RX, RS232_TX);
            isRs232Active = true;
            app_log("[HARDWARE] Serial RS232 ativa - Baud: %ld\n", rs232Baud);
        }
    }
}

void checkBalanceConnection() {
    if (millis() - lastBalanceCheck < BALANCE_CHECK_INTERVAL) return;
    lastBalanceCheck = millis();
    
    bool previousState = balanceConnected;
    
    // Verifica se há comunicação com a balança
    if (isRs232Active && SerialRS232.available()) {
        balanceConnected = true;
        lastWebAlert = "";
    } else {
        // Tenta detectar se a balança está enviando dados
        static unsigned long lastDataTime = 0;
        static String lastCheckWeight = "";
        
        if (lastSentWeight != lastCheckWeight) {
            lastDataTime = millis();
            lastCheckWeight = lastSentWeight;
            balanceConnected = true;
            lastWebAlert = "";
        } else if (millis() - lastDataTime > 10000) { // 10 segundos sem dados
            balanceConnected = false;
            if (lastSentWeight != "0" && lastSentWeight != "" && lastSentWeight != "0.00") {
                lastWebAlert = "Balança desligada";
                app_log("[BALANCA] ALERTA: %s\n", lastWebAlert.c_str());
            }
        }
    }
    
    if (previousState != balanceConnected) {
        app_log("[BALANCA] Status alterado: %s\n", balanceConnected ? "Conectada" : "Desligada");
    }
}

// ========== 13. PROCESSAMENTO DE DADOS ==========
String cleanWeightLine(const String &line) {
    String filtered = "";
    for (char c : line) { 
        if (c >= 32 && c <= 126) filtered += c; 
    }
    return filtered;
}

void processRS232() {
    if (!isRs232Active) return;
    
    while (SerialRS232.available()) {
        char c = (char)SerialRS232.read();
        if (c == '\n' || c == '\r') {
            currentWeightBuffer.trim();
            if (currentWeightBuffer.length() > 0) {
                String cw = cleanWeightLine(currentWeightBuffer);
                bool changed = (cw != lastSentWeight);
                bool shouldSend = sendAlways || (!enableLineChangeDetector) || changed;

                if (shouldSend) {
                    String payload;
                    if (dataSendFormat == 0) {
                        payload = "{\"weight\":\"" + cw + "\",\"timestamp_ms\":" + String(millis()) + "}";
                    } else {
                        payload = cw;
                    }

                    broadcastDataToClients(payload);
                    lastSentWeight = cw;

                    if (!dataClients.empty() && networkConnected) {
                        broadcastWeightAndClientStatusToLog(cw);
                    } else {
                        app_log("[BALANCA] Peso: %s\n", cw.c_str());
                    }
                }
            }
            currentWeightBuffer = "";
        } else {
            currentWeightBuffer += c;
            if (currentWeightBuffer.length() > 256) {
                currentWeightBuffer = currentWeightBuffer.substring(currentWeightBuffer.length() - 128);
            }
        }
    }
}

void broadcastDataToClients(const String &payload) {
    size_t len = payload.length();
    unsigned long now = millis();
    
    for (int i = (int)dataClients.size() - 1; i >= 0; i--) {
        auto &c = dataClients[i];
        if (!c.client || !c.client->connected()) {
            if (c.client) c.client->stop();
            app_log("[DADOS] Cliente desconectado: %s\n", c.ip.c_str());
            dataClients.erase(dataClients.begin() + i);
            continue;
        }
        
        c.client->write((const uint8_t*)payload.c_str(), len);
        c.client->write('\n');
        c.lastSentMs = now;
    }
}

// ========== 14. HANDLERS DE CLIENTES ==========
void handleLogClients() {
    AuthClient lc;
    bool newClientAccepted = false;
    
    if (currentMode == NET_ETHERNET) {
        EthernetClient ec = ethLog.available();
        if (ec) {
            lc.client = std::make_shared<EthernetClient>(ec);
            lc.ip = ec.remoteIP().toString();
            newClientAccepted = true;
        }
    } else {
        WiFiClient wc = wifiLog.available();
        if (wc) {
            lc.client = std::make_shared<WiFiClient>(wc);
            lc.ip = wc.remoteIP().toString();
            newClientAccepted = true;
        }
    }

    if (newClientAccepted) {
        logClients.push_back(lc);
        app_log("[LOG] Novo monitor: %s\n", lc.ip.c_str());
        
        if (logClients.back().client && logClients.back().client->connected()) {
            logClients.back().client->println("[LOG] Conectado ao sistema de logs");
        }
    }

    // Limpeza de clientes desconectados
    for (int i = (int)logClients.size() - 1; i >= 0; i--) {
        if (!logClients[i].client || !logClients[i].client->connected()) {
            app_log("[LOG] Monitor desconectado: %s\n", logClients[i].ip.c_str());
            if (logClients[i].client) logClients[i].client->stop();
            logClients.erase(logClients.begin() + i);
        }
    }
}

void handleDataClient() {
    AuthClient ac;
    bool newClientAccepted = false;
    
    if (currentMode == NET_ETHERNET) {
        EthernetClient ec = ethData.available();
        if (ec) {
            ac.client = std::make_shared<EthernetClient>(ec);
            ac.ip = ec.remoteIP().toString();
            newClientAccepted = true;
        }
    } else {
        WiFiClient wc = wifiData.available();
        if (wc) {
            ac.client = std::make_shared<WiFiClient>(wc);
            ac.ip = wc.remoteIP().toString();
            newClientAccepted = true;
        }
    }

    if (newClientAccepted) {
        dataClients.push_back(ac);
        app_log("[DADOS] Novo cliente: %s\n", ac.ip.c_str());
        
        // Envia peso atual se disponível
        if (lastSentWeight.length() > 0) {
            String payload = (dataSendFormat == 0) ? 
                String("{\"weight\":\"") + lastSentWeight + "\",\"timestamp_ms\":" + String(millis()) + "}" : 
                lastSentWeight;
            ac.client->println(payload);
            dataClients.back().lastSentMs = millis();
        }
    }

    // Limpeza
    for (int i = (int)dataClients.size() - 1; i >= 0; i--) {
        if (!dataClients[i].client || !dataClients[i].client->connected()) {
            app_log("[DADOS] Cliente desconectado: %s\n", dataClients[i].ip.c_str());
            if (dataClients[i].client) dataClients[i].client->stop();
            dataClients.erase(dataClients.begin() + i);
        }
    }
}

// ========== 15. SERVIDOR WEB RESPONSIVO ==========
String urlDecode(String str) {
    String d = ""; char t[] = "0x00";
    for (unsigned int i = 0; i < str.length(); i++) {
        if (str[i] == '%') {
            if (i + 2 < str.length()) { t[2] = str[i+1]; t[3] = str[i+2]; d += (char)strtol(t, NULL, 16); i += 2; }
        } else if (str[i] == '+') d += ' '; else d += str[i];
    } return d;
}

String getParam(String req, String key) {
    String p = key + "="; int s = req.indexOf(p); if(s == -1) return "";
    s += p.length(); int e = req.indexOf('&', s);
    if(e == -1) e = req.indexOf(' ', s); if(e == -1) e = req.length();
    return urlDecode(req.substring(s, e));
}

String scanWiFiNetworks() {
    if (currentMode == NET_ETHERNET) {
        return "[]";
    }
    
    if (isRs232Active) { 
        SerialRS232.end(); 
        isRs232Active = false; 
    }
    
    app_log("[WIFI] Iniciando scan de redes...\n");
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
        json += "{";
        json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        json += "\"encryption\":" + String(WiFi.encryptionType(i));
        json += "}";
        if (i < n - 1) json += ",";
    }
    json += "]";
    
    // Reativa serial RS232
    SerialRS232.begin(rs232Baud, SERIAL_8N1, RS232_RX, RS232_TX);
    isRs232Active = true;
    
    app_log("[WIFI] Scan completo: %d redes encontradas\n", n);
    return json;
}

String getNetworkStatusJSON() {
    String json = "{";
    json += "\"mode\":";
    switch(currentMode) {
        case NET_ETHERNET: json += "\"ETHERNET\","; break;
        case NET_WIFI: json += "\"WIFI\","; break;
        case NET_SOFTAP: json += "\"SOFTAP\","; break;
        default: json += "\"NONE\","; break;
    }
    
    json += "\"ip\":\"";
    if (currentMode == NET_ETHERNET) {
        json += Ethernet.localIP().toString();
    } else {
        json += WiFi.localIP().toString();
    }
    json += "\",";
    
    json += "\"hostname\":\"" + deviceHostname + "\",";
    json += "\"balanceConnected\":" + String(balanceConnected ? "true" : "false") + ",";
    json += "\"webAlert\":\"" + lastWebAlert + "\",";
    json += "\"dataClients\":" + String(dataClients.size()) + ",";
    json += "\"logClients\":" + String(logClients.size()) + ",";
    json += "\"lcdEnabled\":" + String(lastUseLCD ? "true" : "false") + ",";
    json += "\"sendAlways\":" + String(sendAlways ? "true" : "false");
    json += "}";
    
    return json;
}

String generateWebInterface() {
    String html = "<!DOCTYPE html><html lang='pt-BR'><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Sistema de Pesagem - " + deviceHostname + "</title>";
    
    // CSS Responsivo e Bonito
    html += "<style>";
    html += "* { box-sizing: border-box; margin: 0; padding: 0; }";
    html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; padding: 20px; }";
    html += ".container { max-width: 1200px; margin: 0 auto; }";
    html += ".header { background: rgba(255,255,255,0.95); border-radius: 15px; padding: 20px; margin-bottom: 20px; box-shadow: 0 8px 32px rgba(0,0,0,0.1); }";
    html += ".logo-container { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 20px; }";
    html += ".logo-container img { height: 60px; width: auto; object-fit: contain; transition: transform 0.3s ease; }";
    html += ".logo-container img:hover { transform: scale(1.05); }";
    html += ".status-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; margin-bottom: 20px; }";
    html += ".status-card { background: rgba(255,255,255,0.95); border-radius: 12px; padding: 20px; box-shadow: 0 4px 20px rgba(0,0,0,0.1); border-left: 4px solid #007bff; }";
    html += ".status-card.alert { border-left-color: #dc3545; background: rgba(220,53,69,0.05); }";
    html += ".status-card.success { border-left-color: #28a745; }";
    html += ".card-title { font-size: 16px; font-weight: 600; color: #495057; margin-bottom: 10px; }";
    html += ".card-value { font-size: 18px; font-weight: 700; color: #212529; }";
    html += ".card-value.alert { color: #dc3545; }";
    html += ".section { background: rgba(255,255,255,0.95); border-radius: 15px; padding: 25px; margin-bottom: 20px; box-shadow: 0 8px 32px rgba(0,0,0,0.1); }";
    html += ".section-title { font-size: 22px; font-weight: 700; color: #343a40; margin-bottom: 20px; border-bottom: 2px solid #e9ecef; padding-bottom: 10px; }";
    html += ".form-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 15px; }";
    html += ".form-group { margin-bottom: 15px; }";
    html += "label { display: block; font-weight: 600; color: #495057; margin-bottom: 5px; }";
    html += "input, select, textarea { width: 100%; padding: 12px; border: 2px solid #e9ecef; border-radius: 8px; font-size: 16px; transition: border-color 0.3s ease; }";
    html += "input:focus, select:focus, textarea:focus { outline: none; border-color: #007bff; }";
    html += ".checkbox-group { display: flex; align-items: center; gap: 10px; margin: 15px 0; }";
    html += ".checkbox-group input[type='checkbox'] { width: 20px; height: 20px; }";
    html += ".checkbox-group label { margin: 0; font-weight: 500; }";
    html += ".btn { padding: 12px 24px; border: none; border-radius: 8px; font-size: 16px; font-weight: 600; cursor: pointer; transition: all 0.3s ease; text-decoration: none; display: inline-block; text-align: center; }";
    html += ".btn-primary { background: linear-gradient(135deg, #007bff, #0056b3); color: white; }";
    html += ".btn-success { background: linear-gradient(135deg, #28a745, #1e7e34); color: white; }";
    html += ".btn-danger { background: linear-gradient(135deg, #dc3545, #c82333); color: white; }";
    html += ".btn-secondary { background: linear-gradient(135deg, #6c757d, #545b62); color: white; }";
    html += ".btn:hover { transform: translateY(-2px); box-shadow: 0 4px 15px rgba(0,0,0,0.2); }";
    html += ".networks-list { max-height: 300px; overflow-y: auto; border: 1px solid #e9ecef; border-radius: 8px; padding: 10px; }";
    html += ".network-item { padding: 10px; border-bottom: 1px solid #e9ecef; cursor: pointer; transition: background-color 0.2s ease; }";
    html += ".network-item:hover { background-color: #f8f9fa; }";
    html += ".network-item:last-child { border-bottom: none; }";
    html += ".network-ssid { font-weight: 600; color: #495057; }";
    html += ".network-rssi { font-size: 14px; color: #6c757d; }";
    html += ".hidden { display: none; }";
    html += ".alert-banner { background: linear-gradient(135deg, #dc3545, #c82333); color: white; padding: 15px; border-radius: 8px; margin-bottom: 20px; text-align: center; font-weight: 600; }";
    html += "@media (max-width: 768px) { .logo-container { flex-direction: column; text-align: center; } .status-grid { grid-template-columns: 1fr; } }";
    html += "</style>";
    html += "</head><body>";
    
    html += "<div class='container'>";
    
    // Header com Logos Aumentados
    html += "<div class='header'>";
    html += "<div class='logo-container'>";
    html += "<img src='https://www.solenis.com/globalassets/images/miscellaneous-images/solenis-rgb-840x450.png' alt='Solenis' style='height: 65px;'>";
    html += "<h1 style='color: #343a40; text-align: center; flex-grow: 1;'>Sistema de Pesagem<br><small style='font-size: 16px; color: #6c757d;'>" + deviceHostname + "</small></h1>";
    html += "<img src='https://www.nch.com.br/nchbrasil/img/nch-logo.png' alt='NCH' style='height: 65px;'>";
    html += "</div>";
    html += "</div>";
    
    // Alertas
    if (lastWebAlert.length() > 0) {
        html += "<div class='alert-banner'>⚠️ " + lastWebAlert + "</div>";
    }
    
    // Status em Grid
    html += "<div class='status-grid'>";
    
    String ipStr = (currentMode == NET_ETHERNET) ? Ethernet.localIP().toString() : WiFi.localIP().toString();
    String modeStr = (currentMode == NET_ETHERNET) ? "Cabo (Ethernet)" : (currentMode == NET_WIFI ? "Wi-Fi" : "Modo AP (Config)");
    String balanceStatus = balanceConnected ? "Conectada" : "Desligada";
    
    html += "<div class='status-card " + String(balanceConnected ? "success" : "alert") + "'>";
    html += "<div class='card-title'>Status da Balança</div>";
    html += "<div class='card-value " + String(balanceConnected ? "" : "alert") + "'>" + balanceStatus + "</div>";
    html += "</div>";
    
    html += "<div class='status-card'>";
    html += "<div class='card-title'>Conexão de Rede</div>";
    html += "<div class='card-value'>" + modeStr + "</div>";
    html += "</div>";
    
    html += "<div class='status-card'>";
    html += "<div class='card-title'>Endereço IP</div>";
    html += "<div class='card-value'>" + ipStr + "</div>";
    html += "</div>";
    
    html += "<div class='status-card'>";
    html += "<div class='card-title'>Clientes Conectados</div>";
    html += "<div class='card-value'>Dados: " + String(dataClients.size()) + " | Log: " + String(logClients.size()) + "</div>";
    html += "</div>";
    
    html += "</div>"; // Fim do status-grid
    
    // Seção de Configuração de Rede
    html += "<div class='section'>";
    html += "<h2 class='section-title'>Configuração de Rede</h2>";
    
    html += "<button class='btn btn-secondary' onclick='toggleSection(\"networkConfig\")'>Configurar Rede WiFi/Ethernet</button>";
    html += "<div id='networkConfig' class='hidden' style='margin-top: 20px;'>";
    
    html += "<div class='form-grid'>";
    
    // Scan de Redes
    html += "<div class='form-group'>";
    html += "<label>Redes Disponíveis:</label>";
    html += "<div class='networks-list' id='networksList'>Carregando redes...</div>";
    html += "<button type='button' class='btn btn-secondary' onclick='scanNetworks()' style='margin-top: 10px;'>Atualizar Lista</button>";
    html += "</div>";
    
    // Formulário de Conexão
    html += "<div class='form-group'>";
    html += "<form id='wifiForm' action='/connect' method='get'>";
    html += "<label>SSID (Nome da Rede)</label>";
    html += "<input type='text' name='ssid' id='ssid' value='" + lastSSID + "' placeholder='Nome da rede WiFi'>";
    
    html += "<label>Senha</label>";
    html += "<input type='password' name='pass' id='pass' value='" + lastPass + "' placeholder='Senha da rede'>";
    
    html += "<div class='checkbox-group'>";
    html += "<input type='checkbox' name='ent' id='ent' " + String(lastEnterprise ? "checked" : "") + " onchange='toggleEnterprise()'>";
    html += "<label for='ent'>Rede Enterprise 802.1x</label>";
    html += "</div>";
    
    html += "<div id='enterpriseFields' class='" + String(lastEnterprise ? "" : "hidden") + "'>";
    html += "<label>Usuário</label>";
    html += "<input type='text' name='user' id='user' value='" + lastUser + "' placeholder='Usuário para autenticação'>";
    html += "</div>";
    
    html += "<div class='checkbox-group'>";
    html += "<input type='checkbox' name='st' id='st' " + String(lastUseStaticIP ? "checked" : "") + " onchange='toggleStaticIP()'>";
    html += "<label for='st'>Usar IP Estático</label>";
    html += "</div>";
    
    html += "<div id='staticIPFields' class='" + String(lastUseStaticIP ? "" : "hidden") + "'>";
    html += "<label>Endereço IP</label>";
    html += "<input type='text' name='ip' value='" + lastStaticIP + "' placeholder='Ex: 192.168.1.100'>";
    
    html += "<label>Máscara de Sub-rede</label>";
    html += "<input type='text' name='msk' value='" + lastSubnet + "' placeholder='Ex: 255.255.255.0'>";
    
    html += "<label>Gateway</label>";
    html += "<input type='text' name='gw' value='" + lastGateway + "' placeholder='Ex: 192.168.1.1'>";
    
    html += "<label>DNS Primário</label>";
    html += "<input type='text' name='d1' value='" + lastDns1 + "' placeholder='Ex: 8.8.8.8'>";
    
    html += "<label>DNS Secundário</label>";
    html += "<input type='text' name='d2' value='" + lastDns2 + "' placeholder='Ex: 8.8.4.4'>";
    
    html += "<label>Servidor WINS 1</label>";
    html += "<input type='text' name='w1' value='" + winsConfig.wins1 + "' placeholder='Opcional'>";
    
    html += "<label>Servidor WINS 2</label>";
    html += "<input type='text' name='w2' value='" + winsConfig.wins2 + "' placeholder='Opcional'>";
    html += "</div>";
    
    html += "<button type='submit' class='btn btn-success'>Salvar e Conectar</button>";
    html += "</form>";
    html += "</div>";
    
    html += "</div>"; // Fim do form-grid
    html += "</div>"; // Fim da networkConfig
    html += "</div>"; // Fim da section
    
    // Configurações Avançadas
    html += "<div class='section'>";
    html += "<h2 class='section-title'>Configurações Avançadas</h2>";
    
    html += "<form id='advConfigForm' action='/save' method='post'>";
    html += "<div class='form-grid'>";
    
    html += "<div class='form-group'>";
    html += "<label>Hostname do Dispositivo</label>";
    html += "<input type='text' name='hname' value='" + deviceHostname + "' placeholder='Nome único na rede'>";
    html += "</div>";
    
    html += "<div class='form-group'>";
    html += "<label>SSID do Modo AP</label>";
    html += "<input type='text' name='sapssid' value='" + softApSSID + "' placeholder='Nome da rede no modo AP'>";
    html += "</div>";
    
    html += "<div class='form-group'>";
    html += "<label>Senha do Modo AP</label>";
    html += "<input type='password' name='sap' value='" + lastSoftApPass + "' placeholder='Mínimo 8 caracteres'>";
    html += "</div>";
    
    html += "<div class='form-group'>";
    html += "<label>Porta de Dados TCP</label>";
    html += "<input type='number' name='dp' value='" + String(dataPort) + "' min='1' max='65535'>";
    html += "</div>";
    
    html += "<div class='form-group'>";
    html += "<label>Porta de Log TCP</label>";
    html += "<input type='number' name='lp' value='" + String(logPort) + "' min='1' max='65535'>";
    html += "</div>";
    
    html += "<div class='form-group'>";
    html += "<label>Baud Rate (RS232)</label>";
    html += "<input type='number' name='br' value='" + String(rs232Baud) + "' min='9600' max='115200'>";
    html += "</div>";
    
    html += "</div>"; // Fim do form-grid
    
    html += "<div class='checkbox-group'>";
    html += "<input type='checkbox' name='lcd' id='lcd' " + String(lastUseLCD ? "checked" : "") + ">";
    html += "<label for='lcd'>Line Change Detector (LCD)</label>";
    html += "</div>";
    
    html += "<div class='checkbox-group'>";
    html += "<input type='checkbox' name='sendAlways' id='sendAlways' " + String(sendAlways ? "checked" : "") + ">";
    html += "<label for='sendAlways'>Enviar Sempre (Send Always)</label>";
    html += "</div>";
    
    html += "<label>Formato de Envio de Dados</label>";
    html += "<select name='dsf'>";
    html += "<option value='0' " + String(dataSendFormat==0?"selected":"") + ">JSON</option>";
    html += "<option value='1' " + String(dataSendFormat==1?"selected":"") + ">Texto Puro</option>";
    html += "</select>";
    
    html += "<button type='submit' class='btn btn-success' style='margin-top: 20px;'>Salvar Configurações Avançadas</button>";
    html += "</form>";
    html += "</div>";
    
    // Seção de Manutenção
    html += "<div class='section'>";
    html += "<h2 class='section-title'>Manutenção</h2>";
    
    html += "<div style='display: flex; gap: 10px; flex-wrap: wrap;'>";
    html += "<button class='btn btn-danger' onclick='if(confirm(\"Isso irá resetar todas as configurações para os padrões de fábrica. Continuar?\")) location.href=\"/factory_reset\"'>Factory Reset</button>";
    html += "<button class='btn btn-secondary' onclick='if(confirm(\"Esquecer rede WiFi atual?\")) location.href=\"/disconnect\"'>Esquecer WiFi</button>";
    html += "<button class='btn btn-secondary' onclick='location.href=\"/restart\"'>Reiniciar Sistema</button>";
    html += "<button class='btn btn-primary' onclick='getSystemStatus()'>Atualizar Status</button>";
    html += "</div>";
    html += "</div>";
    
    // JavaScript
    html += "<script>";
    html += "function toggleSection(sectionId) {";
    html += "  const section = document.getElementById(sectionId);";
    html += "  section.classList.toggle('hidden');";
    html += "}";
    html += "function toggleEnterprise() {";
    html += "  const entCheckbox = document.getElementById('ent');";
    html += "  const entFields = document.getElementById('enterpriseFields');";
    html += "  entFields.classList.toggle('hidden', !entCheckbox.checked);";
    html += "}";
    html += "function toggleStaticIP() {";
    html += "  const stCheckbox = document.getElementById('st');";
    html += "  const ipFields = document.getElementById('staticIPFields');";
    html += "  ipFields.classList.toggle('hidden', !stCheckbox.checked);";
    html += "}";
    html += "function scanNetworks() {";
    html += "  const networksList = document.getElementById('networksList');";
    html += "  networksList.innerHTML = 'Escaneando redes...';";
    html += "  fetch('/scan')";
    html += "    .then(response => response.json())";
    html += "    .then(networks => {";
    html += "      if (networks.length === 0) {";
    html += "        networksList.innerHTML = 'Nenhuma rede encontrada';";
    html += "        return;";
    html += "      }";
    html += "      networksList.innerHTML = '';";
    html += "      networks.forEach(network => {";
    html += "        const item = document.createElement('div');";
    html += "        item.className = 'network-item';";
    html += "        item.innerHTML = `<div class='network-ssid'>${network.ssid}</div><div class='network-rssi'>Sinal: ${network.rssi} dBm</div>`;";
    html += "        item.onclick = () => {";
    html += "          document.getElementById('ssid').value = network.ssid;";
    html += "          document.getElementById('pass').focus();";
    html += "        };";
    html += "        networksList.appendChild(item);";
    html += "      });";
    html += "    })";
    html += "    .catch(error => {";
    html += "      networksList.innerHTML = 'Erro ao escanear redes';";
    html += "      console.error(error);";
    html += "    });";
    html += "}";
    html += "function getSystemStatus() {";
    html += "  fetch('/status')";
    html += "    .then(response => response.json())";
    html += "    .then(status => {";
    html += "      console.log('Status atualizado:', status);";
    html += "      alert('Status atualizado! Verifique o console para detalhes.');";
    html += "    });";
    html += "}";
    html += "// Inicialização";
    html += "document.addEventListener('DOMContentLoaded', function() {";
    html += "  scanNetworks();";
    html += "  // Atualizar status a cada 30 segundos";
    html += "  setInterval(() => {";
    html += "    fetch('/status').then(r => r.json()).then(console.log);";
    html += "  }, 30000);";
    html += "});";
    html += "</script>";
    
    html += "</div></body></html>";
    
    return html;
}

template <typename T>
void handleWebClientGeneric(T client) {
    String req = "";
    unsigned long timeout = millis();
    
    // Leitura da requisição com timeout
    while (client.connected() && millis() - timeout < 5000) {
        if (client.available()) {
            char c = client.read();
            req += c;
            if (req.endsWith("\r\n\r\n")) break;
        }
    }
    
    // Endpoints da API
    if (req.indexOf("GET /scan") != -1) {
        client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n" + scanWiFiNetworks());
        return;
    }
    
    if (req.indexOf("GET /status") != -1) {
        client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n" + getNetworkStatusJSON());
        return;
    }
    
    // POST Save Config
    if (req.indexOf("POST /save") != -1) {
        int contentLength = 0;
        int clIndex = req.indexOf("Content-Length:");
        if (clIndex != -1) {
            int clEnd = req.indexOf("\r\n", clIndex);
            contentLength = req.substring(clIndex + 15, clEnd).toInt();
        }

        String body = "";
        unsigned long startTime = millis();
        while (body.length() < contentLength && (millis() - startTime) < 3000) {
            if (client.available()) {
                body += (char)client.read();
            }
        }

        app_log("[WEB] POST /save recebido. Body size: %d\n", body.length());

        // Validação dos campos
        String val;
        
        // Hostname
        val = getParam(body, "hname");
        if(val.length() > 0) deviceHostname = val;

        // Portas
        val = getParam(body, "dp");
        if(val.length() > 0) {
            int port = val.toInt();
            if(port >= 1 && port <= 65535) dataPort = port;
        }
        
        val = getParam(body, "lp");
        if(val.length() > 0) {
            int port = val.toInt();
            if(port >= 1 && port <= 65535) logPort = port;
        }

        // RS232
        val = getParam(body, "br");
        if(val.length() > 0) {
            long baud = val.toInt();
            if(baud >= 9600 && baud <= 115200) rs232Baud = baud;
        }

        // Formato
        val = getParam(body, "dsf");
        if(val.length() > 0) {
            int format = val.toInt();
            if(format >= 0 && format <= 1) dataSendFormat = format;
        }

        // SoftAP
        val = getParam(body, "sap");
        if(val.length() >= 8) lastSoftApPass = val;
        
        val = getParam(body, "sapssid");
        if(val.length() > 0) softApSSID = val;
        
        // Configurações do sistema
        lastUseLCD = (getParam(body, "lcd") == "on");
        sendAlways = (getParam(body, "sendAlways") == "on");

        enableLineChangeDetector = lastUseLCD;

        saveCredentials();

        // Resposta de sucesso
        String html = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
        html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<style>body{font-family:Arial,sans-serif;text-align:center;padding:50px;background:#f0f2f5;}";
        html += ".container{background:white;padding:30px;border-radius:12px;box-shadow:0 6px 20px rgba(0,0,0,0.1);max-width:500px;margin:0 auto;}";
        html += "h2{color:#28a745;}</style></head><body><div class='container'>";
        html += "<h2>Configurações Salvas!</h2><p>O sistema será reiniciado...</p></div>";
        html += "<script>setTimeout(function(){window.location.href='/';}, 3000);</script></body></html>";
        
        client.print(html);
        client.flush();
        client.stop();
        
        app_log("[CONFIG] Reiniciando...\n");
        delay(1000);
        ESP.restart();
        return;
    }

    // GET Connect (Rede)
    if (req.indexOf("GET /connect") != -1) {
        lastSSID = getParam(req, "ssid"); 
        lastPass = getParam(req, "pass");
        lastUser = getParam(req, "user"); 
        lastEnterprise = (req.indexOf("ent=on") != -1);
        lastUseStaticIP = (req.indexOf("st=on") != -1);
        
        if(lastUseStaticIP){
            lastStaticIP = getParam(req, "ip"); 
            lastSubnet = getParam(req, "msk");
            lastGateway = getParam(req, "gw"); 
            lastDns1 = getParam(req, "d1"); 
            lastDns2 = getParam(req, "d2");
            winsConfig.wins1 = getParam(req, "w1");
            winsConfig.wins2 = getParam(req, "w2");
        } else {
            lastStaticIP=""; lastSubnet=""; lastGateway=""; lastDns1=""; lastDns2="";
            winsConfig.wins1 = ""; winsConfig.wins2 = "";
        }
        
        saveCredentials();

        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nConfigurações salvas. Reconectando...");
        delay(1000);
        ESP.restart();
        return;
    }

    // GET Disconnect / Factory Reset / Restart
    if (req.indexOf("GET /disconnect") != -1) {
        clearCredentials(); 
        client.println("HTTP/1.1 200 OK\r\n\r\nWiFi esquecido. Reiniciando...");
        delay(1000);
        ESP.restart();
        return;
    }
    
    if (req.indexOf("GET /factory_reset") != -1) {
        performFactoryReset();
        client.println("HTTP/1.1 200 OK\r\n\r\nFactory reset executado. Reiniciando...");
        return;
    }
    
    if (req.indexOf("GET /restart") != -1) {
        client.println("HTTP/1.1 200 OK\r\n\r\nReiniciando...");
        delay(1000);
        ESP.restart();
        return;
    }






// ---------------------------------------------------------
    // AQUI É A MUDANÇA CRÍTICA PARA A PÁGINA PRINCIPAL
    // ---------------------------------------------------------

    // 1. Gere o HTML completo na memória
    String htmlContent = generateWebInterface();

    // 2. Envie PRIMEIRO apenas os cabeçalhos HTTP
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.print("Content-Length: ");
    client.println(htmlContent.length());
    client.println(); // Linha em branco obrigatória entre header e body

    // 3. Envie o corpo HTML em pedaços (Chunks) seguros para o W5500
    // O buffer do W5500 é geralmente 2048 bytes. Usaremos 1024 para segurança.
    const int chunkSize = 1024; 
    int totalLen = htmlContent.length();
    int currentPos = 0;

    while (currentPos < totalLen) {
        int bytesRemaining = totalLen - currentPos;
        int bytesToSend = (bytesRemaining < chunkSize) ? bytesRemaining : chunkSize;
        
        // Envia o pedaço atual
        client.print(htmlContent.substring(currentPos, currentPos + bytesToSend));
        
        // Avança a posição
        currentPos += bytesToSend;
        
        // Pequeno delay para garantir que o buffer do W5500 esvazie
        // Isso é crucial para estabilidade em cabos longos ou redes congestionadas
        delay(2); 
    }

    // Finaliza conexão
    client.flush();
    client.stop();




    // Página principal
    client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n" + generateWebInterface());
    client.flush();
    client.stop();
}

// ========== 16. MANUTENÇÃO E COMANDOS SERIAL ==========
void performFactoryReset() {
    app_log("[MANUTENCAO] Factory Reset iniciado...\n");
    
    prefs.begin("wifiCreds", false);
    prefs.clear();
    prefs.end();
    
    prefs.begin("sysConfig", false);
    prefs.clear();
    prefs.end();
    
    app_log("[MANUTENCAO] Todas configurações limpas\n");
    app_log("[MANUTENCAO] Reiniciando...\n");
    
    delay(1000);
    ESP.restart();
}

void processSerialUSBCommands() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            serialUsbBuffer.trim();
            if (serialUsbBuffer.length() > 0) {
                app_log("[SERIAL] Comando recebido: %s\n", serialUsbBuffer.c_str());
                
                String cmd = serialUsbBuffer;
                cmd.toUpperCase();

                if (cmd == "FACTORY_RESET") {
                    app_log("[SERIAL] Executando Factory Reset...\n");
                    performFactoryReset();
                } 
                else if (cmd == "DISCONNECT" || cmd == "FORGET_WIFI") {
                    app_log("[SERIAL] Limpando credenciais WiFi...\n");
                    clearCredentials();
                    ESP.restart();
                }
                else if (cmd == "STATUS") {
                    app_log("[SERIAL] === STATUS DO SISTEMA ===\n");
                    app_log("IP: %s | Modo: %d\n", (currentMode == NET_ETHERNET ? Ethernet.localIP().toString().c_str() : WiFi.localIP().toString().c_str()), currentMode);
                    app_log("Hostname: %s\n", deviceHostname.c_str());
                    app_log("Clientes Dados: %d | Log: %d\n", dataClients.size(), logClients.size());
                    app_log("Balança: %s | Alerta: %s\n", balanceConnected ? "Conectada" : "DESLIGADA", lastWebAlert.c_str());
                    app_log("LCD (Detector): %s | Enviar Sempre: %s\n", lastUseLCD ? "ON" : "OFF", sendAlways ? "ON" : "OFF");
                }
                else if (cmd == "RESTART") {
                    ESP.restart();
                }
                else if (cmd == "SCAN") {
                     app_log("[SERIAL] Iniciando Scan de Redes...\n");
                     int n = WiFi.scanNetworks();
                     for (int i=0; i<n; ++i) {
                        app_log("  %s (%d dBm) - Encryption: %d\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i));
                     }
                     app_log("[SERIAL] Scan finalizado.\n");
                }
                else if (cmd.startsWith("SET_HOSTNAME ")) {
                    String newH = serialUsbBuffer.substring(13);
                    newH.trim();
                    if(newH.length() > 0) {
                        deviceHostname = newH;
                        saveCredentials();
                        app_log("[SERIAL] Hostname alterado para: %s (Reiniciando...)\n", deviceHostname.c_str());
                        delay(1000);
                        ESP.restart();
                    }
                }
                else if (cmd.startsWith("SET_AP_SSID ")) {
                    String newSSID = serialUsbBuffer.substring(12);
                    newSSID.trim();
                    if(newSSID.length() > 0) {
                        softApSSID = newSSID;
                        saveCredentials();
                        app_log("[SERIAL] SSID do AP alterado para: %s\n", softApSSID.c_str());
                    }
                }
                else if (cmd == "HELP") {
                    app_log("[SERIAL] === AJUDA ===\n");
                    app_log("STATUS, SCAN, RESTART, FACTORY_RESET, DISCONNECT\n");
                    app_log("SET_HOSTNAME <nome>, SET_AP_SSID <nome>\n");
                }
                else {
                    app_log("[SERIAL] Comando desconhecido. Digite HELP.\n");
                }
            }
            serialUsbBuffer = "";
        } else {
            serialUsbBuffer += c;
            if (serialUsbBuffer.length() > 128) {
                serialUsbBuffer = serialUsbBuffer.substring(serialUsbBuffer.length() - 128);
            }
        }
    }
}

// ========== 17. SETUP E LOOP PRINCIPAIS ==========
void setup() {
    Serial.begin(115200);
    pinMode(ledPin, OUTPUT);
    
    app_log("\n\n[SYSTEM] === ESP32 BALANCA INICIANDO ===\n");
    app_log("[SYSTEM] Versão: Final com Todas Funcionalidades\n");
    
    // Inicializa hardware
    SPI.begin(W5500_SCLK, W5500_MISO, W5500_MOSI, W5500_CS);
    SerialRS232.begin(rs232Baud, SERIAL_8N1, RS232_RX, RS232_TX);
    isRs232Active = true;

    // Carrega configurações
    loadCredentials();

    // Inicia rede com prioridade
    app_log("[SYSTEM] Iniciando rede...\n");
    if (!startEthernet()) {
        if (!startWiFi()) {
            startSoftAP();
        }
    }

    app_log("[SYSTEM] Setup completo - Sistema pronto\n");
    app_log("[SYSTEM] Digite HELP na Serial para ver comandos\n");
}

void loop() {
    // Gestão de Rede
    manageNetwork();

    // Atualização LED
    updateLed();

    // OTA Updates (apenas WiFi)
    if (currentMode == NET_WIFI || currentMode == NET_SOFTAP || currentMode == NET_ETHERNET) {
        ArduinoOTA.handle();
    }

    // Desativa SoftAP após conexão estabelecida
    if (currentMode != NET_SOFTAP && connectionJustSucceeded && 
        (millis() - connectionSuccessTime > SOFTAP_GRACE_PERIOD_MS)) {
        stopSoftAP(); 
        connectionJustSucceeded = false;
    }

    // LOG DE STATUS A CADA 1 MINUTO (apenas se houver conexão de rede)
    if (networkConnected && millis() - lastStatusLogTime > STATUS_LOG_INTERVAL) {
        lastStatusLogTime = millis();
        String statusMsg = "\n[STATUS 1min] -----------------------------\n";
        statusMsg += "Conexao: " + String(currentMode == NET_ETHERNET ? "CABO (RJ45)" : (currentMode == NET_WIFI ? "WIFI" : "AP Mode")) + "\n";
        statusMsg += "IP: " + (currentMode == NET_ETHERNET ? Ethernet.localIP().toString() : WiFi.localIP().toString()) + "\n";
        statusMsg += "Hostname: " + deviceHostname + "\n";
        statusMsg += "Clientes LOG: " + String(logClients.size()) + " | Clientes DADOS: " + String(dataClients.size()) + "\n";
        statusMsg += "Balança: " + String(balanceConnected ? "Conectada" : "DESLIGADA") + " | Alerta: " + lastWebAlert + "\n";
        statusMsg += "Config: LCD=" + String(lastUseLCD?"ON":"OFF") + " | EnviarSempre=" + String(sendAlways?"ON":"OFF") + "\n";
        statusMsg += "Ultimo Peso: " + lastSentWeight + "\n";
        statusMsg += "---------------------------------------------\n";
        app_log(statusMsg.c_str());
    }

    // Verificação da Balança
    checkBalanceConnection();

    // Processamento Serial e RS232
    processSerialUSBCommands();
    processRS232();

    // Serviços Web
    if (currentMode == NET_ETHERNET) {
        EthernetClient cl = ethWeb.available();
        if (cl) handleWebClientGeneric(cl);
    } else if (currentMode == NET_WIFI || currentMode == NET_SOFTAP) {
        WiFiClient cl = wifiWeb.available();
        if (cl) handleWebClientGeneric(cl);
    }

    // Clientes de Dados e Log
    handleLogClients();
    handleDataClient();

    // Não bloquear o loop
    delay(1);
}