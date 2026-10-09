/* =====================================================================
   ESP32 CYD Oscilloscope  (Cheap Yellow Display)
   ---------------------------------------------------------------------
   A single-channel, touch-controlled oscilloscope for the
   ESP32-2432S028R "Cheap Yellow Display", with an ANALOG external
   trigger input.

   Hardware:
     * ESP32-WROOM-32 + 2.8" 240x320 ILI9341 TFT (HSPI)
     * XPT2046 resistive touch (VSPI)
     * CH1 analog input .... GPIO35  (ADC1 channel 7, input-only)
     * EXT trigger input ... GPIO27  (ADC2 channel 7, on CN1 header)

   Libraries (install via Library Manager):
     * TFT_eSPI            by Bodmer
     * XPT2046_Touchscreen by Paul Stoffregen

   IMPORTANT: replace the User_Setup.h inside the TFT_eSPI library with
   the one supplied alongside this sketch (see the project README).

   Trigger sources:
     * CH1  - trigger off the displayed signal (GPIO35)          [default]
     * EXT  - ANALOG trigger off the external input (GPIO27) while
              still displaying CH1. The external trigger has its own
              adjustable level (in volts) and its own hysteresis band,
              and both buffers are time-aligned so the trace stays
              locked to the external edge.

   Analog trigger features:
     * Independent trigger level per source (CH1 vs EXT)
     * Hysteresis band to reject noise on the trigger signal
     * AUTO button: samples the active source and sets the level to the
       mid-point between its min and max (great for analog EXT signals)

   Trigger modes (MODE button):
     * AUTO   - free-run: if no edge is found the trace is still drawn
                (the classic "auto" mode - always shows something)
     * NORMAL - only update the trace when the trigger fires; otherwise
                hold the last captured trace (no jitter on unstable sigs)
     * SINGLE - arm a one-shot capture (press ARM); the next trigger is
                captured, drawn, and the scope stops (great for one-off
                events)

   Holdoff (HOLD button):
     * Adjustable dead time (0..200 ms) inserted after every sweep before
       the next acquisition starts. Useful to stabilise complex or
       multi-edge waveforms (e.g. a burst) by skipping re-triggers.

   NOTE: GPIO27 is ADC2, which is shared with the WiFi radio. If you
   enable WiFi, external-trigger sampling on ADC2 will fail. Keep WiFi
   off for the EXT trigger mode. Some CYD revisions also fit a 10k
   pull-up on GPIO27 (and a few leave it unconnected) - see the README.
   ===================================================================== */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ---------------------------------------------------------------------
// Touchscreen pins (VSPI) — fixed on the CYD
// ---------------------------------------------------------------------
#define XPT2046_IRQ   36   // T_IRQ
#define XPT2046_MOSI  32   // T_DIN
#define XPT2046_MISO  39   // T_OUT
#define XPT2046_CLK   25   // T_CLK
#define XPT2046_CS    33   // T_CS

// ---------------------------------------------------------------------
// Analog inputs
// ---------------------------------------------------------------------
#define ADC_PIN_CH1   35   // ADC1_CH7  (P3 header)  -> displayed trace
#define ADC_PIN_EXT   27   // ADC2_CH7  (CN1 header) -> analog external trigger

// ---------------------------------------------------------------------
// Display geometry
// ---------------------------------------------------------------------
#define SCREEN_W      320
#define SCREEN_H      240

#define PLOT_X        40                       // left edge of the plot area
#define PLOT_Y        26                       // top edge of the plot area
#define PLOT_W        276                      // plot width  (samples across)
#define PLOT_H        172                      // plot height (8 divisions)
#define PLOT_X1       (PLOT_X + PLOT_W)        // right edge
#define PLOT_Y1       (PLOT_Y + PLOT_H)        // bottom edge

#define DIVS_H        8                        // horizontal divisions
#define DIVS_V        8                        // vertical divisions
#define SAMPLES       PLOT_W                   // one sample per pixel column

// ---------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------
#define COL_BG        0x0000   // black  (screen + plot background)
#define COL_GRID      0x4208   // dark grey
#define COL_GRID_CTR  0x7BEF   // lighter grey for centre lines
#define COL_TRACE     0x07E0   // green
#define COL_TEXT      0xFFFF   // white
#define COL_ACCENT    0xFFE0   // yellow
#define COL_EXT       0xF800   // red (external trigger)
#define COL_BAR       0x18E3   // control bar background
#define COL_BTN       0x2C7B   // button background
#define COL_BTN_ACT   0x05BF   // active button
#define COL_ARM       0xFBE0   // amber (armed / single-shot)

// ---------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// ---------------------------------------------------------------------
// Scope settings
// ---------------------------------------------------------------------
const float VREF = 3.3f;                 // ADC reference (approx, 11 dB atten.)
const float ADC_MAX = 4095.0f;
const float HYST = 0.08f;                // trigger hysteresis band (volts, ~2.4% FS)

// Timebase: microseconds per horizontal division
const uint32_t timeDivs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000 };
const uint8_t  TIME_DIV_COUNT = sizeof(timeDivs) / sizeof(timeDivs[0]);
uint8_t timeDivIdx = 4;                   // start at 1000 us/div

// Vertical: volts per division
const float voltDivs[] = { 0.1f, 0.2f, 0.5f, 1.0f, 2.0f };
const uint8_t VOLT_DIV_COUNT = sizeof(voltDivs) / sizeof(voltDivs[0]);
uint8_t voltDivIdx = 2;                   // start at 0.5 V/div

float vOffset = 1.65f;                    // vertical centre voltage
bool  risingEdge = true;                  // trigger edge
bool  extTrig = false;                    // false = trigger on CH1, true = on EXT
bool  running = true;                     // run / stop (also = "armed" in SINGLE)

// Independent analog trigger levels (volts) — one per source
float trigLevelCh1 = 1.65f;
float trigLevelExt = 1.65f;

// ---------------------------------------------------------------------
// Trigger mode + holdoff
// ---------------------------------------------------------------------
enum TrigMode { TM_AUTO, TM_NORMAL, TM_SINGLE };
TrigMode trigMode = TM_AUTO;

// Holdoff: dead time (ms) inserted after each sweep before re-arming
const uint16_t holdoffVals[] = { 0, 1, 5, 10, 20, 50, 100, 200 };
const uint8_t  HOLDOFF_COUNT = sizeof(holdoffVals) / sizeof(holdoffVals[0]);
uint8_t  holdoffIdx = 0;
uint16_t holdoffMs  = 0;
uint32_t nextAcquireMs = 0;               // earliest time for the next sweep

// ---------------------------------------------------------------------
// Sample buffers
// ---------------------------------------------------------------------
uint16_t buf[SAMPLES];                    // CH1 raw ADC values (0..4095)
uint16_t bufExt[SAMPLES];                 // EXT raw ADC values (0..4095)
int16_t  yPrev[SAMPLES];                  // previous trace y positions
bool     havePrev = false;

// ---------------------------------------------------------------------
// Measurement results
// ---------------------------------------------------------------------
float mMin, mMax, mVpp, mAvg, mFreq;

// ---------------------------------------------------------------------
// Control-bar buttons (12 across two rows of 6)
// ---------------------------------------------------------------------
struct Button {
  int16_t x, y, w, h;
  const char* label;
};
#define BTN_COUNT 12
Button buttons[BTN_COUNT] = {
  // row 1 — scales & level
  {   2, 202, 52, 18, "T-"  },
  {  55, 202, 52, 18, "T+"  },
  { 108, 202, 52, 18, "V-"  },
  { 161, 202, 52, 18, "V+"  },
  { 214, 202, 52, 18, "L-"  },
  { 267, 202, 52, 18, "L+"  },
  // row 2 — trigger & run
  {   2, 222, 52, 18, "RISE"},   // toggles RISE <-> FALL
  {  55, 222, 52, 18, "CH1" },   // toggles CH1  <-> EXT
  { 108, 222, 52, 18, "AUTO"},   // auto-set trigger level
  { 161, 222, 52, 18, "MODE"},   // cycle AUTO / NORMAL / SINGLE
  { 214, 222, 52, 18, "HOLD"},   // cycle holdoff time
  { 267, 222, 52, 18, "RUN" },   // run/stop, or ARM in SINGLE mode
};

// =====================================================================
//  HELPERS
// =====================================================================
static inline float activeTrigLevel() {
  return extTrig ? trigLevelExt : trigLevelCh1;
}

static inline void setActiveTrigLevel(float v) {
  if (v < 0) v = 0;
  if (v > VREF) v = VREF;
  if (extTrig) trigLevelExt = v; else trigLevelCh1 = v;
}

static inline const char* modeName() {
  return (trigMode == TM_AUTO) ? "AUTO" : (trigMode == TM_NORMAL) ? "NORM" : "SNGL";
}

// =====================================================================
//  SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);

  // ---- Touch ----
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);   // use 3 if your touch axes are flipped

  // ---- Display ----
  tft.init();
  tft.setRotation(1);  // landscape
  tft.fillScreen(COL_BG);

  // ---- ADC ----
  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN_CH1, ADC_11db);  // ~0..3.3 V range
  analogSetPinAttenuation(ADC_PIN_EXT, ADC_11db);  // ~0..3.3 V range
  // NOTE: the ESP32 Arduino core has no analogSetCycles() (that is an ESP8266
  // API). ESP32 sampling speed is set by analogSetClockDiv(), whose default
  // value (1) is already the fastest, so nothing else is required here.

  nextAcquireMs = millis();
  drawStaticUI();
  Serial.println("CYD Oscilloscope ready. Source CH1 (analog), mode AUTO.");
}

// =====================================================================
//  MAIN LOOP
// =====================================================================
void loop() {
  handleTouch();

  if (running) {
    // Holdoff gate: only start a new sweep once the dead time has elapsed.
    if ((int32_t)(millis() - nextAcquireMs) >= 0) {
      acquire();
      bool found = triggerAlign();

      // AUTO always draws (free-run if no edge). NORMAL/SINGLE draw only
      // when the trigger actually fired.
      if (found || trigMode == TM_AUTO) {
        render();
        measure();
        drawMeasurements();
        nextAcquireMs = millis() + holdoffMs;   // apply holdoff before next sweep

        if (trigMode == TM_SINGLE) {
          // one-shot complete: stop and go back to "ready to arm"
          running = false;
          buttons[11].label = "ARM";
          drawButton(11, false);
        }
      } else {
        // NORMAL / SINGLE, waiting for an edge: keep the last trace and
        // retry immediately (no holdoff while waiting).
        nextAcquireMs = millis();
      }
    }
  }
  // small yield so the touch controller and WiFi stack (if used) stay alive
  delay(2);
}

// =====================================================================
//  ACQUISITION
// =====================================================================
// Delay between samples derived from the selected timebase.
static inline uint32_t sampleIntervalUs() {
  // timeDiv is spread across (SAMPLES / DIVS_H) samples per division
  uint32_t samplesPerDiv = SAMPLES / DIVS_H;
  return timeDivs[timeDivIdx] / samplesPerDiv;
}

void acquire() {
  uint32_t interval = sampleIntervalUs();
  for (int i = 0; i < SAMPLES; i++) {
    buf[i] = analogRead(ADC_PIN_CH1);
    if (extTrig) bufExt[i] = analogRead(ADC_PIN_EXT);  // analog trigger source
    if (interval > 0) delayMicroseconds(interval);
  }
}

// =====================================================================
//  ANALOG TRIGGER
// =====================================================================
// Edge trigger with hysteresis. The selected source buffer is scanned for
// an edge that crosses the trigger level (plus/minus a hysteresis band) in
// the chosen direction. Both buffers are then rotated so the crossing sits
// near the left edge (~1/8 in), keeping CH1 locked to the trigger.
// Returns true if a trigger edge was found and the buffers were aligned.
bool triggerAlign() {
  int pre = SAMPLES / 8;
  const uint16_t* src = extTrig ? bufExt : buf;
  float level = activeTrigLevel();
  float hys = HYST;
  int idx = -1;

  // --- Pass 1: hysteresis-based edge search (noise immune) ---
  bool armed = false;
  for (int i = 1; i < SAMPLES; i++) {
    float v = src[i] * VREF / ADC_MAX;
    if (risingEdge) {
      if (!armed && v < level - hys) armed = true;      // arm below the band
      if (armed && v > level + hys) { idx = i; break; } // fire above the band
    } else {
      if (!armed && v > level + hys) armed = true;      // arm above the band
      if (armed && v < level - hys) { idx = i; break; } // fire below the band
    }
  }

  // --- Pass 2: fallback to a plain level crossing if hysteresis found none ---
  if (idx < 0) {
    for (int i = 1; i < SAMPLES; i++) {
      float v0 = src[i - 1] * VREF / ADC_MAX;
      float v1 = src[i]     * VREF / ADC_MAX;
      if (risingEdge) {
        if (v0 < level && v1 >= level) { idx = i; break; }
      } else {
        if (v0 > level && v1 <= level) { idx = i; break; }
      }
    }
  }

  if (idx < 0) return false;         // no trigger -> caller decides what to do

  static uint16_t tmp[SAMPLES];
  // rotate CH1
  for (int i = 0; i < SAMPLES; i++) {
    int j = idx - pre + i;
    if (j < 0) j += SAMPLES;
    if (j >= SAMPLES) j -= SAMPLES;
    tmp[i] = buf[j];
  }
  memcpy(buf, tmp, sizeof(buf));
  // rotate EXT identically so the two stay time-aligned
  for (int i = 0; i < SAMPLES; i++) {
    int j = idx - pre + i;
    if (j < 0) j += SAMPLES;
    if (j >= SAMPLES) j -= SAMPLES;
    tmp[i] = bufExt[j];
  }
  memcpy(bufExt, tmp, sizeof(bufExt));
  return true;
}

// =====================================================================
//  AUTO TRIGGER LEVEL
// =====================================================================
// Samples the active source and sets the trigger level to the mid-point
// between its min and max. Ideal for an unknown analog EXT signal.
void autoTriggerLevel() {
  uint16_t vmin = 4095, vmax = 0;

  if (!extTrig) {
    for (int i = 0; i < SAMPLES; i++) {
      uint16_t s = buf[i];
      if (s < vmin) vmin = s;
      if (s > vmax) vmax = s;
    }
  } else {
    // grab a fresh burst of the external trigger signal
    for (int i = 0; i < SAMPLES; i++) {
      uint16_t s = analogRead(ADC_PIN_EXT);
      if (s < vmin) vmin = s;
      if (s > vmax) vmax = s;
      delayMicroseconds(50);
    }
  }

  float mid = ((vmin + vmax) / 2.0f) * VREF / ADC_MAX;
  setActiveTrigLevel(mid);
}

// =====================================================================
//  RENDERING
// =====================================================================
static inline int16_t voltToY(float v) {
  float pixelsPerDiv = (float)PLOT_H / DIVS_V;
  float y = (PLOT_Y + PLOT_H / 2.0f) - ((v - vOffset) / voltDivs[voltDivIdx]) * pixelsPerDiv;
  if (y < PLOT_Y)  y = PLOT_Y;
  if (y > PLOT_Y1) y = PLOT_Y1;
  return (int16_t)y;
}

void render() {
  int16_t yNew[SAMPLES];
  for (int i = 0; i < SAMPLES; i++) {
    float v = buf[i] * VREF / ADC_MAX;
    yNew[i] = voltToY(v);
  }

  // 1) Erase the previous trace by painting it in the background colour.
  //    This leaves small gaps wherever the old trace crossed a grid line...
  if (havePrev) {
    for (int i = 1; i < SAMPLES; i++) {
      tft.drawLine(PLOT_X + i - 1, yPrev[i - 1], PLOT_X + i, yPrev[i], COL_BG);
    }
  }

  // 2) Clear the trigger-tag strip (removes any stale tag text) BEFORE the
  //    grid is restored, so the strip never ends up with a hole in it.
  tft.fillRect(PLOT_X, PLOT_Y, 150, 12, COL_BG);

  // 3) ...so immediately redraw the grid lines to fill all those gaps back in.
  //    The grid is therefore ALWAYS fully present and can never be wiped out
  //    or "bleached" by the moving waveform.
  drawGridLines();

  // 4) Draw the new trace on top of the (now intact) grid.
  for (int i = 1; i < SAMPLES; i++) {
    tft.drawLine(PLOT_X + i - 1, yNew[i - 1], PLOT_X + i, yNew[i], COL_TRACE);
  }

  memcpy(yPrev, yNew, sizeof(yNew));
  havePrev = true;

  // 5) Draw the trigger tag on top (this no longer clears anything).
  drawTriggerMarker();
}

void drawTriggerMarker() {
  // The on-screen level arrow only makes sense on the CH1 scale when
  // triggering on CH1. In EXT mode the level refers to the external signal,
  // so we show a red "EXT TRIG" tag with the level value instead.
  //
  // NOTE: the tag strip is cleared at the start of render(), *before* the grid
  // is redrawn, so nothing here erases the grid.
  //
  // The arrow lives just OUTSIDE the plot area (to the left of PLOT_X), so the
  // grid redraw never touches it. We therefore remember where it was and erase
  // it ourselves when it moves -- this keeps exactly ONE arrow on screen.
  static int16_t prevMarkerY = -1;   // y of the CH1 arrow last drawn (-1 = none)

  if (!extTrig) {
    int16_t y = voltToY(trigLevelCh1);

    // Erase the previous arrow (restoring the axis labels it covered) so the
    // marker simply MOVES instead of leaving a trail behind it.
    if (prevMarkerY >= 0 && prevMarkerY != y) {
      tft.fillRect(PLOT_X - 9, prevMarkerY - 5, 9, 11, COL_BG);
      drawAxisLabels();
    }

    tft.fillTriangle(PLOT_X - 8, y, PLOT_X - 2, y - 4, PLOT_X - 2, y + 4, COL_ACCENT);
    prevMarkerY = y;
  } else {
    // EXT mode has no CH1 arrow: remove it if it was showing.
    if (prevMarkerY >= 0) {
      tft.fillRect(PLOT_X - 9, prevMarkerY - 5, 9, 11, COL_BG);
      drawAxisLabels();
      prevMarkerY = -1;
    }

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_EXT);   // transparent background: the grid stays visible
    char s[24];
    snprintf(s, sizeof(s), "EXT TRIG %.2fV", trigLevelExt);
    tft.drawString(s, PLOT_X + 2, PLOT_Y + 1, 1);
  }
}

// =====================================================================
//  MEASUREMENTS  (always on the CH1 buffer)
// =====================================================================
void measure() {
  uint16_t vmin = 4095, vmax = 0;
  uint32_t sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    uint16_t s = buf[i];
    if (s < vmin) vmin = s;
    if (s > vmax) vmax = s;
    sum += s;
  }
  mMin = vmin * VREF / ADC_MAX;
  mMax = vmax * VREF / ADC_MAX;
  mVpp = mMax - mMin;
  mAvg = (sum / (float)SAMPLES) * VREF / ADC_MAX;

  // Frequency estimate via rising mean-crossings (needs a decent swing)
  mFreq = 0.0f;
  if (mVpp > 0.05f) {
    float mean = mAvg;
    int first = -1, last = -1, crossings = 0;
    for (int i = 1; i < SAMPLES; i++) {
      float v0 = buf[i - 1] * VREF / ADC_MAX;
      float v1 = buf[i]     * VREF / ADC_MAX;
      if (v0 < mean && v1 >= mean) {         // rising mean crossing
        if (first < 0) first = i;
        last = i;
        crossings++;
      }
    }
    if (crossings >= 2 && last > first) {
      float interval = sampleIntervalUs() * (last - first) / (float)(crossings - 1);
      if (interval > 0) mFreq = 1e6f / interval;   // Hz
    }
  }
}

// =====================================================================
//  STATIC UI
// =====================================================================
void drawStaticUI() {
  tft.fillScreen(COL_BG);
  drawGrid();
  drawControlBar();
  drawTitle();
  drawStatus();
}

void drawTitle() {
  tft.fillRect(0, 0, SCREEN_W, PLOT_Y - 2, COL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString("CYD OSCILLOSCOPE", 6, 4, 2);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("CH1:GPIO35", 232, 6, 1);
}

// Draw ONLY the grid lines (no background fill). This is cheap enough to be
// called on every frame, which is what keeps the grid permanently on screen
// no matter where the sampled trace has travelled.
void drawGridLines() {
  int16_t dx = PLOT_W / DIVS_H;
  int16_t dy = PLOT_H / DIVS_V;

  for (int i = 0; i <= DIVS_H; i++) {
    int16_t x = PLOT_X + i * dx;
    uint16_t c = (i == DIVS_H / 2) ? COL_GRID_CTR : COL_GRID;
    tft.drawFastVLine(x, PLOT_Y, PLOT_H, c);
  }
  for (int i = 0; i <= DIVS_V; i++) {
    int16_t y = PLOT_Y + i * dy;
    uint16_t c = (i == DIVS_V / 2) ? COL_GRID_CTR : COL_GRID;
    tft.drawFastHLine(PLOT_X, y, PLOT_W, c);
  }
}

// Draw the vertical-scale labels down the left edge of the plot. Kept as a
// separate function so the trigger marker can restore the labels it covers
// when the level (and therefore the marker) moves.
void drawAxisLabels() {
  int16_t dy = PLOT_H / DIVS_V;
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  for (int i = 0; i <= DIVS_V; i++) {
    int16_t y = PLOT_Y + i * dy;
    float v = vOffset + (DIVS_V / 2 - i) * voltDivs[voltDivIdx];
    char lbl[8];
    dtostrf(v, 4, 1, lbl);
    tft.drawString(lbl, PLOT_X - 4, y, 1);
  }
}

void drawGrid() {
  // Full redraw: clear the plot area, then lay the grid back down.
  tft.fillRect(PLOT_X, PLOT_Y, PLOT_W, PLOT_H, COL_BG);
  drawGridLines();
  drawAxisLabels();
  havePrev = false;   // grid cleared the plot area
}

void drawControlBar() {
  tft.fillRect(0, 200, SCREEN_W, 40, COL_BAR);
  for (int i = 0; i < BTN_COUNT; i++) {
    drawButton(i, false);
  }
}

void drawButton(int i, bool active) {
  Button &b = buttons[i];
  uint16_t bg = active ? COL_BTN_ACT : COL_BTN;
  tft.fillRoundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 4, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TEXT, bg);
  tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2, 1);
}

void drawStatus() {
  // edge + source + mode + timebase + vertical scale + level + holdoff
  tft.fillRect(0, 16, SCREEN_W, 10, COL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  char s[64];
  float tdiv = timeDivs[timeDivIdx] / 1000.0f;   // ms/div
  snprintf(s, sizeof(s), "%s %s %s  %.3g ms/div %.2g V/div L%.2fV HO%ums",
           risingEdge ? "RISE" : "FALL", extTrig ? "EXT" : "CH1", modeName(),
           tdiv, voltDivs[voltDivIdx], activeTrigLevel(), holdoffMs);
  tft.drawString(s, 6, 16, 1);
}

void drawMeasurements() {
  // trigger info (top-left of plot)
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(extTrig ? COL_EXT : COL_ACCENT, COL_BG);
  char s[48];
  snprintf(s, sizeof(s), "Trig %.2fV %s", activeTrigLevel(), extTrig ? "EXT" : "CH1");
  tft.drawString(s, PLOT_X + 2, PLOT_Y + 14, 1);

  // Vpp / frequency (top-right)
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  snprintf(s, sizeof(s), "Vpp %.2fV  F %.1fHz", mVpp, mFreq);
  tft.drawString(s, PLOT_X1 - 2, PLOT_Y + 2, 1);

  // min / max / avg (bottom-right)
  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  snprintf(s, sizeof(s), "min %.2f  max %.2f  avg %.2f", mMin, mMax, mAvg);
  tft.drawString(s, PLOT_X1 - 2, PLOT_Y1 - 2, 1);
}

// =====================================================================
//  TOUCH HANDLING
// =====================================================================
void handleTouch() {
  if (!ts.touched()) return;
  TS_Point p = ts.getPoint();

  // map raw touch to screen coordinates
  int x = map(p.x, 200, 3700, 0, SCREEN_W);
  int y = map(p.y, 240, 3800, 0, SCREEN_H);
  x = constrain(x, 0, SCREEN_W - 1);
  y = constrain(y, 0, SCREEN_H - 1);

  for (int i = 0; i < BTN_COUNT; i++) {
    Button &b = buttons[i];
    if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
      pressButton(i);
      // visual feedback
      drawButton(i, true);
      delay(120);
      drawButton(i, false);
      // wait for release
      while (ts.touched()) delay(10);
      break;
    }
  }
}

void pressButton(int i) {
  switch (i) {
    case 0: // T-
      if (timeDivIdx > 0) timeDivIdx--;
      break;
    case 1: // T+
      if (timeDivIdx < TIME_DIV_COUNT - 1) timeDivIdx++;
      break;
    case 2: // V-
      if (voltDivIdx > 0) voltDivIdx--;
      drawGrid();
      break;
    case 3: // V+
      if (voltDivIdx < VOLT_DIV_COUNT - 1) voltDivIdx++;
      drawGrid();
      break;
    case 4: // L-  (active source's level)
      setActiveTrigLevel(activeTrigLevel() - 0.05f);
      break;
    case 5: // L+  (active source's level)
      setActiveTrigLevel(activeTrigLevel() + 0.05f);
      break;
    case 6: // EDGE  (toggles label RISE <-> FALL)
      risingEdge = !risingEdge;
      buttons[6].label = risingEdge ? "RISE" : "FALL";
      drawButton(6, false);
      break;
    case 7: // SRC  (toggles label CH1 <-> EXT) — auto-set level on switch
      extTrig = !extTrig;
      buttons[7].label = extTrig ? "EXT" : "CH1";
      drawButton(7, false);
      autoTriggerLevel();          // set a sensible analog level for the new source
      break;
    case 8: // AUTO  (auto-set trigger level to source mid-point)
      autoTriggerLevel();
      break;
    case 9: // MODE  (cycle AUTO -> NORMAL -> SINGLE)
      trigMode = (TrigMode)((trigMode + 1) % 3);
      if (trigMode == TM_SINGLE) {
        // entering single-shot: stop and wait for the user to arm
        running = false;
        buttons[11].label = "ARM";
      } else {
        // AUTO / NORMAL run continuously
        running = true;
        buttons[11].label = "RUN";
      }
      drawButton(11, false);
      break;
    case 10: // HOLD  (cycle holdoff dead time)
      holdoffIdx = (holdoffIdx + 1) % HOLDOFF_COUNT;
      holdoffMs  = holdoffVals[holdoffIdx];
      break;
    case 11: // RUN / STOP  (or ARM in SINGLE mode)
      if (trigMode == TM_SINGLE) {
        running = true;               // arm a one-shot capture
        nextAcquireMs = millis();     // no holdoff while arming
        buttons[11].label = "ARM";    // stays ARM until the shot completes
      } else {
        running = !running;
        buttons[11].label = running ? "RUN" : "STOP";
      }
      drawButton(11, false);
      break;
  }
  drawStatus();
}
