#include <memory>
#include <thread>
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

#include "vnc2rdp/RDPServer.h"
#include "vnc2rdp/RDPSession.h"

using namespace std;

RDPServer::RDPServer()
  : instance(nullptr) {}

RDPServer::~RDPServer() {
  if (instance) {
    freerdp_listener_free(instance);
  }
  WSACleanup();
}

bool RDPServer::init(int port) {
  WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());
  winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT);
  instance = freerdp_listener_new();
  if (!instance) {
    return false;
  }
  instance->info = this;
  instance->PeerAccepted = rdpPeerAccepted;
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    return false;
  }
  if (!instance->Open(instance, nullptr, port)) {
    return false;
  }
  return true;
}

bool RDPServer::run() {
  HANDLE handles[32];
  DWORD count;
  while (true) {
    count = instance->GetEventHandles(instance, handles, 32);
    if (count == 0) {
      break;
    }
    if (WaitForMultipleObjects(count, handles, false, INFINITE) == WAIT_FAILED) {
      break;
    }
    if (!instance->CheckFileDescriptor(instance)) {
      break;
    }
  }
  instance->Close(instance);
  return true;
}

BOOL RDPServer::rdpPeerAccepted(freerdp_listener* instance, freerdp_peer* client) {
  RDPServer* server = (RDPServer *)instance->info;
  return server->peerAccepted(client);
}

bool RDPServer::peerAccepted(freerdp_peer* client) {
  auto session = make_shared<RDPSession>(client);
  if (!session->init()) {
    return false;
  }
  thread peerThread([client, session] {
    session->run();
  });
  peerThread.detach();
  return true;
}