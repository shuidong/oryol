//------------------------------------------------------------------------------
//  drawStateFactoryBase.cc
//------------------------------------------------------------------------------
#include "Pre.h"
#include "drawStateFactoryBase.h"
#include "Gfx/Resource/drawState.h"
#include "Gfx/Resource/resourcePools.h"

namespace Oryol {
namespace _priv {

//------------------------------------------------------------------------------
drawStateFactoryBase::drawStateFactoryBase() :
isValid(false) {
    // empty
}

//------------------------------------------------------------------------------
drawStateFactoryBase::~drawStateFactoryBase() {
    o_assert_dbg(!this->isValid);
}

//------------------------------------------------------------------------------
void
drawStateFactoryBase::Setup(const gfxPointers& ptrs) {
    o_assert_dbg(!this->isValid);
    this->pointers = ptrs;
    this->isValid = true;
}

//------------------------------------------------------------------------------
void
drawStateFactoryBase::Discard() {
    o_assert_dbg(this->isValid);
    this->pointers = gfxPointers();
    this->isValid = false;
}

//------------------------------------------------------------------------------
bool
drawStateFactoryBase::IsValid() const {
    return this->isValid;
}

//------------------------------------------------------------------------------
ResourceState::Code
drawStateFactoryBase::SetupResource(drawState& ds) {
    o_assert_dbg(this->isValid);

    this->resolveInputMeshes(ds);
    this->checkInputMeshes(ds);
    ds.shd = this->pointers.shaderPool->Lookup(ds.Setup.Shader);
    o_assert_dbg(ds.shd && (ResourceState::Valid == ds.shd->State));
    return ResourceState::Valid;
}

//------------------------------------------------------------------------------
void
drawStateFactoryBase::resolveInputMeshes(drawState& ds) {
    for (int i = 0; i < GfxConfig::MaxNumInputMeshes; i++) {
        if (ds.Setup.Meshes[i].IsValid()) {
            ds.meshes[i] = this->pointers.meshPool->Get(ds.Setup.Meshes[i]);
            o_assert(ds.meshes[i]);
        }
        else {
            ds.meshes[i] = nullptr;
        }
    }
}

//------------------------------------------------------------------------------
void
drawStateFactoryBase::checkInputMeshes(const drawState& ds) const {
    // checks that:
    //  - at least one input mesh must be attached, and it must be in slot 0
    //  - all attached input meshes must be valid
    //  - PrimitivesGroups may only be attached to the mesh in slot 0
    //  - if indexed rendering is used, the mesh in slot 0 must have an index buffer
    //  - the mesh in slot 0 cannot contain per-instance-data
    //  - no colliding vertex attributes across all input meshes

    if (nullptr == ds.meshes[0]) {
        o_error("invalid drawState: at least one input mesh must be provided, in slot 0!\n");
    }
    if ((ds.meshes[0]->indexBufferAttrs.Type != IndexType::None) &&
        (ds.meshes[0]->indexBufferAttrs.NumIndices == 0)) {
        o_error("invalid drawState: the input mesh at slot 0 uses indexed rendering, but has no indices!\n");
    }
    if (ds.meshes[0]->vertexBufferAttrs.StepFunction != VertexStepFunction::PerVertex) {
        o_error("invalid drawState: VertexStepFunction of mesh at slot 0 must be PerVertex!\n");
    }

    StaticArray<int, VertexAttr::NumVertexAttrs> vertexAttrCounts;
    vertexAttrCounts.Fill(0);
    for (int mshIndex = 0; mshIndex < GfxConfig::MaxNumInputMeshes; mshIndex++) {
        const mesh* msh = ds.meshes[mshIndex];
        if (msh) {
            if (ResourceState::Valid != msh->State) {
                o_error("invalid drawState: input mesh at slot '%d' no valid!\n", mshIndex);
            }
            if ((mshIndex > 0) && (msh->indexBufferAttrs.Type != IndexType::None)) {
                o_error("invalid drawState: input mesh at slot '%d' has indices, only allowed for slot 0!", mshIndex);
            }
            if ((mshIndex > 0) && (msh->numPrimGroups > 0)) {
                o_error("invalid drawState: input mesh at slot '%d' has primitive groups, only allowed for slot 0!", mshIndex);
            }
            const int numComps = msh->vertexBufferAttrs.Layout.NumComponents();
            for (int compIndex = 0; compIndex < numComps; compIndex++) {
                const auto& comp = msh->vertexBufferAttrs.Layout.ComponentAt(compIndex);
                vertexAttrCounts[comp.Attr]++;
                if (vertexAttrCounts[comp.Attr] > 1) {
                    o_error("invalid drawState: same vertex attribute declared in multiple input meshes!");
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
void
drawStateFactoryBase::DestroyResource(drawState& ds) {
    ds.Clear();
}

} // namespace _priv
} // namespace Oryol
