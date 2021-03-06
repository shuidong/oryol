//------------------------------------------------------------------------------
//  TextureLoader.cc
//------------------------------------------------------------------------------
#include "Pre.h"
#include "TextureLoader.h"
#include "IO/IO.h"
#include "Gfx/Gfx.h"
#define GLIML_ASSERT(x) o_assert(x)
#include "gliml.h"

namespace Oryol {

OryolClassImpl(TextureLoader);

//------------------------------------------------------------------------------
TextureLoader::TextureLoader(const TextureSetup& setup_) :
TextureLoaderBase(setup_) {
    // empty
}

//------------------------------------------------------------------------------
TextureLoader::~TextureLoader() {
    o_assert_dbg(!this->ioRequest);
}

//------------------------------------------------------------------------------
void
TextureLoader::Cancel() {
    if (this->ioRequest) {
        this->ioRequest->SetCancelled();
        this->ioRequest = nullptr;
    }
}

//------------------------------------------------------------------------------
Id
TextureLoader::Start() {
    
    // prepare the Gfx resource
    this->resId = Gfx::resource().prepareAsync(this->setup);
    
    // fire IO request to start loading the texture data
    this->ioRequest = IOProtocol::Read::Create();
    this->ioRequest->Url = setup.Locator.Location();
    IO::Put(this->ioRequest);
    
    return this->resId;
}

//------------------------------------------------------------------------------
ResourceState::Code
TextureLoader::Continue() {
    o_assert_dbg(this->resId.IsValid());
    o_assert_dbg(this->ioRequest.isValid());
    
    ResourceState::Code result = ResourceState::Pending;
    
    if (this->ioRequest->Handled()) {
        if (IOStatus::OK == this->ioRequest->Status) {
            // yeah, IO is done, let gliml parse the texture data
            // and create the texture resource
            const uint8* data = this->ioRequest->Data.Data();
            const int32 numBytes = this->ioRequest->Data.Size();
            
            gliml::context ctx;
            ctx.enable_dxt(true);
            ctx.enable_pvrtc(true);
            ctx.enable_etc2(true);
            if (ctx.load(data, numBytes)) {
                TextureSetup texSetup = this->buildSetup(this->setup, &ctx, data);
                // NOTE: the prepared texture resource might have already been
                // destroyed at this point, if this happens, initAsync will
                // silently fail and return ResourceState::InvalidState
                // (the same for failedAsync)
                result = Gfx::resource().initAsync(this->resId, texSetup, data, numBytes);
            }
            else {
                result = Gfx::resource().failedAsync(this->resId);
            }
        }
        else {
            // IO had failed
            result = Gfx::resource().failedAsync(this->resId);
        }
        this->ioRequest = nullptr;
    }
    return result;
}

//------------------------------------------------------------------------------
TextureSetup
TextureLoader::buildSetup(const TextureSetup& blueprint, const gliml::context* ctx, const uint8* data) {
    const int32 w = ctx->image_width(0, 0);
    const int32 h = ctx->image_height(0, 0);
    const int32 numFaces = ctx->num_faces();
    const int32 numMips = ctx->num_mipmaps(0);
    PixelFormat::Code pixelFormat = PixelFormat::InvalidPixelFormat;
    switch(ctx->image_internal_format()) {
        case GLIML_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
            pixelFormat = PixelFormat::DXT1;
            break;
        case GLIML_GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
            pixelFormat = PixelFormat::DXT3;
            break;
        case GLIML_GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
            pixelFormat = PixelFormat::DXT5;
            break;
        case GLIML_GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG:
            pixelFormat = PixelFormat::PVRTC4_RGB;
            break;
        case GLIML_GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG:
            pixelFormat = PixelFormat::PVRTC2_RGB;
            break;
        case GLIML_GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG:
            pixelFormat = PixelFormat::PVRTC4_RGBA;
            break;
        case GLIML_GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG:
            pixelFormat = PixelFormat::PVRTC2_RGBA;
            break;
        case GLIML_GL_COMPRESSED_RGB8_ETC2:
            pixelFormat = PixelFormat::ETC2_RGB8;
            break;
        case GLIML_GL_COMPRESSED_SRGB8_ETC2:
            pixelFormat = PixelFormat::ETC2_SRGB8;
            break;
        case GLIML_GL_RGB:
            if (ctx->image_type() == GLIML_GL_UNSIGNED_BYTE) {
                pixelFormat = PixelFormat::RGB8;
            }
            else if (ctx->image_type() == GLIML_GL_UNSIGNED_SHORT_5_6_5) {
                pixelFormat = PixelFormat::R5G6B5;
            }
            break;
            
        case GLIML_GL_RGBA:
            switch (ctx->image_type()) {
                case GLIML_GL_UNSIGNED_BYTE:
                    pixelFormat = PixelFormat::RGBA8;
                    break;
                case GLIML_GL_UNSIGNED_SHORT_4_4_4_4:
                    pixelFormat = PixelFormat::RGBA4;
                    break;
                case GLIML_GL_UNSIGNED_SHORT_5_5_5_1:
                    pixelFormat = PixelFormat::R5G5B5A1;
                    break;
                default:
                    break;
            }
            break;
            
        default:
            break;
    }
    o_assert(PixelFormat::InvalidPixelFormat != pixelFormat);
    TextureType::Code type = TextureType::InvalidTextureType;
    switch (ctx->texture_target()) {
        case GLIML_GL_TEXTURE_2D:
            type = TextureType::Texture2D;
            break;
        case GLIML_GL_TEXTURE_3D:
            type = TextureType::Texture3D;
            break;
        case GLIML_GL_TEXTURE_CUBE_MAP:
            type = TextureType::TextureCube;
            break;
        default:
            o_error("Unknown texture type!\n");
            break;
    }
    TextureSetup newSetup = TextureSetup::FromPixelData(w, h, numMips, type, pixelFormat, this->setup);
    
    // setup mipmap offsets
    o_assert_dbg(GfxConfig::MaxNumTextureMipMaps >= ctx->num_mipmaps(0));
    for (int32 faceIndex = 0; faceIndex < numFaces; faceIndex++) {
        for (int32 mipIndex = 0; mipIndex < numMips; mipIndex++) {
            const uint8* cur = (const uint8*) ctx->image_data(faceIndex, mipIndex);
            newSetup.ImageData.Offsets[faceIndex][mipIndex] = int32(cur - data);
            newSetup.ImageData.Sizes[faceIndex][mipIndex] = ctx->image_size(faceIndex, mipIndex);
        }
    }
    return newSetup;
}

} // namespace Oryol