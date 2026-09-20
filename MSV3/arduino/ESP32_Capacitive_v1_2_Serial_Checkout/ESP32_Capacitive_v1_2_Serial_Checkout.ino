/*
  Serial-only checkout for a Capacitive Soil Moisture Sensor v1.2.

  Wiring (ESP32-S3):
    VCC  -> 3V3 (never 5V/VIN)
    GND  -> GND
    AOUT -> GPIO4 / IO4 (ADC1_CH3)

  Lower millivolts means wetter. No BLE is initialized, so this checkout cannot
  collide with an existing probe identity or write test data into cloud history.
*/

const int SENSE_PIN = 4;
const uint8_t SAMPLE_COUNT = 5;
const uint32_t SAMPLE_SPACING_MS = 2;
const uint32_t READING_PERIOD_MS = 500;

float smoothedMv = -1.0f;
uint16_t sessionMinMv = 0xFFFF;
uint16_t sessionMaxMv = 0;

uint16_t medianOf(uint16_t values[], uint8_t count)
{
  for (uint8_t i = 1; i < count; ++i)
  {
    uint16_t key = values[i];
    int8_t j = i - 1;
    while (j >= 0 && values[j] > key)
    {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = key;
  }
  return values[count / 2];
}

void setup()
{
  Serial.begin(115200);
  uint32_t waitStart = millis();
  while (!Serial && millis() - waitStart < 3000)
  {
    delay(10);
  }

  analogReadResolution(12);
  analogSetPinAttenuation(SENSE_PIN, ADC_11db);

  Serial.println();
  Serial.println("Capacitive Soil Moisture Sensor v1.2 - serial checkout");
  Serial.printf("Chip: %s rev%d, GPIO%d ADC, 3.3V supply expected\n",
                ESP.getChipModel(), ESP.getChipRevision(), SENSE_PIN);
  Serial.println("Lower mV = wetter; touch/wet tests should cause a clear drop");
  Serial.println("median_mV,smoothed_mV,session_min_mV,session_max_mV,session_span_mV");
}

void loop()
{
  uint16_t samples[SAMPLE_COUNT];
  for (uint8_t i = 0; i < SAMPLE_COUNT; ++i)
  {
    samples[i] = analogReadMilliVolts(SENSE_PIN);
    if (i + 1 < SAMPLE_COUNT)
    {
      delay(SAMPLE_SPACING_MS);
    }
  }

  uint16_t medianMv = medianOf(samples, SAMPLE_COUNT);
  if (smoothedMv < 0.0f)
  {
    smoothedMv = medianMv;
  }
  else
  {
    smoothedMv += 0.2f * ((float)medianMv - smoothedMv);
  }

  if (medianMv < sessionMinMv) sessionMinMv = medianMv;
  if (medianMv > sessionMaxMv) sessionMaxMv = medianMv;

  Serial.printf("%u,%.1f,%u,%u,%u\n",
                medianMv,
                smoothedMv,
                sessionMinMv,
                sessionMaxMv,
                (uint16_t)(sessionMaxMv - sessionMinMv));

  delay(READING_PERIOD_MS);
}
