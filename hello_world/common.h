#include <stdio.h>
#include "rpc.h"

static const std::string kServerHostname = "192.168.31.204";
static const std::string kClientHostname = "192.168.31.102";

static constexpr uint16_t kServerUDPPort = 31850;
static constexpr uint16_t kClientUDPPort = 31860;
static constexpr uint8_t kReqType = 2;
static constexpr size_t kMsgSize = 1600;
