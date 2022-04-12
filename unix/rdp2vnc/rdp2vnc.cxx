/* Copyright 2021 Dinglan Peng
 *    
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

// FIXME: Check cases when screen width/height is not a multiply of 32.
//        e.g. 800x600.

#include <mutex>
#include <thread>
#include <vector>
#include <iostream>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <rfb/Logger_stdio.h>
#include <rfb/LogWriter.h>
#include <rfb/VNCServerST.h>
#include <rfb/Configuration.h>
#include <rfb/Timer.h>
#include <network/TcpSocket.h>
#include <network/UnixSocket.h>

#include <signal.h>

#include <rdp2vnc/RDPDesktop.h>
#include <rdp2vnc/Geometry.h>
#include <rdp2vnc/RDPClient.h>
#include <rdp2vnc/DesktopMux.h>
#include <rdp2vnc/Terminal.h>
#include <rdp2vnc/Greeter.h>

extern char buildtime[];

using namespace std;
using namespace rfb;
using namespace network;

static LogWriter vlog("Main");

IntParameter rfbport("rfbport", "TCP port to listen for RFB protocol",5900);
StringParameter rfbunixpath("rfbunixpath", "Unix socket to listen for RFB protocol", "");
IntParameter rfbunixmode("rfbunixmode", "Unix socket access mode", 0600);
StringParameter hostsFile("HostsFile", "File with IP access control rules", "");
BoolParameter localhostOnly("localhost",
                            "Only allow connections from localhost",
                            false);
BoolParameter rdpArg("RdpArg", "Command line arguments after this are for freerdp client", false);
BoolParameter interactiveLogin("InteractiveLogin", "Show an interactive greeter", false);
IntParameter terminalWidth("TerminalWidth", "Width of the interactive login terminal", 1024);
IntParameter terminalHeight("TerminalHeight", "Width of the interactive login terminal", 768);

//
// Allow the main loop terminate itself gracefully on receiving a signal.
//

static bool caughtSignal = false;

static void CleanupSignalHandler(int sig)
{
  caughtSignal = true;
}


class FileTcpFilter : public TcpFilter
{

public:

  FileTcpFilter(const char *fname)
    : TcpFilter("-"), fileName(NULL), lastModTime(0)
  {
    if (fname != NULL)
      fileName = strdup((char *)fname);
  }

  virtual ~FileTcpFilter()
  {
    if (fileName != NULL)
      free(fileName);
  }

  virtual bool verifyConnection(Socket* s)
  {
    if (!reloadRules()) {
      vlog.error("Could not read IP filtering rules: rejecting all clients");
      filter.clear();
      filter.push_back(parsePattern("-"));
      return false;
    }

    return TcpFilter::verifyConnection(s);
  }

protected:

  bool reloadRules()
  {
    if (fileName == NULL)
      return true;

    struct stat st;
    if (stat(fileName, &st) != 0)
      return false;

    if (st.st_mtime != lastModTime) {
      // Actually reload only if the file was modified
      FILE *fp = fopen(fileName, "r");
      if (fp == NULL)
        return false;

      // Remove all the rules from the parent class
      filter.clear();

      // Parse the file contents adding rules to the parent class
      char buf[32];
      while (readLine(buf, 32, fp)) {
        if (buf[0] && strchr("+-?", buf[0])) {
          filter.push_back(parsePattern(buf));
        }
      }

      fclose(fp);
      lastModTime = st.st_mtime;
    }
    return true;
  }

protected:

  char *fileName;
  time_t lastModTime;

private:

  //
  // NOTE: we silently truncate long lines in this function.
  //

  bool readLine(char *buf, int bufSize, FILE *fp)
  {
    if (fp == NULL || buf == NULL || bufSize == 0)
      return false;

    if (fgets(buf, bufSize, fp) == NULL)
      return false;

    char *ptr = strchr(buf, '\n');
    if (ptr != NULL) {
      *ptr = '\0';              // remove newline at the end
    } else {
      if (!feof(fp)) {
        int c;
        do {                    // skip the rest of a long line
          c = getc(fp);
        } while (c != '\n' && c != EOF);
      }
    }
    return true;
  }

};

char* programName;

static void printVersion(FILE *fp)
{
  fprintf(fp, "rdp2vnc 0.1\n");
}

static void usage()
{
  printVersion(stderr);
  fprintf(stderr, "\nUsage: %s [<parameters>]\n", programName);
  fprintf(stderr, "       %s --version\n", programName);
  fprintf(stderr,"\n"
          "Parameters can be turned on with -<param> or off with -<param>=0\n"
          "Parameters which take a value can be specified as "
          "-<param> <value>\n"
          "Other valid forms are <param>=<value> -<param>=<value> "
          "--<param>=<value>\n"
          "Parameter names are case-insensitive.  The parameters are:\n\n");
  Configuration::listParams(79, 14);
  exit(1);
}

static void listenServer(std::list<SocketListener*>& listeners) {
  if (rfbunixpath.getValueStr()[0] != '\0') {
    listeners.push_back(new network::UnixListener(rfbunixpath, rfbunixmode));
    vlog.info("Listening on %s (mode %04o)", (const char*)rfbunixpath, (int)rfbunixmode);
  }

  if ((int)rfbport != -1) {
    if (localhostOnly)
      createLocalTcpListeners(&listeners, (int)rfbport);
    else
      createTcpListeners(&listeners, 0, (int)rfbport);
    vlog.info("Listening on port %d", (int)rfbport);
  }

  const char *hostsData = hostsFile.getData();
  FileTcpFilter fileTcpFilter(hostsData);
  if (strlen(hostsData) != 0)
    for (std::list<SocketListener*>::iterator i = listeners.begin();
         i != listeners.end();
         i++)
      (*i)->setFilter(&fileTcpFilter);
  delete[] hostsData;
}

int main(int argc, char** argv)
{
  initStdIOLoggers();
  LogWriter::setLogParams("*:stderr:30");

  programName = argv[0];

  Configuration::enableServerParams();
  int rdpArgc = 0;
  char **rdpArgv = argv;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-RdpArg") == 0) {
      rdpArgc = argc - i;
      rdpArgv = argv + i;
      break;
    }
    if (Configuration::setParam(argv[i]))
      continue;

    if (argv[i][0] == '-') {
      if (i+1 < argc) {
        if (Configuration::setParam(&argv[i][1], argv[i+1])) {
          i++;
          continue;
        }
      }
      if (strcmp(argv[i], "-v") == 0 ||
          strcmp(argv[i], "-version") == 0 ||
          strcmp(argv[i], "--version") == 0) {
        printVersion(stdout);
        return 0;
      }
      usage();
    }

    usage();
  }

  //signal(SIGHUP, CleanupSignalHandler);
  //signal(SIGINT, CleanupSignalHandler);
  //signal(SIGTERM, CleanupSignalHandler);

  std::unique_ptr<VNCServerST> server;
  std::unique_ptr<RDPClient> rdpClient;
  std::unique_ptr<RDPDesktop> rdpDesktop;
  std::unique_ptr<Geometry> geo;
  std::unique_ptr<DesktopMux> desktopMux;
  std::unique_ptr<TerminalDesktop> terminalDesktop;
  std::list<SocketListener*> listeners;
  Geometry terminalGeo((int)terminalWidth, (int)terminalHeight);

  if (interactiveLogin) {
    try {
      desktopMux.reset(new DesktopMux());
      terminalDesktop.reset(new TerminalDesktop(&terminalGeo));
      Greeter greeter(rdpArgc, rdpArgv, caughtSignal, *terminalDesktop.get());
      if (!terminalDesktop->initTerminal(-1, -1)) {
        return -1;
      }
      desktopMux->desktop = terminalDesktop.get();
      server.reset(new VNCServerST("rdp2vnc", desktopMux.get()));
      
      listenServer(listeners);
      if (!runTerminal(terminalDesktop.get(), server.get(), listeners, &caughtSignal, [&](int infd, int outfd) {
        greeter.handle(infd, outfd);
      })) {
        return 1;
      }
      rdpClient = move(greeter.getRDPClient());
      if (!rdpClient) {
        desktopMux->desktop = nullptr;
        server.reset(nullptr);
        return 2;
      }

      geo.reset(new Geometry(rdpClient->width(), rdpClient->height()));
      rdpDesktop.reset(new RDPDesktop(geo.get(), rdpClient.get()));
      desktopMux->desktop = rdpDesktop.get();
      //terminalDesktop->stop();
      rdpDesktop->start(server.get());

      rdpClient->setRDPDesktop(rdpDesktop.get());
      rdpClient->startThread();
    } catch (rdr::Exception &e) {
      vlog.error("%s", e.str());
      return 1;
    }
  } else {
    try {
      rdpClient.reset(new RDPClient(rdpArgc, rdpArgv, caughtSignal));
      if (!rdpClient->init(NULL, NULL, NULL, -1, -1) || !rdpClient->start() || !rdpClient->waitConnect()) {
        return 2;
      }

      geo.reset(new Geometry(rdpClient->width(), rdpClient->height()));
      rdpDesktop.reset(new RDPDesktop(geo.get(), rdpClient.get()));

      server.reset(new VNCServerST("rdp2vnc", rdpDesktop.get()));
      rdpClient->setRDPDesktop(rdpDesktop.get());
      listenServer(listeners);
      //rdpClient->startThread();
    } catch (rdr::Exception &e) {
      vlog.error("%s", e.str());
      return 1;
    }
  }

  // RDP client and VNC Desktop have been set
  try {
    while (!caughtSignal) {
      struct timeval tv;
      fd_set rfds, wfds;
      std::list<Socket*> sockets;
      std::list<Socket*>::iterator i;
      {
        std::lock_guard<std::mutex> lock(rdpClient->getMutex());
        
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);

        for (std::list<SocketListener*>::iterator i = listeners.begin();
             i != listeners.end();
             i++)
          FD_SET((*i)->getFd(), &rfds);

        server->getSockets(&sockets);
        int clients_connected = 0;
        for (i = sockets.begin(); i != sockets.end(); i++) {
          if ((*i)->isShutdown()) {
            server->removeSocket(*i);
            delete (*i);
          } else {
            FD_SET((*i)->getFd(), &rfds);
            if ((*i)->outStream().hasBufferedData())
              FD_SET((*i)->getFd(), &wfds);
            clients_connected++;
          }
        }
      }

      tv.tv_sec = 0;
      tv.tv_usec = 10000;
      // Do the wait...
      int n = select(FD_SETSIZE, &rfds, &wfds, 0, &tv);

      if (n < 0) {
        if (errno == EINTR) {
          vlog.debug("Interrupted select() system call");
          continue;
        } else {
          throw rdr::SystemException("select", errno);
        }
      }


      {
        std::lock_guard<std::mutex> lock(rdpClient->getMutex());
        // Accept new VNC connections
        for (std::list<SocketListener*>::iterator i = listeners.begin();
             i != listeners.end();
             i++) {
          if (FD_ISSET((*i)->getFd(), &rfds)) {
            Socket* sock = (*i)->accept();
            if (sock) {
              server->addSocket(sock);
            } else {
              vlog.status("Client connection rejected");
            }
          }
        }

        Timer::checkTimeouts();

        // Client list could have been changed.
        server->getSockets(&sockets);

        // Nothing more to do if there are no client connections.
        if (sockets.empty())
          continue;

        // Process events on existing VNC connections
        for (i = sockets.begin(); i != sockets.end(); i++) {
          if (FD_ISSET((*i)->getFd(), &rfds)) {
            server->processSocketReadEvent(*i);
          }
          if (FD_ISSET((*i)->getFd(), &wfds)) {
            server->processSocketWriteEvent(*i);
          }
        }

        // Process events on RDP connection
        rdpClient->processsEvents();
      }
    }

  } catch (rdr::Exception &e) {
    vlog.error("%s", e.str());
    return 1;
  }

  for (std::list<SocketListener*>::iterator i = listeners.begin();
       i != listeners.end();
       i++) {
    delete *i;
  }

  if (rdpClient) {
    rdpClient->stop();
  }
  vlog.info("Terminated");

  return 0;
}
