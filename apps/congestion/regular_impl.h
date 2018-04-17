/**
 * @file regular_impl.h
 * @brief The regular traffic component
 */
#ifndef REGULAR_IMPL_H
#define REGULAR_IMPL_H

#include "congestion.h"

void connect_sessions_func_regular(AppContext *c) {
  erpc::rt_assert(FLAGS_process_id != 0);
  erpc::rt_assert(c->thread_id >= FLAGS_incast_threads_other);

  c->session_num_vec.resize(
      (FLAGS_incast_threads_other * (FLAGS_num_processes - 1)) - 1);

  size_t session_idx = 0;
  for (size_t p_i = 1; p_i < FLAGS_num_processes; p_i++) {
    for (size_t t_i = FLAGS_incast_threads_other;
         t_i < FLAGS_incast_threads_other + FLAGS_regular_threads_other;
         t_i++) {
      c->session_num_vec.at(session_idx) =
          c->rpc->create_session(erpc::get_uri_for_process(p_i), t_i);
      erpc::rt_assert(c->session_num_vec[session_idx] >= 0);

      if (kAppVerbose) {
        printf("congestion: Regular thread %zu: Creating session to %zu/%zu.\n",
               c->thread_id, p_i, t_i);
      }

      session_idx++;
    }
  }

  while (c->num_sm_resps != c->session_num_vec.size()) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }
}

void cont_regular(erpc::RespHandle *, void *, size_t);  // Forward declaration

// Send a regular request using this MsgBuffer
void send_req_regular(AppContext *c, size_t msgbuf_idx) {
  erpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_idx];
  assert(req_msgbuf.get_data_size() == FLAGS_regular_req_size);

  if (kAppVerbose) {
    printf("congestion: Thread %zu sending regular req using msgbuf_idx %zu.\n",
           c->thread_id, msgbuf_idx);
  }

  size_t session_idx = c->fastrand.next_u32() % c->session_num_vec.size();
  c->req_ts[msgbuf_idx] = erpc::rdtsc();
  c->rpc->enqueue_request(c->session_num_vec[session_idx], kAppReqTypeRegular,
                          &req_msgbuf, &c->resp_msgbuf[msgbuf_idx],
                          cont_regular, msgbuf_idx);
}

// Request handler for regular traffic
void req_handler_regular(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);
  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();

  req_handle->prealloc_used = true;
  erpc::MsgBuffer &resp_msgbuf = req_handle->pre_resp_msgbuf;
  c->rpc->resize_msg_buffer(&resp_msgbuf, FLAGS_regular_resp_size);

  resp_msgbuf.buf[0] = req_msgbuf->buf[0];  // Touch the response
  c->rpc->enqueue_response(req_handle);
}

// Continuation for regular traffic
void cont_regular(erpc::RespHandle *resp_handle, void *_context, size_t _tag) {
  const erpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  size_t msgbuf_idx = _tag;
  if (kAppVerbose) {
    printf("congestion: Received regular resp for msgbuf %zu.\n", msgbuf_idx);
  }

  // Measure latency. 1 us granularity is sufficient for large RPC latency.
  auto *c = static_cast<AppContext *>(_context);
  double usec = erpc::to_usec(erpc::rdtsc() - c->req_ts[msgbuf_idx],
                              c->rpc->get_freq_ghz());
  c->regular_latency.update(usec);

  assert(resp_msgbuf->get_data_size() == FLAGS_regular_resp_size);
  erpc::rt_assert(resp_msgbuf->buf[0] == kAppDataByte);  // Touch

  c->rpc->release_response(resp_handle);
  send_req_regular(c, msgbuf_idx);  // Clock this response
}

// The function executed by each regular thread in the cluster
void thread_func_regular(size_t thread_id, app_stats_t *app_stats,
                         erpc::Nexus *nexus) {
  AppContext c;
  c.thread_id = thread_id;
  c.app_stats = app_stats;
  if (thread_id == 0) {
    c.tmp_stat = new TmpStat("rx_gbps tx_gbps re_tx avg_us 99_us");
  }

  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  erpc::rt_assert(port_vec.size() > 0);
  uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id),
                                  basic_sm_handler, phy_port);
  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;

  connect_sessions_func_regular(&c);
  printf("congestion: Regular thread %zu: Sessions connected.\n", thread_id);

  for (size_t i = 0; i < FLAGS_regular_concurrency; i++) {
    c.resp_msgbuf[i] = rpc.alloc_msg_buffer(FLAGS_regular_resp_size);
    erpc::rt_assert(c.resp_msgbuf[i].buf != nullptr, "Alloc failed");
    c.req_msgbuf[i] = rpc.alloc_msg_buffer(FLAGS_regular_req_size);
    erpc::rt_assert(c.req_msgbuf[i].buf != nullptr, "Alloc failed");
    memset(c.req_msgbuf[i].buf, kAppDataByte, FLAGS_regular_req_size);
  }

  size_t console_ref_tsc = erpc::rdtsc();
  for (size_t msgbuf_i = 0; msgbuf_i < FLAGS_regular_concurrency; msgbuf_i++) {
    send_req_regular(&c, msgbuf_i);
  }

  for (size_t i = 0; i < FLAGS_test_ms; i += kAppEvLoopMs) {
    rpc.run_event_loop(kAppEvLoopMs);
    if (unlikely(ctrl_c_pressed == 1)) break;
    if (c.session_num_vec.size() == 0) continue;  // No stats to print

    // Publish stats
    auto &stats = c.app_stats[c.thread_id];
    assert(stats.incast_gbps == 0);
    stats.re_tx = c.rpc->get_num_retransmissions(c.session_num_vec[0]);
    stats.regular_50_us = c.regular_latency.perc(0.50);
    stats.regular_99_us = c.regular_latency.perc(0.99);

    // Reset stats for next iteration
    c.rpc->reset_num_retransmissions(c.session_num_vec[0]);
    c.regular_latency.reset();

    printf(
        "congestion: Regular thread %zu: Retransmissions %zu. "
        "Latency {%.1f us, %.1f us}.\n",
        c.thread_id, stats.re_tx, stats.regular_50_us, stats.regular_99_us);
    // An incast thread will write to tmp_stat
  }

  erpc::TimingWheel *wheel = rpc.get_wheel();
  if (wheel != nullptr && !wheel->record_vec.empty()) {
    const size_t num_to_print = 200;
    const size_t tot_entries = wheel->record_vec.size();
    const size_t base_entry = tot_entries * .9;

    printf("Printing up to 200 entries toward the end of wheel record\n");
    size_t num_printed = 0;

    for (size_t i = base_entry; i < tot_entries; i++) {
      auto &rec = wheel->record_vec.at(i);
      printf("wheel: %s\n",
             rec.to_string(console_ref_tsc, rpc.get_freq_ghz()).c_str());

      if (num_printed++ == num_to_print) break;
    }
  }

  // We don't disconnect sessions
}

#endif
