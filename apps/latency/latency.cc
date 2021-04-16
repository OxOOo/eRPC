#include "util/latency.h"
#include <gflags/gflags.h>
#include <signal.h>
#include <cstring>
#include <string>
#include "../apps_common.h"
#include "rpc.h"
#include "util/autorun_helpers.h"
#include "util/numautils.h"
#include "util/pmem.h"

static constexpr size_t kAppEvLoopMs = 1000;  // Duration of event loop
static constexpr bool kAppVerbose = false;    // Print debug info on datapath
static constexpr double kAppLatFac = 10.0;    // Precision factor for latency
static constexpr size_t kAppReqType = 1;      // eRPC request type

static constexpr size_t kAppRespSize = 8;
static constexpr size_t kAppMinReqSize = 64;
static constexpr size_t kAppMaxReqSize = 1024;

const std::string uris[] = {
  "172.17.224.104:31850",
  "172.17.224.105:31860"
};

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

class ServerContext : public BasicAppContext {
 public:
  size_t file_offset = 0;
  uint8_t *pbuf;
};

class ClientContext : public BasicAppContext {
 public:
  size_t start_tsc;
  size_t req_size;  // Between kAppMinReqSize and kAppMaxReqSize
  erpc::Latency latency;
  erpc::MsgBuffer req_msgbuf, resp_msgbuf;
  ~ClientContext() {}
};

void req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<ServerContext *>(_context);

  erpc::Rpc::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                 kAppRespSize);
  c->rpc->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf);
}

void server_func(erpc::Nexus *nexus) {
  // std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  // uint8_t phy_port = port_vec.at(0);

  ServerContext c;
  erpc::Rpc rpc(nexus, static_cast<void *>(&c), 0 /* tid */,
                                  basic_sm_handler);
  c.rpc = &rpc;

  while (true) {
    rpc.run_event_loop(1000);
    if (ctrl_c_pressed == 1) break;
  }
}

void connect_session(ClientContext &c) {
  std::string server_uri = uris[0];
  printf("Process %zu: Creating session to %s.\n", FLAGS_process_id,
         server_uri.c_str());

  int session_num = c.rpc->create_session(server_uri, 0 /* tid */);
  erpc::rt_assert(session_num >= 0, "Failed to create session");
  c.session_num_vec.push_back(session_num);

  while (c.num_sm_resps != 1) {
    c.rpc->run_event_loop(kAppEvLoopMs);
    if (unlikely(ctrl_c_pressed == 1)) return;
  }
}

void app_cont_func(void *, void *);
inline void send_req(ClientContext &c) {
  c.start_tsc = erpc::rdtsc();
  c.rpc->enqueue_request(c.session_num_vec[0], kAppReqType, &c.req_msgbuf,
                         &c.resp_msgbuf, app_cont_func, nullptr);
}

void app_cont_func(void *_context, void *) {
  auto *c = static_cast<ClientContext *>(_context);
  assert(c->resp_msgbuf.get_data_size() == kAppRespSize);

  double req_lat_us =
      erpc::to_usec(erpc::rdtsc() - c->start_tsc, c->rpc->get_freq_ghz());
  c->latency.update(static_cast<size_t>(req_lat_us * kAppLatFac));

  send_req(*c);
}

void client_func(erpc::Nexus *nexus) {
  // std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  // uint8_t phy_port = port_vec.at(0);

  ClientContext c;
  erpc::Rpc rpc(nexus, static_cast<void *>(&c), 0,
                                  basic_sm_handler);

  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;
  c.req_size = kAppMinReqSize;

  c.req_msgbuf = rpc.alloc_msg_buffer_or_die(kAppMaxReqSize);
  c.resp_msgbuf = rpc.alloc_msg_buffer_or_die(kAppMaxReqSize);
  c.rpc->resize_msg_buffer(&c.req_msgbuf, c.req_size);
  c.rpc->resize_msg_buffer(&c.resp_msgbuf, c.req_size);

  connect_session(c);

  printf("Process %zu: Session connected. Starting work.\n", FLAGS_process_id);
  printf("write_size median_us 5th_us 99th_us 999th_us max_us\n");

  send_req(c);
  for (size_t i = 0; i < FLAGS_test_ms; i += 1000) {
    rpc.run_event_loop(kAppEvLoopMs);  // 1 second
    if (ctrl_c_pressed == 1) break;
    printf("%zu %.1f %.1f %.1f %.1f %.1f\n", c.req_size,
           c.latency.perc(.5) / kAppLatFac, c.latency.perc(.05) / kAppLatFac,
           c.latency.perc(.99) / kAppLatFac, c.latency.perc(.999) / kAppLatFac,
           c.latency.max() / kAppLatFac);

    c.req_size *= 2;
    if (c.req_size > kAppMaxReqSize) c.req_size = kAppMinReqSize;
    c.rpc->resize_msg_buffer(&c.req_msgbuf, c.req_size);
    c.rpc->resize_msg_buffer(&c.resp_msgbuf, c.req_size);

    c.latency.reset();
  }
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  erpc::Nexus nexus(uris[FLAGS_process_id], 0, 0);
  nexus.register_req_func(kAppReqType, req_handler);

  auto t =
      std::thread(FLAGS_process_id == 0 ? server_func : client_func, &nexus);
  erpc::bind_to_core(t, 0, 0);
  t.join();
}
