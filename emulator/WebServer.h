#pragma once

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef WEB_PORT
#define WEB_PORT 8080
#endif

#include "web_content.h"

struct WebState {
  float lastResult_dKH = 0.0f;
  float lastRefPH = 0.0f;
  float lastTankPH = 0.0f;
  float lastRefSD = 0.0f;
  float lastTankSD = 0.0f;
  float lastTemp = 0.0f;
  const char* cyclePhase = "startup";
  float history[24] = {};
  int historyCount = 0;
  // calibration / mode (kept in sync after every command)
  float anchor_dKH = 0.0f;
  int   mode       = 0;      // 0 = PHDI, 1 = TITR
  float acidNorm   = 0.1f;
  float sampleML   = 25.0f;
  float doseMl     = 0.02f;
  float sampleSPM  = 3200.0f;
  float acidSPM    = 6400.0f;
  // pH calibration wizard state
  int   calStep    = 0;      // 0=idle,1=buf7,2=buf4,3=buf10,4=done
};

static WebState  g_webState;
static std::mutex g_webMutex;   // guards g_webState reads/writes across threads
static int       g_listenFd  = -1;
static std::atomic<bool> g_webRunning{false};

static void webServer_sendResponse(int fd, const char* status, const char* contentType, const char* body, size_t bodyLen) {
  char header[256];
  int hlen = snprintf(header, sizeof(header),
    "HTTP/1.0 %s\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %zu\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n"
    "Connection: close\r\n\r\n",
    status, contentType, bodyLen);
  write(fd, header, hlen);
  if (bodyLen > 0) write(fd, body, bodyLen);
}

static void webServer_handleRequest(int fd, const char* req, size_t reqLen) {
  if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index", 10) == 0) {
    webServer_sendResponse(fd, "200 OK", "text/html", WEB_HTML, strlen(WEB_HTML));
  }
  else if (strncmp(req, "GET /api/status", 15) == 0) {
    char json[2048];
    char histArr[512] = "[";
    std::lock_guard<std::mutex> lock(g_webMutex);
    int count = g_webState.historyCount < 24 ? g_webState.historyCount : 24;
    for (int i = 0; i < count; i++) {
      char num[16];
      snprintf(num, sizeof(num), "%.2f", g_webState.history[i]);
      if (i > 0) strcat(histArr, ",");
      strcat(histArr, num);
    }
    strcat(histArr, "]");
    int len = snprintf(json, sizeof(json),
      "{\"anchor\":%.2f,\"lastResult\":%.2f,\"refPH\":%.4f,\"tankPH\":%.4f,"
      "\"refSD\":%.5f,\"tankSD\":%.5f,\"temp\":%.2f,\"phase\":\"%s\","
      "\"history\":%s,\"historyCount\":%d,"
      "\"mode\":%d,\"acidNorm\":%.4f,\"sampleML\":%.1f,\"doseMl\":%.4f,"
      "\"sampleSPM\":%.0f,\"acidSPM\":%.0f,\"calStep\":%d}",
      g_webState.anchor_dKH, g_webState.lastResult_dKH,
      g_webState.lastRefPH, g_webState.lastTankPH,
      g_webState.lastRefSD, g_webState.lastTankSD,
      g_webState.lastTemp, g_webState.cyclePhase,
      histArr, g_webState.historyCount,
      g_webState.mode, g_webState.acidNorm, g_webState.sampleML,
      g_webState.doseMl, g_webState.sampleSPM, g_webState.acidSPM,
      g_webState.calStep);
    webServer_sendResponse(fd, "200 OK", "application/json", json, len);
  }
  else if (strncmp(req, "POST /api/command", 17) == 0) {
    const char* body = strstr(req, "\r\n\r\n");
    if (body) {
      body += 4;
      std::string cmd(body, reqLen - (body - req));
      while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' '))
        cmd.pop_back();
      for (auto& c : cmd) c = toupper((unsigned char)c);
      extern void handleWebCommand(const char* cmd);
      handleWebCommand(cmd.c_str());
      const char* ok = "{\"ok\":true}";
      webServer_sendResponse(fd, "200 OK", "application/json", ok, strlen(ok));
    } else {
      webServer_sendResponse(fd, "400 Bad Request", "text/plain", "no body", 7);
    }
  }
  else if (strncmp(req, "OPTIONS", 7) == 0) {
    webServer_sendResponse(fd, "204 No Content", "text/plain", "", 0);
  }
  else {
    webServer_sendResponse(fd, "404 Not Found", "text/plain", "not found", 9);
  }
}

// Runs on a background thread — always ready to accept connections.
static void webServer_threadFunc() {
  while (g_webRunning.load()) {
    struct sockaddr_in6 clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = accept(g_listenFd, (struct sockaddr*)&clientAddr, &clientLen);
    if (clientFd < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }

    // Give Safari up to 500ms to finish sending headers before reading.
    struct timeval tv = {0, 500000};
    setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[8192] = {};
    ssize_t n = read(clientFd, buf, sizeof(buf) - 1);
    if (n > 0) webServer_handleRequest(clientFd, buf, (size_t)n);
    close(clientFd);
  }
}

static void webServer_begin() {
  // IPv6 dual-stack: accepts both ::1 and 127.0.0.1 on the same socket.
  // Safari tries IPv6 first; AF_INET-only causes instant connection refused.
  g_listenFd = socket(AF_INET6, SOCK_STREAM, 0);
  if (g_listenFd < 0) {
    g_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listenFd < 0) { perror("socket"); return; }
    int opt = 1;
    setsockopt(g_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(WEB_PORT);
    if (bind(g_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      perror("bind"); close(g_listenFd); g_listenFd = -1; return;
    }
  } else {
    int opt = 1;
    setsockopt(g_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int v6only = 0;
    setsockopt(g_listenFd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    struct sockaddr_in6 addr = {};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr   = in6addr_any;
    addr.sin6_port   = htons(WEB_PORT);
    if (bind(g_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      perror("bind"); close(g_listenFd); g_listenFd = -1; return;
    }
  }

  listen(g_listenFd, 32);
  // Blocking accept in thread — do NOT set O_NONBLOCK on the listen socket.

  g_webRunning.store(true);
  std::thread(webServer_threadFunc).detach();

  printf("Web server on http://localhost:%d\n", WEB_PORT);
  fflush(stdout);
}

// No-op: the background thread handles connections independently.
static void webServer_poll() {}
