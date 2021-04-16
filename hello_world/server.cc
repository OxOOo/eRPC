#include "common.h"
erpc::Rpc *rpc;

void req_handler(erpc::ReqHandle *req_handle, void *) {
  printf("received %s\n", req_handle->get_req_msgbuf()->buf);
  auto &resp = req_handle->pre_resp_msgbuf;
  rpc->resize_msg_buffer(&resp, 16);
  sprintf(reinterpret_cast<char *>(resp.buf), "world");

  rpc->enqueue_response(req_handle, &resp);
}

int main() {
  std::string server_uri = kServerHostname + ":" + std::to_string(kServerUDPPort);
  erpc::Nexus nexus(server_uri, 0, 0);
  nexus.register_req_func(kReqType, req_handler);

  rpc = new erpc::Rpc(&nexus, nullptr, 0, nullptr);
  rpc->run_event_loop(100000);
}
