#include "common.h"
erpc::Rpc *rpc;

void req_handler(erpc::ReqHandle *req_handle, void *) {
  if (kRespMsgSize <= erpc::Transport::kMaxDataPerPkt) {
    auto &resp = req_handle->pre_resp_msgbuf;
    rpc->resize_msg_buffer(&resp, kRespMsgSize);
    rpc->enqueue_response(req_handle, &resp);
  } else {
    erpc::MsgBuffer &resp_msgbuf = req_handle->dyn_resp_msgbuf;
    resp_msgbuf = rpc->alloc_msg_buffer_or_die(kRespMsgSize);
    rpc->enqueue_response(req_handle, &resp_msgbuf);
  }
}

int main() {
  std::string server_uri = kServerHostname + ":" + std::to_string(kServerUDPPort);
  erpc::Nexus nexus(server_uri, 0, 0);
  nexus.register_req_func(kReqType, req_handler);

  rpc = new erpc::Rpc(&nexus, nullptr, 0, nullptr);
  rpc->run_event_loop(100000);
}
