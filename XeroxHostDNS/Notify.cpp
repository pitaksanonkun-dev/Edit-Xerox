#include "Notify.h"
#include "AppState.h"
#include "Util.h"
#include <lwip/sockets.h>
#include <time.h>

bool notifySend(int severity, const String& text) {
  uint32_t ip;
  if (!g_netReady || !cfg.notify.syslog.length() || !parseIp4(cfg.notify.syslog, ip)) return false;
  // <PRI>Mmm dd hh:mm:ss host tag: message   (facility 16 = local0)
  char ts[20] = "Jan  1 00:00:00";
  if (timeValid()) {
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(ts, sizeof(ts), "%b %e %H:%M:%S", &tm);
  }
  String msg = "<" + String(16 * 8 + constrain(severity, 0, 7)) + ">" + ts + " " + cfg.net.hostname +
               " XeroxHostDNS: [" + ip4Str(g_myIp) + "] " + text;
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0) return false;
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(cfg.notify.syslogPort);
  a.sin_addr.s_addr = ip;
  int n = sendto(s, msg.c_str(), msg.length(), 0, (struct sockaddr*)&a, sizeof(a));
  close(s);
  Serial.printf("[NOTIFY] %s\n", text.c_str());
  return n == (int)msg.length();
}
