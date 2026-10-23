#include "engine/render/world_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "control/control.hpp"
#include "engine/demomode.h"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "options.h"
#include "utils/display.h"
#include "utils/log.hpp"
#include "utils/sdl_wrap.h"
#include "utils/str_cat.hpp"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

float WorldScale = 1.0F;
std::optional<float> WorldScaleOverride;

std::optional<OwnedSurface> WorldBuffer;
Size WorldBufferSize;

/** Frame buffer viewport right after the world was scaled into it (before any UI). */
std::vector<uint8_t> Baseline;
Size BaselineSize;

/** Source column for each destination column when scaling the world into the frame buffer. */
std::vector<uint16_t> ColumnMap;
Size ColumnMapFor;
float ColumnMapScale = 0;

bool GameFrameInBuffer = false;
bool WorldLayerValid = false;

bool IsNearInteger(float value)
{
	return std::abs(value - std::round(value)) < 0.02F;
}

int CeilDiv(int value, float scale)
{
	return std::max(1, static_cast<int>(std::ceil(value / scale - 0.001F)));
}

} // namespace

float GetOutputScale()
{
#if !defined(USE_SDL1) && !defined(USE_SDL3)
	if (renderer == nullptr || HeadlessMode || gnScreenWidth == 0 || gnScreenHeight == 0)
		return 1.0F;
	int outputWidth;
	int outputHeight;
	if (SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight) != 0)
		return 1.0F;
	const float scale = std::min(static_cast<float>(outputWidth) / gnScreenWidth, static_cast<float>(outputHeight) / gnScreenHeight);
	return scale > 0 ? scale : 1.0F;
#else
	return 1.0F;
#endif
}

float GetWorldScale()
{
	return WorldScale;
}

bool IsWorldScaled()
{
	return WorldScale != 1.0F;
}

void SetWorldScaleForTesting(float scale)
{
	WorldScaleOverride = scale;
	WorldScale = scale;
}

bool UpdateWorldScale()
{
	float scale = 1.0F;
	if (WorldScaleOverride) {
		scale = *WorldScaleOverride;
	} else {
		// Demos replay recorded mouse positions, so they need the classic 1:1 geometry.
		bool nativeOnly = demo::IsRunning() || demo::IsRecording();
		if (const char *env = std::getenv("DETH_VISUAL_TEST"); env != nullptr && env[0] == '1')
			nativeOnly = false;
		if (!nativeOnly) {
			const float zoom = *GetOptions().Graphics.zoom / 100.0F;
			scale = std::clamp(zoom / GetOutputScale(), 0.25F, 4.0F);
			if (IsNearInteger(scale))
				scale = std::round(scale);
		}
	}
	if (scale == WorldScale)
		return false;
	WorldScale = scale;
	return true;
}

Size GetWorldViewSize()
{
	if (!IsWorldScaled())
		return { gnScreenWidth, gnViewportHeight };
	return { CeilDiv(gnScreenWidth, WorldScale), CeilDiv(gnViewportHeight, WorldScale) };
}

Surface GetWorldRenderTarget()
{
	const Size size = GetWorldViewSize();
	if (!WorldBuffer || WorldBufferSize != size) {
		WorldBuffer.emplace(size);
		WorldBufferSize = size;
		SDL_FillRect(WorldBuffer->surface, nullptr, 0);
	}
	return *WorldBuffer;
}

void PresentWorldIntoBuffer(const Surface &dst)
{
	if (!WorldBuffer)
		return;
	const Surface &src = *WorldBuffer;
	const int dstWidth = dst.w();
	const int dstHeight = dst.h();

	if (ColumnMapFor != Size { dstWidth, src.w() } || ColumnMapScale != WorldScale) {
		ColumnMap.resize(dstWidth);
		for (int x = 0; x < dstWidth; ++x) {
			ColumnMap[x] = static_cast<uint16_t>(std::min(static_cast<int>((x + 0.5F) / WorldScale), src.w() - 1));
		}
		ColumnMapFor = { dstWidth, src.w() };
		ColumnMapScale = WorldScale;
	}

	int lastSrcY = -1;
	for (int y = 0; y < dstHeight; ++y) {
		const int srcY = std::min(static_cast<int>((y + 0.5F) / WorldScale), src.h() - 1);
		uint8_t *out = dst.at(0, y);
		if (srcY == lastSrcY) {
			std::memcpy(out, dst.at(0, y - 1), dstWidth);
			continue;
		}
		const uint8_t *in = src.at(0, srcY);
		for (int x = 0; x < dstWidth; ++x) {
			out[x] = in[ColumnMap[x]];
		}
		lastSrcY = srcY;
	}

	BaselineSize = { dstWidth, dstHeight };
	Baseline.resize(static_cast<size_t>(dstWidth) * dstHeight);
	for (int y = 0; y < dstHeight; ++y) {
		std::memcpy(&Baseline[static_cast<size_t>(y) * dstWidth], dst.at(0, y), dstWidth);
	}
	WorldLayerValid = true;
}

void MarkGameFrame()
{
	GameFrameInBuffer = true;
	if (!IsWorldScaled())
		WorldLayerValid = false;
}

void InvalidateWorldLayer()
{
	GameFrameInBuffer = false;
	WorldLayerValid = false;
}

Point BufferToWorld(Point bufferPosition)
{
	if (!IsWorldScaled())
		return bufferPosition;
	return {
		static_cast<int>(std::floor((bufferPosition.x + 0.5F) / WorldScale)),
		static_cast<int>(std::floor((bufferPosition.y + 0.5F) / WorldScale)),
	};
}

Point WorldToBuffer(Point worldPosition)
{
	if (!IsWorldScaled())
		return worldPosition;
	return {
		static_cast<int>(std::lround(worldPosition.x * WorldScale)),
		static_cast<int>(std::lround(worldPosition.y * WorldScale)),
	};
}

Displacement WorldToBuffer(Displacement worldOffset)
{
	if (!IsWorldScaled())
		return worldOffset;
	return {
		static_cast<int>(std::lround(worldOffset.deltaX * WorldScale)),
		static_cast<int>(std::lround(worldOffset.deltaY * WorldScale)),
	};
}

#if !defined(USE_SDL1) && !defined(USE_SDL3)
namespace {

SDLTextureUniquePtr UiTexture;
SDLTextureUniquePtr WorldTexture;
Size UiTextureSize;
Size WorldTextureSize;
SDL_Renderer *TexturesRenderer = nullptr;

struct ScaleChoice {
	int prescale;
	SDL_ScaleMode mode;
};

/**
 * Integer scale factors use plain nearest neighbour. Fractional factors use
 * "sharp bilinear": an integer nearest prescale on the CPU, then a small linear
 * step on the GPU, which keeps pixels crisp without uneven pixel widths.
 */
ScaleChoice ChooseScale(float scale)
{
	if (*GetOptions().Graphics.scaleQuality == ScalingQuality::NearestPixel || IsNearInteger(scale) || scale < 1.0F)
		return { 1, IsNearInteger(scale) || *GetOptions().Graphics.scaleQuality == ScalingQuality::NearestPixel ? SDL_ScaleModeNearest : SDL_ScaleModeLinear };
	return { std::max(1, static_cast<int>(std::floor(scale))), SDL_ScaleModeLinear };
}

bool EnsureTexture(SDLTextureUniquePtr &texture, Size &current, Size wanted)
{
	if (texture && current == wanted)
		return true;
	texture.reset();
	SDL_Texture *created = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, wanted.width, wanted.height);
	if (created == nullptr) {
		LogError("DETH: could not create {}x{} texture: {}", wanted.width, wanted.height, SDL_GetError());
		SDL_ClearError();
		current = {};
		return false;
	}
	texture.reset(created);
	current = wanted;
	return true;
}

void SetBlendPremultiplied(SDL_Texture *texture)
{
	static const SDL_BlendMode Premultiplied = SDL_ComposeCustomBlendMode(
	    SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
	    SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
	if (SDL_SetTextureBlendMode(texture, Premultiplied) != 0) {
		// The software renderer has no custom blend modes. Transparent texels are fully
		// transparent black, so plain alpha blending gives the same result there.
		SDL_ClearError();
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	}
}

struct OpaqueRect {
	int x0, y0, x1, y1;
	/** When `r2 > 0`, only the circle (cx, cy, sqrt(r2)) inside the rect is opaque. */
	float cx = 0, cy = 0, r2 = 0;
};

/**
 * Areas always covered by UI art (main panel, open side panels). Pixels there are
 * opaque even if their color happens to match the world below.
 */
std::vector<OpaqueRect> CollectOpaqueRects()
{
	std::vector<OpaqueRect> rects;
	auto add = [&](const Rectangle &r) {
		rects.push_back({ r.position.x, r.position.y, r.position.x + r.size.width, r.position.y + r.size.height });
	};
	if (IsCompactPanel()) {
		const Point panel = GetMainPanel().position;
		for (const Point &orb : { CompactLifeOrbCenter, CompactManaOrbCenter }) {
			const float cx = static_cast<float>(panel.x + orb.x) + 0.5F;
			const float cy = static_cast<float>(panel.y + orb.y) + 0.5F;
			const float r = CompactOrbRadius + 1.0F;
			rects.push_back({ static_cast<int>(cx - r) - 1, static_cast<int>(cy - r) - 1, static_cast<int>(cx + r) + 2, static_cast<int>(cy + r) + 2, cx, cy, r * r });
		}
		add({ GetBeltPanelPosition() + Displacement { 202, 2 }, Size { 238, 35 } }); // belt and its frame
	} else {
		add(GetMainPanel());
	}
	if (IsLeftPanelOpen())
		add(GetLeftPanel());
	if (IsRightPanelOpen())
		add(GetRightPanel());
	return rects;
}

/**
 * Writes `src` (8-bit) into a locked ARGB texture, `prescale` times larger.
 * When `baseline` is set, pixels equal to it become fully transparent, except
 * inside `opaque` rectangles.
 */
void Upload(SDL_Texture *texture, const uint8_t *src, int srcPitch, int width, int height, int prescale, const uint32_t (&lut)[256],
    const uint8_t *baseline, int baselineWidth, int baselineHeight, const std::vector<OpaqueRect> &opaque = {})
{
	void *pixels;
	int pitch;
	if (SDL_LockTexture(texture, nullptr, &pixels, &pitch) != 0) {
		LogError("DETH: SDL_LockTexture: {}", SDL_GetError());
		SDL_ClearError();
		return;
	}
	auto *dstBase = static_cast<uint8_t *>(pixels);
	std::vector<uint32_t> row(static_cast<size_t>(width) * prescale);
	for (int y = 0; y < height; ++y) {
		const uint8_t *in = src + static_cast<ptrdiff_t>(y) * srcPitch;
		const uint8_t *base = (baseline != nullptr && y < baselineHeight) ? baseline + static_cast<ptrdiff_t>(y) * baselineWidth : nullptr;
		uint32_t *out = row.data();
		if (base != nullptr) {
			const int baseWidth = std::min(width, baselineWidth);
			for (int x = 0; x < width; ++x) {
				uint32_t color = (x < baseWidth && in[x] == base[x]) ? 0U : lut[in[x]];
				if (color == 0U) {
					for (const OpaqueRect &r : opaque) {
						if (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1) {
							if (r.r2 > 0) {
								const float dx = static_cast<float>(x) + 0.5F - r.cx;
								const float dy = static_cast<float>(y) + 0.5F - r.cy;
								if (dx * dx + dy * dy > r.r2)
									continue;
							}
							color = lut[in[x]];
							break;
						}
					}
				}
				for (int k = 0; k < prescale; ++k)
					*out++ = color;
			}
		} else {
			for (int x = 0; x < width; ++x) {
				const uint32_t color = lut[in[x]];
				for (int k = 0; k < prescale; ++k)
					*out++ = color;
			}
		}
		for (int k = 0; k < prescale; ++k) {
			std::memcpy(dstBase + static_cast<ptrdiff_t>(y * prescale + k) * pitch, row.data(), row.size() * sizeof(uint32_t));
		}
	}
	SDL_UnlockTexture(texture);
}

} // namespace

void SaveSnapshotIfRequested(SDL_Renderer *renderer)
{
	static const char *dir = std::getenv("DETH_SNAPSHOT_DIR");
	if (dir == nullptr || *dir == '\0')
		return;
	static const int every = std::max(1, std::getenv("DETH_SNAPSHOT_EVERY") != nullptr ? std::atoi(std::getenv("DETH_SNAPSHOT_EVERY")) : 60);
	static const int maxCount = std::getenv("DETH_SNAPSHOT_MAX") != nullptr ? std::atoi(std::getenv("DETH_SNAPSHOT_MAX")) : 10;
	static int frame = 0;
	static int saved = 0;
	if (saved >= maxCount || (++frame % every) != 0)
		return;
	int width;
	int height;
	if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0)
		return;
	SDLSurfaceUniquePtr shot = SDLWrap::CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
	// Read the whole output, not just the logical viewport.
	int logicalWidth;
	int logicalHeight;
	SDL_RenderGetLogicalSize(renderer, &logicalWidth, &logicalHeight);
	SDL_RenderSetLogicalSize(renderer, 0, 0);
	SDL_RenderSetViewport(renderer, nullptr);
	SDL_RenderSetScale(renderer, 1.0F, 1.0F);
	const SDL_Rect all { 0, 0, width, height };
	const int result = SDL_RenderReadPixels(renderer, &all, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch);
	if (logicalWidth != 0)
		SDL_RenderSetLogicalSize(renderer, logicalWidth, logicalHeight);
	if (result != 0) {
		LogError("DETH snapshot: {}", SDL_GetError());
		SDL_ClearError();
		return;
	}
	const std::string path = StrCat(dir, "/frame_", saved, ".bmp");
	if (SDL_SaveBMP(shot.get(), path.c_str()) == 0) {
		Log("DETH snapshot saved: {} ({}x{}, buffer {}x{}, world scale {}, output scale {})", path, width, height, gnScreenWidth, gnScreenHeight, WorldScale, GetOutputScale());
		++saved;
	}
}


void ReleaseWorldLayerTextures()
{
	UiTexture.reset();
	WorldTexture.reset();
	UiTextureSize = {};
	WorldTextureSize = {};
	TexturesRenderer = nullptr;
}

void ApplyDefaultTextureScaleMode(SDL_Texture *texture)
{
	if (texture == nullptr)
		return;
	SDL_SetTextureScaleMode(texture, ChooseScale(GetOutputScale()).mode);
}

bool RenderGameFrameLayers(SDL_Renderer *renderer, SDL_Surface *frameBuffer, const SDL_Palette *palette)
{
	if (!GameFrameInBuffer || !gbRunGame || frameBuffer == nullptr || palette == nullptr || palette->ncolors < 256)
		return false;
	static const bool disabled = std::getenv("DETH_DISABLE_LAYERS") != nullptr;
	if (disabled)
		return false;
	if (TexturesRenderer != renderer) {
		ReleaseWorldLayerTextures();
		TexturesRenderer = renderer;
	}

	uint32_t lut[256];
	for (int i = 0; i < 256; ++i) {
		const SDL_Color &c = palette->colors[i];
		lut[i] = 0xFF000000U | (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | c.b;
	}

	const float outputScale = GetOutputScale();
	const bool useWorld = WorldLayerValid && IsWorldScaled() && WorldBuffer && BaselineSize.width == frameBuffer->w;

	// UI layer: the whole frame buffer.
	const ScaleChoice uiScale = ChooseScale(outputScale);
	if (!EnsureTexture(UiTexture, UiTextureSize, { frameBuffer->w * uiScale.prescale, frameBuffer->h * uiScale.prescale }))
		return false;
	SDL_SetTextureScaleMode(UiTexture.get(), uiScale.mode);

	if (useWorld) {
		const ScaleChoice worldScale = ChooseScale(WorldScale * outputScale);
		const Size worldSize { WorldBuffer->w(), WorldBuffer->h() };
		if (!EnsureTexture(WorldTexture, WorldTextureSize, { worldSize.width * worldScale.prescale, worldSize.height * worldScale.prescale }))
			return false;
		SDL_SetTextureScaleMode(WorldTexture.get(), worldScale.mode);
		SDL_SetTextureBlendMode(WorldTexture.get(), SDL_BLENDMODE_NONE);
		Upload(WorldTexture.get(), WorldBuffer->at(0, 0), WorldBuffer->pitch(), worldSize.width, worldSize.height, worldScale.prescale, lut, nullptr, 0, 0);
		SetBlendPremultiplied(UiTexture.get());
		Upload(UiTexture.get(), static_cast<const uint8_t *>(frameBuffer->pixels), frameBuffer->pitch, frameBuffer->w, frameBuffer->h, uiScale.prescale, lut,
		    Baseline.data(), BaselineSize.width, BaselineSize.height, CollectOpaqueRects());
	} else {
		SDL_SetTextureBlendMode(UiTexture.get(), SDL_BLENDMODE_NONE);
		Upload(UiTexture.get(), static_cast<const uint8_t *>(frameBuffer->pixels), frameBuffer->pitch, frameBuffer->w, frameBuffer->h, uiScale.prescale, lut, nullptr, 0, 0);
	}

	if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) <= -1) ErrSdl();
	if (SDL_RenderClear(renderer) <= -1) ErrSdl();
	if (useWorld) {
		const SDL_Rect clip { 0, 0, frameBuffer->w, BaselineSize.height };
		SDL_RenderSetClipRect(renderer, &clip);
		const Size worldSize { WorldBuffer->w(), WorldBuffer->h() };
		const SDL_FRect dst { 0, 0, worldSize.width * WorldScale, worldSize.height * WorldScale };
		if (SDL_RenderCopyF(renderer, WorldTexture.get(), nullptr, &dst) <= -1) ErrSdl();
		SDL_RenderSetClipRect(renderer, nullptr);
	}
	if (SDL_RenderCopy(renderer, UiTexture.get(), nullptr, nullptr) <= -1) ErrSdl();

	return true;
}
#endif

} // namespace devilution
