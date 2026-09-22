#pragma once
#include "config.h"

inline AppConfig sampleConfig() {
  AppConfig c;
  c.mountRoot = "/mnt/test";
  c.servers.append({"test-server", "Test server", "nas.example", "192.0.2.10",
                    Protocol::Nfs});
  c.shares.append({"test-share", "test-server", "Media", "/exports/media",
                   "/mnt/test/Media", "", AutomountMode::Disabled, true});
  return c;
}
