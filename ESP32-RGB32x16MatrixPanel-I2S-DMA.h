#ifndef _ESP32_RGB_32_16_MATRIX_PANEL_I2S_DMA
#define _ESP32_RGB_32_16_MATRIX_PANEL_I2S_DMA

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

//#include "esp_heap_caps.h"
//#include "esp_heap_trace.h"
#include "esp32_i2s_parallel.h"

#include "Adafruit_GFX.h"
#include "gamma.h"

/*

	This is example code to driver a p3(2121)64*32 -style RGB LED display. These types of displays do not have memory and need to be refreshed
	continuously. The display has 2 RGB inputs, 4 inputs to select the active line, a pixel clock input, a latch enable input and an output-enable
	input. The display can be seen as 2 64x16 displays consisting of the upper half and the lower half of the display. Each half has a separate 
	RGB pixel input, the rest of the inputs are shared.

	Each display half can only show one line of RGB pixels at a time: to do this, the RGB data for the line is input by setting the RGB input pins
	to the desired value for the first pixel, giving the display a clock pulse, setting the RGB input pins to the desired value for the second pixel,
	giving a clock pulse, etc. Do this 64 times to clock in an entire row. The pixels will not be displayed yet: until the latch input is made high, 
	the display will still send out the previously clocked in line. Pulsing the latch input high will replace the displayed data with the data just 
	clocked in.

	The 4 line select inputs select where the currently active line is displayed: when provided with a binary number (0-15), the latched pixel data
	will immediately appear on this line. Note: While clocking in data for a line, the *previous* line is still displayed, and these lines should
	be set to the value to reflect the position the *previous* line is supposed to be on.

	Finally, the screen has an OE input, which is used to disable the LEDs when latching new data and changing the state of the line select inputs:
	doing so hides any artifacts that appear at this time. The OE line is also used to dim the display by only turning it on for a limited time every
	line.

	All in all, an image can be displayed by 'scanning' the display, say, 100 times per second. The slowness of the human eye hides the fact that
	only one line is showed at a time, and the display looks like every pixel is driven at the same time.

	Now, the RGB inputs for these types of displays are digital, meaning each red, green and blue subpixel can only be on or off. This leads to a
	color palette of 8 pixels, not enough to display nice pictures. To get around this, we use binary code modulation.

	Binary code modulation is somewhat like PWM, but easier to implement in our case. First, we define the time we would refresh the display without
	binary code modulation as the 'frame time'. For, say, a four-bit binary code modulation, the frame time is divided into 15 ticks of equal length.

	We also define 4 subframes (0 to 3), defining which LEDs are on and which LEDs are off during that subframe. (Subframes are the same as a 
	normal frame in non-binary-coded-modulation mode, but are showed faster.)  From our (non-monochrome) input image, we take the (8-bit: bit 7 
	to bit 0) RGB pixel values. If the pixel values have bit 7 set, we turn the corresponding LED on in subframe 3. If they have bit 6 set,
	we turn on the corresponding LED in subframe 2, if bit 5 is set subframe 1, if bit 4 is set in subframe 0.

	Now, in order to (on average within a frame) turn a LED on for the time specified in the pixel value in the input data, we need to weigh the
	subframes. We have 15 pixels: if we show subframe 3 for 8 of them, subframe 2 for 4 of them, subframe 1 for 2 of them and subframe 1 for 1 of
	them, this 'automatically' happens. (We also distribute the subframes evenly over the ticks, which reduces flicker.)


	In this code, we use the I2S peripheral in parallel mode to achieve this. Essentially, first we allocate memory for all subframes. This memory
	contains a sequence of all the signals (2xRGB, line select, latch enable, output enable) that need to be sent to the display for that subframe.
	Then we ask the I2S-parallel driver to set up a DMA chain so the subframes are sent out in a sequence that satisfies the requirement that
	subframe x has to be sent out for (2^x) ticks. Finally, we fill the subframes with image data.

	We use a frontbuffer/backbuffer technique here to make sure the display is refreshed in one go and drawing artifacts do not reach the display.
	In practice, for small displays this is not really necessarily.

	Finally, the binary code modulated intensity of a LED does not correspond to the intensity as seen by human eyes. To correct for that, a
	luminance correction is used. See val2pwm.c for more info.

	Note: Because every subframe contains one bit of grayscale information, they are also referred to as 'bitplanes' by the code below.

*/

/***************************************************************************************/
/* ESP32 Pin Definition. You can change this, but best if you keep it as is...         */

#define R1_PIN  5
#define G1_PIN  17
#define B1_PIN  18
#define R2_PIN  19
#define G2_PIN  16
#define B2_PIN  25

#define A_PIN   26
#define B_PIN   4
#define C_PIN   27
#define D_PIN   -1 //2
#define E_PIN   -1

#define LAT_PIN 15
#define OE_PIN  13

#define CLK_PIN 14

/***************************************************************************************/
/* HUB75 RGB Panel definitions and DMA Config. It's best you don't change any of this. */

#define MATRIX_HEIGHT			    16
#define MATRIX_WIDTH			    32  //64
#define MATRIX_ROWS_IN_PARALLEL   	2 //2

// Panel Upper half RGB (numbering according to order in DMA gpio_bus configuration)
#define BIT_R1  (1<<0)   
#define BIT_G1  (1<<1)   
#define BIT_B1  (1<<2)   

// Panel Lower half RGB
#define BIT_R2  (1<<3)   
#define BIT_G2  (1<<4)   
#define BIT_B2  (1<<5)   

// Panel Control Signals
#define BIT_LAT (1<<6) 
#define BIT_OE  (1<<7)  

// Panel GPIO Pin Addresses (A, B, C, D etc..)
#define BIT_A (1<<8)    
#define BIT_B (1<<9)    
#define BIT_C (1<<10)   
#define BIT_D (1<<11)   
#define BIT_E (1<<12)   

// RGB Panel Constants / Calculated Values
#define COLOR_CHANNELS_PER_PIXEL 3 // 3 , only used in calculation of COLOR_DEPTH_BITS
#define PIXELS_PER_LATCH    32 //((MATRIX_WIDTH * MATRIX_HEIGHT) / MATRIX_HEIGHT) // = 64
#define COLOR_DEPTH_BITS    8 //(COLOR_DEPTH/COLOR_CHANNELS_PER_PIXEL)  //  = 8
#define ROWS_PER_FRAME      (MATRIX_HEIGHT/MATRIX_ROWS_IN_PARALLEL) //  = 2

/***************************************************************************************/
/* You really don't want to change this stuff                                          */

#define CLKS_DURING_LATCH   0 // ADDX is output directly using GPIO
#define MATRIX_I2S_MODE I2S_PARALLEL_BITS_16
#define MATRIX_DATA_STORAGE_TYPE uint16_t

#define ESP32_NUM_FRAME_BUFFERS           2 
//#define ESP32_OE_OFF_CLKS_AFTER_LATCH     1 // NOT USED
#define ESP32_I2S_CLOCK_SPEED (5000000UL)
#define COLOR_DEPTH 24 //24     

/***************************************************************************************/            

// note: sizeof(data) must be multiple of 32 bits, as ESP32 DMA linked list buffer address pointer must be word-aligned.
struct rowBitStruct {
    MATRIX_DATA_STORAGE_TYPE data[((MATRIX_WIDTH * MATRIX_HEIGHT)/ MATRIX_HEIGHT)+ CLKS_DURING_LATCH]; // was /32 -> Matrix Height
    // this evaluates to just MATRIX_DATA_STORAGE_TYPE data[64] really; 
    // and array of 64 uint16_t's or 32 unint16_t's in case of 16 Height
};

struct rowColorDepthStruct {
    rowBitStruct rowbits[COLOR_DEPTH_BITS];
};

struct frameStruct {
    rowColorDepthStruct rowdata[ROWS_PER_FRAME];
};

typedef struct rgb_24 {
    rgb_24() : rgb_24(0,0,0) {}
    rgb_24(uint8_t r, uint8_t g, uint8_t b) {
        red = r; green = g; blue = b;
    }
    rgb_24& operator=(const rgb_24& col);

    uint8_t red;
    uint8_t green;
    uint8_t blue;
} rgb_24;

/***************************************************************************************/   
class RGB32x16MatrixPanel_I2S_DMA : public Adafruit_GFX {
  // ------- PUBLIC -------
  public:
    RGB32x16MatrixPanel_I2S_DMA(bool _doubleBuffer = false) // doublebuffer always enabled, option makes no difference
      : Adafruit_GFX(MATRIX_WIDTH, MATRIX_HEIGHT), doubleBuffer(_doubleBuffer) {
      allocateDMAbuffers();
	  
		backbuf_id = 0;
		brightness = 4; // default to max brightness. Range 2..MATRIX_WIDTH-1
		
    }
	
    void begin(void)
    {
      allocateDMAbuffers();
      configureDMA(); //DMA and I2S configuration and setup
      flushDMAbuffer();
	    swapBuffer(false);
	    flushDMAbuffer();
	    swapBuffer(false);
    }
 
	
    // Draw pixels
    virtual void drawPixel(int16_t x, int16_t y, uint16_t color); // adafruit implementation
    inline void drawPixelRGB565(int16_t x, int16_t y, uint16_t color);
    inline void drawPixelRGB888(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b);
	  inline void drawPixelRGB24(int16_t x, int16_t y, rgb_24 color);
	  inline void clearDisplay(void);
void swapBuffer(boolean copy);

	// Original RGBmatrixPanel library used 3/3/3 color.  Later version used
// 4/4/4.  Then Adafruit_GFX (core library used across all Adafruit
// display devices now) standardized on 5/6/5.  The matrix still operates
// internally on 4/4/4 color, but all the graphics functions are written
// to expect 5/6/5...the matrix lib will truncate the color components as
// needed when drawing.  These next functions are mostly here for the
// benefit of older code using one of the original color formats.

// Promote 3/3/3 RGB to Adafruit_GFX 5/6/5
uint16_t Color333(uint8_t r, uint8_t g, uint8_t b) {
  // RRRrrGGGgggBBBbb
  return ((r & 0x7) << 13) | ((r & 0x6) << 10) |
         ((g & 0x7) <<  8) | ((g & 0x7) <<  5) |
         ((b & 0x7) <<  2) | ((b & 0x6) >>  1);
}
    
    // Color 444 is a 4 bit scale, so 0 to 15, color 565 takes a 0-255 bit value, so scale up by 255/15 (i.e. 17)!
    uint16_t color444(uint8_t r, uint8_t g, uint8_t b) { return color565(r*17,g*17,b*17); }

    // Converts RGB888 to RGB565
    uint16_t color565(uint8_t r, uint8_t g, uint8_t b); // This is what is used by Adafruit GFX!


/*
void RGBmatrixPanel::swapBuffers(boolean copy) {
  if(matrixbuff[0] != matrixbuff[1]) {
    // To avoid 'tearing' display, actual swap takes place in the interrupt
    // handler, at the end of a complete screen refresh cycle.
    swapflag = true;                  // Set flag here, then...
    while(swapflag == true) delay(1); // wait for interrupt to clear it
    if(copy == true)
      memcpy(matrixbuff[backindex], matrixbuff[1-backindex], WIDTH * nRows * 3);
  }
}
*/





    void setBrightness(int _brightness)
    {
	  // Change to set the brightness of the display, range of 2 to matrixWidth-1 
      if ((_brightness>1) && (_brightness<MATRIX_WIDTH))
        brightness=_brightness;
    }

uint16_t ColorHSV(
  long hue, uint8_t sat, uint8_t val, boolean gflag) {

  uint8_t  r, g, b, lo;
  uint16_t s1, v1;

  // Hue
  hue %= 1536;             // -1535 to +1535
  if(hue < 0) hue += 1536; //     0 to +1535
  lo = hue & 255;          // Low byte  = primary/secondary color mix
  switch(hue >> 8) {       // High byte = sextant of colorwheel
    case 0 : r = 255     ; g =  lo     ; b =   0     ; break; // R to Y
    case 1 : r = 255 - lo; g = 255     ; b =   0     ; break; // Y to G
    case 2 : r =   0     ; g = 255     ; b =  lo     ; break; // G to C
    case 3 : r =   0     ; g = 255 - lo; b = 255     ; break; // C to B
    case 4 : r =  lo     ; g =   0     ; b = 255     ; break; // B to M
    default: r = 255     ; g =   0     ; b = 255 - lo; break; // M to R
  }

  // Saturation: add 1 so range is 1 to 256, allowig a quick shift operation
  // on the result rather than a costly divide, while the type upgrade to int
  // avoids repeated type conversions in both directions.
  s1 = sat + 1;
  r  = 255 - (((255 - r) * s1) >> 8);
  g  = 255 - (((255 - g) * s1) >> 8);
  b  = 255 - (((255 - b) * s1) >> 8);

  // Value (brightness) & 16-bit color reduction: similar to above, add 1
  // to allow shifts, and upgrade to int makes other conversions implicit.
  v1 = val + 1;
  if(gflag) { // Gamma-corrected color?
    r = pgm_read_byte(&gamma_table[(r * v1) >> 8]); // Gamma correction table maps
    g = pgm_read_byte(&gamma_table[(g * v1) >> 8]); // 8-bit input to 4-bit output
    b = pgm_read_byte(&gamma_table[(b * v1) >> 8]);
  } else { // linear (uncorrected) color
    r = (r * v1) >> 12; // 4-bit results
    g = (g * v1) >> 12;
    b = (b * v1) >> 12;
  }
  return (r << 12) | ((r & 0x8) << 8) | // 4/4/4 -> 5/6/5
         (g <<  7) | ((g & 0xC) << 3) |
         (b <<  1) | ( b        >> 3);
}

   // ------- PRIVATE -------
  private:
  
  void allocateDMAbuffers() 
  {
  	matrixUpdateFrames = (frameStruct *)heap_caps_malloc(sizeof(frameStruct) * ESP32_NUM_FRAME_BUFFERS, MALLOC_CAP_DMA);
  	Serial.printf("Allocating Refresh Buffer:\r\nDMA Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_DMA), heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    Serial.printf("Sizeof %d", sizeof(frameStruct) * ESP32_NUM_FRAME_BUFFERS);
  } // end initMatrixDMABuffer()
	
	void flushDMAbuffer()
	{
		 //Serial.printf("Flushing buffer %d", backbuf_id);
		  // Need to wipe the contents of the matrix buffers or weird things happen.
		  for (int y=0;y<MATRIX_HEIGHT; y++)
			for (int x=0;x<MATRIX_WIDTH; x++)
			  updateMatrixDMABuffer( x, y, 0, 0, 0);
	}

  void clearMatrixDMABuffer();
  void configureDMA(); // Get everything setup. Refer to the .c file
  // Paint a pixel to the DMA buffer directly
  void updateMatrixDMABuffer(int16_t x, int16_t y, uint8_t red, uint8_t green, uint8_t blue);
 	// Internal variables
 	bool dma_configuration_success;
  bool doubleBuffer;
  	
  	// Pixel data is organized from LSB to MSB sequentially by row, from row 0 to row matrixHeight/matrixRowsInParallel (two rows of pixels are refreshed in parallel)
  frameStruct *matrixUpdateFrames;
  
  	int  lsbMsbTransitionBit;
  	int  refreshRate; 
  	
  	int  backbuf_id; // which buffer is the DMA backbuffer, as in, which one is not active so we can write to it
    int  brightness;

}; // end Class header

/***************************************************************************************/   

inline void RGB32x16MatrixPanel_I2S_DMA::drawPixel(int16_t x, int16_t y, uint16_t color) 
{
  drawPixelRGB565( x, y, color);
} 

// For adafruit
inline void RGB32x16MatrixPanel_I2S_DMA::drawPixelRGB565(int16_t x, int16_t y, uint16_t color) 
{
  uint8_t r = ((((color >> 11) & 0x1F) * 527) + 23) >> 6;
  uint8_t g = ((((color >> 5) & 0x3F) * 259) + 33) >> 6;
  uint8_t b = (((color & 0x1F) * 527) + 23) >> 6;
  
  updateMatrixDMABuffer( x, y, r, g, b);
}

inline void RGB32x16MatrixPanel_I2S_DMA::drawPixelRGB888(int16_t x, int16_t y, uint8_t r, uint8_t g,uint8_t b) 
{
  updateMatrixDMABuffer( x, y, r, g, b);
}

inline void RGB32x16MatrixPanel_I2S_DMA::drawPixelRGB24(int16_t x, int16_t y, rgb_24 color) 
{
  updateMatrixDMABuffer( x, y, color.red, color.green, color.blue);
}
// clear everything
inline void RGB32x16MatrixPanel_I2S_DMA::clearDisplay(void) {
		 //Serial.printf("Flushing buffer %d\n", backbuf_id);
	  // Need to wipe the contents of the matrix buffers or weird things happen.
		 for (int y=0;y<MATRIX_HEIGHT; y++)
			for (int x=0;x<MATRIX_WIDTH; x++)
			  updateMatrixDMABuffer( x, y, 0, 0, 0);
}
// Pass 8-bit (each) R,G,B, get back 16-bit packed color
//https://github.com/squix78/ILI9341Buffer/blob/master/ILI9341_SPI.cpp
inline uint16_t RGB32x16MatrixPanel_I2S_DMA::color565(uint8_t r, uint8_t g, uint8_t b) {

  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
#endif
