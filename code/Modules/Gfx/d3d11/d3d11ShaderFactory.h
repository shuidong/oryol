#pragma once
//------------------------------------------------------------------------------
/**
    @class Oryol::_priv::d3d11ShaderFactory
    @ingroup _priv
    @brief D3D11 implementation of shaderFactory
*/
#include "Resource/ResourceState.h"
#include "Gfx/Core/gfxPointers.h"
#include "Gfx/d3d11/d3d11_decl.h"

namespace Oryol {
namespace _priv {

class shader;

class d3d11ShaderFactory {
public:
    /// constructor
    d3d11ShaderFactory();
    /// destructor
    ~d3d11ShaderFactory();
    
    /// setup with a pointer to the state wrapper object
    void Setup(const gfxPointers& ptrs);
    /// discard the factory
    void Discard();
    /// return true if the object has been setup
    bool IsValid() const;
    
    /// setup resource
    ResourceState::Code SetupResource(shader& shd);
    /// destroy the resource
    void DestroyResource(shader& shd);

private:
    gfxPointers pointers;
    ID3D11Device* d3d11Device;
    bool isValid;
};
    
} // namespace _priv
} // namespace Oryol
