
// Define Driver
#define ILI9341_DRIVER

/*
For touch LSO need to common
T_DO / TFT_MISO
T_DIN  / TFT_MOSI
T_CLK / SCLK

*/

// Define Pins
#define TFT_MISO 22
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   21  // Chip select control pin
#define TFT_DC    19  // Data Command control pin
#define TFT_RST   5 // Reset pin (could connect to RST pin)
#define TOUCH_CS 2     // Chip select pin (T_CS) of touch screen


// Define fonts
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
//#define LOAD_FONT8N // Font 8. Alternative to Font 8 above, slightly narrower, so 3 digits fit a 160 pixel TFT
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts
#define SMOOTH_FONT

//Define SPI speed
//#define SPI_FREQUENCY   1000000 //1 000 000
//#define SPI_FREQUENCY   10000000 //10 000 000
//#define SPI_FREQUENCY  27000000 //27 000 000 Actually sets it to 26.67MHz = 80/3
#define SPI_FREQUENCY  40000000 //40 000 000  Maximum to use SPIFFS

// Optional reduced SPI frequency for reading TFT
#define SPI_READ_FREQUENCY  20000000

// The XPT2046 requires a lower SPI clock rate of 2.5MHz so we define that here:
#define SPI_TOUCH_FREQUENCY  2500000

// The ESP32 has 2 free SPI ports i.e. VSPI and HSPI, the VSPI is the default.
// If the VSPI port is in use and pins are not accessible (e.g. TTGO T-Beam)
// then uncomment the following line:
#define USE_HSPI_PORT


