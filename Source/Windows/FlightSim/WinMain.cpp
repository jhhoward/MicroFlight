#include <SDL.h>
#include <stdio.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include "Defines.h"
#include "Game.h"
#include "Platform.h"
#include "FixedMath.h"
#include "lodepng.h"

#define ZOOM_SCALE 1
#define TONES_END 0x8000

SDL_Window* AppWindow;
SDL_Renderer* AppRenderer;
SDL_Surface* ScreenSurface;
SDL_Texture* ScreenTexture;

uint8_t InputMask = 0;
uint8_t sBuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];

bool isAudioEnabled = true;
bool IsRecording = false;
int CurrentRecordingFrame = 0;

struct KeyMap
{
	SDL_Scancode key;
	uint8_t mask;
};

std::vector<KeyMap> KeyMappings =
{
	{ SDL_SCANCODE_LEFT, INPUT_LEFT },
	{ SDL_SCANCODE_RIGHT, INPUT_RIGHT },
	{ SDL_SCANCODE_UP, INPUT_UP },
	{ SDL_SCANCODE_DOWN, INPUT_DOWN },
	{ SDL_SCANCODE_Z, INPUT_A },
	{ SDL_SCANCODE_X, INPUT_B },
};

constexpr int audioSampleRate = 48000;

const uint16_t* currentAudioPattern = nullptr;
int currentPatternBufferPos = 0;

void Play(const uint16_t* pattern)
{
	currentAudioPattern = pattern;
	currentPatternBufferPos = 0;
}

void swap(int16_t& a, int16_t& b)
{
	int16_t temp = a;
	a = b;
	b = temp;
}

void FillAudioBuffer(void *udata, uint8_t *stream, int len)
{
	int feedPos = 0;
	
	static int waveSamplesLeft = 0;
	static int noteSamplesLeft = 0;
	static int frequency = 0;
	static bool high = false;

	while(feedPos < len)
	{
		if(!isAudioEnabled)
		{
			while(feedPos < len)
			{
				stream[feedPos++] = 0;
			}
			return;
		}
		
		if(currentAudioPattern != nullptr)
		{
			if(noteSamplesLeft == 0)
			{
				frequency = currentAudioPattern[currentPatternBufferPos];
				uint16_t duration = currentAudioPattern[currentPatternBufferPos + 1];
				
				noteSamplesLeft = (audioSampleRate * duration) / 1000;
				
				waveSamplesLeft = frequency > 0 ? audioSampleRate / frequency : noteSamplesLeft;
				
				currentPatternBufferPos += 2;
				if(currentAudioPattern[currentPatternBufferPos] == TONES_END)
				{
					currentAudioPattern = nullptr;
				}
			}
		}
		
		if(frequency == 0)
		{
			while(feedPos < len && (!currentAudioPattern || noteSamplesLeft > 0))
			{
				stream[feedPos++] = 0;
				
				if(noteSamplesLeft > 0)
					noteSamplesLeft--;
			}
		}
		else
		{
			while(feedPos < len && waveSamplesLeft > 0 && noteSamplesLeft > 0)
			{
				int volume = 32;
				stream[feedPos++] = high ? 128 + volume : 128 - volume;
				waveSamplesLeft--;
				noteSamplesLeft--;
			}
			
			if(waveSamplesLeft == 0)
			{
				high = !high;
				waveSamplesLeft = audioSampleRate / frequency;
			}
		}
		
	}
}

void Platform::SetLED(uint8_t r, uint8_t g, uint8_t b)
{

}

void Platform::FillScreen(uint8_t colour)
{
	for (int y = 0; y < DISPLAY_HEIGHT; y++)
	{
		for (int x = 0; x < DISPLAY_WIDTH; x++)
		{
			PutPixel(x, y, colour);
		}
	}
}

void Platform::DrawSprite(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame)
{
	uint8_t w = bitmap[0];
	uint8_t h = bitmap[1];

	bitmap += 2;

	for (int j = 0; j < h; j++)
	{
		for (int i = 0; i < w; i++)
		{
			int blockY = j / 8;
			int blockIndex = (w * blockY + i) * 2;
			uint8_t pixels = bitmap[blockIndex];
			uint8_t maskPixels = bitmap[blockIndex + 1];
			uint8_t bitmask = 1 << (j % 8);

			if (maskPixels & bitmask)
			{
				if (x + i >= 0 && y + j >= 0)
				{
					if (pixels & bitmask)
					{
						PutPixel(x + i, y + j, 1);
					}
					else
					{
						PutPixel(x + i, y + j, 0);
					}
				}
			}
		}
	}
}

void Platform::DrawSprite(int16_t x, int16_t y, const uint8_t *bitmap, const uint8_t *mask, uint8_t frame, uint8_t mask_frame)
{
	uint8_t w = bitmap[0];
	uint8_t h = bitmap[1];

	bitmap += 2;

	for (int j = 0; j < h; j++)
	{
		for (int i = 0; i < w; i++)
		{
			int blockY = j / 8;
			int blockIndex = w * blockY + i;
			uint8_t pixels = bitmap[blockIndex];
			uint8_t maskPixels = mask[blockIndex];
			uint8_t bitmask = 1 << (j % 8);

			if (maskPixels & bitmask)
			{
				if (x + i >= 0 && y + j >= 0)
				{
					if (pixels & bitmask)
					{
						PutPixel(x + i, y + j, 1);
					}
					else
					{
						PutPixel(x + i, y + j, 0);
					}
				}
			}
		}
	}
}

//void Platform::DrawFastVLine(int16_t x, int16_t y1, int16_t y2, uint8_t pattern)
//{
//	for (int y = y1; y <= y2; y++)
//	{
//		if (y >= 0)
//		{
//			uint8_t patternIndex = y % 8;
//			uint8_t mask = 1 << patternIndex;
//			PutPixel(x, y, (mask & pattern) != 0 ? 1 : 0);
//		}
//	}
//}

void Platform::DrawFastVLine(int16_t x, int16_t y, uint8_t w, uint8_t pattern)
{
	while(w > 0)
	{
		uint8_t patternIndex = y % 8;
		uint8_t mask = 1 << patternIndex;
		PutPixel((uint8_t) x, (uint8_t) y, (mask & pattern) != 0 ? 1 : 0);
		y++;
		w--;
	}
}

/*
// since the AVR has no barrel shifter, we'll do a progmem lookup
const uint8_t topmask_[] PROGMEM = {
	0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80 };
const uint8_t bottommask_[] PROGMEM = {
	0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff };

void Platform::FillVLine(int8_t y0_, int8_t y1_, uint8_t pattern, uint8_t *screenptr) {
	if (y1_ < y0_ || y1_ < 0 || y0_ > 63) return;

	// clip (FIXME; clipping should be handled elsewhere)
	// cast to unsigned after clipping to simplify generated code below
	uint8_t y0 = y0_, y1 = y1_;
	if (y0_ < 0) y0 = 0;
	if (y1_ > 63) y1 = 63;

	uint8_t *page0 = screenptr + ((y0 & 0x38) << 4);
	uint8_t *page1 = screenptr + ((y1 & 0x38) << 4);
	if (page0 == page1) {
		uint8_t mask = pgm_read_byte(topmask_ + (y0 & 7))
			& pgm_read_byte(bottommask_ + (y1 & 7));
		*page0 &= ~mask;
		*page0 |= pattern & mask;  // fill y0..y1 in same page in one shot
	}
	else {
		uint8_t mask = pgm_read_byte(topmask_ + (y0 & 7));
		*page0 &= ~mask;
		*page0 |= pattern & mask;  // write top 1..8 pixels
		page0 += 128;
		while (page0 != page1) {
			*page0 = pattern;  // fill middle 8 pixels at a time
			page0 += 128;
		}
		mask = pgm_read_byte(bottommask_ + (y1 & 7));  // and bottom 1..8 pixels
		*page0 &= ~mask;
		*page0 |= pattern & mask;
	}
}
*/
// draw triangle into screen buffer
// 4 bits of subpixel accuracy, so screen is 128*16 x 64*16 = 2048x1024
// does efficient left-right clipping, and unoptimized top-bottom clipping
/*
void Platform::FillTriangle(
	int16_t x0, int16_t y0,
	int16_t x1, int16_t y1,
	int16_t x2, int16_t y2,
	uint8_t *pattern) 
{
	uint8_t *screen = sBuffer;

	//x0 *= 16;
	//x1 *= 16;
	//x2 *= 16;
	//y0 *= 16;
	//y1 *= 16;
	//y2 *= 16;


	// sort coordinates by x w/ optimal 3-sorting network
		{
			int16_t t;
			if (x0 > x1) {
				t = x1; x1 = x0; x0 = t;
				t = y1; y1 = y0; y0 = t;
			}
			if (x1 > x2) {
				t = x2; x2 = x1; x1 = t;
				t = y2; y2 = y1; y1 = t;
			}
			if (x0 > x1) {
				t = x1; x1 = x0; x0 = t;
				t = y1; y1 = y0; y0 = t;
			}
		}
		if (x2 <= 0 || x0 >= DISPLAY_WIDTH || x0 == x2) {
			// entire triangle is off screen or degenerate
			return;
		}

		// we want to fill in pixels which are *inside* the triangle
		// first we need to bump from x0,y0 to the next whole x (as x0 is
		// fractional)

		// we can use the standard ddx algorithm, with a pre-divided whole part
		// (normally assumed to be 0 in bresenham's)

		// first trapezoid: x0 to x1
		int16_t dx02 = x2 - x0;  // dx02 is guaranteed to be >0
		int16_t dy02 = 0;
		int16_t fy02 = (y2 - y0);
		// unroll divmod here, this is sadly much faster
		while (fy02 >= dx02) { ++dy02; fy02 -= dx02; }
		while (fy02 <= -dx02) { --dy02; fy02 += dx02; }

		// the top and bottom variable names are a misnomer, as they can also
		// be inverted. the "upside down" case is handled by checking in the
		// inner loop; ideally that would be a separate branch here.
		int8_t yt = y0 >> 4, yb = y0 >> 4;  // "top" and "bottom" y coords
		int16_t ytf = y0 & 15, ybf = y0 & 15;  // fractional part of each

											   // now we need to advance to the next whole pixel ((x0 + 15) & ~15)
											   // update yt, ytf along the slope of (dy01/dx01) for x0 - ((x0 + 15) & ~15) steps
											   // dx' = ((x0 + 15) & ~15) - x0
											   // y' = y0 + dx' * (y1-y0) / (x1-x0)
											   // yt/ytf are stored in fractions of (x1-x0)
											   // so
		int16_t dx01 = x1 - x0;
		if (x0 >= 0) {
			// round up to the next 16
			int8_t dx0 = x0; // (16 - x0) & 15;
			int16_t dyt = (dx0 * (y1 - y0));// >> 4;
			int16_t dyb = (dx0 * (y2 - y0));// >> 4;
			if (dx01) {
				// unroll divmod here, as it's a lot faster in the common case
				while (dyt >= dx01) { ++yt; dyt -= dx01; }
				while (dyt <= -dx01) { --yt; dyt += dx01; }
				ytf += dyt;
			}

			while (dyb >= dx02) { ++yb; dyb -= dx02; }
			while (dyb <= -dx02) { --yb; dyb += dx02; }
			ybf += dyb;
			x0 += dx0;
		}
		else {
			// if x0 is off the left edge of the screen, advance all the way to
			// the left edge of the screen
			int32_t dx0 = -x0;
			int32_t dyt = (dx0 * (y1 - y0));// >> 4;
			int32_t dyb = (dx0 * (y2 - y0));// >> 4;
			if (dx01) {
				yt += dyt / dx01;
				ytf += dyt % dx01;
			}
			yb += dyb / dx02;
			ybf += dyb % dx02;
			x0 = 0;
		}

		// x0 is now aligned to a whole number of pixels,
		// and yt/yb/ytf/ybf are initialized
		int8_t x = x0;// >> 4;
		uint8_t pattern_offset = x & 3;
		screen += x;
		// so technically we are downsampling both dy and dx here by a factor of 16,
		// but that's a wash, so we can still use the existing fractional slope and
		// step along exactly (x1-x0) / 16 pixels and we should be just short of x1
		if (x1 > DISPLAY_WIDTH) {
			// safe to modify x1 now as all slopes have been computed
			x1 = DISPLAY_WIDTH;
		}
		if (dx01) {
			int16_t dy01 = 0, fy01 = y1 - y0;
			while (fy01 >= dx01) { ++dy01; fy01 -= dx01; }
			while (fy01 <= -dx01) { --dy01; fy01 += dx01; }

			while (x0 < x1) {
				// now, we include the bottom pixel if ybf != 0, otherwise we don't
				if (yt < yb) {
					FillVLine(yt, yb - (ybf == 0 ? 1 : 0), pattern[pattern_offset], screen);
				}
				else {
					FillVLine(yb, yt - (ytf == 0 ? 1 : 0), pattern[pattern_offset], screen);
				}
				yt += dy01;
				ytf += fy01;
				if (ytf < 0) { yt--; ytf += dx01; }
				if (ytf >= dx01) { yt++; ytf -= dx01; }

				yb += dy02;
				ybf += fy02;
				if (ybf < 0) { yb--; ybf += dx02; }
				if (ybf >= dx02) { yb++; ybf -= dx02; }
				++screen;
				++pattern_offset;
				pattern_offset &= 3;
				x0 += 16;
			}
		}
		if (x0 >= DISPLAY_WIDTH) {
			return;  // off right edge of screen
		}
		// now x0 >= x1, we may have gone slightly too far.
		yt = y1;// >> 4;  // new top y
		ytf = y1 & 15;  // .. and fractional part
		int16_t dx12 = x2 - x1;
		int16_t dy12;
		int16_t fy12;
		// we need to adjust yt, ytf for the new slope of (y2-y1)/(x2-x1)
		if (dx12 == 0) {
			// we're already done
			return;
		}
		if (x1 >= 0) {
			// we're just making a sub-pixel adjustment
			int8_t dx0 = x0 - x1;
			int16_t dyt = (dx0 * (y2 - y1));// >> 4;
			while (dyt >= dx12) { ++yt; dyt -= dx12; }
			while (dyt <= -dx12) { --yt; dyt += dx12; }
		}
		else {
			// we're advancing to the left edge
			int32_t dx0 = x0 - x1;
			int32_t dyt = (dx0 * (y2 - y1));// >> 4;
			yt += dyt / dx12;
			ytf += dyt % dx12;
		}

		dy12 = 0;
		fy12 = y2 - y1;
		// we need to adjust yt, ytf for the new slope of (y2-y1)/(x2-x1)
		// 18.2 -> 18.7
		while (fy12 >= dx12) { ++dy12; fy12 -= dx12; }
		while (fy12 <= -dx12) { --dy12; fy12 += dx12; }

		// draw 2nd trapezoid
		if (x2 > DISPLAY_WIDTH) {  // clip to right edge
			x2 = DISPLAY_WIDTH;
		}
		while (x0 < x2) {
			// now, we include the bottom pixel if ybf != 0, otherwise we don't
			if (yt < yb) {
				FillVLine(yt, yb - (ybf == 0 ? 1 : 0), pattern[pattern_offset], screen);
			}
			else {
				FillVLine(yb, yt - (ytf == 0 ? 1 : 0), pattern[pattern_offset], screen);
			}
			yt += dy12;
			ytf += fy12;
			if (ytf < 0) { yt--; ytf += dx12; }
			if (ytf >= dx12) { yt++; ytf -= dx12; }

			yb += dy02;
			ybf += fy02;
			if (ybf < 0) { yb--; ybf += dx02; }
			if (ybf >= dx02) { yb++; ybf -= dx02; }
			++screen;
			++pattern_offset;
			pattern_offset &= 3;
			x0 += 16;
		}
}*/

/*void Platform::FillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color)
{

	int16_t a, b, y, last;
	// Sort coordinates by Y order (y2 >= y1 >= y0)
	if (y0 > y1)
	{
		swap(y0, y1); swap(x0, x1);
	}
	if (y1 > y2)
	{
		swap(y2, y1); swap(x2, x1);
	}
	if (y0 > y1)
	{
		swap(y0, y1); swap(x0, x1);
	}

	if (y0 == y2)
	{ // Handle awkward all-on-same-line case as its own thing
		a = b = x0;
		if (x1 < a)
		{
			a = x1;
		}
		else if (x1 > b)
		{
			b = x1;
		}
		if (x2 < a)
		{
			a = x2;
		}
		else if (x2 > b)
		{
			b = x2;
		}
		DrawFastHLine(a, y0, b - a + 1, color);
		return;
	}

	int16_t dx01 = x1 - x0,
		dy01 = y1 - y0,
		dx02 = x2 - x0,
		dy02 = y2 - y0,
		dx12 = x2 - x1,
		dy12 = y2 - y1,
		sa = 0,
		sb = 0;

	// For upper part of triangle, find scanline crossings for segments
	// 0-1 and 0-2.  If y1=y2 (flat-bottomed triangle), the scanline y1
	// is included here (and second loop will be skipped, avoiding a /0
	// error there), otherwise scanline y1 is skipped here and handled
	// in the second loop...which also avoids a /0 error here if y0=y1
	// (flat-topped triangle).
	if (y1 == y2)
	{
		last = y1;   // Include y1 scanline
	}
	else
	{
		last = y1 - 1; // Skip it
	}


	for (y = y0; y <= last; y++)
	{
		a = x0 + sa / dy01;
		b = x0 + sb / dy02;
		sa += dx01;
		sb += dx02;

		if (a > b)
		{
			swap(a, b);
		}

		DrawFastHLine(a, y, b - a + 1, color);
	}

	// For lower part of triangle, find scanline crossings for segments
	// 0-2 and 1-2.  This loop is skipped if y1=y2.
	sa = dx12 * (y - y1);
	sb = dx02 * (y - y0);

	for (; y <= y2; y++)
	{
		a = x1 + sa / dy12;
		b = x0 + sb / dy02;
		sa += dx12;
		sb += dx02;

		if (a > b)
		{
			swap(a, b);
		}

		DrawFastHLine(a, y, b - a + 1, color);
	}
}
*/

void Platform::FillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t* pattern)
{

	int16_t a, b, x, last;
	// Sort coordinates by Y order (y2 >= y1 >= y0)
	if (x0 > x1)
	{
		swap(y0, y1); swap(x0, x1);
	}
	if (x1 > x2)
	{
		swap(y2, y1); swap(x2, x1);
	}
	if (x0 > x1)
	{
		swap(y0, y1); swap(x0, x1);
	}

	if (x0 == x2)
	{ // Handle awkward all-on-same-line case as its own thing
		a = b = y0;
		if (y1 < a)
		{
			a = y1;
		}
		else if (y1 > b)
		{
			b = y1;
		}
		if (y2 < a)
		{
			a = y2;
		}
		else if (y2 > b)
		{
			b = y2;
		}
		DrawFastVLine(x0, a, b - a + 1, pattern[x0 & 1]);
		return;
	}

	int16_t dx01 = x1 - x0,
		dy01 = y1 - y0,
		dx02 = x2 - x0,
		dy02 = y2 - y0,
		dx12 = x2 - x1,
		dy12 = y2 - y1,
		sa = 0,
		sb = 0;

	// For upper part of triangle, find scanline crossings for segments
	// 0-1 and 0-2.  If y1=y2 (flat-bottomed triangle), the scanline y1
	// is included here (and second loop will be skipped, avoiding a /0
	// error there), otherwise scanline y1 is skipped here and handled
	// in the second loop...which also avoids a /0 error here if y0=y1
	// (flat-topped triangle).
	if (x1 == x2)
	{
		last = x1;   // Include x1 scanline
	}
	else
	{
		last = x1 - 1; // Skip it
	}


	for (x = x0; x <= last; x++)
	{
		a = y0 + sa / dx01;
		b = y0 + sb / dx02;
		sa += dy01;
		sb += dy02;

		if (a > b)
		{
			swap(a, b);
		}

		DrawFastVLine(x, a, b - a + 1, pattern[x & 1]);
	}

	// For lower part of triangle, find scanline crossings for segments
	// 0-2 and 1-2.  This loop is skipped if y1=y2.
	sa = dy12 * (x - x1);
	sb = dy02 * (x - x0);

	for (; x <= x2; x++)
	{
		a = y1 + sa / dx12;
		b = y0 + sb / dx02;
		sa += dy12;
		sb += dy02;

		if (a > b)
		{
			swap(a, b);
		}

		DrawFastVLine(x, a, b - a + 1, pattern[x & 1]);
	}
}

void Platform::DrawFastHLine(int16_t x, int16_t y, uint8_t w, uint8_t color)
{
	int16_t xEnd; // last x point + 1

				  // Do y bounds checks
	if (y < 0 || y >= DISPLAY_HEIGHT)
		return;

	xEnd = x + w;

	// Check if the entire line is not on the display
	if (xEnd <= 0 || x >= DISPLAY_WIDTH)
		return;

	// Don't start before the left edge
	if (x < 0)
		x = 0;

	// Don't end past the right edge
	if (xEnd > DISPLAY_WIDTH)
		xEnd = DISPLAY_WIDTH;

	// calculate actual width (even if unchanged)
	w = xEnd - x;

	// buffer pointer plus row offset + x offset
	register uint8_t *pBuf = sBuffer + ((y / 8) * DISPLAY_WIDTH) + x;

	// pixel mask
	register uint8_t mask = 1 << (y & 7);

	switch (color)
	{
	case COLOUR_WHITE:
		while (w--)
		{
			*pBuf++ |= mask;
		}
		break;

	case COLOUR_BLACK:
		mask = ~mask;
		while (w--)
		{
			*pBuf++ &= mask;
		}
		break;
	}
}

/*
// Adpated from https://github.com/a1k0n/arduboy3d/blob/master/draw.cpp
// since the AVR has no barrel shifter, we'll do a progmem lookup
const uint8_t topmask_[] PROGMEM = {
	0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80 };
const uint8_t bottommask_[] PROGMEM = {
	0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff };

void DrawVLine(uint8_t x, int8_t y0_, int8_t y1_, uint8_t pattern) 
{
	uint8_t *screenptr = arduboy.getBuffer() + x;

	if (y1_ < y0_ || y1_ < 0 || y0_ > 63) return;

	// clip (FIXME; clipping should be handled elsewhere)
	// cast to unsigned after clipping to simplify generated code below
	uint8_t y0 = y0_, y1 = y1_;
	if (y0_ < 0) y0 = 0;
	if (y1_ > 63) y1 = 63;

	uint8_t *page0 = screenptr + ((y0 & 0x38) << 4);
	uint8_t *page1 = screenptr + ((y1 & 0x38) << 4);
	if (page0 == page1) 
	{
		uint8_t mask = pgm_read_byte(topmask_ + (y0 & 7))
			& pgm_read_byte(bottommask_ + (y1 & 7));
		*page0 &= ~mask;
		*page0 |= pattern & mask;  // fill y0..y1 in same page in one shot
	}
	else
	{
		uint8_t mask = pgm_read_byte(topmask_ + (y0 & 7));
		*page0 &= ~mask;
		*page0 |= pattern & mask;  // write top 1..8 pixels
		page0 += 128;
		while (page0 != page1) 
		{
			*page0 = pattern;  // fill middle 8 pixels at a time
			page0 += 128;
		}
		mask = pgm_read_byte(bottommask_ + (y1 & 7));  // and bottom 1..8 pixels
		*page0 &= ~mask;
		*page0 |= pattern & mask;
	}
}
*/

void Platform::PutPixel(uint8_t x, uint8_t y, uint8_t colour)
{
	if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT)
	{
		return;
	}

	uint16_t row_offset;
	uint8_t bit;

	bit = 1 << (y & 7);
	row_offset = (y & 0xF8) * DISPLAY_WIDTH / 8 + x;
	uint8_t data = sBuffer[row_offset] | bit;
	if (!colour) data ^= bit;
	sBuffer[row_offset] = data;
}

uint8_t GetPixel(uint8_t x, uint8_t y)
{
	uint8_t row = y / 8;
	uint8_t bit_position = y % 8;
	return (sBuffer[(row*DISPLAY_WIDTH) + x] & (1 << bit_position)) >> bit_position;
}

uint8_t* Platform::GetScreenBuffer()
{
	return sBuffer;
}

void ResolveScreen(SDL_Surface* surface)
{
	Uint32 black = SDL_MapRGBA(surface->format, 0, 0, 0, 255); 
	Uint32 white = SDL_MapRGBA(surface->format, 255, 255, 255, 255);

	int bpp = surface->format->BytesPerPixel;
	
	for(int y = 0; y < DISPLAY_HEIGHT; y++)
	{
		for(int x = 0; x < DISPLAY_WIDTH; x++)
		{
			Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;
			
			*(Uint32 *)p = GetPixel(x, y) ? white : black;
		}
	}
}

void PutPixelImmediate(uint8_t x, uint8_t y, uint8_t colour)
{
	if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT)
	{
		return;
	}

	SDL_Surface* surface = ScreenSurface;

	Uint32 col = colour ? SDL_MapRGBA(surface->format, 255, 255, 255, 255) : SDL_MapRGBA(surface->format, 0, 0, 0, 255);

	int bpp = surface->format->BytesPerPixel;
	Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

	*(Uint32 *)p = col;
}

void DrawBitmapInternal(const uint8_t* data, uint16_t x, uint16_t y, uint8_t w, uint8_t h)
{
	for (int j = 0; j < h; j++)
	{
		for (int i = 0; i < w; i++)
		{
			int blockX = i / 8;
			int blockY = j / 8;
			int blocksPerWidth = w / 8;
			int blockIndex = blockY * blocksPerWidth + blockX;
			uint8_t pixels = data[blockIndex * 8 + i % 8];
			uint8_t mask = 1 << (j % 8);
			if (x + i >= 0 && y + j >= 0)
			{
				if (pixels & mask)
				{
					Platform::PutPixel(x + i, y + j, 1);
				}
				else
				{
					Platform::PutPixel(x + i, y + j, 0);
				}
			}
		}
	}
}

void Platform::DrawBitmap(int16_t x, int16_t y, const uint8_t *bitmap)
{
	DrawBitmapInternal(bitmap + 2, x, y, bitmap[0], bitmap[1]);
}

void Platform::DrawSolidBitmap(int16_t x, int16_t y, const uint8_t *bitmap)
{
	DrawBitmapInternal(bitmap + 2, x, y, bitmap[0], bitmap[1]);
}

void Platform::DrawBackground()
{
	for (int y = 0; y < DISPLAY_HEIGHT; y++)
	{
		for (int x = 0; x < DISPLAY_WIDTH; x++)
		{
			uint8_t col = y < DISPLAY_HEIGHT / 2 ? (x | y) & 1 ? COLOUR_BLACK : COLOUR_WHITE : (x ^ y) & 1 ? COLOUR_BLACK : COLOUR_WHITE; //192;
																																		  //col = 192;
			PutPixel(x, y, col);
		}
	}
}

void Platform::PlaySound(const uint16_t* audioPattern)
{
	Play(audioPattern);
}

bool Platform::IsAudioEnabled()
{
	return isAudioEnabled;
}

void Platform::SetAudioEnabled(bool isEnabled)
{
	isAudioEnabled = isEnabled;
}

uint8_t Platform::GetInput()
{
	uint8_t inputMask = 0;

	const uint8_t* keyStates = SDL_GetKeyboardState(NULL);

	for (unsigned int n = 0; n < KeyMappings.size(); n++)
	{
		if (keyStates[KeyMappings[n].key])
		{
			inputMask |= KeyMappings[n].mask;
		}
	}

	return inputMask;
}

void Platform::FillRect(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t colour)
{
	if (x1 < 0)
		x1 = 0;
	if (x2 >= DISPLAY_WIDTH)
		x2 = DISPLAY_WIDTH - 1;
	if (y1 < 0)
		y1 = 0;
	if (y2 >= DISPLAY_HEIGHT)
		y2 = DISPLAY_HEIGHT - 1;

	for (int y = y1; y <= y2; y++)
	{
		for (int x = x1; x <= x2; x++)
		{
			PutPixel((uint8_t)x, (uint8_t)y, colour);
		}
	}
}

void Platform::DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color)
{
	// bresenham's algorithm - thx wikpedia
	bool steep = abs(y1 - y0) > abs(x1 - x0);
	if (steep) {
		swap(x0, y0);
		swap(x1, y1);
	}

	if (x0 > x1) {
		swap(x0, x1);
		swap(y0, y1);
	}

	int16_t dx, dy;
	dx = x1 - x0;
	dy = abs(y1 - y0);

	int16_t err = dx / 2;
	int8_t ystep;

	if (y0 < y1)
	{
		ystep = 1;
	}
	else
	{
		ystep = -1;
	}

	for (; x0 <= x1; x0++)
	{
		if (steep)
		{
			PutPixel((uint8_t)y0, (uint8_t)x0, color);
		}
		else
		{
			PutPixel((uint8_t) x0, (uint8_t)y0, color);
		}

		err -= dy;
		if (err < 0)
		{
			y0 += ystep;
			err += dx;
		}
	}
}


void Platform::ExpectLoadDelay()
{

}

/*#include "Font.h"
void DumpFont()
{
	constexpr int numChars = 96;
	constexpr int charWidth = 4;
	constexpr int charHeight = 8;
	constexpr int pageWidth = numChars * charWidth;
	uint8_t fontPage[pageWidth * charHeight * 4];
	char tempStr[2] = { 0, 0 };

	for (int n = 0; n < numChars * charWidth * charHeight * 4; n++)
	{
		fontPage[n] = 255;
	}

	for (int n = 0; n < numChars; n++)
	{
		Platform::FillScreen(COLOUR_WHITE);
		tempStr[0] = (char) (n + 32);
		DrawString(tempStr, 0, 1);

		for (int y = 0; y < charHeight; y++)
		{
			for (int x = 0; x < charWidth; x++)
			{
				if (GetPixel(x, y) == COLOUR_BLACK)
				{
					int index = pageWidth * y + (n * charWidth) + x;
					fontPage[index * 4] = 0;
					fontPage[index * 4 + 1] = 0;
					fontPage[index * 4 + 2] = 0;
				}
			}
		}
	}

	lodepng::encode("font.png", fontPage, pageWidth, charHeight);
}*/

void DebugDisplayNow()
{
	ResolveScreen(ScreenSurface);
	SDL_UpdateTexture(ScreenTexture, NULL, ScreenSurface->pixels, ScreenSurface->pitch);
	SDL_Rect src, dest;
	src.x = src.y = dest.x = dest.y = 0;
	src.w = DISPLAY_WIDTH;
	src.h = DISPLAY_HEIGHT;
	dest.w = DISPLAY_WIDTH;
	dest.h = DISPLAY_HEIGHT;
	SDL_RenderCopy(AppRenderer, ScreenTexture, &src, &dest);
	SDL_RenderPresent(AppRenderer);

//	SDL_Delay(1000 / TARGET_FRAMERATE);
	//SDL_Delay(1);
}

int main(int argc, char* argv[])
{
	SDL_Init(SDL_INIT_EVERYTHING);

	SDL_CreateWindowAndRenderer(DISPLAY_WIDTH * ZOOM_SCALE, DISPLAY_HEIGHT * ZOOM_SCALE, SDL_WINDOW_RESIZABLE, &AppWindow, &AppRenderer);
	SDL_RenderSetLogicalSize(AppRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);

	ScreenSurface = SDL_CreateRGBSurface(0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 32,
		0x000000ff,
		0x0000ff00,
		0x00ff0000,
		0xff000000
	);
	ScreenTexture = SDL_CreateTexture(AppRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, ScreenSurface->w, ScreenSurface->h);

	SDL_SetWindowPosition(AppWindow, 1900 - DISPLAY_WIDTH * 2, 1020 - DISPLAY_HEIGHT);

	SDL_AudioSpec wanted;
	wanted.freq = audioSampleRate;
	wanted.format = AUDIO_U8;
	wanted.channels = 1;
	wanted.samples = 4096;
	wanted.callback = FillAudioBuffer;

	if (SDL_OpenAudio(&wanted, NULL) <0) {
		printf("Error: %s\n", SDL_GetError());
	}
	SDL_PauseAudio(0);
	
	//DumpFont();

	//SeedRandom((uint16_t)time(nullptr));
	SeedRandom(0);
	Game::Init();
	
	bool running = true;
	int playRate = 1;
	static int testAudio = 0;

	while (running)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:
				running = false;
				break;
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym)
				{
				case SDLK_ESCAPE:
					running = false;
					break;
				case SDLK_F12:
					{
						lodepng::encode(std::string("screenshot.png"), (unsigned char*)(ScreenSurface->pixels), ScreenSurface->w, ScreenSurface->h);
					}
					break;
				case SDLK_F11:
					IsRecording = !IsRecording;
					break;
				}
				break;
			case SDL_KEYUP:
				if (event.key.keysym.sym == SDLK_TAB)
					playRate = 1;
				break;
			}
		}

		SDL_SetRenderDrawColor(AppRenderer, 206, 221, 231, 255);
		SDL_RenderClear(AppRenderer);

		for (int n = 0; n < playRate; n++)
		{
			memset(ScreenSurface->pixels, 0, ScreenSurface->format->BytesPerPixel * ScreenSurface->w * ScreenSurface->h);
			
			Game::Tick();
			Game::Draw();
			//Map::DebugDraw();
			
			ResolveScreen(ScreenSurface);
		}

		if (IsRecording)
		{
			std::ostringstream filename;
			filename << "Frame";
			filename << std::setfill('0') << std::setw(5) << CurrentRecordingFrame << ".png";

			lodepng::encode(filename.str(), (unsigned char*)(ScreenSurface->pixels), ScreenSurface->w, ScreenSurface->h);
			CurrentRecordingFrame++;
		}

		SDL_UpdateTexture(ScreenTexture, NULL, ScreenSurface->pixels, ScreenSurface->pitch);
		SDL_Rect src, dest;
		src.x = src.y = dest.x = dest.y = 0;
		src.w = DISPLAY_WIDTH;
		src.h = DISPLAY_HEIGHT;
		dest.w = DISPLAY_WIDTH;
		dest.h = DISPLAY_HEIGHT;
		SDL_RenderCopy(AppRenderer, ScreenTexture, &src, &dest);
		SDL_RenderPresent(AppRenderer);

		SDL_Delay(1000 / TARGET_FRAMERATE);
	}

	return 0;
}
