#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <SPIFFS.h>
#include <LittleFS.h>

// --- Configuration WiFi ---
const char* ssid = "VOTRE_SSID";
const char* password = "VOTRE_MOT_DE_PASSE";

// --- Initialisation du serveur web ---
AsyncWebServer server(80);

// --- Configuration des GPIO ---
// KVM
const int kvm1_gpio[] = {8}; // KVM 1 (1 GPIO)
const int kvm2_gpio[] = {0, 1, 6, 7}; // KVM 2 (4 GPIO)
const int kvm3_gpio[] = {24, 23, 15, 27}; // KVM 3 (4 GPIO)

// Émetteurs IR
const int ir1_gpio = 9;  // IR 1
const int ir2_gpio = 10; // IR 2
const int ir3_gpio = 26; // IR 3

// Initialiser les émetteurs IR
IRsend irsend1(ir1_gpio);
IRsend irsend2(ir2_gpio);
IRsend irsend3(ir3_gpio);

// --- Variables globales ---
DynamicJsonDocument config(1024); // Pour stocker la configuration
DynamicJsonDocument irCodes(4096); // Pour stocker les codes IR

// --- Fonction pour lire un fichier JSON ---
DynamicJsonDocument readJsonFile(const char* path) {
  DynamicJsonDocument doc(4096);
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Erreur : impossible d'ouvrir le fichier");
    return doc;
  }
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.println("Erreur de désérialisation JSON");
  }
  return doc;
}

// --- Fonction pour écrire un fichier JSON ---
void writeJsonFile(const char* path, DynamicJsonDocument doc) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Erreur : impossible d'ouvrir le fichier en écriture");
    return;
  }
  serializeJson(doc, file);
  file.close();
}

// --- Fonction pour convertir un JsonArray en tableau uint16_t ---
uint16_t* jsonArrayToUint16Array(JsonArray jsonArray, size_t& size) {
  size = jsonArray.size();
  uint16_t* array = new uint16_t[size];
  for (size_t i = 0; i < size; i++) {
    array[i] = jsonArray[i];
  }
  return array;
}

// --- Fonction pour envoyer un code IR ---
void sendIRCode(int irPin, const char* device, const char* codeName) {
  if (irCodes.containsKey(device) && irCodes[device].containsKey(codeName)) {
    JsonArray code = irCodes[device][codeName];
    size_t codeSize;
    uint16_t* codeArray = jsonArrayToUint16Array(code, codeSize);

    if (irPin == ir1_gpio) {
      irsend1.sendRaw(codeArray, codeSize, 38); // 38 kHz
    } else if (irPin == ir2_gpio) {
      irsend2.sendRaw(codeArray, codeSize, 38);
    } else if (irPin == ir3_gpio) {
      irsend3.sendRaw(codeArray, codeSize, 38);
    }

    delete[] codeArray; // Libérer la mémoire allouée
    Serial.printf("Code IR envoyé : %s -> %s\n", device, codeName);
  } else {
    Serial.printf("Code IR introuvable : %s -> %s\n", device, codeName);
  }
}

// --- Fonction pour activer/désactiver un GPIO KVM ---
void setKVMGPIO(const char* kvmName, int gpioIndex, int state) {
  if (strcmp(kvmName, "kvm1") == 0) {
    digitalWrite(kvm1_gpio[gpioIndex], state);
  } else if (strcmp(kvmName, "kvm2") == 0) {
    digitalWrite(kvm2_gpio[gpioIndex], state);
  } else if (strcmp(kvmName, "kvm3") == 0) {
    digitalWrite(kvm3_gpio[gpioIndex], state);
  }
  Serial.printf("GPIO %s[%d] = %d\n", kvmName, gpioIndex, state);
}

// --- Fonction pour exécuter une action ---
String executeAction(const char* actionName) {
  if (config["actions"].containsKey(actionName)) {
    JsonArray steps = config["actions"][actionName];
    for (JsonObject step : steps) {
      const char* type = step["type"];
      if (strcmp(type, "kvm") == 0) {
        const char* kvmName = step["name"];
        int gpioIndex = step["gpio"];
        int state = step["state"];
        setKVMGPIO(kvmName, gpioIndex, state);
      } else if (strcmp(type, "ir") == 0) {
        const char* irName = step["name"];
        const char* code = step["code"];
        int irPin = (strcmp(irName, "ir1") == 0) ? ir1_gpio :
                   (strcmp(irName, "ir2") == 0) ? ir2_gpio : ir3_gpio;
        const char* device = config["ir"][irName]["device"];
        sendIRCode(irPin, device, code);
      }
    }
    return "{\"status\":\"success\",\"message\":\"Action " + String(actionName) + " exécutée\"}";
  } else {
    return "{\"status\":\"error\",\"message\":\"Action " + String(actionName) + " introuvable\"}";
  }
}

// --- Routes du serveur web ---
void setupRoutes() {
  // Route pour la page d'accueil
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  // Route pour exécuter une action
  server.on("/execute/:action", HTTP_GET, [](AsyncWebServerRequest *request) {
    String actionName = request->pathArg(0);
    String response = executeAction(actionName.c_str());
    request->send(200, "application/json", response);
  });

  // Route pour ajouter un code IR (POST)
  server.on("/ir/add", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("device", true) && request->hasParam("code_name", true) && request->hasParam("code", true)) {
      String device = request->getParam("device", true)->value();
      String codeName = request->getParam("code_name", true)->value();
      String codeStr = request->getParam("code", true)->value();

      // Convertir la chaîne JSON en JsonArray
      DynamicJsonDocument codeDoc(1024);
      DeserializationError error = deserializeJson(codeDoc, codeStr);
      if (error) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Code IR invalide\"}");
        return;
      }

      // Ajouter le code au document irCodes
      if (!irCodes.containsKey(device)) {
        irCodes[device] = JsonObject();
      }
      irCodes[device][codeName] = codeDoc.as<JsonArray>();

      // Sauvegarder dans le fichier
      String filename = "/ir_codes/" + device + ".json";
      writeJsonFile(filename.c_str(), irCodes);

      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Code IR ajouté\"}");
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Données manquantes\"}");
    }
  });

  // Route pour les fichiers statiques (CSS, JS, etc.)
  server.on("/static/:filename", HTTP_GET, [](AsyncWebServerRequest *request) {
    String filename = "/static/" + request->pathArg(0);
    if (LittleFS.exists(filename)) {
      request->send(LittleFS, filename, "text/plain");
    } else {
      request->send(404);
    }
  });

  // Démarrer le serveur
  server.begin();
}

void setup() {
  Serial.begin(115200);

  // Initialiser LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("Erreur : impossible de monter LittleFS");
    return;
  }

  // Charger la configuration
  config = readJsonFile("/config.json");

  // Charger les codes IR depuis tous les fichiers JSON dans /ir_codes
  Dir dir = LittleFS.openDir("/ir_codes");
  while (dir.next()) {
    if (dir.fileName().endsWith(".json")) {
      DynamicJsonDocument doc(1024);
      File file = LittleFS.open(dir.filePath(), "r");
      if (file) {
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (!error) {
          String deviceName = dir.fileName().substring(0, dir.fileName().length() - 5); // Enlever ".json"
          irCodes[deviceName] = doc.as<JsonObject>();
        }
      }
    }
  }

  // Initialiser les GPIO pour les KVM
  for (int pin : kvm1_gpio) pinMode(pin, OUTPUT);
  for (int pin : kvm2_gpio) pinMode(pin, OUTPUT);
  for (int pin : kvm3_gpio) pinMode(pin, OUTPUT);

  // Initialiser les GPIO pour les émetteurs IR
  pinMode(ir1_gpio, OUTPUT);
  pinMode(ir2_gpio, OUTPUT);
  pinMode(ir3_gpio, OUTPUT);

  // Se connecter au WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connecté !");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());

  // Configurer les routes du serveur
  setupRoutes();
}

void loop() {
  // Rien à faire ici, tout est géré par le serveur asynchrone
}