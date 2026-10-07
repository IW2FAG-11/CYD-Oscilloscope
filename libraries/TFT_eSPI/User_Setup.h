//                            USER DEFINED SETTINGS
//   Set driver type, fonts to be loaded, pins used and SPI control method etc.
//
//   ========================================================================
//   Configuration for the ESP32-2432S028R "Cheap Yellow Display" (CYD)
//   Display: 2.8" ILI9341 (240x320), SPI on HSPI
//   Touch:   XPT2046 resistive, SPI on VSPI (handled by XPT2046_Touchscreen)
//   ========================================================================
//
//   Install this file by REPLACING the User_Setup.h that ships inside the
//   TFT_eSPI library folder:
//      .../Arduino/libraries/TFT_eSPI/User_Setup.h
//
//   (You may also keep the original and instead select a setup from the
//    User_Setups folder, but replacing is the simplest route.)

// See SetupX_Template.h for all options available

#define USER_SETUP_ID 70

// --- Display driver ---
#define ILI9341_2_DRIVER     // Alternative ILI9341 driver (works with most CYDs)
// Some newer CYD revisions ship with an ST7789 panel instead of ILI9341.
// If your screen stays white, comment out the line above and uncomment these:
//   #define ST7789_DRIVER
//   #define TFT_INVERSION_OFF

// --- Panel geometry (portrait native; we rotate in software) ---
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// --- Backlight ---
#define TFT_BL   21            // LED back-light control pin
#define TFT_BACKLIGHT_ON HIGH  // Level to turn ON back-light (HIGH is often the case)

// --- Display SPI pins (HSPI) ---
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15  // Chip select control pin
#define TFT_DC    2  // Data Command control pin
#define TFT_RST  -1  // Set TFT_RST to -1 if display RESET is connected to ESP32 board RST

// The CYD wires the display to the HSPI port, so force TFT_eSPI to use it.
#define USE_HSPI_PORT

// --- Fonts to load ---
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 chars
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 chars
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

#define SMOOTH_FONT

// --- SPI frequencies ---
#define SPI_FREQUENCY        55000000  // ~55MHz works well on the CYD; drop to 40000000 if unstable
#define SPI_READ_FREQUENCY   20000000
#define SPI_TOUCH_FREQUENCY   2500000
