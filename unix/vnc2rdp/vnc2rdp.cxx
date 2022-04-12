#include "vnc2rdp/RDPServer.h"

int main(int argc, char** argv) {
  RDPServer server;
  if (!server.init(3389)) {
      return 1;
  }
  server.run();
  return 0;
}