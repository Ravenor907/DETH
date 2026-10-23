/**
 * @file world_view.hpp
 *
 * DETH: fractional world zoom independent of the UI scale.
 *
 * The frame buffer holds the UI at its native size (the renderer scales the
 * buffer by the UI scale). When the world zoom differs from the UI scale, the
 * world is rendered 1:1 into its own surface (world pixels), then:
 *  - nearest-scaled into the frame buffer so translucent UI and text keep
 *    working exactly as before, and
 *  - uploaded as its own texture so the GPU scales it once, in high quality.
 * At present time, pixels that the UI did not touch are made transparent so
 * the high quality world shows through.
 */
#pragma once

#include <cstdint>

#ifdef USE_SDL3
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#else
#include <SDL.h>
#endif

#include "engine/displacement.hpp"
#include "engine/point.hpp"
#include "engine/size.hpp"
#include "engine/surface.hpp"

namespace devilution {

/** @brief Frame buffer pixels per world pixel. 1 means the world is drawn straight into the frame buffer. */
float GetWorldScale();

/** @brief Whether the world is rendered to its own surface (scale != 1). */
bool IsWorldScaled();

/**
 * @brief Recomputes the world scale from the zoom option and the current output scale.
 * @return true if the scale changed.
 */
bool UpdateWorldScale();

/** @brief Forces a specific world scale (tests only). */
void SetWorldScaleForTesting(float scale);

/** @brief Size of the world view in world pixels (covers the frame buffer width and viewport height). */
Size GetWorldViewSize();

/** @brief Surface to render the world into when the world is scaled. */
Surface GetWorldRenderTarget();

/**
 * @brief Scales the rendered world into the frame buffer viewport and remembers
 * the result, so the presenter can tell world pixels from UI pixels.
 */
void PresentWorldIntoBuffer(const Surface &bufferViewport);

/** @brief Marks that the frame buffer now holds a game frame (set by the game renderer). */
void MarkGameFrame();

/** @brief Marks that something other than the game frame was drawn (menus, cutscenes, movies). */
void InvalidateWorldLayer();

/** @brief Converts frame buffer coordinates to world pixel coordinates. */
Point BufferToWorld(Point bufferPosition);

/** @brief Converts a world pixel coordinate to frame buffer coordinates. */
Point WorldToBuffer(Point worldPosition);
Displacement WorldToBuffer(Displacement worldOffset);

/** @brief Output pixels per frame buffer pixel (the effective UI scale). */
float GetOutputScale();

#if !defined(USE_SDL1) && !defined(USE_SDL3)
/**
 * @brief Draws the current game frame with the renderer: the world texture (if any)
 * and the frame buffer on top.
 * @return false if this frame should use the default presenter.
 */
bool RenderGameFrameLayers(SDL_Renderer *renderer, SDL_Surface *frameBuffer, const SDL_Palette *palette);

/** @brief Picks a sharp scaling mode for the default presenter's texture. */
void ApplyDefaultTextureScaleMode(SDL_Texture *texture);

/** @brief Debug aid: saves the composed frame when DETH_SNAPSHOT_DIR is set. */
void SaveSnapshotIfRequested(SDL_Renderer *renderer);

/** @brief Releases GPU resources (renderer is being recreated). */
void ReleaseWorldLayerTextures();
#endif

} // namespace devilution
