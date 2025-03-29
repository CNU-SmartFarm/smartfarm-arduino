#include <DHT.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// 센서 핀 정의
#define DHTPIN 2          // DHT-11 온습도 센서 데이터 핀
#define DHTTYPE DHT11     // DHT11 센서 타입 지정
#define LIGHT_SENSOR_PIN A0   // 조도 센서 핀
#define SOIL_MOISTURE_PIN A1  // DFRobot 토양 습도 센서 핀

// ESP8266 통신용 소프트웨어 시리얼 
SoftwareSerial esp8266(10, 11); // RX, TX

// WiFi 및 서버 설정 //////////////////////////////////////////////////////////////////////////ssid, password,host는 수정 필요////////
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* host = "YOUR_SERVER_IP";
const int port = 80;  // HTTP 기본 포트

// DeviceID 생성 및 관리 함수
String generateDeviceID() {
  randomSeed(millis());
  String deviceID = "FARM-";
  for (int i = 0; i < 6; i++) {
    deviceID += String(random(16), HEX);
  }
  return deviceID;
}

void saveDeviceID(String deviceID) {
  for (int i = 0; i < deviceID.length(); i++) {
    EEPROM.write(i, deviceID[i]);
  }
  EEPROM.write(deviceID.length(), '\0'); // 널 종료 문자
}

String loadDeviceID() {
  String deviceID = "";
  char ch;
  int i = 0;
  while ((ch = EEPROM.read(i)) != '\0' && i < 32) {
    deviceID += ch;
    i++;
  }
  return deviceID;
}

// DeviceID 전역 변수
String DEVICE_ID = "";

// 센서 객체 생성
DHT dht(DHTPIN, DHTTYPE);

// 데이터 전송 주기(밀리초)
const long sensorReadInterval = 2000; // 2초마다 센서 읽기
const long serverSendInterval = 30000; // 30초마다 서버 전송
unsigned long previousSensorMillis = 0;
unsigned long previousServerMillis = 0;

// ESP8266 명령어 응답 대기 시간
const int timeout = 5000;

// WiFi 연결 상태
boolean wifiConnected = false;

void setup() {
  Serial.begin(9600); // 시리얼 통신 시작
  esp8266.begin(9600);
  
  Serial.println("스마트팜 시스템 초기화 중...");
  
  // 센서 초기화
  dht.begin();
  
  // 디바이스 ID 확인 및 생성
  DEVICE_ID = loadDeviceID();
  if (DEVICE_ID == "") {
    DEVICE_ID = generateDeviceID();
    saveDeviceID(DEVICE_ID);
    Serial.print("새 디바이스 ID 생성: ");
    Serial.println(DEVICE_ID);
  }
  
  // ESP8266 초기화 및 WiFi 연결
  initESP8266();
}

void loop() {
  unsigned long currentMillis = millis();
  
  // 센서 데이터 읽기 (2초마다)
  if (currentMillis - previousSensorMillis >= sensorReadInterval) {
    previousSensorMillis = currentMillis;
    
    // 센서 데이터 읽기
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();
    int lightLevel = readLightLevel();
    int soilMoisture = readSoilMoisture();
    
    // 센서 데이터 유효성 검사
    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("DHT 센서 읽기 실패!");
    } else {
      // 데이터 출력 - 시리얼 모니터링
      printSensorData(temperature, humidity, lightLevel, soilMoisture);
    }
  }
  
  // 서버 데이터 전송 (30초마다)
  if (currentMillis - previousServerMillis >= serverSendInterval) {
    previousServerMillis = currentMillis;
    
    // 최신 센서 데이터 읽기
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();
    int lightLevel = readLightLevel();
    int soilMoisture = readSoilMoisture();
    
    // 데이터 유효성 확인 및 DeviceID 존재 확인 후 전송
    if (!isnan(humidity) && !isnan(temperature) && DEVICE_ID != "") {
      Serial.println("서버로 데이터 전송 시도 중...");
      // 서버로 데이터 전송
      sendDataToServer(temperature, humidity, lightLevel, soilMoisture);
    } else {
      Serial.println("DeviceID가 설정되지 않았거나 센서 데이터가 유효하지 않습니다.");
    }
  }
  
  // ESP8266로부터 응답 확인
  while (esp8266.available()) {
    Serial.write(esp8266.read());
  }
  
  // 시리얼 모니터 입력 처리
  while (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    // DeviceID 수동 설정 처리
    if (command.startsWith("AT+DEVICEID=")) {
      processDeviceIdCommand(command);
    } else {
      esp8266.println(command);
    }
  }
}

// DeviceID 설정 함수
void processDeviceIdCommand(String command) {
  DEVICE_ID = command.substring(12);
  saveDeviceID(DEVICE_ID);
  Serial.print("DeviceID 설정됨: ");
  Serial.println(DEVICE_ID);
}

// ESP8266 초기화 및 WiFi 연결
void initESP8266() {
  Serial.println("ESP8266 초기화 중...");
  
  // ESP8266 초기화 AT 명령어
  sendCommand("AT", "OK", timeout);
  sendCommand("AT+CWMODE=1", "OK", timeout); // Station 모드 설정
  
  // WiFi 연결
  String connectCommand = "AT+CWJAP=\"" + String(ssid) + "\",\"" + String(password) + "\"";
  if (sendCommand(connectCommand, "OK", 10000)) {
    wifiConnected = true;
    Serial.println("WiFi 연결 성공!");
  } else {
    wifiConnected = false;
    Serial.println("WiFi 연결 실패!");
  }
}

// ESP8266에 AT 명령 전송
boolean sendCommand(String command, String response, int timeout) {
  esp8266.println(command);
  
  unsigned long startTime = millis();
  boolean found = false;
  
  while (millis() - startTime < timeout) {
    while (esp8266.available()) {
      String resp = esp8266.readString();
      if (resp.indexOf(response) != -1) {
        found = true;
        break;
      }
    }
    
    if (found) break;
  }
  
  return found;
}

// 서버로 데이터 전송 (DeviceID 추가)
void sendDataToServer(float temp, float humidity, int light, int soilMoisture) {
  if (!wifiConnected) {
    Serial.println("WiFi가 연결되어 있지 않아 서버 전송을 건너뜁니다.");
    return;
  }
  
  Serial.println("\n===== 서버 데이터 전송 시작 =====");
  
  // 연결 종료 (이전 연결이 있을 경우)
  sendCommand("AT+CIPCLOSE", "OK", 1000);
  
  // TCP 연결 설정
  String cmd = "AT+CIPSTART=\"TCP\",\"";
  cmd += host;
  cmd += "\",";
  cmd += port;
  
  if (sendCommand(cmd, "OK", 5000)) {
    Serial.println("서버 연결 성공");
    
    // HTTP POST 요청 데이터 준비 (DeviceID 추가)
    String jsonData = "{\"deviceId\":\"" + DEVICE_ID + "\",\"temperature\":";
    jsonData += String(temp);
    jsonData += ",\"humidity\":";
    jsonData += String(humidity);
    jsonData += ",\"light_level\":";
    jsonData += String(light);
    jsonData += ",\"soil_moisture\":";
    jsonData += String(soilMoisture);
    jsonData += "}";
    
    // 전송할 HTTP 요청 준비
    String httpRequest = "POST /api/sensorData HTTP/1.1\r\n";
    httpRequest += "Host: ";
    httpRequest += host;
    httpRequest += "\r\n";
    httpRequest += "Content-Type: application/json\r\n";
    httpRequest += "Content-Length: ";
    httpRequest += String(jsonData.length());
    httpRequest += "\r\n\r\n";
    httpRequest += jsonData;
    
    // 요청 전송
    sendCommand("AT+CIPSEND=" + String(httpRequest.length()), "OK", 5000);
    delay(2000); // 서버로 데이터 전송 대기
    esp8266.print(httpRequest);
    Serial.println("서버에 데이터 전송 완료");
    
    // 연결 종료
    sendCommand("AT+CIPCLOSE", "OK", 1000);
  } else {
    Serial.println("서버 연결 실패!");
  }
}

// 조도 센서 읽기
int readLightLevel() {
  int rawValue = analogRead(LIGHT_SENSOR_PIN);
  int percentValue = map(rawValue, 0, 1023, 0, 100);
  return percentValue;
}

// 토양 습도 센서 읽기
int readSoilMoisture() {
  int rawValue = analogRead(SOIL_MOISTURE_PIN);
  
  // 디버깅용 출력 추가
  Serial.print("토양 습도 raw 값: ");
  Serial.println(rawValue);
  
  // 센서 타입에 맞게 값 매핑 수정
  int moisturePercentage = map(rawValue, 0, 1023, 0, 100);
  
  // 값 범위 제한 (0-100%)
  moisturePercentage = constrain(moisturePercentage, 0, 100);
  
  return moisturePercentage;
}

// 센서 데이터 시리얼 출력
void printSensorData(float temp, float humidity, int light, int soilMoisture) {
  Serial.println("\n--------- 센서 데이터 ---------");
  Serial.print("DeviceID: ");
  Serial.println(DEVICE_ID);
  Serial.print("온도: ");
  Serial.print(temp);
  Serial.println(" °C");
  
  Serial.print("습도: ");
  Serial.print(humidity);
  Serial.println(" %");
  
  Serial.print("조도: ");
  Serial.print(light);
  Serial.println(" %");
  
  Serial.print("토양 습도: ");
  Serial.print(soilMoisture);
  Serial.println(" %");
  
  Serial.print("WiFi 상태: ");
  Serial.println(wifiConnected ? "연결됨" : "연결 안됨");
  Serial.println("-----------------------------\n");
}