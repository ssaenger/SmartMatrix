/*
 * SmartMatrix Library - Background Layer Class
 *
 * Copyright (c) 2020 Louis Beaudoin (Pixelmatix)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>     

#define INLINE __attribute__( ( always_inline ) ) inline

/* RGB specific methods */

// call when backgroundBuffers and backgroundColorCorrectionLUT buffer is allocated outside of class
template <typename RGB, unsigned int optionFlags>
SMLayerBackgroundGFX<RGB, optionFlags>::SMLayerBackgroundGFX(RGB * buffer, uint16_t width, uint16_t height, color_chan_t * colorCorrectionLUT) : Adafruit_GFX(width, height) {
    backgroundBuffers[0] = buffer;
    backgroundBuffers[1] = buffer + (width * height);
    backgroundColorCorrectionLUT = colorCorrectionLUT;
    this->matrixWidth = width;
    this->matrixHeight = height;
#ifdef SM_WRITE_CALLBACK
    writeCb = NULL;
#endif
}

// call this when buffers should be sourced from malloc inside begin()
template <typename RGB, unsigned int optionFlags>
SMLayerBackgroundGFX<RGB, optionFlags>::SMLayerBackgroundGFX(uint16_t width, uint16_t height) : Adafruit_GFX(width, height) {
    this->matrixWidth = width;
    this->matrixHeight = height;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::begin(void) {
#if defined(ESP32)
    // PSRAM works for the background layers on ESP32, but having interrupt
    // code use PSRAM can cause hard deadlocks with WIFI + Reading/Writing flash
    // (FatFS) -- merlin
    #if defined(BOARD_HAS_PSRAM) && defined(SMARTMATRIX_USE_PSRAM)
    #pragma message "SmartMatrix using PSRAM on ESP32"
    #define ESPmalloc ps_malloc
    #else
    #define ESPmalloc malloc
    #endif
    if(!backgroundBuffers[0] && !backgroundBuffers[1]) {
        //printf("largest free block %d: \r\n", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
        backgroundBuffers[0] = (RGB *)ESPmalloc(sizeof(RGB) * this->matrixWidth * this->matrixHeight);
        assert(backgroundBuffers[0] != NULL);
        //printf("largest free block %d: \r\n", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
        backgroundBuffers[1] = (RGB *)ESPmalloc(sizeof(RGB) * this->matrixWidth * this->matrixHeight);
        assert(backgroundBuffers[1] != NULL);
        //printf("largest free block %d: \r\n", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
        memset((void *)backgroundBuffers[0], 0x00, sizeof(RGB) * this->matrixWidth * this->matrixHeight);
        memset((void *)backgroundBuffers[1], 0x00, sizeof(RGB) * this->matrixWidth * this->matrixHeight);
        //printf("largest free block %d: \r\n", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    }
    if(!backgroundColorCorrectionLUT) {
        backgroundColorCorrectionLUT = (color_chan_t *)malloc(sizeof(color_chan_t) * (sizeof(RGB) <= 3 ? 256 : 4096));
        assert(backgroundColorCorrectionLUT != NULL);
        //printf("largest free block %d: \r\n", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    }
#endif
    
    currentDrawBuffer = 0;
    currentRefreshBuffer = 1;
    swapPending = false;

#ifdef SM_BACKGROUND_GFX_OLD_DRAWING_FUNCTIONS
    font = (bitmap_font *) &apple3x5;
#endif

    currentDrawBufferPtr = backgroundBuffers[0];
    currentRefreshBufferPtr = backgroundBuffers[1];
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::handleBufferSwap(void) {
    if (!swapPending)
        return;

    unsigned char newDrawBuffer = currentRefreshBuffer;

    currentRefreshBuffer = currentDrawBuffer;
    currentDrawBuffer = newDrawBuffer;

    currentRefreshBufferPtr = backgroundBuffers[currentRefreshBuffer];
    currentDrawBufferPtr = backgroundBuffers[currentDrawBuffer];

    swapPending = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::frameRefreshCallback(void) {
    handleBufferSwap();

    if(sizeof(RGB) > 3)
        calculate12BitBackgroundLUT(backgroundColorCorrectionLUT, backgroundBrightness);
    else
        calculate8BitBackgroundLUT(backgroundColorCorrectionLUT, backgroundBrightness);
}

template <typename RGB, unsigned int optionFlags> template <typename RGB_OUT>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillRefreshRowTemplated(uint16_t hardwareY, RGB_OUT refreshRow[], int brightnessShifts) {
    RGB currentPixel;
    int i;

    // if the row requested is outside of this layer with the layerYOffset applied, we have nothing to do
    if(((hardwareY - layerYOffset) > (this->matrixHeight - 1)) || ((hardwareY - layerYOffset) < 0))
        return;

    RGB *ptr = currentRefreshBufferPtr + ((hardwareY - layerYOffset) * this->matrixWidth);

    int16_t iRangeMin = 0;
    int16_t iRangeMax = this->matrixWidth;
    if(layerXOffset < 0) {
        ptr -= layerXOffset; // increase ptr by offset
        iRangeMax += layerXOffset; // decrease range, with offset on the max end
    }

    if(layerXOffset > 0) {
        iRangeMin += layerXOffset; // decrease range, with offset on the min end
    }

    if(this->ccEnabled) {
        for(i=iRangeMin; i<iRangeMax; i++) {
            currentPixel = *ptr++;
            // load background pixel with color correction
            if(sizeof(RGB) <= 3) {
                // 24-bit source (8 bits per color channel): backgroundColorCorrectionLUT expects 8-bit value, returns 16-bit value
                refreshRow[i] = rgb48(backgroundColorCorrectionLUT[currentPixel.red << brightnessShifts],
                    backgroundColorCorrectionLUT[currentPixel.green << brightnessShifts],
                    backgroundColorCorrectionLUT[currentPixel.blue << brightnessShifts]);                
            } else {
                // 48-bit source (16 bits per color channel): backgroundColorCorrectionLUT expects 12-bit value, returns 16-bit value
                refreshRow[i] = rgb48(backgroundColorCorrectionLUT[currentPixel.red >> (4 - brightnessShifts)],
                    backgroundColorCorrectionLUT[currentPixel.green >> (4 - brightnessShifts)],
                    backgroundColorCorrectionLUT[currentPixel.blue >> (4 - brightnessShifts)]);
            }
        }
    } else {
        for(i=iRangeMin; i<iRangeMax; i++) {
            currentPixel = *ptr++;
            // load background pixel without color correction
            if(sizeof(RGB) <= 3) {
                // 24-bit source (8 bits per color channel)
                refreshRow[i] = rgb24(currentPixel.red << brightnessShifts,
                    currentPixel.green << brightnessShifts,
                    currentPixel.blue << brightnessShifts);
            } else {
                // 48-bit source (16 bits per color channel)
                refreshRow[i] = rgb48(currentPixel.red << brightnessShifts,
                    currentPixel.green << brightnessShifts,
                    currentPixel.blue << brightnessShifts);                
            }
        }
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillRefreshRow(uint16_t hardwareY, rgb48 refreshRow[], int brightnessShifts) {
    fillRefreshRowTemplated(hardwareY, refreshRow, brightnessShifts);
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillRefreshRow(uint16_t hardwareY, rgb24 refreshRow[], int brightnessShifts) {
    fillRefreshRowTemplated(hardwareY, refreshRow, brightnessShifts);
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::copyRefreshToDrawing() {
    memcpy((void *)currentDrawBufferPtr, (void *)currentRefreshBufferPtr, sizeof(RGB) * (this->matrixWidth * this->matrixHeight));
}

// waits until previous swap is complete
// waits until current swap is complete if copy is enabled
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::swapBuffers(bool copy) {
    while (swapPending);

    swapPending = true;

    if (copy) {
        while (swapPending);
#if 1
        // workaround for bizarre (optimization) bug - currentDrawBuffer and currentRefreshBuffer are volatile and are changed by an ISR while we're waiting for swapPending here.  They can't be used as parameters to memcpy directly though.  
        if(currentDrawBuffer)
            memcpy((void *)backgroundBuffers[1], (void *)backgroundBuffers[0], sizeof(RGB) * (this->matrixWidth * this->matrixHeight));
        else
            memcpy((void *)backgroundBuffers[0], (void *)backgroundBuffers[1], sizeof(RGB) * (this->matrixWidth * this->matrixHeight));
#else
        // Similar code also drawing from volatile variables doesn't work if optimization is turned on: currentDrawBuffer will be equal to currentRefreshBuffer and cause a crash from memcpy copying a buffer to itself.  Why?
        memcpy((void *)backgroundBuffers[currentDrawBuffer], (void *)backgroundBuffers[currentRefreshBuffer], sizeof(RGB) * (this->matrixWidth * this->matrixHeight));

        // this also doesn't work
        //copyRefreshToDrawing();  

        // first checking for (currentDrawBuffer != currentRefreshBuffer) prevents a crash by skipping the copy, but currentDrawBuffer should never be equal to currentRefreshBuffer, except briefly inside an ISR during handleBufferSwap() call
        //if(currentDrawBuffer != currentRefreshBuffer)     
        //   memcpy(backgroundBuffers[currentDrawBuffer], backgroundBuffers[currentRefreshBuffer], sizeof(RGB) * (this->matrixWidth * this->matrixHeight));
#endif
    }
}

/* RGB Specific Core Drawing Methods */

template <typename RGB, unsigned int optionFlags>
INLINE void SMLayerBackgroundGFX<RGB, optionFlags>::loadPixelToDrawBuffer(int16_t hwx, int16_t hwy, const RGB& color) {
    currentDrawBufferPtr[(hwy * this->matrixWidth) + hwx] = color;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawPixel(int16_t x, int16_t y, const RGB& color) {
    int hwx, hwy;

    // check for out of bounds coordinates
    if (x < 0 || y < 0 || x >= this->localWidth || y >= this->localHeight)
        return;

    // map pixel into hardware buffer before writing
    if (this->layerRotation == rotation0) {
        hwx = x;
        hwy = y;
    } else if (this->layerRotation == rotation180) {
        hwx = (this->matrixWidth - 1) - x;
        hwy = (this->matrixHeight - 1) - y;
    } else if (this->layerRotation == rotation90) {
        hwx = (this->matrixWidth - 1) - y;
        hwy = x;
    } else { /* if (layerRotation == rotation270)*/
        hwx = y;
        hwy = (this->matrixHeight - 1) - x;
    }

#ifdef SM_WRITE_CALLBACK
if (writeCb != NULL) {
    writeCb(wCbCtx, wCbCtxVal, (hwy * this->matrixWidth) + hwx, color);
    return;
}
#endif
    loadPixelToDrawBuffer(hwx, hwy, color);
}


/* RGB Specific Adafruit_GFX methods */

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if(passThruColorFlag)
        drawPixel(x, y, passThruColor);
    else
        drawPixel(x, y, (rgb16)color);
}

/* RGB Specific SmartMatrix Library 3.0 Backwards Compatibility */

#ifdef SM_BACKGROUND_GFX_BACKWARDS_COMPATIBILITY

// x0, x1, and y must be in bounds (0-this->localWidth/Height-1), x1 > x0
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawHardwareHLine(uint16_t x0, uint16_t x1, uint16_t y, const RGB& color) {
    int i;

    for (i = x0; i <= x1; i++) {
#ifdef SM_WRITE_CALLBACK

        if (writeCb != NULL) {
            writeCb(wCbCtx, wCbCtxVal, (y * this->matrixWidth) + i, color);
            continue;
        }
#endif
        loadPixelToDrawBuffer(i, y, color);
    }
}

// x, y0, and y1 must be in bounds (0-this->localWidth/Height-1), y1 > y0
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawHardwareVLine(uint16_t x, uint16_t y0, uint16_t y1, const RGB& color) {
    int i;

    for (i = y0; i <= y1; i++) {
#ifdef SM_WRITE_CALLBACK

        if (writeCb != NULL) {
            writeCb(wCbCtx, wCbCtxVal, (i * this->matrixWidth) + x, color);
            continue;
        }
#endif
        loadPixelToDrawBuffer(x, i, color);
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawFastHLine(int16_t x0, int16_t x1, int16_t y, const RGB& color) {
    // make sure line goes from x0 to x1
    if (x1 < x0)
        SWAPint(x1, x0);

    // check for completely out of bounds line
    if (x1 < 0 || x0 >= this->localWidth || y < 0 || y >= this->localHeight)
        return;

    // truncate if partially out of bounds
    if (x0 < 0)
        x0 = 0;

    if (x1 >= this->localWidth)
        x1 = this->localWidth - 1;

    // map to hardware drawline function
    if (this->layerRotation == rotation0) {
        drawHardwareHLine(x0, x1, y, color);
    } else if (this->layerRotation == rotation180) {
        drawHardwareHLine((this->matrixWidth - 1) - x1, (this->matrixWidth - 1) - x0, (this->matrixHeight - 1) - y, color);
    } else if (this->layerRotation == rotation90) {
        drawHardwareVLine((this->matrixWidth - 1) - y, x0, x1, color);
    } else { /* if (rotation == rotation270)*/
        drawHardwareVLine(y, (this->matrixHeight - 1) - x1, (this->matrixHeight - 1) - x0, color);
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawFastVLine(int16_t x, int16_t y0, int16_t y1, const RGB& color) {
    // make sure line goes from y0 to y1
    if (y1 < y0)
        SWAPint(y1, y0);

    // check for completely out of bounds line
    if (y1 < 0 || y0 >= this->localHeight || x < 0 || x >= this->localWidth)
        return;

    // truncate if partially out of bounds
    if (y0 < 0)
        y0 = 0;

    if (y1 >= this->localHeight)
        y1 = this->localHeight - 1;

    // map to hardware drawline function
    if (this->layerRotation == rotation0) {
        drawHardwareVLine(x, y0, y1, color);
    } else if (this->layerRotation == rotation180) {
        drawHardwareVLine((this->matrixWidth - 1) - x, (this->matrixHeight - 1) - y1, (this->matrixHeight - 1) - y0, color);
    } else if (this->layerRotation == rotation90) {
        drawHardwareHLine((this->matrixWidth - 1) - y1, (this->matrixWidth - 1) - y0, x, color);
    } else { /* if (layerRotation == rotation270)*/
        drawHardwareHLine(y0, y1, (this->matrixHeight - 1) - x, color);
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const RGB& color) {
    int i;
    // Loop only works if y1 > y0
    if (y0 > y1) {
        SWAPint(y0, y1);
    };
    // Putting the x coordinates in order saves multiple swaps in drawFastHLine
    if (x0 > x1) {
        SWAPint(x0, x1);
    };

    for (i = y0; i <= y1; i++) {
        drawFastHLine(x0, x1, i, color);
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillScreen(const RGB& color) {
    fillRectangle(0, 0, this->localWidth - 1, this->localHeight - 1, color);
}
#endif

/* RGB Specific Raw Buffer Access */

template <typename RGB, unsigned int optionFlags>
INLINE const RGB SMLayerBackgroundGFX<RGB, optionFlags>::readPixelFromDrawBuffer(int16_t hwx, int16_t hwy) {
    RGB pixel = currentDrawBufferPtr[(hwy * this->matrixWidth) + hwx];
    return pixel;
}

// reads pixel from drawing buffer, not refresh buffer
template<typename RGB, unsigned int optionFlags>
const RGB SMLayerBackgroundGFX<RGB, optionFlags>::readPixel(int16_t x, int16_t y) {
    int hwx, hwy;

    // check for out of bounds coordinates
    if (x < 0 || y < 0 || x >= this->localWidth || y >= this->localHeight)
        return (RGB){0, 0, 0};

    // map pixel into hardware buffer before reading
    if (this->layerRotation == rotation0) {
        hwx = x;
        hwy = y;
    } else if (this->layerRotation == rotation180) {
        hwx = (this->matrixWidth - 1) - x;
        hwy = (this->matrixHeight - 1) - y;
    } else if (this->layerRotation == rotation90) {
        hwx = (this->matrixWidth - 1) - y;
        hwy = x;
    } else { /* if (layerRotation == rotation270)*/
        hwx = y;
        hwy = (this->matrixHeight - 1) - x;
    }

    return readPixelFromDrawBuffer(hwx, hwy);
}

// return pointer to start of currentDrawBuffer, so application can do efficient loading of bitmaps
template <typename RGB, unsigned int optionFlags>
RGB *SMLayerBackgroundGFX<RGB, optionFlags>::backBuffer(void) {
    return currentDrawBufferPtr;
}

template<typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::setBackBuffer(RGB *newBuffer) {
  currentDrawBufferPtr = newBuffer;
}


template<typename RGB, unsigned int optionFlags>
RGB *SMLayerBackgroundGFX<RGB, optionFlags>::getRealBackBuffer() {
  return backgroundBuffers[currentDrawBuffer];
}

#ifdef SM_WRITE_CALLBACK
template<typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::registerWriteCallback(void* ctx, uint16_t ctxVal, void (*WriteCb)(void *ctx, uint16_t ctxVal, uint16_t pixNum, const RGB& color)) {
    writeCb = WriteCb;
    wCbCtx = ctx;
    wCbCtxVal = ctxVal;
}

template<typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::unregisterWriteCallback() {
    writeCb = NULL;
}
#endif

/* Shared */

// numShifts must be in range of 0-4, otherwise 16-bit to 12-bit conversion code breaks (would be an easy fix, but 4 is enough for APA102 GBC application)
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::setBrightnessShifts(int numShifts) {
    pendingIdealBrightnessShifts = numShifts;
}

template <typename RGB, unsigned int optionFlags>
int SMLayerBackgroundGFX<RGB, optionFlags>::getRequestedBrightnessShifts() {
    return idealBrightnessShifts;
}

template <typename RGB, unsigned int optionFlags>
bool SMLayerBackgroundGFX<RGB, optionFlags>::isLayerChanged() {
    return swapPending;
}

template <typename RGB, unsigned int optionFlags>
bool SMLayerBackgroundGFX<RGB, optionFlags>::isSwapPending(void) {
    return swapPending;
}

template<typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::setBrightness(uint8_t brightness) {
    backgroundBrightness = brightness;
}

template<typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::enableColorCorrection(bool enabled) {
    this->ccEnabled = enabled;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::setRotation(rotationDegrees newrotation) {
    this->layerRotation = newrotation;

    // update Adafruit_GFX rotation
    rotation = (uint8_t)newrotation;

    if (this->layerRotation == rotation0 || this->layerRotation == rotation180) {
        this->localWidth = this->matrixWidth;
        this->localHeight = this->matrixHeight;
        _width = this->matrixWidth;
        _height = this->matrixHeight;
    } else {
        this->localWidth = this->matrixHeight;
        this->localHeight = this->matrixWidth;
        _width = this->matrixHeight;
        _height = this->matrixWidth;        
    }
}

/* Shared SmartMatrix Library 3.0 Backwards Compatibility */

#ifdef SM_BACKGROUND_GFX_BACKWARDS_COMPATIBILITY
#ifdef SM_BACKGROUND_GFX_OLD_DRAWING_FUNCTIONS
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawChar(int16_t x, int16_t y, const RGB& charColor, char character) {
    int xcnt, ycnt;

    for (ycnt = 0; ycnt < font->Height; ycnt++) {
        for (xcnt = 0; xcnt < font->Width; xcnt++) {
            if (getBitmapFontPixelAtXY(character, xcnt, ycnt, font)) {
                drawPixel(x + xcnt, y + ycnt, charColor);
            }
        }
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawString(int16_t x, int16_t y, const RGB& charColor, const char text[]) {
    int xcnt, ycnt, offset = 0;
    char character;

    while ((character = text[offset++]) != '\0') {
        for (ycnt = 0; ycnt < font->Height; ycnt++) {
            for (xcnt = 0; xcnt < font->Width; xcnt++) {
                if (getBitmapFontPixelAtXY(character, xcnt, ycnt, font)) {
                    drawPixel(x + xcnt, y + ycnt, charColor);
                }
            }
        }
        x += font->Width;
    }
}

// draw string while clearing background
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawString(int16_t x, int16_t y, const RGB& charColor, const RGB& backColor, const char text[]) {
    int xcnt, ycnt, offset = 0;
    char character;

    while ((character = text[offset++]) != '\0') {
        for (ycnt = 0; ycnt < font->Height; ycnt++) {
            for (xcnt = 0; xcnt < font->Width; xcnt++) {
                if (getBitmapFontPixelAtXY(character, xcnt, ycnt, font)) {
                    drawPixel(x + xcnt, y + ycnt, charColor);
                } else {
                    drawPixel(x + xcnt, y + ycnt, backColor);
                }
            }
        }
        x += font->Width;
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawMonoBitmap(int16_t x, int16_t y, uint8_t width, uint8_t height,
  const RGB& bitmapColor, const uint8_t *bitmap) {
    int xcnt, ycnt;

    for (ycnt = 0; ycnt < height; ycnt++) {
        for (xcnt = 0; xcnt < width; xcnt++) {
            if (getBitmapPixelAtXY(xcnt, ycnt, width, height, bitmap)) {
                drawPixel(x + xcnt, y + ycnt, bitmapColor);
            }
        }
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::setFont(fontChoices newFont) {
    font = (bitmap_font *)fontLookup(newFont);
}

#else // !defined(SM_BACKGROUND_GFX_OLD_DRAWING_FUNCTIONS)

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawChar(int16_t x, int16_t y, const RGB& charColor, char character) {
    // gfxFont = NULL positions xy at top left of font.  Custom gfxFont positions xy at bottom left of font.  SmartMatrix Library used top left, so yAdvance needs to be used for custom font
    int16_t yOffset = 0;

    // yAdvance includes font height plus two spaces between lines, and we subtract one line for the top line of the font
    if(gfxFont)
        yOffset = ((int16_t)textsize_y * (uint8_t)pgm_read_byte(&gfxFont->yAdvance)) - 2 - 1;

    setCursor(x, y + yOffset);
    setTextColor(((rgb16)charColor).rgb);
    write(character);
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawString(int16_t x, int16_t y, const RGB& charColor, const char text[]) {
    // gfxFont = NULL positions xy at top left of font.  Custom gfxFont positions xy at bottom left of font.  SmartMatrix Library used top left, so yAdvance needs to be used for custom font
    int16_t yOffset = 0;

    // yAdvance includes font height plus two spaces between lines, and we subtract one line for the top line of the font
    if(gfxFont)
        yOffset = ((int16_t)textsize_y * (uint8_t)pgm_read_byte(&gfxFont->yAdvance)) - 2 - 1;

    setCursor(x, y + yOffset);
    setTextColor(((rgb16)charColor).rgb);
    write(text, strlen(text));
}

// draw string while clearing background
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawString(int16_t x, int16_t y, const RGB& charColor, const RGB& backColor, const char text[]) {
    // gfxFont = NULL positions xy at top left of font.  Custom gfxFont positions xy at bottom left of font.  SmartMatrix Library used top left, so yAdvance needs to be used for custom font
    int16_t yOffset = 0;

    // yAdvance includes font height plus two spaces between lines, and we subtract one line for the top line of the font
    if(gfxFont)
        yOffset = ((int16_t)textsize_y * (uint8_t)pgm_read_byte(&gfxFont->yAdvance)) - 2 - 1;

    setCursor(x, y + yOffset);
    setTextColor(((rgb16)charColor).rgb);
    write(text, strlen(text));
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawMonoBitmap(int16_t x, int16_t y, uint8_t width, uint8_t height,
  const RGB& bitmapColor, const uint8_t *bitmap) {
    int xcnt, ycnt;

    for (ycnt = 0; ycnt < height; ycnt++) {
        for (xcnt = 0; xcnt < width; xcnt++) {
            if (getBitmapPixelAtXY(xcnt, ycnt, width, height, bitmap)) {
                drawPixel(x + xcnt, y + ycnt, bitmapColor);
            }
        }
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::setFont(fontChoices newFont) {
    const GFXfont *gfxFont;

    switch(newFont) {
        default:
        case font3x5:
            gfxFont = &Apple4x6;
            break;
        case font5x7:
            gfxFont = &Apple5x7;
            break;
        case font6x10:
            gfxFont = &Apple6x10;
            break;
        case font8x13:
            gfxFont = &Apple8x13;
            break;
        case gohufont11:
            gfxFont = &GohuFont6x11;
            break;
        case gohufont11b:
            gfxFont = &GohuFont6x11b;
            break;
    }

    setFont(gfxFont);
}

#endif //SM_BACKGROUND_GFX_OLD_DRAWING_FUNCTIONS

#endif //SM_BACKGROUND_GFX_BACKWARDS_COMPATIBILITY

/* Replaced by Adafruit_GFX */

#ifdef SM_BACKGROUND_GFX_OLD_DRAWING_FUNCTIONS
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::bresteepline(int16_t x3, int16_t y3, int16_t x4, int16_t y4, const RGB& color) {
    // if point x3, y3 is on the right side of point x4, y4, change them
    if ((x3 - x4) > 0) {
        bresteepline(x4, y4, x3, y3, color);
        return;
    }

    int x = x3, y = y3, sum = x4 - x3,  Dx = 2 * (x4 - x3), Dy = abs(2 * (y4 - y3));
    int prirastokDy = ((y4 - y3) > 0) ? 1 : -1;

    for (int i = 0; i <= x4 - x3; i++) {
        drawPixel(y, x, color);
        x++;
        sum -= Dy;
        if (sum < 0) {
            y = y + prirastokDy;
            sum += Dx;
        }
    }
}

// algorithm from http://www.netgraphics.sk/bresenham-algorithm-for-a-line
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const RGB& color) {
    // if point x1, y1 is on the right side of point x2, y2, change them
    if ((x1 - x2) > 0) {
        drawLine(x2, y2, x1, y1, color);
        return;
    }
    // test inclination of line
    // function Math.abs(y) defines absolute value y
    if (abs(y2 - y1) > abs(x2 - x1)) {
        // line and y axis angle is less then 45 degrees
        // thats why go on the next procedure
        bresteepline(y1, x1, y2, x2, color); return;
    }
    // line and x axis angle is less then 45 degrees, so x is guiding
    // auxiliary variables
    int x = x1, y = y1, sum = x2 - x1, Dx = 2 * (x2 - x1), Dy = abs(2 * (y2 - y1));
    int prirastokDy = ((y2 - y1) > 0) ? 1 : -1;
    // draw line
    for (int i = 0; i <= x2 - x1; i++) {
        drawPixel(x, y, color);
        x++;
        sum -= Dy;
        if (sum < 0) {
            y = y + prirastokDy;
            sum += Dx;
        }
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawTriangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, const RGB& color) {
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x3, y3, color);
    drawLine(x1, y1, x3, y3, color);
}

// algorithm from http://en.wikipedia.org/wiki/Midpoint_circle_algorithm
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawCircle(int16_t x0, int16_t y0, uint16_t radius, const RGB& color)
{
    int a = radius, b = 0;
    int radiusError = 1 - a;

    if (radius == 0) {
        drawPixel(x0, y0, color);
        return;
    }

    while (a >= b)
    {
        drawPixel(a + x0, b + y0, color);
        drawPixel(b + x0, a + y0, color);
        drawPixel(-a + x0, b + y0, color);
        drawPixel(-b + x0, a + y0, color);
        drawPixel(-a + x0, -b + y0, color);
        drawPixel(-b + x0, -a + y0, color);
        drawPixel(a + x0, -b + y0, color);
        drawPixel(b + x0, -a + y0, color);

        b++;
        if (radiusError < 0)
            radiusError += 2 * b + 1;
        else
        {
            a--;
            radiusError += 2 * (b - a + 1);
        }
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const RGB& color) {
    drawFastHLine(x0, x1, y0, color);
    drawFastHLine(x0, x1, y1, color);
    drawFastVLine(x0, y0, y1, color);
    drawFastVLine(x1, y0, y1, color);
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawRoundRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
  uint16_t radius, const RGB& outlineColor) {
    if (x1 < x0)
        SWAPint(x1, x0);

    if (y1 < y0)
        SWAPint(y1, y0);

    // decrease large radius that would break shape
    if(radius > (x1-x0)/2)
        radius = (x1-x0)/2;
    if(radius > (y1-y0)/2)
        radius = (y1-y0)/2;

    int a = radius, b = 0;
    int radiusError = 1 - a;

    // draw straight part of outline
    drawFastHLine(x0 + radius, x1 - radius, y0, outlineColor);
    drawFastHLine(x0 + radius, x1 - radius, y1, outlineColor);
    drawFastVLine(x0, y0 + radius, y1 - radius, outlineColor);
    drawFastVLine(x1, y0 + radius, y1 - radius, outlineColor);

    // convert coordinates to point at center of rounded sections
    x0 += radius;
    x1 -= radius;
    y0 += radius;
    y1 -= radius;

    while (a >= b)
    {
        // this pair sweeps from far left towards right
        drawPixel(-a + x0, -b + y0, outlineColor);
        drawPixel(-a + x0, b + y1, outlineColor);

        // this pair sweeps from far right towards left
        drawPixel(a + x1, -b + y0, outlineColor);
        drawPixel(a + x1, b + y1, outlineColor);

        // this pair sweeps from very top towards bottom
        drawPixel(-b + x0, -a + y0, outlineColor);
        drawPixel(b + x1, -a + y0, outlineColor);

        // this pair sweeps from bottom up
        drawPixel(-b + x0, a + y1, outlineColor);
        drawPixel(b + x1, a + y1, outlineColor);

        b++;
        if (radiusError < 0) {
            radiusError += 2 * b + 1;
        } else {
            a--;
            radiusError += 2 * (b - a + 1);
        }
    }
}

// algorithm from drawCircle rearranged with hlines drawn between points on the radius
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillCircle(int16_t x0, int16_t y0, uint16_t radius, const RGB& outlineColor, const RGB& fillColor)
{
    int a = radius, b = 0;
    int radiusError = 1 - a;

    if (radius == 0)
        return;

    // only draw one line per row, skipping the top and bottom
    bool hlineDrawn = true;

    while (a >= b)
    {
        // this pair sweeps from horizontal center down
        drawPixel(a + x0, b + y0, outlineColor);
        drawPixel(-a + x0, b + y0, outlineColor);
        drawFastHLine((a - 1) + x0, (-a + 1) + x0, b + y0, fillColor);

        // this pair sweeps from bottom up
        drawPixel(b + x0, a + y0, outlineColor);
        drawPixel(-b + x0, a + y0, outlineColor);

        // this pair sweeps from horizontal center up
        drawPixel(-a + x0, -b + y0, outlineColor);
        drawPixel(a + x0, -b + y0, outlineColor);
        drawFastHLine((a - 1) + x0, (-a + 1) + x0, -b + y0, fillColor);

        // this pair sweeps from top down
        drawPixel(-b + x0, -a + y0, outlineColor);
        drawPixel(b + x0, -a + y0, outlineColor);

        if (b > 1 && !hlineDrawn) {
            drawFastHLine((b - 1) + x0, (-b + 1) + x0, a + y0, fillColor);
            drawFastHLine((b - 1) + x0, (-b + 1) + x0, -a + y0, fillColor);
            hlineDrawn = true;
        }

        b++;
        if (radiusError < 0) {
            radiusError += 2 * b + 1;
        } else {
            a--;
            hlineDrawn = false;
            radiusError += 2 * (b - a + 1);
        }
    }
}

// algorithm from drawCircle rearranged with hlines drawn between points on the raidus
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillCircle(int16_t x0, int16_t y0, uint16_t radius, const RGB& fillColor)
{
    int a = radius, b = 0;
    int radiusError = 1 - a;

    if (radius == 0)
        return;

    // only draw one line per row, skipping the top and bottom
    bool hlineDrawn = true;

    while (a >= b)
    {
        // this pair sweeps from horizontal center down
        drawFastHLine((a - 1) + x0, (-a + 1) + x0, b + y0, fillColor);

        // this pair sweeps from horizontal center up
        drawFastHLine((a - 1) + x0, (-a + 1) + x0, -b + y0, fillColor);

        if (b > 1 && !hlineDrawn) {
            drawFastHLine((b - 1) + x0, (-b + 1) + x0, a + y0, fillColor);
            drawFastHLine((b - 1) + x0, (-b + 1) + x0, -a + y0, fillColor);
            hlineDrawn = true;
        }

        b++;
        if (radiusError < 0) {
            radiusError += 2 * b + 1;
        } else {
            a--;
            hlineDrawn = false;
            radiusError += 2 * (b - a + 1);
        }
    }
}

// Code from http://www.sunshine2k.de/coding/java/TriangleRasterization/TriangleRasterization.html
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillFlatSideTriangleInt(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
  int16_t x3, int16_t y3, const RGB& color) {
    int16_t t1x, t2x, t1y, t2y;
    bool changed1 = false;
    bool changed2 = false;
    int8_t signx1, signx2, signy1, signy2;
    int16_t dx1, dy1, dx2, dy2;
    int i;
    int16_t e1, e2;

    t1x = t2x = x1; t1y = t2y = y1; // Starting points

    dx1 = abs(x2 - x1);
    dy1 = abs(y2 - y1);
    dx2 = abs(x3 - x1);
    dy2 = abs(y3 - y1);

    if (x2 - x1 < 0) {
        signx1 = -1;
    } else signx1 = 1;
    if (x3 - x1 < 0) {
        signx2 = -1;
    } else signx2 = 1;
    if (y2 - y1 < 0) {
        signy1 = -1;
    } else signy1 = 1;
    if (y3 - y1 < 0) {
        signy2 = -1;
    } else signy2 = 1;

    if (dy1 > dx1) {   // swap values
        SWAPint(dx1, dy1);
        changed1 = true;
    }
    if (dy2 > dx2) {   // swap values
        SWAPint(dy2, dx2);
        changed2 = true;
    }

    e1 = 2 * dy1 - dx1;
    e2 = 2 * dy2 - dx2;

    for (i = 0; i <= dx1; i++)
    {
        drawFastHLine(t1x, t2x, t1y, color);

        while (dx1 > 0 && e1 >= 0)
        {
            if (changed1)
                t1x += signx1;
            else
                t1y += signy1;
            e1 = e1 - 2 * dx1;
        }

        if (changed1)
            t1y += signy1;
        else
            t1x += signx1;

        e1 = e1 + 2 * dy1;

        /* here we rendered the next point on line 1 so follow now line 2
         * until we are on the same y-value as line 1.
         */
        while (t2y != t1y)
        {
            while (dx2 > 0 && e2 >= 0)
            {
                if (changed2)
                    t2x += signx2;
                else
                    t2y += signy2;
                e2 = e2 - 2 * dx2;
            }

            if (changed2)
                t2y += signy2;
            else
                t2x += signx2;

            e2 = e2 + 2 * dy2;
        }
    }
}

// Code from http://www.sunshine2k.de/coding/java/TriangleRasterization/TriangleRasterization.html
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillTriangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, const RGB& fillColor) {
    // Sort vertices
    if (y1 > y2) {
        SWAPint(y1, y2);
        SWAPint(x1, x2);
    }
    if (y1 > y3) {
        SWAPint(y1, y3);
        SWAPint(x1, x3);
    }
    if (y2 > y3) {
        SWAPint(y2, y3);
        SWAPint(x2, x3);
    }

    if (y2 == y3)
    {
        fillFlatSideTriangleInt(x1, y1, x2, y2, x3, y3, fillColor);
    }
    /* check for trivial case of top-flat triangle */
    else if (y1 == y2)
    {
        fillFlatSideTriangleInt(x3, y3, x1, y1, x2, y2, fillColor);
    }
    else
    {
        /* general case - split the triangle in a topflat and bottom-flat one */
        int16_t xtmp, ytmp;
        xtmp = (int)(x1 + ((float)(y2 - y1) / (float)(y3 - y1)) * (x3 - x1));
        ytmp = y2;
        fillFlatSideTriangleInt(x1, y1, x2, y2, xtmp, ytmp, fillColor);
        fillFlatSideTriangleInt(x3, y3, x2, y2, xtmp, ytmp, fillColor);
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillTriangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3,
  const RGB& outlineColor, const RGB& fillColor) {
    fillTriangle(x1, y1, x2, y2, x3, y3, fillColor);
    drawTriangle(x1, y1, x2, y2, x3, y3, outlineColor);
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillRoundRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
  uint16_t radius, const RGB& fillColor) {
    fillRoundRectangle(x0, y0, x1, y1, radius, fillColor, fillColor);
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillRoundRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
  uint16_t radius, const RGB& outlineColor, const RGB& fillColor) {
    if (x1 < x0)
        SWAPint(x1, x0);

    if (y1 < y0)
        SWAPint(y1, y0);

    // decrease large radius that would break shape
    if(radius > (x1-x0)/2)
        radius = (x1-x0)/2;
    if(radius > (y1-y0)/2)
        radius = (y1-y0)/2;

    int a = radius, b = 0;
    int radiusError = 1 - a;

    if (radius == 0) {
        fillRectangle(x0, y0, x1, y1, outlineColor, fillColor);
    }

    // draw straight part of outline
    drawFastHLine(x0 + radius, x1 - radius, y0, outlineColor);
    drawFastHLine(x0 + radius, x1 - radius, y1, outlineColor);
    drawFastVLine(x0, y0 + radius, y1 - radius, outlineColor);
    drawFastVLine(x1, y0 + radius, y1 - radius, outlineColor);

    // convert coordinates to point at center of rounded sections
    x0 += radius;
    x1 -= radius;
    y0 += radius;
    y1 -= radius;

    // only draw one line per row/column, skipping the sides
    bool hlineDrawn = true;
    bool vlineDrawn = true;

    while (a >= b)
    {
        // this pair sweeps from far left towards right
        drawPixel(-a + x0, -b + y0, outlineColor);
        drawPixel(-a + x0, b + y1, outlineColor);

        // this pair sweeps from far right towards left
        drawPixel(a + x1, -b + y0, outlineColor);
        drawPixel(a + x1, b + y1, outlineColor);

        if (!vlineDrawn) {
            drawFastVLine(-a + x0, (-b + 1) + y0, (b - 1) + y1, fillColor);
            drawFastVLine(a + x1, (-b + 1) + y0, (b - 1) + y1, fillColor);
            vlineDrawn = true;
        }

        // this pair sweeps from very top towards bottom
        drawPixel(-b + x0, -a + y0, outlineColor);
        drawPixel(b + x1, -a + y0, outlineColor);

        // this pair sweeps from bottom up
        drawPixel(-b + x0, a + y1, outlineColor);
        drawPixel(b + x1, a + y1, outlineColor);

        if (!hlineDrawn) {
            drawFastHLine((-b + 1) + x0, (b - 1) + x1, -a + y0, fillColor);
            drawFastHLine((-b + 1) + x0, (b - 1) + x1, a + y1, fillColor);
            hlineDrawn = true;
        }

        b++;
        if (radiusError < 0) {
            radiusError += 2 * b + 1;
        } else {
            a--;
            hlineDrawn = false;
            vlineDrawn = false;
            radiusError += 2 * (b - a + 1);
        }
    }

    // draw rectangle in center
    fillRectangle(x0 - a, y0 - a, x1 + a, y1 + a, fillColor);
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const RGB& outlineColor, const RGB& fillColor) {
    fillRectangle(x0, y0, x1, y1, fillColor);
    drawRectangle(x0, y0, x1, y1, outlineColor);
}

#else

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const RGB& color) {
    passThruColorFlag = true;
    passThruColor = color;

    drawLine(x1, y1, x2, y2, 0);

    passThruColorFlag = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawTriangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, const RGB& color) {
    passThruColorFlag = true;
    passThruColor = color;

    drawTriangle(x1, y1, x2, y2, x3, y3, 0);

    passThruColorFlag = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawCircle(int16_t x0, int16_t y0, uint16_t radius, const RGB& color) {
    passThruColorFlag = true;
    passThruColor = color;

    drawCircle(x0, y0, (int16_t)radius, 0);

    passThruColorFlag = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillCircle(int16_t x0, int16_t y0, uint16_t radius, const RGB& fillColor) {
    passThruColorFlag = true;
    passThruColor = fillColor;

    fillCircle(x0, y0, (int16_t)radius, 0);

    passThruColorFlag = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const RGB& color) {
    passThruColorFlag = true;
    passThruColor = color;

    drawRect(x0, y0, x1-x0, y1-y0, 0);

    passThruColorFlag = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawRoundRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
    uint16_t radius, const RGB& outlineColor) {
    passThruColorFlag = true;
    passThruColor = outlineColor;

    drawRoundRect(x0, y0, x1-x0, y1-y0, radius, 0);

    passThruColorFlag = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillCircle(int16_t x0, int16_t y0, uint16_t radius, const RGB& outlineColor, const RGB& fillColor) {
    passThruColorFlag = true;
    passThruColor = fillColor;

    fillCircle(x0, y0, (int16_t)radius, 0);

    passThruColor = outlineColor;
    drawCircle(x0, y0, (int16_t)radius, 0);

    passThruColorFlag = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillTriangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, const RGB& fillColor) {
    passThruColorFlag = true;
    passThruColor = fillColor;

    fillTriangle(x1, y1, x2, y2, x3, y3, 0);

    passThruColorFlag = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillTriangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3,
    const RGB& outlineColor, const RGB& fillColor) {
    passThruColorFlag = true;
    passThruColor = fillColor;

    fillTriangle(x1, y1, x2, y2, x3, y3, 0);
    drawTriangle(x1, y1, x2, y2, x3, y3, 0);

    passThruColorFlag = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillRoundRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
    uint16_t radius, const RGB& fillColor) {
    passThruColorFlag = true;
    passThruColor = fillColor;

    fillRoundRect(x0, y0, x1-x0, y1-y0, radius, 0);
    
    passThruColorFlag = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillRoundRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
    uint16_t radius, const RGB& outlineColor, const RGB& fillColor) {
    passThruColorFlag = true;
    passThruColor = fillColor;

    fillRoundRect(x0, y0, x1-x0, y1-y0, radius, 0);

    passThruColor = outlineColor;
    drawRoundRect(x0, y0, x1-x0, y1-y0, radius, 0);

    passThruColorFlag = false;
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::fillRectangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const RGB& outlineColor, const RGB& fillColor) {
    passThruColorFlag = true;
    passThruColor = fillColor;

    fillRect(x0, y0, x1-x0, y1-y0, 0);

    passThruColor = outlineColor;
    drawRect(x0, y0, x1-x0, y1-y0, 0);

    passThruColorFlag = false;
}
#endif

// from https://web.archive.org/web/20120225095359/http://homepage.smc.edu/kennedy_john/belipse.pdf
template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::drawEllipse(int16_t x0, int16_t y0, uint16_t radiusX, uint16_t radiusY, const RGB& color) {
    int16_t twoASquare = 2 * radiusX * radiusX;
    int16_t twoBSquare = 2 * radiusY * radiusY;
    
    int16_t x = radiusX;
    int16_t y = 0;
    int16_t changeX = radiusY * radiusY * (1 - (2 * radiusX));
    int16_t changeY = radiusX * radiusX;
    int16_t ellipseError = 0;
    int16_t stoppingX = twoBSquare * radiusX;
    int16_t stoppingY = 0;
    
    while (stoppingX >= stoppingY) {    // first set of points, y' > -1
        drawPixel(x0 + x, y0 + y, color);
        drawPixel(x0 - x, y0 + y, color);
        drawPixel(x0 - x, y0 - y, color);
        drawPixel(x0 + x, y0 - y, color);
        
        y++;
        stoppingY += twoASquare;
        ellipseError += changeY;
        changeY += twoASquare;
        
        if (((2 * ellipseError) + changeX) > 0) {
            x--;
            stoppingX -= twoBSquare;
            ellipseError += changeX;
            changeX += twoBSquare;
        }
    }
    
    // first point set is done, start the second set of points
    
    x = 0;
    y = radiusY;
    changeX = radiusY * radiusY;
    changeY = radiusX * radiusX * (1 - 2 * radiusY);
    ellipseError = 0;
    stoppingX = 0;
    stoppingY = twoASquare * radiusY;
    
    while (stoppingX <= stoppingY) {    // second set of points, y' < -1
        drawPixel(x0 + x, y0 + y, color);
        drawPixel(x0 - x, y0 + y, color);
        drawPixel(x0 - x, y0 - y, color);
        drawPixel(x0 + x, y0 - y, color);
        
        x++;
        stoppingX += twoBSquare;
        ellipseError += changeX;
        changeX += twoBSquare;
        
        if (((2 * ellipseError) + changeY) > 0) {
            y--;
            stoppingY -= twoASquare;
            ellipseError += changeY;
            changeY += twoASquare;
        }
    }
}

template <typename RGB, unsigned int optionFlags>
void SMLayerBackgroundGFX<RGB, optionFlags>::setRotation(uint8_t x) {
    setRotation((rotationDegrees)(x & 3));
}
