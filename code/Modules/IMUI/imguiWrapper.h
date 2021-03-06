#pragma once
//------------------------------------------------------------------------------
/**
    @class Oryol::_priv::imguiWrapper
    @brief imgui wrapper class for Oryol
*/
#include "Core/Types.h"
#include "Input/Core/Mouse.h"
#include "Input/Core/Key.h"
#include "Gfx/Gfx.h"
#include "Time/Duration.h"
#include "imgui.h"

namespace Oryol {
namespace _priv {

class imguiWrapper {
public:
    /// setup the wrapper
    void Setup();
    /// discard the wrapper
    void Discard();
    /// return true if wrapper is valid
    bool IsValid() const;
    /// call before issuing ImGui commands
    void NewFrame(float32 frameDurationInSeconds);

private:
    /// setup dynamic mesh
    void setupMesh();
    /// setup font texture
    void setupFontTexture();
    /// setup draw state
    void setupDrawState();
    /// imgui's draw callback
    static void imguiRenderDrawLists(ImDrawData* draw_data);

    static const int MaxNumVertices = 64 * 1024;
    static const int MaxNumIndices = 128 * 1024;

    static imguiWrapper* self;

    bool isValid = false;
    ResourceLabel resLabel;
    Id fontTexture;
    Id mesh;
    Id shader;
    Id drawState;
    ImDrawVert vertexData[MaxNumVertices];
    ImDrawIdx indexData[MaxNumIndices];
};

} // namespace _priv
} // namespace Oryol