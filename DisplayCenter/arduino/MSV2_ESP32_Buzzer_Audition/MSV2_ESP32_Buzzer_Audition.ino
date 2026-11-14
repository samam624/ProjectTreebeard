// Standalone buzzer audition firmware for the MSV2 Display Center PCB.
// Target: ESP32-S3-WROOM-1U-N16R8, buzzer LS1 on GPIO38 through Q3.
//
// The original TDK piezo has been replaced by a two-pin passive/resistive
// sounder. Its exact resonant frequency and SPL are unknown, so this sketch
// auditions the whole human-audible span. It never applies sustained DC and
// keeps the high-duty tests short to limit sounder and transistor heating.
//
// Locked sound behavior at a quiet 4% duty:
//   - Startup: candidate 1, the soft rising three-note chime.
//   - Plant needs water: candidate 4, the minimal double buzz.
// This standalone build emulates startup followed three seconds later by a
// three-repeat water warning, then repeats that warning once per minute.
// All audition sounds and tests remain in the serial menu.
//
// Open a 115200-baud serial terminal for repeatable, interactive testing.

#include <Arduino.h>

namespace {

constexpr uint8_t BUZZER_PIN = 38;
constexpr uint8_t LED0_PIN = 40;
constexpr uint8_t LED1_PIN = 41;
constexpr uint8_t LED2_PIN = 42;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
constexpr uint16_t PWM_MAX = (1U << PWM_RESOLUTION_BITS) - 1U;

// The useful square-wave volume range is 1..50%. Above 50% mirrors the AC
// content below 50%, does not get louder, and adds more DC/heating.
constexpr uint8_t MIN_DUTY_PERCENT = 1;
constexpr uint8_t MAX_DUTY_PERCENT = 50;
constexpr uint32_t MIN_TEST_FREQUENCY_HZ = 100;
constexpr uint32_t MAX_TEST_FREQUENCY_HZ = 20000;

// Locked-in starting point: deliberately short and well below full drive.
// Change these three constants after choosing a favorite from the tour.
constexpr uint8_t NOTIFICATION_DUTY_PERCENT = 4;
constexpr uint32_t NOTIFICATION_NOTE_1_HZ = 2600;
constexpr uint32_t NOTIFICATION_NOTE_2_HZ = 3200;
constexpr uint32_t WATER_WARNING_INTERVAL_MS = 60000;
constexpr uint16_t WATER_WARNING_REPEAT_GAP_MS = 350;
constexpr uint8_t WATER_WARNING_REPEAT_COUNT = 3;
// 48 beats at 300 ms = 14.4 seconds for the complete melody.
constexpr uint16_t SONG_BEAT_MS = 300;

struct SongNote {
  uint16_t frequencyHz;
  uint8_t beats;
};

struct ToneStep {
  uint16_t frequencyHz;
  uint16_t durationMs;
  uint16_t gapAfterMs;
};

uint32_t selectedFrequencyHz = NOTIFICATION_NOTE_2_HZ;
uint8_t selectedDutyPercent = NOTIFICATION_DUTY_PERCENT;
uint32_t nextWaterWarningMs = 0;

uint16_t dutyPercentToRaw(uint8_t dutyPercent) {
  return static_cast<uint16_t>((static_cast<uint32_t>(PWM_MAX) * dutyPercent + 50U) / 100U);
}

void silenceBuzzer() {
  ledcWrite(BUZZER_PIN, 0);
}

bool setFrequency(uint32_t frequencyHz) {
  return ledcChangeFrequency(BUZZER_PIN, frequencyHz, PWM_RESOLUTION_BITS) != 0;
}

void playTone(uint32_t frequencyHz, uint8_t dutyPercent, uint32_t durationMs) {
  frequencyHz = constrain(frequencyHz, MIN_TEST_FREQUENCY_HZ, MAX_TEST_FREQUENCY_HZ);
  dutyPercent = constrain(dutyPercent, MIN_DUTY_PERCENT, MAX_DUTY_PERCENT);
  durationMs = constrain(durationMs, 10UL, 2000UL);

  if (!setFrequency(frequencyHz)) {
    Serial.printf("ERROR: LEDC rejected %lu Hz\n", static_cast<unsigned long>(frequencyHz));
    silenceBuzzer();
    return;
  }

  Serial.printf("TONE frequency=%lu Hz, duty=%u%%, duration=%lu ms\n",
                static_cast<unsigned long>(frequencyHz), dutyPercent,
                static_cast<unsigned long>(durationMs));
  ledcWrite(BUZZER_PIN, dutyPercentToRaw(dutyPercent));
  delay(durationMs);
  silenceBuzzer();
}

void playNotification() {
  Serial.println("NOTIFICATION: 2600 -> 3200 Hz, 4% duty");
  playTone(NOTIFICATION_NOTE_1_HZ, NOTIFICATION_DUTY_PERCENT, 55);
  delay(45);
  playTone(NOTIFICATION_NOTE_2_HZ, NOTIFICATION_DUTY_PERCENT, 75);
}

void waitUntil(uint32_t deadlineMs) {
  while (static_cast<int32_t>(deadlineMs - millis()) > 0) {
    delay(1);
  }
}

void playSongNote(uint32_t frequencyHz, uint32_t soundingMs, uint32_t totalMs,
                  uint32_t &deadlineMs) {
  // Do not print from this real-time path. USB CDC writes can block when the
  // upload connection closes, stretching every note by an unpredictable time.
  if (!setFrequency(frequencyHz)) {
    silenceBuzzer();
    deadlineMs += totalMs;
    waitUntil(deadlineMs);
    return;
  }

  ledcWrite(BUZZER_PIN, dutyPercentToRaw(NOTIFICATION_DUTY_PERCENT));
  deadlineMs += soundingMs;
  waitUntil(deadlineMs);
  silenceBuzzer();
  deadlineMs += totalMs - soundingMs;
  waitUntil(deadlineMs);
}

void playSong() {
  // "Twinkle, Twinkle, Little Star" in C major. The traditional melody and
  // the score used to transcribe it are public domain:
  // https://commons.wikimedia.org/wiki/File:Twinkle_Twinkle_Sheet_Music.png
  constexpr SongNote melody[] = {
      {523, 1}, {523, 1}, {784, 1}, {784, 1}, {880, 1}, {880, 1}, {784, 2},
      {698, 1}, {698, 1}, {659, 1}, {659, 1}, {587, 1}, {587, 1}, {523, 2},
      {784, 1}, {784, 1}, {698, 1}, {698, 1}, {659, 1}, {659, 1}, {587, 2},
      {784, 1}, {784, 1}, {698, 1}, {698, 1}, {659, 1}, {659, 1}, {587, 2},
      {523, 1}, {523, 1}, {784, 1}, {784, 1}, {880, 1}, {880, 1}, {784, 2},
      {698, 1}, {698, 1}, {659, 1}, {659, 1}, {587, 1}, {587, 1}, {523, 2},
  };

  const bool serialConnected = static_cast<bool>(Serial);
  if (serialConnected) {
    Serial.println("SONG: Twinkle, Twinkle, Little Star - C major, 4% duty");
  }

  uint32_t deadlineMs = millis();
  for (const SongNote &note : melody) {
    const uint32_t totalMs = SONG_BEAT_MS * note.beats;
    const uint32_t soundingMs = (totalMs * 90U) / 100U;
    playSongNote(note.frequencyHz, soundingMs, totalMs, deadlineMs);
  }
  silenceBuzzer();
  if (serialConnected) {
    Serial.println("SONG COMPLETE (14.4 seconds)");
  }
}

void playCleanTone(uint32_t frequencyHz, uint32_t durationMs,
                   uint32_t gapAfterMs = 0) {
  if (!setFrequency(frequencyHz)) {
    silenceBuzzer();
    delay(durationMs + gapAfterMs);
    return;
  }

  const uint16_t targetDuty = dutyPercentToRaw(NOTIFICATION_DUTY_PERCENT);
  const uint32_t envelopeMs = min(8UL, durationMs / 4UL);
  const uint32_t rampStepUs = max(1UL, (envelopeMs * 1000UL) / targetDuty);

  // A short amplitude envelope suppresses the hard start/stop clicks produced
  // when a square wave is switched directly to its final duty.
  for (uint16_t duty = 1; duty <= targetDuty; ++duty) {
    ledcWrite(BUZZER_PIN, duty);
    delayMicroseconds(rampStepUs);
  }

  const uint32_t sustainMs = durationMs - (2UL * envelopeMs);
  delay(sustainMs);

  for (uint16_t duty = targetDuty; duty > 0; --duty) {
    ledcWrite(BUZZER_PIN, duty - 1);
    delayMicroseconds(rampStepUs);
  }
  silenceBuzzer();
  delay(gapAfterMs);
}

void showCandidateNumber(uint8_t candidate) {
  // The three PCB LEDs display the candidate number in binary (LED0 is bit 0).
  digitalWrite(LED0_PIN, candidate & 0x01U ? HIGH : LOW);
  digitalWrite(LED1_PIN, candidate & 0x02U ? HIGH : LOW);
  digitalWrite(LED2_PIN, candidate & 0x04U ? HIGH : LOW);
}

void playPattern(const ToneStep *steps, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    playCleanTone(steps[i].frequencyHz, steps[i].durationMs, steps[i].gapAfterMs);
  }
}

void playStartupCandidate(uint8_t candidate) {
  // These are original patterns built from simple consonant intervals. Each is
  // under half a second and uses the same confirmed quiet 4% maximum duty.
  constexpr ToneStep softRise[] = {
      {1047, 55, 22}, {1319, 60, 22}, {1568, 115, 0}};
  constexpr ToneStep dewdrop[] = {
      {2637, 65, 28}, {2093, 125, 0}};
  constexpr ToneStep readyFifth[] = {
      {1175, 70, 32}, {1760, 120, 0}};
  constexpr ToneStep gentleDouble[] = {
      {2400, 48, 72}, {2400, 58, 0}};
  constexpr ToneStep majorSparkle[] = {
      {1319, 45, 18}, {1661, 50, 18}, {1976, 115, 0}};
  constexpr ToneStep bootBloom[] = {
      {988, 35, 12}, {1175, 38, 12}, {1319, 42, 14},
      {1568, 55, 18}, {1976, 125, 0}};

  showCandidateNumber(candidate);
  switch (candidate) {
    case 1:
      playPattern(softRise, sizeof(softRise) / sizeof(softRise[0]));
      break;
    case 2:
      playPattern(dewdrop, sizeof(dewdrop) / sizeof(dewdrop[0]));
      break;
    case 3:
      playPattern(readyFifth, sizeof(readyFifth) / sizeof(readyFifth[0]));
      break;
    case 4:
      playPattern(gentleDouble, sizeof(gentleDouble) / sizeof(gentleDouble[0]));
      break;
    case 5:
      playPattern(majorSparkle, sizeof(majorSparkle) / sizeof(majorSparkle[0]));
      break;
    case 6:
      playPattern(bootBloom, sizeof(bootBloom) / sizeof(bootBloom[0]));
      break;
    default:
      break;
  }
  digitalWrite(LED0_PIN, LOW);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
}

void playStartupAudition() {
  for (uint8_t candidate = 1; candidate <= 6; ++candidate) {
    playStartupCandidate(candidate);
    delay(1600);
  }
}

void playBootSound() {
  playStartupCandidate(1);
}

void playWaterWarning() {
  for (uint8_t repeat = 0; repeat < WATER_WARNING_REPEAT_COUNT; ++repeat) {
    playStartupCandidate(4);
    if (repeat + 1 < WATER_WARNING_REPEAT_COUNT) {
      delay(WATER_WARNING_REPEAT_GAP_MS);
    }
  }
}

void playVolumeTour() {
  constexpr uint8_t levels[] = {1, 2, 4, 6, 8, 10, 15, 20, 30, 40, 50};

  Serial.println("\nVOLUME TOUR: 4 kHz, safe/useful full duty range 1..50%");
  Serial.println("Each step is 100 ms; the percentage is PWM duty, not calibrated dB.");
  for (uint8_t level : levels) {
    playTone(4000, level, 100);
    delay(140);
  }
  Serial.println("VOLUME TOUR COMPLETE");
}

void playFrequencyTour() {
  // A logarithmically spaced practical human-audible test span, with extra
  // points through the common 2..5 kHz buzzer region.
  constexpr uint32_t frequenciesHz[] = {
      100, 200, 400, 800, 1000, 1250, 1500, 2000, 2500, 3000, 3500,
      4000, 4500, 5000, 6000, 7000, 8000, 10000, 12000, 15000, 18000, 20000};

  Serial.printf("\nFREQUENCY TOUR: 100 Hz..20 kHz at quiet %u%% duty\n",
                NOTIFICATION_DUTY_PERCENT);
  Serial.println("Low/high steps may be faint or inaudible; note which frequency sounds best.");
  for (uint32_t frequencyHz : frequenciesHz) {
    playTone(frequencyHz, NOTIFICATION_DUTY_PERCENT, 110);
    delay(120);
  }
  Serial.println("FREQUENCY TOUR COMPLETE");
}

void printHelp() {
  Serial.println(R"HELP(
Commands (finish each with Enter):
  b                    Play the locked startup sound (candidate 1)
  w                    Play the 3x plant-needs-water warning (candidate 4)
  a                    Play all six startup candidates
  1..6                 Play one startup candidate
  m                    Play Twinkle, Twinkle, Little Star
  n                    Play the proposed notification
  d                    Replay both full capability tours
  f <100..20000>       Select frequency in Hz
  v <1..50>            Select volume as PWM duty percent
  p <10..2000>         Play selected tone for milliseconds
  t <hz> <duty> <ms>   Play one exact test tone
  s                    Silence immediately
  ?                    Print this help

Examples:
  t 4000 8 100
  f 3500
  v 6
  p 80

Limits: 100..20000 Hz, 1..50% duty, 10..2000 ms.
The volume percentage is electrical PWM duty, not a linear SPL percentage.
)HELP");
}

void handleSerialCommand(String line) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  char command = 0;
  unsigned long first = 0;
  unsigned int second = 0;
  unsigned long third = 0;
  const int fields = sscanf(line.c_str(), " %c %lu %u %lu", &command, &first, &second, &third);
  command = static_cast<char>(tolower(static_cast<unsigned char>(command)));

  if (command == 'b' && fields == 1) {
    playBootSound();
  } else if (command == 'w' && fields == 1) {
    playWaterWarning();
  } else if (command >= '1' && command <= '6' && fields == 1) {
    playStartupCandidate(static_cast<uint8_t>(command - '0'));
  } else if (command == 'a' && fields == 1) {
    playStartupAudition();
  } else if (command == 'm' && fields == 1) {
    playSong();
  } else if (command == 'n' && fields == 1) {
    playNotification();
  } else if (command == 'd' && fields == 1) {
    playVolumeTour();
    delay(800);
    playFrequencyTour();
  } else if (command == 'f' && fields == 2 &&
             first >= MIN_TEST_FREQUENCY_HZ && first <= MAX_TEST_FREQUENCY_HZ) {
    selectedFrequencyHz = first;
    Serial.printf("Selected frequency: %lu Hz\n", first);
  } else if (command == 'v' && fields == 2 &&
             first >= MIN_DUTY_PERCENT && first <= MAX_DUTY_PERCENT) {
    selectedDutyPercent = static_cast<uint8_t>(first);
    Serial.printf("Selected duty: %u%%\n", selectedDutyPercent);
  } else if (command == 'p' && fields == 2 && first >= 10 && first <= 2000) {
    playTone(selectedFrequencyHz, selectedDutyPercent, first);
  } else if (command == 't' && fields == 4 &&
             first >= MIN_TEST_FREQUENCY_HZ && first <= MAX_TEST_FREQUENCY_HZ &&
             second >= MIN_DUTY_PERCENT && second <= MAX_DUTY_PERCENT &&
             third >= 10 && third <= 2000) {
    playTone(first, static_cast<uint8_t>(second), third);
  } else if (command == 's' && fields == 1) {
    silenceBuzzer();
    Serial.println("Buzzer silenced");
  } else if ((command == '?' || command == 'h') && fields == 1) {
    printHelp();
  } else {
    Serial.println("Invalid command or out-of-range value. Enter ? for help.");
  }
}

}  // namespace

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(LED0_PIN, OUTPUT);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  digitalWrite(LED0_PIN, LOW);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);

  Serial.begin(115200);
  delay(1500);
  Serial.println("\nMSV2 Display Center - standalone buzzer audition");

  if (!ledcAttach(BUZZER_PIN, NOTIFICATION_NOTE_2_HZ, PWM_RESOLUTION_BITS)) {
    Serial.println("FATAL: could not attach LEDC to GPIO38");
    return;
  }
  silenceBuzzer();
  printHelp();

  Serial.println("Locked startup/water-warning emulation begins in 2 seconds...");
  delay(2000);
  playBootSound();
  delay(3000);
  playWaterWarning();
  nextWaterWarningMs = millis() + WATER_WARNING_INTERVAL_MS;
  Serial.println("\nWaiting for serial commands. Enter b, w, or ? for help.");
}

void loop() {
  if (static_cast<int32_t>(millis() - nextWaterWarningMs) >= 0) {
    playWaterWarning();
    nextWaterWarningMs = millis() + WATER_WARNING_INTERVAL_MS;
  }

  if (Serial.available()) {
    handleSerialCommand(Serial.readStringUntil('\n'));
  }
  delay(5);
}
