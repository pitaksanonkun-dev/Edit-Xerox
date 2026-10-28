#include "PrinterMon.h"
#include "AppState.h"
#include "Verify.h"
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>

static const uint32_t CHECK_INTERVAL_MS = 60000;
static const uint32_t CONNECT_TIMEOUT_MS = 1500;

static int      chkSock  = -1;
static int      chkIdx   = -1;
static uint32_t chkStart = 0;

static void finish(bool online) {
  if (chkSock >= 0) close(chkSock);
  chkSock = -1;
  if (chkIdx >= 0 && chkIdx < (int)printerState.size()) {
    printerState[chkIdx].online = online;
    printerState[chkIdx].checked = true;
    printerState[chkIdx].lastCheckMs = millis();
  }
  chkIdx = -1;
}

static void start(int idx) {
  chkIdx = idx;
  chkStart = millis();
  chkSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (chkSock < 0) { finish(false); return; }
  fcntl(chkSock, F_SETFL, fcntl(chkSock, F_GETFL, 0) | O_NONBLOCK);
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(80);
  a.sin_addr.s_addr = printerState[idx].ip;
  int r = connect(chkSock, (struct sockaddr*)&a, sizeof(a));
  if (r == 0) finish(true);
  else if (errno == ECONNREFUSED) finish(true);       // host answered with RST: it is up
  else if (errno != EINPROGRESS) finish(false);
}

void printerMonReset() {
  if (chkSock >= 0) close(chkSock);
  chkSock = -1;
  chkIdx = -1;
}

void printerMonLoop() {
  if (chkSock >= 0) {
    fd_set wf;
    FD_ZERO(&wf);
    FD_SET(chkSock, &wf);
    struct timeval tv = {0, 0};
    if (select(chkSock + 1, nullptr, &wf, nullptr, &tv) > 0) {
      int err = 0;
      socklen_t l = sizeof(err);
      getsockopt(chkSock, SOL_SOCKET, SO_ERROR, &err, &l);
      finish(err == 0 || err == ECONNREFUSED);
    } else if (millis() - chkStart > CONNECT_TIMEOUT_MS) {
      finish(false);
    }
    return;
  }
  if (verifyRunning()) return;            // Verify needs the TCP socket budget
  uint32_t now = millis();
  for (size_t i = 0; i < printerState.size(); i++) {
    PrinterState& p = printerState[i];
    if (!p.ip) continue;
    if (!p.checked || now - p.lastCheckMs >= CHECK_INTERVAL_MS) { start(i); return; }
  }
}
