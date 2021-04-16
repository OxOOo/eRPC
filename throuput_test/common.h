#include <stdio.h>
#include "rpc.h"

static const std::string kServerHostname = "172.17.224.104";
static const std::string kClientHostname = "172.17.224.105";

static constexpr uint16_t kServerUDPPort = 31850;
static constexpr uint16_t kClientUDPPort = 31860;
static constexpr uint8_t kReqType = 2;
static constexpr size_t kReqMsgSize = 15000;
static constexpr size_t kRespMsgSize = 32;
static constexpr int kConcurrency = 4;
