#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// =====================================================
// EPDI PROJECT IDENTITY
// =====================================================

#define EPDI_NAME "EPDI - Electromechanical Proxy Diagnostic Interface"
#define EPDI_PLATFORM "ERP-1 Experimental Research Platform"
#define EPDI_WORKBENCH "EPDI Research Workbench (ERW)"
#define EPDI_DOC_NO "SW-001"
#define EPDI_REVISION "1.0"

// =====================================================
// WIFI
// =====================================================

const char* WIFI_SSID = "moto";
const char* WIFI_PASSWORD = "moto12345";

// =====================================================
// HARDWARE
// =====================================================

// ESP32-WROOM
// GPIO34 = ADC1
#define ADC_PIN 34

// SunnySky X2212-13 980KV
// 14 poles = 7 pole pairs
#define POLE_PAIRS 7

// =====================================================
// ADC
// =====================================================

#define ADC_MAX 4095.0
#define ADC_REF 3.3

// =====================================================
// SAMPLING
// =====================================================

// 256 samples
// 2500 us/sample = 2.5 ms
// Total window = 640 ms

#define SAMPLE_COUNT 512
#define SAMPLE_INTERVAL_US 250

uint16_t adcData[SAMPLE_COUNT];
float signalData[SAMPLE_COUNT];

// =====================================================
// MEASUREMENTS
// =====================================================

float adcVoltage = 0.0;
float rmsVoltage = 0.0;
float peakToPeak = 0.0;
float peakVoltage = 0.0;

float frequencyHz = 0.0;
float rpm = 0.0;

float crestFactor = 0.0;

int health = 100;

String fault = "WAITING";

// =====================================================
// BASELINE
// =====================================================

bool baselineSaved = false;

float normalRMS = 0;
float normalPP = 0;
float normalFrequency = 0;

// =====================================================
// TRAINING
// =====================================================

String selectedCondition = "NORMAL";

bool recording = false;

unsigned long sampleCountRecorded = 0;

// =====================================================
// DIAGNOSTIC CONDITION LIBRARY
// =====================================================

const char* CONDITION_NAMES[] = {
  "NORMAL",
  "IMBALANCE",
  "MISALIGNMENT",
  "BEARING DEGRADATION",
  "GEAR DEFECT",
  "CAVITATION",
  "OVERLOAD",
  "LUBRICATION DEFICIENCY",
  "STRUCTURAL DEGRADATION",
  "PROGRESSIVE WEAR",
  "INCIPIENT FAILURE",
  "OTHER",
  "BELT DAMAGE",
  "PULLEY DAMAGE",
  "BELT + PULLEY"
};

const uint8_t BASE_CONDITION_COUNT = sizeof(CONDITION_NAMES) / sizeof(CONDITION_NAMES[0]);
const uint8_t CUSTOM_CONDITION_COUNT = 5;
const uint8_t CONDITION_COUNT = BASE_CONDITION_COUNT + CUSTOM_CONDITION_COUNT;
String customConditionNames[CUSTOM_CONDITION_COUNT];

struct ConditionProfile {
  float rms;
  float peakToPeak;
  float frequency;
  float crest;
  bool valid;
};

ConditionProfile profiles[CONDITION_COUNT];
Preferences epdiPrefs;

String testResult = "NOT TESTED";
float testScore = 0.0;
String matchedCondition = "NONE";

// =====================================================
// SERVER
// =====================================================

WebServer server(80);

// =====================================================
// CONDITION PROFILE HELPERS
// =====================================================


// ===== ADVANCED DIAGNOSTIC PARAMETERS =====
float dcOffset = 0.0f;
float thdPercent = 0.0f;
float fundamentalAmplitude = 0.0f;
float harmonic2Amplitude = 0.0f;
float harmonic3Amplitude = 0.0f;
float rmsPeakRatio = 0.0f;
float kurtosisValue = 0.0f;
float skewnessValue = 0.0f;
int zeroCrossingCount = 0;
float frequencyStability = 0.0f;
float voltageRipple = 0.0f;
float signalVariance = 0.0f;
float snrDb = 0.0f;
float phaseStability = 0.0f;
float vibrationFaultIndex = 0.0f;
// ============================================

int conditionIndex(const String& name)
{
  for (uint8_t i = 0; i < BASE_CONDITION_COUNT; i++)
  {
    if (name.equals(CONDITION_NAMES[i])) return i;
  }
  for (uint8_t j = 0; j < CUSTOM_CONDITION_COUNT; j++)
  {
    if (customConditionNames[j].length() > 0 && name.equals(customConditionNames[j]))
      return BASE_CONDITION_COUNT + j;
  }
  return -1;
}

void clearProfiles()
{
  for (uint8_t i = 0; i < CONDITION_COUNT; i++)
  {
    profiles[i].rms = 0;
    profiles[i].peakToPeak = 0;
    profiles[i].frequency = 0;
    profiles[i].crest = 0;
    profiles[i].valid = false;
  }
}

void loadProfiles()
{
  clearProfiles();

  for (uint8_t j = 0; j < CUSTOM_CONDITION_COUNT; j++)
  {
    char nameKey[8];
    snprintf(nameKey, sizeof(nameKey), "n%02u", j);
    customConditionNames[j] = epdiPrefs.getString(nameKey, "");
  }

  for (uint8_t i = 0; i < CONDITION_COUNT; i++)
  {
    char key[8];
    snprintf(key, sizeof(key), "v%02u", i);
    profiles[i].valid = epdiPrefs.getBool(key, false);

    snprintf(key, sizeof(key), "r%02u", i);
    profiles[i].rms = epdiPrefs.getFloat(key, 0);
    snprintf(key, sizeof(key), "p%02u", i);
    profiles[i].peakToPeak = epdiPrefs.getFloat(key, 0);
    snprintf(key, sizeof(key), "f%02u", i);
    profiles[i].frequency = epdiPrefs.getFloat(key, 0);
    snprintf(key, sizeof(key), "c%02u", i);
    profiles[i].crest = epdiPrefs.getFloat(key, 0);
  }
}

void saveSelectedProfile()
{
  int idx = conditionIndex(selectedCondition);
  if (idx < 0)
    return;

  profiles[idx].rms = rmsVoltage;
  profiles[idx].peakToPeak = peakToPeak;
  profiles[idx].frequency = frequencyHz;
  profiles[idx].crest = crestFactor;
  profiles[idx].valid = true;

  char key[8];
  snprintf(key, sizeof(key), "v%02d", idx);
  epdiPrefs.putBool(key, true);
  snprintf(key, sizeof(key), "r%02d", idx);
  epdiPrefs.putFloat(key, rmsVoltage);
  snprintf(key, sizeof(key), "p%02d", idx);
  epdiPrefs.putFloat(key, peakToPeak);
  snprintf(key, sizeof(key), "f%02d", idx);
  epdiPrefs.putFloat(key, frequencyHz);
  snprintf(key, sizeof(key), "c%02d", idx);
  epdiPrefs.putFloat(key, crestFactor);
}

void testAgainstProfiles()
{
  readSignal();

  float bestDistance = 999999.0;
  int bestIndex = -1;

  for (uint8_t i = 0; i < CONDITION_COUNT; i++)
  {
    if (!profiles[i].valid)
      continue;

    float dR = fabs(rmsVoltage - profiles[i].rms) / max(profiles[i].rms, 0.001f);
    float dP = fabs(peakToPeak - profiles[i].peakToPeak) / max(profiles[i].peakToPeak, 0.001f);
    float dF = fabs(frequencyHz - profiles[i].frequency) / max(profiles[i].frequency, 0.001f);
    float dC = fabs(crestFactor - profiles[i].crest) / max(profiles[i].crest, 0.001f);

    // Weighted feature distance. Lower is a better match.
    float distance = (0.35f * dR) + (0.30f * dP) + (0.25f * dF) + (0.10f * dC);

    if (distance < bestDistance)
    {
      bestDistance = distance;
      bestIndex = i;
    }
  }

  if (bestIndex < 0)
  {
    matchedCondition = "NO STORED PROFILE";
    testScore = 0;
    testResult = "TRAIN A CONDITION FIRST";
    return;
  }

  matchedCondition = CONDITION_NAMES[bestIndex];
  testScore = max(0.0f, min(100.0f, (1.0f - bestDistance) * 100.0f));
  testResult = matchedCondition;
}

void resetVirtual()
{
  recording = false;
  sampleCountRecorded = 0;
  testResult = "NOT TESTED";
  testScore = 0;
  matchedCondition = "NONE";
  fault = "RESET / WAITING";
  health = 100;
}

void clearAllProfiles()
{
  clearProfiles();
  for (uint8_t j = 0; j < CUSTOM_CONDITION_COUNT; j++) customConditionNames[j] = "";
  epdiPrefs.clear();
  baselineSaved = false;
  normalRMS = 0;
  normalPP = 0;
  normalFrequency = 0;
  resetVirtual();
}

// =====================================================
// READ SIGNAL
// =====================================================

void readSignal()
{
  unsigned long startTime = micros();

  float sum = 0;
  float sumSquare = 0;

  int minimum = 4095;
  int maximum = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++)
  {
    while (
      (micros() - startTime) <
      ((unsigned long)i * SAMPLE_INTERVAL_US)
    )
    {
      yield();
    }

    int raw = analogRead(ADC_PIN);

    adcData[i] = raw;

    if (raw < minimum)
      minimum = raw;

    if (raw > maximum)
      maximum = raw;

    float voltage =
      ((float)raw / ADC_MAX) * ADC_REF;

    signalData[i] = voltage;

    sum += voltage;
    sumSquare += voltage * voltage;
  }

  // =================================================
  // DC AVERAGE
  // =================================================

  float average =
    sum / SAMPLE_COUNT;

  // =================================================
  // AC RMS
  // =================================================

  float meanSquare =
    sumSquare / SAMPLE_COUNT;

  float acSquare =
    meanSquare -
    (average * average);

  if (acSquare < 0)
    acSquare = 0;

  rmsVoltage =
    sqrt(acSquare);

  // =================================================
  // PEAK TO PEAK
  // =================================================

  peakToPeak =
    ((float)(maximum - minimum) /
     ADC_MAX) * ADC_REF;

  // =================================================
  // PEAK
  // =================================================

  peakVoltage = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++)
  {
    float ac =
      signalData[i] - average;

    float absoluteValue =
      fabs(ac);

    if (absoluteValue > peakVoltage)
      peakVoltage = absoluteValue;
  }

  // =================================================
  // CREST FACTOR
  // =================================================

  if (rmsVoltage > 0.001)
  {
    crestFactor =
      peakVoltage / rmsVoltage;
  }
  else
  {
    crestFactor = 0;
  }

  // =================================================
  // ADC VOLTAGE
  // =================================================

  adcVoltage =
    ((float)adcData[SAMPLE_COUNT - 1] /
     ADC_MAX) * ADC_REF;

  // =================================================
  // FREQUENCY
  // =================================================

  calculateFrequency();

  // =================================================
  // RPM
  // =================================================

  if (frequencyHz > 0)
  {
    rpm =
      (frequencyHz * 60.0) /
      POLE_PAIRS;
  }
  else
  {
    rpm = 0;
  }

  // =================================================
  // HEALTH
  // =================================================

  calculateHealth();

  // =================================================
  // RECORDING
  // =================================================

  if (recording)
  {
    sampleCountRecorded += SAMPLE_COUNT;
  }


  // -------- Advanced diagnostic analysis --------
  if (SAMPLE_COUNT > 4) {
    double sum = 0.0;
    double sum2 = 0.0;
    float localMin = signalData[0];
    float localMax = signalData[0];

    for (int i = 0; i < SAMPLE_COUNT; i++) {
      double x = signalData[i];
      sum += x;
      sum2 += x * x;
      if (signalData[i] < localMin) localMin = signalData[i];
      if (signalData[i] > localMax) localMax = signalData[i];
    }

    double mean = sum / SAMPLE_COUNT;
    double variance = max(0.0, sum2 / SAMPLE_COUNT - mean * mean);
    double sd = sqrt(variance);

    double sum3 = 0.0;
    double sum4 = 0.0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
      double z = signalData[i] - mean;
      sum3 += z * z * z;
      sum4 += z * z * z * z;
    }

    dcOffset = (float)mean;
    signalVariance = (float)variance;
    peakVoltage = max(fabs(localMax), fabs(localMin));

    if (sd > 1e-9) {
      skewnessValue = (float)((sum3 / SAMPLE_COUNT) / pow(sd, 3));
      kurtosisValue = (float)((sum4 / SAMPLE_COUNT) / pow(sd, 4));
    } else {
      skewnessValue = 0.0f;
      kurtosisValue = 0.0f;
    }

    rmsPeakRatio = (peakVoltage > 1e-9f) ? (rmsVoltage / peakVoltage) : 0.0f;

    // AC-centered zero crossings
    zeroCrossingCount = 0;
    for (int i = 1; i < SAMPLE_COUNT; i++) {
      float a = signalData[i - 1] - dcOffset;
      float b = signalData[i] - dcOffset;
      if ((a <= 0.0f && b > 0.0f) || (a >= 0.0f && b < 0.0f)) {
        zeroCrossingCount++;
      }
    }

    // DFT amplitudes at the measured fundamental frequency
    if (frequencyHz > 0.1f) {
      double w = 2.0 * PI * frequencyHz / 4000.0;

      double c1 = 0.0, s1 = 0.0;
      double c2 = 0.0, s2 = 0.0;
      double c3 = 0.0, s3 = 0.0;

      for (int i = 0; i < SAMPLE_COUNT; i++) {
        double x = signalData[i] - dcOffset;
        double a = w * i;

        c1 += x * cos(a);
        s1 += x * sin(a);
        c2 += x * cos(2.0 * a);
        s2 += x * sin(2.0 * a);
        c3 += x * cos(3.0 * a);
        s3 += x * sin(3.0 * a);
      }

      fundamentalAmplitude = (float)(2.0 * sqrt(c1*c1 + s1*s1) / SAMPLE_COUNT);
      harmonic2Amplitude = (float)(2.0 * sqrt(c2*c2 + s2*s2) / SAMPLE_COUNT);
      harmonic3Amplitude = (float)(2.0 * sqrt(c3*c3 + s3*s3) / SAMPLE_COUNT);

      if (fundamentalAmplitude > 1e-6f) {
        thdPercent = 100.0f * sqrt(
          harmonic2Amplitude * harmonic2Amplitude +
          harmonic3Amplitude * harmonic3Amplitude
        ) / fundamentalAmplitude;
      } else {
        thdPercent = 0.0f;
      }

      // SNR estimate: fundamental power / residual power
      double fundamentalPower = 0.0;
      double residualPower = 0.0;
      double phase = atan2(s1, c1);

      for (int i = 0; i < SAMPLE_COUNT; i++) {
        double a = w * i + phase;
        double fit = fundamentalAmplitude * sin(a);
        double x = signalData[i] - dcOffset;
        double e = x - fit;
        fundamentalPower += fit * fit;
        residualPower += e * e;
      }

      if (fundamentalPower > 1e-12 && residualPower > 1e-12) {
        snrDb = (float)(10.0 * log10(fundamentalPower / residualPower));
      } else {
        snrDb = 0.0f;
      }
    } else {
      fundamentalAmplitude = 0.0f;
      harmonic2Amplitude = 0.0f;
      harmonic3Amplitude = 0.0f;
      thdPercent = 0.0f;
      snrDb = 0.0f;
    }

    // Ripple relative to DC level
    voltageRipple = (fabs(dcOffset) > 1e-6f)
      ? (100.0f * rmsVoltage / fabs(dcOffset))
      : 0.0f;

    // Frequency stability from positive-going zero-crossing intervals
    if (frequencyHz > 0.1f && zeroCrossingCount >= 4) {
      float periods[32];
      int pc = 0;
      int lastCross = -1;

      for (int i = 1; i < SAMPLE_COUNT && pc < 32; i++) {
        float a = signalData[i - 1] - dcOffset;
        float b = signalData[i] - dcOffset;

        if (a <= 0.0f && b > 0.0f) {
          if (lastCross >= 0) periods[pc++] = (float)(i - lastCross);
          lastCross = i;
        }
      }

      if (pc >= 2) {
        float pmean = 0.0f;
        for (int i = 0; i < pc; i++) pmean += periods[i];
        pmean /= pc;

        float pvar = 0.0f;
        for (int i = 0; i < pc; i++) {
          float d = periods[i] - pmean;
          pvar += d * d;
        }
        pvar /= pc;

        float cv = (pmean > 0.0f) ? sqrt(pvar) / pmean : 0.0f;
        frequencyStability = constrain(100.0f * (1.0f - cv), 0.0f, 100.0f);
      } else {
        frequencyStability = 0.0f;
      }
    } else {
      frequencyStability = 0.0f;
    }

    phaseStability = frequencyStability;

    float harmonicRatio = (fundamentalAmplitude > 1e-6f)
      ? (harmonic2Amplitude + harmonic3Amplitude) / fundamentalAmplitude
      : 0.0f;

    float crestPenalty = max(0.0f, crestFactor - 1.4142f);

    vibrationFaultIndex = constrain(
      thdPercent * 0.45f +
      harmonicRatio * 100.0f * 0.30f +
      fabs(skewnessValue) * 5.0f +
      max(0.0f, kurtosisValue - 3.0f) * 4.0f +
      crestPenalty * 15.0f,
      0.0f, 100.0f
    );
  }
  // -------- END Advanced diagnostic analysis --------

}

// =====================================================
// FREQUENCY DETECTION
// =====================================================

void calculateFrequency()
{
  float average = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++)
  {
    average += signalData[i];
  }

  average /= SAMPLE_COUNT;

  float minimum = 999;
  float maximum = -999;

  for (int i = 0; i < SAMPLE_COUNT; i++)
  {
    float value =
      signalData[i] - average;

    if (value < minimum)
      minimum = value;

    if (value > maximum)
      maximum = value;
  }

  // Adaptive threshold

  float threshold =
    (minimum + maximum) / 2.0;

  int crossings = 0;

  bool previous =
    (signalData[0] - average) <
    threshold;

  for (int i = 1; i < SAMPLE_COUNT; i++)
  {
    bool current =
      (signalData[i] - average) <
      threshold;

    if (previous && !current)
    {
      crossings++;
    }

    previous = current;
  }

  float measurementTime =
    (SAMPLE_COUNT *
     SAMPLE_INTERVAL_US) /
    1000000.0;

  if (measurementTime > 0)
  {
    frequencyHz =
      crossings /
      measurementTime;
  }

  if (
    frequencyHz < 1 ||
    frequencyHz > 1000
  )
  {
    frequencyHz = 0;
  }
}

// =====================================================
// HEALTH
// =====================================================

void calculateHealth()
{
  if (
    rmsVoltage < 0.01 ||
    frequencyHz < 1
  )
  {
    health = 0;
    fault = "NO SIGNAL";
    return;
  }

  // No baseline

  if (!baselineSaved)
  {
    health = 100;
    fault = "BASELINE REQUIRED";
    return;
  }

  int score = 100;

  // -----------------------------------------------
  // RMS CHANGE
  // -----------------------------------------------

  float rmsChange =
    fabs(
      rmsVoltage - normalRMS
    ) /
    max(
      normalRMS,
      0.001f
    ) *
    100.0;

  // -----------------------------------------------
  // PEAK-PEAK CHANGE
  // -----------------------------------------------

  float ppChange =
    fabs(
      peakToPeak - normalPP
    ) /
    max(
      normalPP,
      0.001f
    ) *
    100.0;

  // -----------------------------------------------
  // FREQUENCY CHANGE
  // -----------------------------------------------

  float freqChange =
    fabs(
      frequencyHz -
      normalFrequency
    );

  // -----------------------------------------------
  // RMS ANOMALY
  // -----------------------------------------------

  if (rmsChange > 10)
    score -= 15;

  if (rmsChange > 25)
    score -= 20;

  if (rmsChange > 40)
    score -= 20;

  // -----------------------------------------------
  // PEAK ANOMALY
  // -----------------------------------------------

  if (ppChange > 10)
    score -= 15;

  if (ppChange > 25)
    score -= 20;

  // -----------------------------------------------
  // FREQUENCY ANOMALY
  // -----------------------------------------------

  if (freqChange > 5)
    score -= 10;

  if (freqChange > 15)
    score -= 20;

  if (score < 0)
    score = 0;

  health = score;

  if (health >= 80)
  {
    fault = "NORMAL";
  }
  else if (health >= 60)
  {
    fault = "WARNING";
  }
  else
  {
    fault = "ANOMALY";
  }
}

// =====================================================
// SAVE NORMAL BASELINE
// =====================================================

void saveBaseline()
{
  readSignal();

  normalRMS =
    rmsVoltage;

  normalPP =
    peakToPeak;

  normalFrequency =
    frequencyHz;

  baselineSaved = true;

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "NORMAL BASELINE SAVED"
  );

  Serial.println(
    "================================"
  );

  Serial.print(
    "RMS: "
  );

  Serial.println(
    normalRMS,
    4
  );

  Serial.print(
    "Peak-Peak: "
  );

  Serial.println(
    normalPP,
    4
  );

  Serial.print(
    "Frequency: "
  );

  Serial.println(
    normalFrequency,
    2
  );

  Serial.print(
    "RPM: "
  );

  Serial.println(
    rpm,
    0
  );
}

// =====================================================
// JSON DATA
// =====================================================

void sendData()
{
  readSignal();

  String json;

  json.reserve(7000);

  json = "{";

  // ADC

  json += "\"adc\":";
  json += String(
    adcData[SAMPLE_COUNT - 1]
  );

  // ADC voltage

  json += ",\"adcVoltage\":";
  json += String(
    adcVoltage,
    3
  );

  // RMS

  json += ",\"rms\":";
  json += String(
    rmsVoltage,
    3
  );

  // Peak-to-peak

  json += ",\"peakToPeak\":";
  json += String(
    peakToPeak,
    3
  );

  // Crest

  json += ",\"crest\":";
  json += String(
    crestFactor,
    2
  );

  // Frequency

  json += ",\"frequency\":";
  json += String(
    frequencyHz,
    2
  );

  // RPM

  json += ",\"rpm\":";
  json += String(
    rpm,
    0
  );

  // Health

  json += ",\"health\":";
  json += String(
    health
  );

  // Fault

  json += ",\"fault\":\"";
  json += fault;
  json += "\"";

  // Condition

  json += ",\"condition\":\"";
  json += selectedCondition;
  json += "\"";

  // Recording

  json += ",\"recording\":";

  if (recording)
    json += "true";
  else
    json += "false";

  // Samples

  json += ",\"samples\":";
  json += String(
    sampleCountRecorded
  );

  json += ",\"testResult\":\"";
  json += testResult;
  json += "\"";

  json += ",\"matchedCondition\":\"";
  json += matchedCondition;
  json += "\"";

  json += ",\"testScore\":";
  json += String(testScore, 1);

  uint8_t storedProfiles = 0;
  for (uint8_t i = 0; i < CONDITION_COUNT; i++)
    if (profiles[i].valid) storedProfiles++;

  json += ",\"storedProfiles\":";
  json += String(storedProfiles);

  // =================================================
  // ADVANCED DIAGNOSTIC DATA
  // =================================================

  json += ",\"dcOffset\":";
  json += String(dcOffset, 4);

  json += ",\"peakVoltage\":";
  json += String(peakVoltage, 4);

  json += ",\"thd\":";
  json += String(thdPercent, 3);

  json += ",\"fundamental\":";
  json += String(fundamentalAmplitude, 4);

  json += ",\"harmonic2\":";
  json += String(harmonic2Amplitude, 4);

  json += ",\"harmonic3\":";
  json += String(harmonic3Amplitude, 4);

  json += ",\"rmsPeakRatio\":";
  json += String(rmsPeakRatio, 4);

  json += ",\"kurtosis\":";
  json += String(kurtosisValue, 4);

  json += ",\"skewness\":";
  json += String(skewnessValue, 4);

  json += ",\"zeroCrossings\":";
  json += String(zeroCrossingCount);

  json += ",\"frequencyStability\":";
  json += String(frequencyStability, 2);

  json += ",\"phaseStability\":";
  json += String(phaseStability, 2);

  json += ",\"voltageRipple\":";
  json += String(voltageRipple, 2);

  json += ",\"variance\":";
  json += String(signalVariance, 4);

  json += ",\"snrDb\":";
  json += String(snrDb, 2);

  json += ",\"faultIndex\":";
  json += String(vibrationFaultIndex, 2);

  // =================================================
  // WAVEFORM DATA
  // =================================================

  json += ",\"wave\":[";

  const int DISPLAY_POINTS = 256;

  for (
    int i = 0;
    i < DISPLAY_POINTS;
    i++
  )
  {
    int index =
      ((long)i *
       SAMPLE_COUNT) /
      DISPLAY_POINTS;

    // Prevent index overflow

    if (index >= SAMPLE_COUNT)
      index = SAMPLE_COUNT - 1;

    json += String(
      signalData[index],
      4
    );

    if (
      i <
      DISPLAY_POINTS - 1
    )
    {
      json += ",";
    }
  }

  json += "]";

  json += "}";

  server.send(
    200,
    "application/json",
    json
  );
}

// =====================================================
// START RECORDING
// =====================================================

void startRecording()
{
  recording = true;

  sampleCountRecorded = 0;

  server.send(
    200,
    "text/plain",
    "Recording started"
  );
}

// =====================================================
// STOP RECORDING
// =====================================================

void stopRecording()
{
  recording = false;

  server.send(
    200,
    "text/plain",
    "Recording stopped"
  );
}

// =====================================================
// ADD CUSTOM CONDITION
// =====================================================

void addCustomCondition()
{
  if (!server.hasArg("name"))
  {
    server.send(400, "text/plain", "Missing condition name");
    return;
  }

  String name = server.arg("name");
  name.trim();
  name.toUpperCase();
  if (name.length() < 2)
  {
    server.send(400, "text/plain", "Invalid condition name");
    return;
  }

  if (conditionIndex(name) >= 0)
  {
    server.send(200, "text/plain", "Condition already exists");
    return;
  }

  for (uint8_t j = 0; j < CUSTOM_CONDITION_COUNT; j++)
  {
    if (customConditionNames[j].length() == 0)
    {
      customConditionNames[j] = name;
      char key[8];
      snprintf(key, sizeof(key), "n%02u", j);
      epdiPrefs.putString(key, name);
      server.send(200, "text/plain", name);
      return;
    }
  }

  server.send(507, "text/plain", "Custom condition slots full");
}

// =====================================================
// SET CONDITION
// =====================================================

void setCondition()
{
  if (server.hasArg("name"))
  {
    selectedCondition =
      server.arg("name");
  }

  server.send(
    200,
    "text/plain",
    selectedCondition
  );
}

// =====================================================
// SAVE CONDITION PROFILE REQUEST
// =====================================================

void saveConditionRequest()
{
  readSignal();
  saveSelectedProfile();

  server.send(
    200,
    "text/plain",
    "Condition profile saved: " + selectedCondition
  );
}

// =====================================================
// TEST REQUEST
// =====================================================

void testRequest()
{
  testAgainstProfiles();

  server.send(
    200,
    "text/plain",
    testResult
  );
}

// =====================================================
// VIRTUAL RESET REQUEST
// =====================================================

void resetRequest()
{
  resetVirtual();

  server.send(
    200,
    "text/plain",
    "EPDI virtual reset complete"
  );
}

// =====================================================
// CLEAR ALL TRAINED PROFILES REQUEST
// =====================================================

void clearAllRequest()
{
  clearAllProfiles();

  server.send(
    200,
    "text/plain",
    "All stored condition profiles cleared"
  );
}

// =====================================================
// BASELINE REQUEST
// =====================================================

void baselineRequest()
{
  saveBaseline();

  server.send(
    200,
    "text/plain",
    "Baseline saved"
  );
}

// =====================================================
// WEB PAGE
// =====================================================

void webpage()
{
  String html;

  html.reserve(16000);

  html = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta name="viewport"
content="width=device-width,initial-scale=1">

<title>
EPDI - Electromechanical Proxy Diagnostic Interface
</title>

<style>

*{
box-sizing:border-box;
}

body{
margin:0;
background:#07111d;
color:white;
font-family:Arial,sans-serif;
}

header{
background:#0b2035;
padding:20px;
text-align:center;
border-bottom:2px solid #17658d;
}

header h1{
margin:0;
font-size:24px;
}

header p{
color:#91a9bb;
font-size:13px;
}

.container{
max-width:1400px;
margin:auto;
padding:14px;
}

.cards{
display:grid;
grid-template-columns:
repeat(auto-fit,minmax(140px,1fr));
gap:10px;
}

.card{
background:#0d2236;
border:1px solid #18577b;
border-radius:10px;
padding:14px;
text-align:center;
}

.label{
color:#8da6b9;
font-size:12px;
}

.value{
font-size:26px;
font-weight:bold;
margin-top:8px;
}

.blue{
color:#28c9ff;
}

.green{
color:#39ee82;
}

.yellow{
color:#ffd84e;
}

.purple{
color:#a78cff;
}

.orange{
color:#ff9d50;
}

.red{
color:#ff6075;
}

.grid{
display:grid;
grid-template-columns:
2fr 1fr;
gap:12px;
margin-top:12px;
}

.panel{
background:#0b1d2f;
border:1px solid #18577b;
border-radius:10px;
padding:14px;
}

.panel h3{
margin-top:0;
font-size:16px;
}

.chartHeader{
display:flex;
justify-content:space-between;
align-items:center;
flex-wrap:wrap;
gap:8px;
}

canvas{
display:block;
width:100%;
height:360px;
background:#050d15;
border-radius:8px;
}

.waveControls{
display:flex;
gap:5px;
flex-wrap:wrap;
margin-bottom:10px;
}

.waveControls button{
padding:7px 10px;
font-size:11px;
}

button{
padding:11px 15px;
margin:4px;
border:0;
border-radius:6px;
background:#176b9e;
color:white;
font-weight:bold;
cursor:pointer;
}

button:hover{
opacity:.85;
}

.greenBtn{
background:#16b957;
}

.redBtn{
background:#d93636;
}

.purpleBtn{
background:#714bc1;
}

.health{
text-align:center;
}

.healthNumber{
font-size:58px;
font-weight:bold;
color:#39ee82;
}

.fault{
font-size:23px;
font-weight:bold;
padding:15px;
margin-top:12px;
border-radius:8px;
background:#123d2a;
color:#39ee82;
}

.features{
display:grid;
grid-template-columns:
repeat(3,1fr);
gap:8px;
}

.feature{
background:#102940;
padding:12px;
border-radius:7px;
text-align:center;
}

.recordStatus{
padding:10px;
margin-top:10px;
background:#102940;
border-radius:7px;
}

.conditionGrid{
display:grid;
grid-template-columns:repeat(auto-fit,minmax(150px,1fr));
gap:4px;
}

.conditionGrid button{
font-size:11px;
min-height:42px;
}

.moduleGrid{
display:grid;
grid-template-columns:repeat(auto-fit,minmax(220px,1fr));
gap:8px;
}

.moduleGrid div{
background:#102940;
padding:10px;
border-radius:7px;
border-left:3px solid #28c9ff;
}

.moduleGrid b{
display:block;
font-size:13px;
}

.moduleGrid span{
display:block;
color:#9ab0bf;
font-size:11px;
margin-top:4px;
}

.blueBtn{
background:#2678d9;
}

.devicePanel{
border-left:3px solid #28c9ff;
}
.deviceRow{
display:flex;
align-items:center;
flex-wrap:wrap;
gap:10px 14px;
}
.deviceRow label{color:#9ab0bf;font-size:12px;font-weight:bold;}
.deviceRow select{background:#102940;color:#fff;border:1px solid #2a5268;border-radius:6px;padding:9px 12px;min-width:150px;}
.deviceRow span{font-size:12px;font-weight:bold;padding:6px 10px;border-radius:999px;border:1px solid transparent;}
.deviceStatusConnected{color:#39ee82;background:rgba(57,238,130,.10);border-color:#39ee82;}
.deviceStatusDisconnected{color:#ff4d4d;background:rgba(255,77,77,.10);border-color:#ff4d4d;}
.addConditionBtn{border:1px dashed #28c9ff !important;color:#28c9ff;}

.waveInfo{
display:flex;
flex-wrap:wrap;
gap:8px 18px;
padding:8px 2px 2px;
color:#9ab0bf;
font-size:12px;
}

.waveInfo b{
color:#ffffff;
}

.waveTools{
display:flex;
flex-wrap:wrap;
gap:6px;
margin-bottom:8px;
}

#wave{
width:100%;
height:360px;
background:#02070b;
border:1px solid #1c3b4f;
border-radius:6px;
cursor:crosshair;
touch-action:none;
}

@media(max-width:800px){

.grid{
grid-template-columns:1fr;
}

.features{
grid-template-columns:
repeat(2,1fr);
}

canvas{
height:300px;
}

}


.advancedCards{
  display:grid;
  grid-template-columns:repeat(auto-fit,minmax(150px,1fr));
  gap:12px;
  margin-bottom:20px;
}
.advancedCards .card{
  min-height:90px;
}
</style>

</head>

<body>

<header>

<h1>
EPDI - ELECTROMECHANICAL PROXY DIAGNOSTIC INTERFACE
</h1>


</header>


<div class="container">
<div class="panel devicePanel" style="margin-bottom:12px;">
<h3>DEVICE SELECTION</h3>
<div class="deviceRow">
<label for="deviceSelect">DEVICE STATUS</label>
<select id="deviceSelect" onchange="selectDevice(this.value)">
<option value="device_1">DEVICE_1</option>
<option value="device_2">DEVICE_2</option>
<option value="device_3">DEVICE_3</option>
<option value="device_4">DEVICE_4</option>
</select>
<span id="deviceStatus" class="deviceStatusConnected">ONLINE</span>
</div>
</div>



<!-- ================================================= -->
<!-- LIVE MEASUREMENTS -->
<!-- ================================================= -->


<div class="sectionTitle">ADVANCED DIAGNOSTICS</div>
<div class="cards advancedCards">
  <div class="card"><div class="label">DC OFFSET</div><div class="value" id="dcOffset">--</div><div class="unit">V</div></div>
  <div class="card"><div class="label">PEAK VOLTAGE</div><div class="value" id="peakVoltage">--</div><div class="unit">V</div></div>
  <div class="card"><div class="label">THD</div><div class="value" id="thd">--</div><div class="unit">%</div></div>
  <div class="card"><div class="label">FUNDAMENTAL</div><div class="value" id="fundamental">--</div><div class="unit">V</div></div>
  <div class="card"><div class="label">2× HARMONIC</div><div class="value" id="harmonic2">--</div><div class="unit">V</div></div>
  <div class="card"><div class="label">3× HARMONIC</div><div class="value" id="harmonic3">--</div><div class="unit">V</div></div>
  <div class="card"><div class="label">RMS / PEAK</div><div class="value" id="rmsPeakRatio">--</div><div class="unit">ratio</div></div>
  <div class="card"><div class="label">KURTOSIS</div><div class="value" id="kurtosis">--</div><div class="unit"></div></div>
  <div class="card"><div class="label">SKEWNESS</div><div class="value" id="skewness">--</div><div class="unit"></div></div>
  <div class="card"><div class="label">ZERO CROSSINGS</div><div class="value" id="zeroCrossings">--</div><div class="unit">count</div></div>
  <div class="card"><div class="label">FREQ STABILITY</div><div class="value" id="frequencyStability">--</div><div class="unit">%</div></div>
  <div class="card"><div class="label">PHASE STABILITY</div><div class="value" id="phaseStability">--</div><div class="unit">%</div></div>
  <div class="card"><div class="label">VOLTAGE RIPPLE</div><div class="value" id="voltageRipple">--</div><div class="unit">%</div></div>
  <div class="card"><div class="label">VARIANCE</div><div class="value" id="variance">--</div><div class="unit">V²</div></div>
  <div class="card"><div class="label">SNR</div><div class="value" id="snrDb">--</div><div class="unit">dB</div></div>
  <div class="card"><div class="label">FAULT INDEX</div><div class="value" id="faultIndex">--</div><div class="unit">/100</div></div>
</div>

<div class="cards">


<div class="card">

<div class="label">
ADC
</div>

<div id="adc"
class="value blue">
---
</div>

</div>


<div class="card">

<div class="label">
VOUT
</div>

<div id="voltage"
class="value green">
---
</div>

<div class="label">
V
</div>

</div>


<div class="card">

<div class="label">
FREQUENCY
</div>

<div id="frequency"
class="value yellow">
---
</div>

<div class="label">
Hz
</div>

</div>


<div class="card">

<div class="label">
RPM
</div>

<div id="rpm"
class="value purple">
---
</div>

</div>


<div class="card">

<div class="label">
RMS
</div>

<div id="rms"
class="value orange">
---
</div>

<div class="label">
V
</div>

</div>


<div class="card">

<div class="label">
PEAK-PEAK
</div>

<div id="pp"
class="value red">
---
</div>

<div class="label">
V
</div>

</div>


<div class="card">

<div class="label">
CREST FACTOR
</div>

<div id="crest"
class="value blue">
---
</div>

</div>


</div>


<!-- ================================================= -->
<!-- WAVEFORM -->
<!-- ================================================= -->

<div class="panel"
style="margin-top:12px;">

<div class="chartHeader">

<h3>
OUTPUT WAVEFORM — MEASURED SIGNAL
</h3>

<div>

<button onclick="zoomOut()">− ZOOM OUT</button>
<button onclick="zoomIn()">+ ZOOM IN</button>
<button onclick="resetView()">RESET VIEW</button>
<button onclick="toggleHold()" id="holdBtn">HOLD</button>
<button onclick="toggleGrid()" id="gridBtn">GRID ON</button>
<button onclick="exportWaveCSV()">EXPORT CSV</button>
<button onclick="autoScale()">AUTO Y</button>

</div>

</div>


<div class="waveControls">

<button
onclick="setVertical(0.5)">
±0.5 V
</button>

<button
onclick="setVertical(1)">
±1 V
</button>

<button
onclick="setVertical(2)">
±2 V
</button>

<button
onclick="setVertical(3)">
±3 V
</button>

<button
onclick="setVertical(0)">
AUTO Y
</button>

</div>


<canvas
id="wave"
width="1200"
height="360">
</canvas>


<div class="waveInfo">
<span>VIEW: <b id="viewInfo">1× / FULL</b></span>
<span>TIME WINDOW: <b id="timeWindow">128.0 ms</b></span>
<span>CURSOR: <b id="cursorInfo">--</b></span>
<span>MIN: <b id="minInfo">--</b></span>
<span>MAX: <b id="maxInfo">--</b></span>
<span>PK-PK: <b id="viewPP">--</b></span>
</div>

<p style="color:#8da6b9;font-size:12px;">
Mouse wheel = zoom | Drag = pan | Click = measurement cursor | Green = measured signal | Gray = zero/reference center line
</p>

</div>


<!-- ================================================= -->
<!-- HEALTH -->
<!-- ================================================= -->

<div class="grid">


<div class="panel">

<h3>
SIGNAL FEATURES
</h3>


<div class="features">


<div class="feature">

<div class="label">
RMS
</div>

<b id="fRms">
---
</b>

</div>


<div class="feature">

<div class="label">
PEAK-PEAK
</div>

<b id="fPP">
---
</b>

</div>


<div class="feature">

<div class="label">
CREST FACTOR
</div>

<b id="fCrest">
---
</b>

</div>


<div class="feature">

<div class="label">
FREQUENCY
</div>

<b id="fFreq">
---
</b>

</div>


<div class="feature">

<div class="label">
RPM
</div>

<b id="fRPM">
---
</b>

</div>


<div class="feature">

<div class="label">
POLE PAIRS
</div>

<b>
7
</b>

</div>


</div>

</div>


<div class="panel health">

<h3>
MACHINE HEALTH
</h3>


<div
id="health"
class="healthNumber">
---
</div>

<div>
/ 100
</div>


<div
id="fault"
class="fault">
WAITING
</div>


</div>


</div>


<!-- ================================================= -->
<!-- EPDI DIAGNOSTIC COVERAGE -->
<!-- ================================================= -->

<div class="grid" style="margin-top:12px;">

<div class="panel">

<h3>EPDI DIAGNOSTIC COVERAGE</h3>

<p style="color:#9ab0bf;font-size:12px;line-height:1.5;">
Representative operating conditions from the diagnostic framework are available as individually selectable training profiles.
The existing EPDI project-specific belt and pulley fault classes are retained.
</p>

<div class="conditionGrid" id="conditionGrid">

<button onclick="condition('NORMAL')">NORMAL</button>
<button onclick="condition('IMBALANCE')">IMBALANCE</button>
<button onclick="condition('MISALIGNMENT')">MISALIGNMENT</button>
<button onclick="condition('BEARING DEGRADATION')">BEARING DEGRADATION</button>
<button onclick="condition('GEAR DEFECT')">GEAR DEFECT</button>
<button onclick="condition('CAVITATION')">CAVITATION</button>
<button onclick="condition('OVERLOAD')">OVERLOAD</button>
<button onclick="condition('LUBRICATION DEFICIENCY')">LUBRICATION DEFICIENCY</button>
<button onclick="condition('STRUCTURAL DEGRADATION')">STRUCTURAL DEGRADATION</button>
<button onclick="condition('PROGRESSIVE WEAR')">PROGRESSIVE WEAR</button>
<button onclick="condition('INCIPIENT FAILURE')">INCIPIENT FAILURE</button>
<button onclick="condition('BELT DAMAGE')">BELT DAMAGE</button>
<button onclick="condition('PULLEY DAMAGE')">PULLEY DAMAGE</button>
<button onclick="condition('BELT + PULLEY')">BELT + PULLEY</button>
<button class="addConditionBtn" onclick="addOtherCondition()">＋ OTHER / ADD OPTION</button>

</div>

<div class="recordStatus">
Selected condition: <b id="condition">NORMAL</b><br>
Stored profiles: <b id="storedProfiles">0</b> / 20<br>
Test result: <b id="testResult">NOT TESTED</b><br>
Match score: <b id="testScore">0%</b>
</div>

</div>

<div class="panel">

<h3>EXPERIMENT & DATA CONTROL</h3>

<div class="features">
<div class="feature"><div class="label">PLATFORM</div><b>ERP-1</b></div>
<div class="feature"><div class="label">WORKBENCH</div><b>ERW</b></div>
<div class="feature"><div class="label">INTERFACE</div><b>SW-001</b></div>
<div class="feature"><div class="label">SAMPLE RATE</div><b>4 kS/s</b></div>
<div class="feature"><div class="label">WINDOW</div><b>128 ms</b></div>
<div class="feature"><div class="label">POLE PAIRS</div><b>7</b></div>
</div>

<br>

<button class="purpleBtn" onclick="baseline()">SAVE NORMAL BASELINE</button>
<button class="blueBtn" onclick="saveCondition()">SAVE CONDITION</button>
<button class="greenBtn" onclick="startRecording()">START RECORDING</button>
<button class="redBtn" onclick="stopRecording()">STOP</button>
<button onclick="testSignal()">TEST CURRENT SIGNAL</button>
<button onclick="virtualReset()">RESET</button>
<button class="redBtn" onclick="clearAll()">CLEAR ALL PROFILES</button>

<div class="recordStatus">
Recording: <b id="recording">STOPPED</b><br>
Recorded samples: <b id="samples">0</b>
</div>

</div>

</div>


<script>

// ===================================================
// CANVAS
// ===================================================

const canvas =
document.getElementById("wave");

const ctx =
canvas.getContext("2d");


// ===================================================
// WAVEFORM SETTINGS & INTERACTION
// ===================================================

let currentData = [];
let viewMultiplier = 1;
let displayMode = "sine"; // EPDI always displays the measured signal as one clean sine wave
let lastFrequency = 0;
let fixedRange = 0;
let viewCenter = 0.5;
let holdWaveform = false;
let showGrid = true;
let activeDevice = "device_1";
let cursorX = -1;
let dragActive = false;
let dragStartX = 0;
let dragStartCenter = 0.5;
setTimeout(()=>setDisplayMode("sine"),0);

function clamp(v, a, b){ return Math.max(a, Math.min(b, v)); }

function setView(multiplier)
{
    viewMultiplier = clamp(multiplier, 0.03125, 16);
    viewCenter = 0.5;
    drawWave(currentData);
}

function zoomIn()
{
    setView(Math.min(16, viewMultiplier * 2));
}

function zoomOut()
{
    setView(Math.max(0.03125, viewMultiplier / 2));
}

function resetView()
{
    viewMultiplier = 1;
    viewCenter = 0.5;
    cursorX = -1;
    drawWave(currentData);
}


function fitSine(data, frequency)
{
    if(!data || data.length < 8 || !frequency || frequency <= 0) return null;

    let mean = 0;
    for(let i=0;i<data.length;i++) mean += Number(data[i]);
    mean /= data.length;

    const w = 2*Math.PI*frequency/4000.0;
    let ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
    for(let i=0;i<data.length;i++){
        const si = Math.sin(w*i);
        const co = Math.cos(w*i);
        const y = Number(data[i]) - mean;
        ss += si*si; cc += co*co; sc += si*co;
        ys += y*si; yc += y*co;
    }

    const det = ss*cc-sc*sc;
    if(Math.abs(det) < 1e-9) return null;

    const a = (ys*cc-yc*sc)/det;
    const b = (yc*ss-ys*sc)/det;
    let amplitude = Math.sqrt(a*a+b*b);

    // For a rectified/clamped input, use the measured peak-to-peak
    // amplitude when the correlation fit becomes too small.
    let measuredMin = Number(data[0]), measuredMax = Number(data[0]);
    for(let i=1;i<data.length;i++){
        const v=Number(data[i]);
        if(v<measuredMin) measuredMin=v;
        if(v>measuredMax) measuredMax=v;
    }
    const measuredAmp = Math.max(0.005,(measuredMax-measuredMin)/2);
    if(amplitude < measuredAmp*0.35) amplitude = measuredAmp;
    amplitude = Math.max(amplitude, measuredAmp);

    const phase = Math.atan2(b,a);
    return {offset:mean, amplitude:amplitude, phase:phase, frequency:frequency};
}

function sineValue(model, tSeconds)
{
    return model.amplitude*Math.sin(2*Math.PI*model.frequency*tSeconds + model.phase);
}

function toggleHold()
{
    holdWaveform = !holdWaveform;
    document.getElementById("holdBtn").innerText = holdWaveform ? "RESUME" : "HOLD";
}

function toggleGrid()
{
    showGrid = !showGrid;
    document.getElementById("gridBtn").innerText = showGrid ? "GRID ON" : "GRID OFF";
    drawWave(currentData);
}

function setVertical(range)
{
    fixedRange = range;
    drawWave(currentData);
}

function autoScale()
{
    // AUTO Y returns the waveform to the initial oscilloscope view.
    // It restores the original zoom, center position and automatic vertical scale.
    fixedRange = 0;
    viewMultiplier = 1;
    viewCenter = 0.5;
    cursorX = -1;

    if(activeDevice !== "device_1") {
        clearWaveNoSignal();
        return;
    }

    if(currentData && currentData.length >= 2)
        drawWave(currentData, lastFrequency);
    else
        clearWaveNoSignal();
}

function exportWaveCSV()
{
    if(!currentData || currentData.length < 2){ alert("No waveform data available"); return; }
    const sampleRate = 4000;
    let csv = "EPDI Waveform Export\nSample Rate (Hz)," + sampleRate + "\nTime (ms),Voltage (V)\n";
    for(let i=0;i<currentData.length;i++)
        csv += ((i/sampleRate)*1000).toFixed(4) + "," + Number(currentData[i]).toFixed(6) + "\n";
    const blob = new Blob([csv], {type:"text/csv"});
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "EPDI_waveform.csv";
    a.click();
    setTimeout(()=>URL.revokeObjectURL(a.href),1000);
}

function getViewData(data)
{
    if(!data || data.length < 2) return [];
    const n = data.length;
    const span = Math.min(1, 1 / viewMultiplier);
    let start = viewMultiplier >= 1 ? Math.round((viewCenter - span/2) * (n-1)) : 0;
    let end   = viewMultiplier >= 1 ? Math.round((viewCenter + span/2) * (n-1)) : n-1;
    start = clamp(start,0,n-2);
    end = clamp(end,start+1,n-1);
    return data.slice(start,end+1);
}

function clearWaveNoSignal()
{
    currentData = [];
    lastFrequency = 0;
    const W = canvas.width, H = canvas.height;
    const TOP = 35, BOTTOM = 30;
    const GRAPH_HEIGHT = H - TOP - BOTTOM;

    ctx.clearRect(0, 0, W, H);

    // Keep the oscilloscope clean when no signal is present; no warning text is shown.
    if(showGrid){
        ctx.lineWidth = 1;
        ctx.strokeStyle = "#17364a";
        for(let i=0;i<=8;i++){
            let y = TOP + GRAPH_HEIGHT*i/8;
            ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(W,y); ctx.stroke();
        }
        for(let i=0;i<=10;i++){
            let x = W*i/10;
            ctx.beginPath(); ctx.moveTo(x,TOP); ctx.lineTo(x,H-BOTTOM); ctx.stroke();
        }
    }

    const centerY = H/2;
    ctx.strokeStyle = "#627887";
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(0,centerY); ctx.lineTo(W,centerY); ctx.stroke();


}

function drawWave(data, frequency)
{
    if(!data || data.length < 2) { clearWaveNoSignal(); return; }
    currentData = data;
    if(frequency && frequency > 0) lastFrequency = frequency;

    const visible = getViewData(data);
    if(visible.length < 2) return;

    const W = canvas.width, H = canvas.height;
    const TOP = 35, BOTTOM = 30, GRAPH_HEIGHT = H - TOP - BOTTOM;
    ctx.clearRect(0,0,W,H);

    const rawMin = Math.min(...visible), rawMax = Math.max(...visible);
    const rawCenter = (rawMax + rawMin)/2;
    const rawAmplitude = Math.max(0.02,(rawMax-rawMin)/2);
    const model = fitSine(data,lastFrequency);

    // Build the display signal. The ONE displayed waveform is a clean sine representation fitted from the
    // measured signal. The underlying raw ADC samples remain unchanged for diagnostics.
    let plotMin, plotMax, center, amplitude;
    if(displayMode === "sine" && model){
        center = 0;
        amplitude = model.amplitude;
        plotMin = center-amplitude;
        plotMax = center+amplitude;
    } else {
        center = rawCenter;
        amplitude = rawAmplitude;
        plotMin = rawMin;
        plotMax = rawMax;
    }

    if(fixedRange > 0) amplitude = fixedRange;
    else amplitude *= 1.15;

    if(showGrid){
        ctx.lineWidth=1; ctx.strokeStyle="#17364a";
        for(let i=0;i<=8;i++){let y=TOP+GRAPH_HEIGHT*i/8;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(W,y);ctx.stroke();}
        for(let i=0;i<=10;i++){let x=W*i/10;ctx.beginPath();ctx.moveTo(x,TOP);ctx.lineTo(x,H-BOTTOM);ctx.stroke();}
    }

    const centerY=H/2;
    ctx.strokeStyle="#627887";ctx.lineWidth=1;
    ctx.beginPath();ctx.moveTo(0,centerY);ctx.lineTo(W,centerY);ctx.stroke();

    function yFromValue(v){
        let normalized=(v-center)/amplitude;
        normalized=clamp(normalized,-1,1);
        return centerY-normalized*(GRAPH_HEIGHT/2);
    }

    // Draw ideal sine across the complete visible time window.
    if(displayMode === "sine" && model){
        const totalSeconds=data.length/4000.0;
        let startFraction=viewMultiplier>=1 ? clamp(viewCenter-0.5/viewMultiplier,0,1) : 0;
        let endFraction=viewMultiplier>=1 ? clamp(viewCenter+0.5/viewMultiplier,0,1) : 1;
        const startTime=startFraction*totalSeconds;
        const endTime=endFraction*totalSeconds;

        // At zoom-out levels, extend the mathematically fitted sine beyond
        // the captured 128 ms buffer so the user can see many complete cycles.
        const displayWindowSeconds=totalSeconds/viewMultiplier;
        const baseStart=viewMultiplier<1 ? 0 : startTime;
        const baseEnd=viewMultiplier<1 ? displayWindowSeconds : endTime;

        ctx.beginPath();
        const samples=Math.max(1200,Math.min(5000,W*4));
        for(let k=0;k<=samples;k++){
            const t=baseStart+(baseEnd-baseStart)*k/samples;
            const y=yFromValue(sineValue(model,t));
            const x=W*k/samples;
            if(k===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
        }
        ctx.strokeStyle="#35ed83";ctx.lineWidth=3;ctx.lineJoin="round";ctx.lineCap="round";ctx.stroke();

        // Overlay a very subtle raw trace only when zoomed in, to show the
        // relationship between measured data and the fitted sine.
        if(viewMultiplier>=1){
            const step=W/(visible.length-1);
            ctx.beginPath();
            for(let i=0;i<visible.length;i++){
                const x=i*step, y=yFromValue(visible[i]);
                if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
            }
            ctx.strokeStyle="rgba(170,180,190,0.28)";ctx.lineWidth=1;ctx.stroke();
        }
    } else {
        // Smooth raw measured signal for diagnostic inspection.
        function getY(i){return yFromValue(visible[clamp(i,0,visible.length-1)]);}
        function catmullRom(p0,p1,p2,p3,t){return 0.5*(2*p1+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t*t+(-p0+3*p1-3*p2+p3)*t*t*t);}
        ctx.beginPath();
        const points=visible.length, step=W/(points-1), subdivisions=8;
        for(let i=0;i<points-1;i++){
            const p0=getY(i-1),p1=getY(i),p2=getY(i+1),p3=getY(i+2);
            for(let j=0;j<subdivisions;j++){
                const t=j/subdivisions,y=catmullRom(p0,p1,p2,p3,t),x=(i+t)*step;
                if(i===0&&j===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
            }
        }
        ctx.lineTo(W,getY(points-1));
        ctx.strokeStyle="#35ed83";ctx.lineWidth=3;ctx.lineJoin="round";ctx.lineCap="round";ctx.stroke();
    }

    // Reference peak/minimum markers from the measured signal.
    let maxI=0,minI=0;
    for(let i=1;i<visible.length;i++){if(visible[i]>visible[maxI])maxI=i;if(visible[i]<visible[minI])minI=i;}
    const markerStep=W/(visible.length-1);
    ctx.setLineDash([5,5]);
    ctx.strokeStyle="#e9c46a";ctx.beginPath();ctx.moveTo(maxI*markerStep,TOP);ctx.lineTo(maxI*markerStep,H-BOTTOM);ctx.stroke();
    ctx.strokeStyle="#d88bff";ctx.beginPath();ctx.moveTo(minI*markerStep,TOP);ctx.lineTo(minI*markerStep,H-BOTTOM);ctx.stroke();ctx.setLineDash([]);

    if(cursorX>=0){
        ctx.strokeStyle="#fff";ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(cursorX,TOP);ctx.lineTo(cursorX,H-BOTTOM);ctx.stroke();
        const ci=Math.round(cursorX/W*(visible.length-1));
        const cv=visible[clamp(ci,0,visible.length-1)];
        const totalMs=data.length/4000*1000;
        const spanMs=totalMs/Math.max(viewMultiplier,1);
        const timeMs=(Math.max(0,viewCenter-1/(2*Math.max(viewMultiplier,1)))*totalMs)+(ci/(visible.length-1))*spanMs;
        document.getElementById("cursorInfo").innerText=timeMs.toFixed(2)+" ms / "+cv.toFixed(4)+" V";
    }else document.getElementById("cursorInfo").innerText="--";

    ctx.fillStyle="#9ab0bf";ctx.font="13px Arial";
    ctx.fillText((center+amplitude).toFixed(2)+" V",8,20);
    ctx.fillText(center.toFixed(2)+" V",8,centerY-6);
    ctx.fillText((center-amplitude).toFixed(2)+" V",8,H-8);
    ctx.fillText((displayMode==="sine"?"FULL SINE ":"RAW ")+" ZOOM "+viewMultiplier.toFixed(3)+"×",W-190,20);

    document.getElementById("viewInfo").innerText=viewMultiplier.toFixed(3)+"×"+(viewMultiplier===1?" / FULL":(viewMultiplier<1?" / ZOOM OUT":" / PAN"));
    document.getElementById("timeWindow").innerText=(data.length/4000*1000/viewMultiplier).toFixed(2)+" ms";
    if(displayMode==="sine" && model){
        document.getElementById("minInfo").innerText=(-model.amplitude).toFixed(4)+" V";
        document.getElementById("maxInfo").innerText=(model.amplitude).toFixed(4)+" V";
        document.getElementById("viewPP").innerText=(2*model.amplitude).toFixed(4)+" V";
    }else{
        document.getElementById("minInfo").innerText=rawMin.toFixed(4)+" V";
        document.getElementById("maxInfo").innerText=rawMax.toFixed(4)+" V";
        document.getElementById("viewPP").innerText=(rawMax-rawMin).toFixed(4)+" V";
    }
}

canvas.addEventListener("wheel", function(e){
    e.preventDefault();
    if(e.deltaY < 0) zoomIn(); else zoomOut();
});

canvas.addEventListener("mousedown", function(e){
    const r=canvas.getBoundingClientRect();
    dragActive=true;
    dragStartX=e.clientX-r.left;
    dragStartCenter=viewCenter;
    cursorX=dragStartX;
    drawWave(currentData);
});

canvas.addEventListener("mousemove", function(e){
    if(!currentData.length) return;
    const r=canvas.getBoundingClientRect();
    const x=clamp(e.clientX-r.left,0,canvas.width);
    if(dragActive && viewMultiplier>1){
        const delta=(x-dragStartX)/canvas.width*(1/viewMultiplier);
        viewCenter=clamp(dragStartCenter-delta,1/(2*viewMultiplier),1-1/(2*viewMultiplier));
    }
    cursorX=x;
    drawWave(currentData);
});

canvas.addEventListener("mouseup",()=>dragActive=false);
canvas.addEventListener("mouseleave",()=>dragActive=false);

// ===================================================
// ADVANCED DIAGNOSTIC DISPLAY
// ===================================================

function updateAdvancedDiagnostics(d) {
  const vals = {
    dcOffset: d.dcOffset,
    peakVoltage: d.peakVoltage,
    thd: d.thd,
    fundamental: d.fundamental,
    harmonic2: d.harmonic2,
    harmonic3: d.harmonic3,
    rmsPeakRatio: d.rmsPeakRatio,
    kurtosis: d.kurtosis,
    skewness: d.skewness,
    zeroCrossings: d.zeroCrossings,
    frequencyStability: d.frequencyStability,
    phaseStability: d.phaseStability,
    voltageRipple: d.voltageRipple,
    variance: d.variance,
    snrDb: d.snrDb,
    faultIndex: d.faultIndex
  };
  Object.keys(vals).forEach(k => {
    const el = document.getElementById(k);
    if (el) el.textContent = (vals[k] === undefined || vals[k] === null) ? '--' : vals[k];
  });
}

async function updateData()
{
    try
    {

        let response =
            await fetch(
                "/data"
            );


        let d =
            await response.json();

        // =========================================
        // ADVANCED DIAGNOSTIC VALUES
        // =========================================
        updateAdvancedDiagnostics(d);

        // =========================================
        // BASIC VALUES
        // =========================================

        document
        .getElementById("adc")
        .innerText =
            d.adc;


        document
        .getElementById("voltage")
        .innerText =
            d.adcVoltage
            .toFixed(2);


        document
        .getElementById("frequency")
        .innerText =
            d.frequency
            .toFixed(1);


        document
        .getElementById("rpm")
        .innerText =
            Math.round(
                d.rpm
            );


        document
        .getElementById("rms")
        .innerText =
            d.rms
            .toFixed(3);


        document
        .getElementById("pp")
        .innerText =
            d.peakToPeak
            .toFixed(3);


        document
        .getElementById("crest")
        .innerText =
            d.crest
            .toFixed(2);


        // =========================================
        // FEATURES
        // =========================================

        document
        .getElementById("fRms")
        .innerText =
            d.rms.toFixed(3)
            + " V";


        document
        .getElementById("fPP")
        .innerText =
            d.peakToPeak.toFixed(3)
            + " V";


        document
        .getElementById("fCrest")
        .innerText =
            d.crest.toFixed(2);


        document
        .getElementById("fFreq")
        .innerText =
            d.frequency.toFixed(1)
            + " Hz";


        document
        .getElementById("fRPM")
        .innerText =
            Math.round(
                d.rpm
            );


        // =========================================
        // HEALTH
        // =========================================

        document
        .getElementById("health")
        .innerText =
            d.health;


        document
        .getElementById("fault")
        .innerText =
            d.fault;


        // =========================================
        // CONDITION
        // =========================================

        document
        .getElementById("condition")
        .innerText =
            d.condition;


        document
        .getElementById("samples")
        .innerText =
            d.samples;


        document
        .getElementById("storedProfiles")
        .innerText =
            d.storedProfiles;

        document
        .getElementById("testResult")
        .innerText =
            d.testResult;

        document
        .getElementById("testScore")
        .innerText =
            d.testScore.toFixed(1) + "%";

        document
        .getElementById("recording")
        .innerText =
            d.recording ? "RECORDING" : "STOPPED";


        // =========================================
        // WAVEFORM
        // =========================================

        if(!holdWaveform)
        {
            // IMPORTANT: never keep the previous sine visible after the
            // machine/signal has stopped. Frequency is the primary signal
            // presence indicator; RMS also rejects a near-zero input.
            const signalPresent =
                Number(d.frequency) >= 1.0 &&
                Number(d.rms) >= 0.01 &&
                Array.isArray(d.wave) &&
                d.wave.length >= 2;

            // Display live waveform/details only for the currently connected DEVICE_1.
            if(activeDevice === "device_1" && signalPresent)
                drawWave(d.wave, d.frequency);
            else
                clearWaveNoSignal();
        }

    }

    catch(error)
    {

        document
        .getElementById("fault")
        .innerText =
            "CONNECTION ERROR";

    }

}


// ===================================================
// DEVICE SELECTION
// ===================================================
function updateDeviceVisibility()
{
    // DEVICE_1 is the only currently connected acquisition device.
    // Keep the device selector visible, but hide every other dashboard panel
    // whenever another device slot is selected.
    document.querySelectorAll(".panel:not(.devicePanel)").forEach(function(panel){
        panel.style.display = (activeDevice === "device_1") ? "" : "none";
    });

    // Also hide the standalone diagnostic/data blocks that are not wrapped in
    // a .panel in older EPDI layouts.
    const ids = [
        "adc","voltage","frequency","rpm","rms","pp","crest",
        "health","fault","condition","storedProfiles","testResult",
        "testScore","recording","samples"
    ];
    ids.forEach(function(id){
        const el = document.getElementById(id);
        if(el) {
            const holder = el.closest(".panel");
            if(!holder) el.style.display = (activeDevice === "device_1") ? "" : "none";
        }
    });
}

function selectDevice(device)
{
    activeDevice = device;

    // Device selection itself always remains visible.
    if(activeDevice === "device_1") {
        document.getElementById("deviceStatus").innerText = "ONLINE";
        document.getElementById("deviceStatus").className = "deviceStatusConnected";

        // Restore the initial waveform view when DEVICE_1 is selected.
        viewMultiplier = 1;
        viewCenter = 0.5;
        fixedRange = 0;
        cursorX = -1;

        updateDeviceVisibility();

        if(currentData && currentData.length >= 2)
            drawWave(currentData, lastFrequency);
        else
            clearWaveNoSignal();
    } else {
        document.getElementById("deviceStatus").innerText =
            device.toUpperCase() + " OFFLINE";
        document.getElementById("deviceStatus").className = "deviceStatusDisconnected";

        // Immediately remove waveform and all device-specific details.
        currentData = [];
        lastFrequency = 0;
        updateDeviceVisibility();
        clearWaveNoSignal();
    }
}

// Apply DEVICE_1 visibility on initial page load.
setTimeout(updateDeviceVisibility, 0);

// ===================================================
// CUSTOM DIAGNOSTIC OPTIONS
// ===================================================
async function addOtherCondition()
{
    const name = prompt("Enter a new EPDI diagnostic condition:");
    if(!name) return;

    const clean = name.trim().replace(/\s+/g, " ").toUpperCase();
    if(clean.length < 2) return;

    const response = await fetch("/addCondition?name=" + encodeURIComponent(clean));
    const result = await response.text();
    if(!response.ok){ alert(result || "Unable to add diagnostic option"); return; }
    if(result === "Condition already exists"){ alert(result); return; }

    const grid = document.getElementById("conditionGrid");
    const btn = document.createElement("button");
    btn.innerText = clean;
    btn.onclick = () => condition(clean);
    const addBtn = grid.querySelector(".addConditionBtn");
    grid.insertBefore(btn, addBtn);
    condition(clean);
}

// ===================================================
// CONDITION
// ===================================================

async function condition(name)
{

    await fetch(
        "/condition?name=" +
        encodeURIComponent(name)
    );


    document
    .getElementById("condition")
    .innerText =
        name;

}


// ===================================================
// SAVE BASELINE
// ===================================================

async function baseline()
{

    await fetch(
        "/baseline"
    );


    alert(
        "Normal baseline saved"
    );

}


// ===================================================
// START RECORDING
// ===================================================

async function startRecording()
{

    await fetch(
        "/start"
    );

}


// ===================================================
// STOP RECORDING
// ===================================================

async function stopRecording()
{

    await fetch(
        "/stop"
    );

}


// ===================================================
// SAVE CONDITION PROFILE
// ===================================================

async function saveCondition()
{
    await fetch("/saveCondition");
    alert("Saved profile: " + document.getElementById("condition").innerText);
}

// ===================================================
// TEST CURRENT SIGNAL
// ===================================================

async function testSignal()
{
    const response = await fetch("/test");
    const result = await response.text();
    alert("EPDI TEST RESULT: " + result);
    updateData();
}

// ===================================================
// VIRTUAL RESET
// ===================================================

async function virtualReset()
{
    await fetch("/reset");
    updateData();
}

// ===================================================
// CLEAR ALL PROFILES
// ===================================================

async function clearAll()
{
    if(!confirm("Clear all stored condition profiles and baseline?"))
        return;

    await fetch("/clearAll");
    updateData();
}

// ===================================================
// UPDATE EVERY SECOND
// ===================================================

setInterval(
    updateData,
    1000
);


updateData();

</script>

</body>

</html>

)rawliteral";


  server.send(
    200,
    "text/html",
    html
  );
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();

  Serial.println(
    "================================"
  );

  Serial.println(
    EPDI_NAME
  );

  Serial.println(
    "================================"
  );


  // =================================================
  // PERSISTENT EPDI PROFILE STORAGE
  // =================================================

  epdiPrefs.begin("epdi", false);
  loadProfiles();


  // =================================================
  // ADC
  // =================================================

  analogReadResolution(12);

  analogSetPinAttenuation(
    ADC_PIN,
    ADC_11db
  );


  // =================================================
  // WIFI
  // =================================================

  WiFi.mode(
    WIFI_STA
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );


  Serial.print(
    "Connecting to WiFi"
  );


  while(
    WiFi.status() !=
    WL_CONNECTED
  )
  {
    delay(500);

    Serial.print(".");
  }


  Serial.println();

  Serial.println(
    "WiFi connected"
  );


  Serial.print(
    "IP Address: "
  );


  Serial.println(
    WiFi.localIP()
  );


  // =================================================
  // WEB ROUTES
  // =================================================

  server.on(
    "/",
    webpage
  );


  server.on(
    "/data",
    sendData
  );


  server.on(
    "/start",
    startRecording
  );


  server.on(
    "/stop",
    stopRecording
  );


  server.on(
    "/condition",
    setCondition
  );

  server.on(
    "/addCondition",
    addCustomCondition
  );


  server.on(
    "/baseline",
    baselineRequest
  );


  server.on(
    "/saveCondition",
    saveConditionRequest
  );


  server.on(
    "/test",
    testRequest
  );


  server.on(
    "/reset",
    resetRequest
  );


  server.on(
    "/clearAll",
    clearAllRequest
  );


  server.begin();


  Serial.println(
    "Web server started"
  );


  Serial.println(
    "System ready"
  );
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  server.handleClient();

  delay(2);
}