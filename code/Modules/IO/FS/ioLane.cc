//------------------------------------------------------------------------------
//  ioLane.cc
//------------------------------------------------------------------------------
#include "Pre.h"
#include "ioLane.h"
#include "Messaging/Dispatcher.h"

// FIXME: access to IO.h from down here is a bit hacky :/
#include "IO/IO.h"

namespace Oryol {
namespace _priv {

OryolClassImpl(ioLane);

//------------------------------------------------------------------------------
ioLane::ioLane(const ioPointers& ptrs) :
pointers(ptrs) {
    // empty
}

//------------------------------------------------------------------------------
ioLane::~ioLane() {
    o_assert(this->threadStopped);
}

//------------------------------------------------------------------------------
void
ioLane::onThreadEnter() {
    ThreadedQueue::onThreadEnter();

    // setup a Dispatcher to route messages to this object's callback methods
    Ptr<Dispatcher<IOProtocol>> disp = Dispatcher<IOProtocol>::Create();
    using namespace std::placeholders;
    disp->Subscribe<IOProtocol::Read>(std::bind(&ioLane::onRead, this, _1));
    disp->Subscribe<IOProtocol::Write>(std::bind(&ioLane::onWrite, this, _1));
    disp->Subscribe<IOProtocol::notifyFileSystemAdded>(std::bind(&ioLane::onNotifyFileSystemAdded, this, _1));
    disp->Subscribe<IOProtocol::notifyFileSystemReplaced>(std::bind(&ioLane::onNotifyFileSystemReplaced, this, _1));
    disp->Subscribe<IOProtocol::notifyFileSystemRemoved>(std::bind(&ioLane::onNotifyFileSystemRemoved, this, _1));
    this->forwardingPort = disp;
}

//------------------------------------------------------------------------------
void
ioLane::onThreadLeave() {
    this->forwardingPort = 0;
    this->fileSystems.Clear();
    ThreadedQueue::onThreadLeave();
}

//------------------------------------------------------------------------------
Ptr<FileSystem>
ioLane::fileSystemForURL(const URL& url) {
    StringAtom scheme = url.Scheme();
    if (this->fileSystems.Contains(scheme)) {
        return this->fileSystems[scheme];
    }
    else {
        o_warn("ioLane::fileSystemForURL: no filesystem registered for URL scheme '%s'!\n", scheme.AsCStr());
        return Ptr<FileSystem>();
    }
}

//------------------------------------------------------------------------------
bool
ioLane::checkCancelled(const Ptr<IOProtocol::Request>& msg) {
    if (msg->Cancelled()) {
        msg->Status = IOStatus::Cancelled;
        msg->SetHandled();
        return true;
    }
    else {
        return false;
    }
}

//------------------------------------------------------------------------------
void
ioLane::onRead(const Ptr<IOProtocol::Read>& msg) {
    if (!this->checkCancelled(msg)) {
        Ptr<FileSystem> fs = this->fileSystemForURL(msg->Url);
        if (fs) {
            fs->onRead(msg);
        }
    }
}

//------------------------------------------------------------------------------
void
ioLane::onWrite(const Ptr<IOProtocol::Write>& msg) {
    if (!this->checkCancelled(msg)) {
        Ptr<FileSystem> fs = this->fileSystemForURL(msg->Url);
        if (fs) {
            fs->onWrite(msg);
        }
    }
}

//------------------------------------------------------------------------------
void
ioLane::onNotifyFileSystemAdded(const Ptr<IOProtocol::notifyFileSystemAdded>& msg) {
    const StringAtom& urlScheme = msg->Scheme;
    o_assert(!this->fileSystems.Contains(urlScheme));
    Ptr<FileSystem> newFileSystem = this->pointers.schemeRegistry->CreateFileSystem(urlScheme);
    this->fileSystems.Add(urlScheme, newFileSystem);
}

//------------------------------------------------------------------------------
void
ioLane::onNotifyFileSystemReplaced(const Ptr<IOProtocol::notifyFileSystemReplaced>& msg) {
    const StringAtom& urlScheme = msg->Scheme;
    o_assert(this->fileSystems.Contains(urlScheme));
    Ptr<FileSystem> newFileSystem = this->pointers.schemeRegistry->CreateFileSystem(urlScheme);
    this->fileSystems[urlScheme] = newFileSystem;
}

//------------------------------------------------------------------------------
void
ioLane::onNotifyFileSystemRemoved(const Ptr<IOProtocol::notifyFileSystemRemoved>& msg) {
    const StringAtom& urlScheme = msg->Scheme;
    o_assert(this->fileSystems.Contains(urlScheme));
    this->fileSystems.Erase(urlScheme);
}

} // namespace _priv
} // namespace Oryol
