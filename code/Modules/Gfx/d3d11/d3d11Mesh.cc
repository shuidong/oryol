//------------------------------------------------------------------------------
//  d3d11Mesh.cc
//------------------------------------------------------------------------------
#include "Pre.h"
#include "d3d11Mesh.h"
#include "d3d11_impl.h"

namespace Oryol {
namespace _priv {

//------------------------------------------------------------------------------
d3d11Mesh::d3d11Mesh() :
d3d11VertexBuffer(nullptr),
d3d11IndexBuffer(nullptr),
vbUpdateFrameIndex(-1),
ibUpdateFrameIndex(-1),
d3d11PrimTopology(D3D_PRIMITIVE_TOPOLOGY_UNDEFINED) {
    // empty
}

//------------------------------------------------------------------------------
d3d11Mesh::~d3d11Mesh() {
    o_assert_dbg(nullptr == this->d3d11VertexBuffer);
    o_assert_dbg(nullptr == this->d3d11IndexBuffer);
}

//------------------------------------------------------------------------------
void
d3d11Mesh::Clear() {
    this->d3d11VertexBuffer = nullptr;
    this->d3d11IndexBuffer = nullptr;
    this->vbUpdateFrameIndex = -1;
    this->ibUpdateFrameIndex = -1;
    this->d3d11PrimTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    meshBase::Clear();
}

} // namespace _priv
} // namespace Oryol
