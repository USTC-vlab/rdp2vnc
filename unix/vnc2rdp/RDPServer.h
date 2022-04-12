#pragma once
#include <winpr/crt.h>
#include <winpr/ssl.h>
#include <winpr/synch.h>
#include <winpr/string.h>
#include <winpr/path.h>
#include <winpr/winsock.h>
#include <freerdp/freerdp.h>
#include <freerdp/listener.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/channels/wtsvc.h>
#include <freerdp/server/audin.h>
#include <freerdp/server/rdpsnd.h>
#include <freerdp/server/encomsp.h>
#include <freerdp/channels/wtsvc.h>
#include <freerdp/channels/channels.h>
#include <freerdp/constants.h>
#include <freerdp/server/rdpsnd.h>

class RDPServer {
public:
    RDPServer();
    ~RDPServer();
    bool init(int port);
    bool run();
private:
    static BOOL rdpPeerAccepted(freerdp_listener* instance, freerdp_peer* client);
    bool peerAccepted(freerdp_peer* client);
    freerdp_listener* instance;
};