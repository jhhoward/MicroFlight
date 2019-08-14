#include "Defines.h"
#include "Game.h"
#include "Platform.h"
#include "FixedMath.h"
#include <math.h>
#include <stdlib.h>
//#include <stdio.h>

#include "Generated/TextureData.inc.h"

// When enabled will effectively render at quarter resolution
#define USE_COARSE_RENDERING 1

struct ScreenPoint
{
	int x, y;
	fixed16_t z;
};

struct Camera
{
	Vector3s position;
	Matrix3x3 rotation;
	Matrix3x3 invRotation;
	angle_t pitch, roll, yaw;
};

Camera camera;

void Game::Init()
{
	camera.rotation = Matrix3x3::Identity();
	camera.position = Vector3s(64 * 16, 75, 64 * 16);

	// White out areas for HUD
	for (int x = 0; x < 38; x++)
	{
		for (int y = 0; y < 20; y++)
		{
			Platform::PutPixel(x + 1, y + DISPLAY_HEIGHT - 21, COLOUR_WHITE);
			Platform::PutPixel(x + DISPLAY_WIDTH - 39, y + DISPLAY_HEIGHT - 21, COLOUR_WHITE);
		}
	}
}

ScreenPoint ProjectPoint(const Vector3s& inPoint)
{
	constexpr fixed16_t multiplier = 64.0f;

	ScreenPoint result;
	
	if (inPoint.z != 0)
	{
		fixed16_t w = multiplier / inPoint.z;
		result.x = DISPLAY_WIDTH / 2 + int(inPoint.x * w);
		result.y = DISPLAY_HEIGHT / 2 - int(inPoint.y * w);
		result.z = inPoint.z;
	}
	else
	{
		result.x = result.y = 0;
		result.z = 0;
	}

	return result;
}

Vector3s WorldToView(const Vector3s& inPoint)
{
	Vector3s worldPosition = inPoint - camera.position;
	return camera.invRotation * worldPosition;
}

const uint8_t ditherPattern1[] =
{
	1, 3,
	4, 2
};
const uint8_t ditherPattern2[] =
{
	4, 2,
	1, 3,
};

inline Vector3b Lerp(const Vector3b& a, const Vector3b& b, fixed8_t alpha)
{
	const fixed8_t oneMinusAlpha = fixed8_t::FromRaw(64 - alpha.rawValue);
	return Vector3b(
		fixed8_t::FromRaw((a.x.rawValue * oneMinusAlpha.rawValue + b.x.rawValue * alpha.rawValue) >> FixedFractionalBits),
		fixed8_t::FromRaw((a.y.rawValue * oneMinusAlpha.rawValue + b.y.rawValue * alpha.rawValue) >> FixedFractionalBits),
		fixed8_t::FromRaw((a.z.rawValue * oneMinusAlpha.rawValue + b.z.rawValue * alpha.rawValue) >> FixedFractionalBits));
}
	
#if USE_COARSE_RENDERING
void Game::Draw()
{
	constexpr fixed16_t groundHeight = 0;
	constexpr fixed16_t cloudHeight = 128;
	constexpr int maxDrawDistance = 2000;
	constexpr int skyColour = 3;
	constexpr int groundColour = 2;
	constexpr fixed8_t half = fixed8_t::FromRaw(1 << (FixedFractionalBits - 1));

	// Calculate world view direction for each corner of the viewport
	Vector3b topLeftViewDir = camera.rotation * Vector3b(-1, half, 1);
	Vector3b topRightViewDir = camera.rotation * Vector3b(1, half, 1);
	Vector3b bottomLeftViewDir = camera.rotation * Vector3b(-1, -half, 1);
	Vector3b bottomRightViewDir = camera.rotation * Vector3b(1, -half, 1);

	for(uint8_t x = 0; x < DISPLAY_WIDTH; x += 2)
	{
		const uint8_t* ditherPattern = ditherPattern1;
		uint8_t* bufferPtr = &Platform::GetScreenBuffer()[x];
		uint8_t buffer1 = 0, buffer2 = 0;
		uint8_t bufferPos = 0;
		uint8_t writeMask = 1;

		// Interpolate view directions for this column 
		fixed8_t viewXAlpha = fixed8_t::FromRaw(x / 2);
		Vector3b viewDirTop = Lerp(topLeftViewDir, topRightViewDir, viewXAlpha);
		Vector3b viewDirBottom = Lerp(bottomLeftViewDir, bottomRightViewDir, viewXAlpha);

		// Left and right thirds are obscured by MFD so render less
		uint8_t displayHeight = x < 40 || x >= DISPLAY_WIDTH - 40 ? DISPLAY_HEIGHT - 24 : DISPLAY_HEIGHT - 16;

		for(uint8_t y = 0; y < displayHeight; y += 2)
		{
			uint8_t outColour = skyColour;

			Vector3b worldDir = Lerp(viewDirTop, viewDirBottom, fixed8_t::FromRaw(y + 8));

			if(worldDir.y < fixed8_t::FromRaw(-4))
			{
				// Vector hits the ground plane
				fixed16_t distance = FixedMath::QuickDivide(fixed16_t(camera.position.y), -worldDir.y);

				fixed16_t intersectionX = camera.position.x + worldDir.x * distance;
				fixed16_t intersectionZ = camera.position.z + worldDir.z * distance;

				// Convert to texture space 64x64
				uint8_t u = ((int)(intersectionX) >> 4) & 63;
				uint8_t v = ((int)(intersectionZ) >> 4) & 63;

				int index = v * 64 + u;
				outColour = pgm_read_byte(&mapTexture[index]);
			}
			else if (worldDir.y < 0)
			{
				// Hit the ground plane but near the horizon so just assume ground colour to avoid overflow problems
				outColour = groundColour;
			}
			if (worldDir.y > fixed8_t::FromRaw(4))
			{
				// Vector has hit the cloud plane
				fixed16_t distance = FixedMath::QuickDivide(fixed16_t(cloudHeight - camera.position.y), worldDir.y);
				fixed16_t intersectionX = camera.position.x + worldDir.x * distance;
				fixed16_t intersectionZ = camera.position.z + worldDir.z * distance;

				// Convert to texture space 32x32
				uint8_t u = ((int)(intersectionX) >> 4) & 31;
				uint8_t v = ((int)(intersectionZ) >> 4) & 31;

				int index = v * 32 + u;
				outColour = pgm_read_byte(&cloudTexture[index]);
			}
			
			// Fill in 2x2 pixels based on dither pattern
			if(outColour >= ditherPattern[0])
			{
				buffer1 |= writeMask;
			}
			if(outColour >= ditherPattern[1])
			{
				buffer2 |= writeMask;
			}
			writeMask <<= 1;
			if(outColour >= ditherPattern[2])
			{
				buffer1 |= writeMask;
			}
			if(outColour >= ditherPattern[3])
			{
				buffer2 |= writeMask;
			}
			writeMask <<= 1;
			
			bufferPos++;
			if(bufferPos == 4)
			{
				// Write out buffer and move on to the next 8 pixels
				bufferPtr[0] = buffer1;
				bufferPtr[1] = buffer2;
				bufferPtr += DISPLAY_WIDTH;
				writeMask = 1;
				bufferPos = 0;
				buffer1 = 0;
				buffer2 = 0;
			}
		}
	}

	// Draw HUD outline
	{
		uint8_t* bufferPtr = &Platform::GetScreenBuffer()[40];
		(*bufferPtr) = 1;
		bufferPtr += DISPLAY_WIDTH;
		for (int y = 1; y < DISPLAY_HEIGHT / 8; y++)
		{
			*bufferPtr = 0;
			bufferPtr += DISPLAY_WIDTH;
		}
		bufferPtr = &Platform::GetScreenBuffer()[DISPLAY_WIDTH - 40];
		(*bufferPtr) = 1;
		bufferPtr += DISPLAY_WIDTH;
		for (int y = 1; y < DISPLAY_HEIGHT / 8; y++)
		{
			*bufferPtr = 0;
			bufferPtr += DISPLAY_WIDTH;
		}

		for (int x = 41; x < DISPLAY_WIDTH - 40; x++)
		{
			Platform::GetScreenBuffer()[x] &= 0xfe;
		}
	}

	/*
	// Draw minimap
	int startX = (int(camera.position.x) >> 4) - 18;
	int startY = (int(camera.position.z) >> 4) - 10;
	for (int x = 0; x < 36; x++)
	{
		for (int y = 0; y < 20; y++)
		{
			uint8_t u = (startX + x) & 63;
			uint8_t v = (startY + y) & 63;
			uint8_t mapValue = pgm_read_byte(&mapTexture[v * 64 + u]);
			Platform::PutPixel(x + 2, DISPLAY_HEIGHT - 3 - y, mapValue > 1 ? COLOUR_WHITE : COLOUR_BLACK);
		}
	}
	int headingX = (int)(FixedMath::Sin(yaw) * fixed16_t(4));
	int headingY = (int)(FixedMath::Cos(yaw) * fixed16_t(4));
	Platform::DrawLine(20, DISPLAY_HEIGHT - 12, 20 - headingX, DISPLAY_HEIGHT - 12 - headingY, COLOUR_BLACK);
	*/
}

#else

// 128x64 resolution rendering - too slow!
void Game::Draw()
{
	constexpr fixed16_t groundHeight = 0;
	constexpr fixed16_t cloudHeight = 128;
	constexpr int maxDrawDistance = 2000;
	constexpr int skyColour = 3;
	constexpr int groundColour = 2;

	camera.rotation = Matrix3x3::RotateZ(camera.roll) * Matrix3x3::RotateX(-camera.pitch) * Matrix3x3::RotateY(-camera.yaw);
	camera.position = camera.position + Vector3s(camera.rotation.Forward());

	if (camera.position.y < 1)
		camera.position.y = 1;
	if (camera.position.y > 120)
		camera.position.y = 120;

	Vector3b topLeftViewDir = camera.rotation * Vector3b(-1, fixed8_t::FromRaw(32), 1);
	Vector3b topRightViewDir = camera.rotation * Vector3b(1, fixed8_t::FromRaw(32), 1);
	Vector3b bottomLeftViewDir = camera.rotation * Vector3b(-1, fixed8_t::FromRaw(-32), 1);
	Vector3b bottomRightViewDir = camera.rotation * Vector3b(1, fixed8_t::FromRaw(-32), 1);

	for (uint8_t x = 0; x < DISPLAY_WIDTH; x++)
	{
		const uint8_t* ditherPattern = (x & 1) != 0 ? ditherPattern1 : ditherPattern1 + 2;
		uint8_t* bufferPtr = &Platform::GetScreenBuffer()[x];
		uint8_t buffer = 0;
		uint8_t bufferPos = 0;
		uint8_t writeMask = 1;

		fixed8_t viewXAlpha = fixed8_t::FromRaw(x / 2);
		Vector3b viewDirTop = Lerp(topLeftViewDir, topRightViewDir, viewXAlpha);
		Vector3b viewDirBottom = Lerp(bottomLeftViewDir, bottomRightViewDir, viewXAlpha);

		uint8_t displayHeight = x < 40 || x >= DISPLAY_WIDTH - 40 ? DISPLAY_HEIGHT - 24 : DISPLAY_HEIGHT - 16;

		for (uint8_t y = 0; y < displayHeight; y++)
		{
			uint8_t outColour = skyColour;

			Vector3b worldDir = Lerp(viewDirTop, viewDirBottom, fixed8_t::FromRaw(y + 8));

			if (worldDir.y < fixed8_t::FromRaw(-4))
			{
				fixed16_t distance = FixedMath::QuickDivide(fixed16_t(camera.position.y), -worldDir.y);
				fixed16_t intersectionX = camera.position.x + worldDir.x * distance;
				fixed16_t intersectionZ = camera.position.z + worldDir.z * distance;

				uint8_t u = ((int)(intersectionX) >> 4) & 63;
				uint8_t v = ((int)(intersectionZ) >> 4) & 63;

				int index = v * 64 + u;
				outColour = pgm_read_byte(&mapTexture[index]);
			}
			else if (worldDir.y < 0)
			{
				outColour = groundColour;
			}
			if (worldDir.y > fixed8_t::FromRaw(4))
			{
				fixed16_t distance = FixedMath::QuickDivide(fixed16_t(cloudHeight - camera.position.y), worldDir.y);
				fixed16_t intersectionX = camera.position.x + worldDir.x * distance;
				fixed16_t intersectionZ = camera.position.z + worldDir.z * distance;

				uint8_t u = ((int)(intersectionX) >> 4) & 31;
				uint8_t v = ((int)(intersectionZ) >> 4) & 31;

				int index = v * 32 + u;
				outColour = pgm_read_byte(&cloudTexture[index]);
			}

			if (outColour >= ditherPattern[y & 1])
			{
				buffer |= writeMask;
			}
			writeMask <<= 1;

			bufferPos++;
			if (bufferPos == 8)
			{
				*bufferPtr = buffer;
				bufferPtr += DISPLAY_WIDTH;
				writeMask = 1;
				bufferPos = 0;
				buffer = 0;
			}
		}
	}

	// Draw HUD outline
	{
		uint8_t* bufferPtr = &Platform::GetScreenBuffer()[40];
		(*bufferPtr) = 1;
		bufferPtr += DISPLAY_WIDTH;
		for (int y = 1; y < DISPLAY_HEIGHT / 8; y++)
		{
			*bufferPtr = 0;
			bufferPtr += DISPLAY_WIDTH;
		}
		bufferPtr = &Platform::GetScreenBuffer()[DISPLAY_WIDTH - 40];
		(*bufferPtr) = 1;
		bufferPtr += DISPLAY_WIDTH;
		for (int y = 1; y < DISPLAY_HEIGHT / 8; y++)
		{
			*bufferPtr = 0;
			bufferPtr += DISPLAY_WIDTH;
		}

		for (int x = 41; x < DISPLAY_WIDTH - 40; x++)
		{
			Platform::GetScreenBuffer()[x] &= 0xfe;
		}
	}
}

#endif

void Game::Tick()
{
	if (Platform::GetInput() & INPUT_DOWN)
	{
		camera.pitch++;
	}
	if (Platform::GetInput() & INPUT_UP)
	{
		camera.pitch--;
	}
	if (Platform::GetInput() & INPUT_LEFT)
	{
		camera.roll++;
	}
	if (Platform::GetInput() & INPUT_RIGHT)
	{
		camera.roll--;
	}

	int8_t rollSigned = (int8_t)(camera.roll);
	int8_t turn = rollSigned / 8;

	if (turn > 3)
		turn = 3;
	if (turn < -3)
		turn = -3;

	camera.yaw += turn;

	camera.rotation = Matrix3x3::RotateZ(camera.roll) * Matrix3x3::RotateX(-camera.pitch) * Matrix3x3::RotateY(-camera.yaw);
	camera.invRotation = camera.rotation.Transpose();
	camera.position = camera.position + Vector3s(camera.rotation.Forward());

	if (camera.position.y < 1)
		camera.position.y = 1;
	if (camera.position.y > 120)
		camera.position.y = 120;
}

