// ESP32 Fluorimeter
// Libraries: Adafruit ADS1X15, Adafruit ILI9341, Adafruit GFX
// Board: ESP32 Dev Module

#include <Wire.h>
#include <SPI.h>
#include <Preferences.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#define TFT_CS      5
#define TFT_DC      2
#define TFT_RST     4
#define LED_PIN     25
#define BTN_MEASURE 32
#define BTN_BLANK   33

const float RF = 1.0e6;       // feedback resistor (ohms)
const int   PAIRS = 32;       // LED on/off pairs per reading
const int   REPEATS = 5;      // readings averaged for blank / standard
const char* UNITS = "uM";

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
Adafruit_ADS1115 ads;
Preferences prefs;

float blank = 0;
bool  blankSet = false;
float stdConc[8], stdSig[8];
int   numStd = 0;
float slope = 0, intercept = 0;
bool  calibrated = false;
bool  continuous = false;

float net, noise, dark;
bool  saturated;

// ---------- measurement ----------

float readVolts() {
  int16_t raw = ads.readADC_Differential_0_1();
  if (raw >= 32767 || raw <= -32768) saturated = true;
  return ads.computeVolts(raw);
}

void measure() {
  float sum = 0, sumSq = 0, sumOff = 0;
  saturated = false;

  for (int i = 0; i < PAIRS; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(3);
    float off = readVolts();
    digitalWrite(LED_PIN, HIGH);
    delay(3);
    float on = readVolts();

    float d = on - off;
    sum += d;
    sumSq += d * d;
    sumOff += off;
  }
  digitalWrite(LED_PIN, LOW);

  float mean = sum / PAIRS;
  float var = max(0.0f, (sumSq - sum * sum / PAIRS) / (PAIRS - 1));

  net   = mean / RF * 1e9;                 // nA
  noise = sqrt(var / PAIRS) / RF * 1e9;    // nA
  dark  = sumOff / PAIRS / RF * 1e9;       // nA
}

float averageNet() {
  float sum = 0;
  for (int i = 0; i < REPEATS; i++) {
    measure();
    sum += net;
  }
  return sum / REPEATS;
}

// ---------- calibration ----------

void fit() {
  calibrated = false;
  if (!blankSet || numStd == 0) return;

  if (numStd == 1) {
    slope = (stdSig[0] - blank) / stdConc[0];
    intercept = 0;
  } else {
    float sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < numStd; i++) {
      float x = stdConc[i], y = stdSig[i] - blank;
      sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    slope = (numStd * sxy - sx * sy) / (numStd * sxx - sx * sx);
    intercept = (sy - slope * sx) / numStd;
  }
  calibrated = (slope != 0);
}

float concentration() {
  return (net - blank - intercept) / slope;
}

void save() {
  prefs.begin("fluoro", false);
  prefs.putBool("blankSet", blankSet);
  prefs.putFloat("blank", blank);
  prefs.putInt("numStd", numStd);
  prefs.putBytes("conc", stdConc, sizeof(stdConc));
  prefs.putBytes("sig", stdSig, sizeof(stdSig));
  prefs.end();
}

void load() {
  prefs.begin("fluoro", true);
  blankSet = prefs.getBool("blankSet", false);
  blank = prefs.getFloat("blank", 0);
  numStd = constrain(prefs.getInt("numStd", 0), 0, 8);
  prefs.getBytes("conc", stdConc, sizeof(stdConc));
  prefs.getBytes("sig", stdSig, sizeof(stdSig));
  prefs.end();
  fit();
}

// ---------- display ----------

void line(int y, int size, uint16_t color, String text) {
  while ((int)text.length() < 320 / (6 * size)) text += " ";
  tft.setTextSize(size);
  tft.setTextColor(color, ILI9341_BLACK);
  tft.setCursor(4, y);
  tft.print(text);
}

void message(String text, uint16_t color) {
  line(214, 2, color, text);
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 28, ILI9341_NAVY);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(6, 7);
  tft.print("FLUORIMETER");
  tft.setTextSize(1);
  tft.setTextColor(blankSet ? ILI9341_GREEN : ILI9341_LIGHTGREY);
  tft.setCursor(170, 5);
  tft.print(blankSet ? "BLANK: OK" : "BLANK: --");
  tft.setTextColor(calibrated ? ILI9341_GREEN : ILI9341_LIGHTGREY);
  tft.setCursor(170, 17);
  tft.print(calibrated ? "CAL: OK" : "CAL: --");
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(250, 11);
  tft.print(continuous ? "CONTINUOUS" : "SINGLE");
}

void showResult() {
  if (calibrated) {
    line(40, 1, ILI9341_LIGHTGREY, "CONCENTRATION");
    line(54, 4, ILI9341_WHITE, String(concentration(), 3) + " " + UNITS);
  } else {
    line(40, 1, ILI9341_LIGHTGREY, blankSet ? "SIGNAL - BLANK" : "SIGNAL");
    line(54, 4, ILI9341_WHITE, String(net - (blankSet ? blank : 0), 3) + " nA");
  }
  line(110, 2, ILI9341_GREEN,   "Net:   " + String(net, 3) + " nA");
  line(132, 2, ILI9341_MAGENTA, "Noise: +/-" + String(noise, 3));
  line(154, 2, ILI9341_CYAN,    "Dark:  " + String(dark, 3) + " nA");
  if (saturated) message("SATURATED", ILI9341_RED);
  else message("", ILI9341_WHITE);
}

// ---------- actions ----------

void doMeasure() {
  measure();
  showResult();
  Serial.printf("net %.3f nA, noise %.3f, dark %.3f", net, noise, dark);
  if (calibrated) Serial.printf(", conc %.3f %s", concentration(), UNITS);
  Serial.println(saturated ? "  SATURATED" : "");
}

void doBlank() {
  message("Measuring blank...", ILI9341_YELLOW);
  blank = averageNet();
  blankSet = true;
  fit();
  save();
  drawHeader();
  message("Blank stored", ILI9341_GREEN);
  Serial.printf("Blank = %.3f nA\n", blank);
}

void doStandard(float conc) {
  if (numStd >= 8) return;
  message("Measuring standard...", ILI9341_YELLOW);
  stdConc[numStd] = conc;
  stdSig[numStd] = averageNet();
  Serial.printf("Standard %d: %.3f %s -> %.3f nA\n", numStd + 1, conc, UNITS, stdSig[numStd]);
  numStd++;
  save();
  message("Standard stored", ILI9341_GREEN);
}

int button(int pin) {          // 0 = none, 1 = short press, 2 = long press
  if (digitalRead(pin) == HIGH) return 0;
  delay(30);
  unsigned long t = millis();
  while (digitalRead(pin) == LOW) {
    if (millis() - t > 1000) {
      while (digitalRead(pin) == LOW) delay(10);
      return 2;
    }
    delay(10);
  }
  return 1;
}

void serialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "m") doMeasure();
  else if (cmd == "blank") doBlank();
  else if (cmd.startsWith("std ")) doStandard(cmd.substring(4).toFloat());
  else if (cmd == "fit") {
    fit();
    drawHeader();
    if (calibrated) Serial.printf("signal - blank = %.4f * conc + %.4f\n", slope, intercept);
    else Serial.println("Need blank and standards first");
  }
  else if (cmd == "clear") {
    numStd = 0;
    calibrated = false;
    save();
    drawHeader();
    Serial.println("Standards cleared");
  }
}

// ---------- main ----------

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_MEASURE, INPUT_PULLUP);
  pinMode(BTN_BLANK, INPUT_PULLUP);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  Wire.begin(21, 22);
  if (!ads.begin(0x48)) {
    line(100, 2, ILI9341_RED, "ADS1115 not found");
    while (true) delay(1000);
  }
  ads.setGain(GAIN_TWO);                 // +/-2.048 V
  ads.setDataRate(RATE_ADS1115_860SPS);

  load();
  drawHeader();
  message("Press MEASURE", ILI9341_WHITE);
  Serial.println("Commands: m, blank, std <conc>, fit, clear");
}

void loop() {
  int b = button(BTN_MEASURE);
  if (b == 1) {
    if (continuous) { continuous = false; drawHeader(); }
    else doMeasure();
  }
  if (b == 2) {
    continuous = !continuous;
    drawHeader();
  }

  if (button(BTN_BLANK) == 1) doBlank();

  serialCommands();

  if (continuous) doMeasure();
}
