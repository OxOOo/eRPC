#include "common.h"
erpc::Rpc *rpc;
erpc::MsgBuffer req;
erpc::MsgBuffer resp;
int session_num;

size_t tx_bytes = 0;
size_t rx_bytes = 0;

void send_req();
void cont_func(void *, void *) {
  tx_bytes += kReqMsgSize;
  rx_bytes += kRespMsgSize;
  send_req();
}

void send_req() {
  rpc->enqueue_request(session_num, kReqType, &req, &resp, cont_func, nullptr);
}

void sm_handler(int, erpc::SmEventType, erpc::SmErrType, void *) {}

int main() {
  std::string client_uri = kClientHostname + ":" + std::to_string(kClientUDPPort);
  erpc::Nexus nexus(client_uri, 0, 0);

  rpc = new erpc::Rpc(&nexus, nullptr, 0, sm_handler);

  std::string server_uri = kServerHostname + ":" + std::to_string(kServerUDPPort);
  session_num = rpc->create_session(server_uri, 0);

  while (!rpc->is_connected(session_num)) rpc->run_event_loop_once();
  printf("rpc connected\n");

  req = rpc->alloc_msg_buffer_or_die(kReqMsgSize);
  resp = rpc->alloc_msg_buffer_or_die(kRespMsgSize);

  for (int i = 0; i < kConcurrency; i ++) {
    send_req();
  }
  for (int i = 0; i < 20; i ++) {
    rpc->run_event_loop(1000);
    printf("TX = %.2lf Gbps, RX = %.2lf Gbps\n",
      static_cast<double>(tx_bytes) * 8 / 1000000000,
      static_cast<double>(rx_bytes) * 8 / 1000000000);
    tx_bytes = 0;
    rx_bytes = 0;
  }

  delete rpc;
}
