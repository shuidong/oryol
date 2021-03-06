#pragma once
//------------------------------------------------------------------------------
/**
    @class Oryol::IOSetup
    @ingroup IO
    @brief configure the IO system
*/
#include "Core/String/String.h"
#include "Core/Containers/Map.h"
#include "Core/Containers/KeyValuePair.h"
#include "IO/FS/FileSystem.h"
#include <functional>

namespace Oryol {
    
class IOSetup {
public:
    /// initial assigns
    Map<String, String> Assigns;
    /// initial file systems
    Map<StringAtom, std::function<Ptr<FileSystem>()>> FileSystems;
    /// number of IOLanes
    int32 NumIOLanes = 4;
};
    
} // namespace Oryol
