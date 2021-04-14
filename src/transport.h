/**
 * @file transport.h
 * @brief General definitions for all transport types.
 */
#pragma once

#include <functional>
#include <memory>
#include <sys/socket.h>
#include <stdint.h>
#include "common.h"
#include "msg_buffer.h"
#include "pkthdr.h"
#include "util/buffer.h"
#include "util/math_utils.h"

namespace erpc {

class STDAlloc;  // Forward declaration: HugeAlloc needs MemRegInfo

/// Generic unreliable transport
class Transport {
 public:
  static constexpr size_t kMTU = 1024;

  /// Maximum data bytes (i.e., non-header) in a packet
  static constexpr size_t kMaxDataPerPkt = (kMTU - sizeof(pkthdr_t));

  // rx_ring是一个循环队列，第一次收到10个包会放在0~9，之后放10
  static constexpr size_t kNumRxRingEntries = 4096;

  // 好象是批发传输的包数量
  static constexpr size_t kPostlist = 32;

  // 控制信息(CR/RFR)的内存池大小
  static constexpr size_t kCtrlBufferSize = 64;
  static_assert(kCtrlBufferSize >= kPostlist, "kCtrlBufferSize too small");

 public:
  // Info about dest
  struct RoutingInfo {
    uint8_t buf[64]; // ai_addrlen:sizeof(socklen_t)
  };
  static RoutingInfo make_routing_info(std::string hostname, uint16_t port);

  /// Info about a packet to transmit
  struct tx_burst_item_t {
    RoutingInfo* routing_info;  ///< Routing info for this packet
    MsgBuffer* msg_buffer;      ///< MsgBuffer for this packet

    size_t pkt_idx;  /// Packet index (not pkt_num) in msg_buffer to transmit
    size_t* tx_ts = nullptr;  ///< TX timestamp, only for congestion control
    bool drop;                ///< Drop this packet. Used only with kTesting.
  };

  /**
   * @brief Partially construct the transport object without using eRPC's
   * hugepage allocator. The device driver is allowed to use its own hugepages
   * (with a different allocator).
   *
   * This function must initialize \p reg_mr_func and \p dereg_mr_func, which
   * will be used to construct the allocator.
   *
   * @param rpc_id The RPC ID of the parent RPC
   *
   * @throw runtime_error if creation fails
   */
  Transport(uint16_t data_udp_port, uint8_t rpc_id, size_t numa_node, FILE* trace_file);

  void init_mem(uint8_t** rx_ring);

  ~Transport();

  /**
   * @brief Transmit a batch of packets
   *
   * Multiple packets may belong to the same msgbuf; burst items contain
   * offsets into a msgbuf.
   *
   * @param tx_burst_arr Info about the packets to TX
   * @param num_pkts The total number of packets to transmit (<= \p kPostlist)
   */
  void tx_burst(const tx_burst_item_t* tx_burst_arr, size_t num_pkts);

  /// Complete pending TX DMAs, returning ownership of all TX buffers to eRPC
  void tx_flush();

  /**
   * @brief The generic packet RX function
   *
   * @return the number of new packets available in the RX ring. The Rpc layer
   * controls posting of RECVs explicitly using post_recvs().
   */
  size_t rx_burst();

  /**
   * @brief Post RECVs to the receive queue
   *
   * @param num_recvs The zero or more RECVs to post
   */
  void post_recvs(size_t num_recvs);

  /// Return the link bandwidth (bytes per second)
  size_t get_bandwidth() const { return 1 << 25; } // FIXME

  // Constructor args first.
  const uint16_t data_udp_port;   ///< UDP port for datapath
  const uint8_t rpc_id;    ///< The parent Rpc's ID
  const size_t numa_node;  ///< The NUMA node of the parent Nexus

  // Members initialized after the hugepage allocator is provided
  FILE* trace_file;       ///< The parent Rpc's high-verbosity log file

  struct {
    size_t tx_flush_count = 0;  ///< Number of times tx_flush() has been called
  } testing;

private:
  std::allocator<uint8_t> allocator;
  uint8_t** rx_ring;
  size_t rx_ring_head, rx_ring_tail;  ///< Current unused RX ring buffer
  int sock_fd;
  uint8_t* send_buf;
  uint8_t* recv_buf;
};
}  // namespace erpc
