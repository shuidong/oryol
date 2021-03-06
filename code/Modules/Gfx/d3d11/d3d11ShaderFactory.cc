//------------------------------------------------------------------------------
//  d3d11ShaderFactory
//------------------------------------------------------------------------------
#include "Pre.h"
#include "Gfx/Core/renderer.h"
#include "d3d11ShaderFactory.h"
#include "Gfx/Resource/shader.h"
#include "d3d11_impl.h"

namespace Oryol {
namespace _priv {

//------------------------------------------------------------------------------
d3d11ShaderFactory::d3d11ShaderFactory() :
d3d11Device(nullptr),
isValid(false) {
    // empty
}

//------------------------------------------------------------------------------
d3d11ShaderFactory::~d3d11ShaderFactory() {
    o_assert_dbg(!this->isValid);
}

//------------------------------------------------------------------------------
void
d3d11ShaderFactory::Setup(const gfxPointers& ptrs) {
    o_assert_dbg(!this->isValid);
    this->isValid = true;
    this->pointers = ptrs;
    this->d3d11Device = this->pointers.renderer->d3d11Device;
}

//------------------------------------------------------------------------------
void 
d3d11ShaderFactory::Discard() {
    o_assert_dbg(this->isValid);
    this->isValid = false;
    this->pointers = gfxPointers();
    this->d3d11Device = nullptr;
}

//------------------------------------------------------------------------------
bool
d3d11ShaderFactory::IsValid() const {
    return this->isValid;
}

//------------------------------------------------------------------------------
ResourceState::Code
d3d11ShaderFactory::SetupResource(shader& shd) {
    o_assert_dbg(this->isValid);
    o_assert_dbg(this->d3d11Device);
    HRESULT hr;

    this->pointers.renderer->invalidateShaderState();
    const ShaderLang::Code slang = ShaderLang::HLSL5;
    const ShaderSetup& setup = shd.Setup;

    // for each program in the bundle
    const int32 numProgs = setup.NumPrograms();
    for (int32 progIndex = 0; progIndex < numProgs; progIndex++) {

        // create vertex shader (only support byte code)
        ID3D11VertexShader* vs = nullptr;
        const void* vsPtr = nullptr;
        uint32 vsSize = 0;
        setup.VertexShaderByteCode(progIndex, slang, vsPtr, vsSize);
        o_assert_dbg(vsPtr);
        hr = this->d3d11Device->CreateVertexShader(vsPtr, vsSize, NULL, &vs);
        o_assert(SUCCEEDED(hr));
        o_assert_dbg(vs);

        // create pixel shader
        ID3D11PixelShader* ps = nullptr;
        const void* psPtr = nullptr;
        uint32 psSize = 0;
        setup.FragmentShaderByteCode(progIndex, slang, psPtr, psSize);
        o_assert_dbg(psPtr);
        hr = this->d3d11Device->CreatePixelShader(psPtr, psSize, NULL, &ps);
        o_assert(SUCCEEDED(hr));
        o_assert_dbg(ps);

        // add vertexshader/pixelshader pair to program bundle
        shd.addShaders(setup.Mask(progIndex), vs, ps);
    }

    // create constant buffers
    D3D11_BUFFER_DESC cbDesc;
    Memory::Clear(&cbDesc, sizeof(cbDesc));
    for (int i = 0; i < setup.NumUniformBlocks(); i++) {
        const ShaderStage::Code bindStage = setup.UniformBlockBindStage(i);
        const int32 bindSlot = setup.UniformBlockBindSlot(i);
        const UniformBlockLayout& layout = setup.UniformBlockLayout(i);
        o_assert_dbg(InvalidIndex != bindSlot);

        // NOTE: constant buffer size must be multiple of 16 bytes
        o_assert_dbg(layout.ByteSize() > 0);
        cbDesc.ByteWidth = Memory::RoundUp(layout.ByteSize(), 16);
        cbDesc.Usage = D3D11_USAGE_DEFAULT;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = 0;

        ID3D11Buffer* d3d11ConstantBuffer = nullptr;
        hr = this->d3d11Device->CreateBuffer(&cbDesc, nullptr, &d3d11ConstantBuffer);
        o_assert(SUCCEEDED(hr));
        o_assert_dbg(d3d11ConstantBuffer);

        // the d3d11ConstantBuffer ptr can be 0 at this point, if the
        // uniform block only contains textures
        shd.addUniformBlockEntry(bindStage, bindSlot, d3d11ConstantBuffer);
    }

    return ResourceState::Valid;
}

//------------------------------------------------------------------------------
void
d3d11ShaderFactory::DestroyResource(shader& shd) {
    o_assert_dbg(this->isValid);
    o_assert_dbg(this->d3d11Device);

    this->pointers.renderer->invalidateShaderState();

    // clear the vertex and pixel shaders
    const int32 numProgs = shd.getNumPrograms();
    for (int32 progIndex = 0; progIndex < numProgs; progIndex++) {
        ID3D11VertexShader* vs = shd.getVertexShaderAtIndex(progIndex);
        if (vs) {
            vs->Release();
        }
        ID3D11PixelShader* ps = shd.getPixelShaderAtIndex(progIndex);
        if (ps) {
            ps->Release();
        }
    }

    // release constant buffers 
    for (int bindStage = 0; bindStage < ShaderStage::NumShaderStages; bindStage++) {
        for (int bindSlot = 0; bindSlot < GfxConfig::MaxNumUniformBlocksPerStage; bindSlot++) {
            ID3D11Buffer* cb = shd.getConstantBuffer((ShaderStage::Code)bindStage, bindSlot);
            if (cb) {
                cb->Release();
            }
        }
    }

    // reset shader object for reuse
    shd.Clear();
}

} // namespace _priv
} // namespace Oryol
