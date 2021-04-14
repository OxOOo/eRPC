#include "transport.h"
#include "util/logger.h"
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdexcept>

namespace erpc {

constexpr size_t Transport::kMaxDataPerPkt;

Transport::RoutingInfo Transport::make_routing_info(std::string hostname, uint16_t port)
{
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%u", port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  struct addrinfo *addrinfo = nullptr;
  int r = getaddrinfo(hostname.c_str(), port_str, &hints, &addrinfo);
  if (r != 0 || addrinfo == nullptr) {
    char issue_msg[1000];
    sprintf(issue_msg, "Failed to resolve %s:%u. getaddrinfo error = %s.", hostname.c_str(), port, gai_strerror(r));
    throw std::runtime_error(issue_msg);
  }
  RoutingInfo info;
  memcpy(info.buf, &addrinfo->ai_addrlen, sizeof(addrinfo->ai_addrlen));
  memcpy(info.buf + sizeof(addrinfo->ai_addrlen), addrinfo->ai_addr, addrinfo->ai_addrlen);
  freeaddrinfo(addrinfo);

  return info;
}

Transport::Transport(uint16_t data_udp_port, uint8_t rpc_id, size_t numa_node, FILE* trace_file)
  : data_udp_port(data_udp_port), rpc_id(rpc_id), numa_node(numa_node), trace_file(trace_file)
{
  sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); //AF_INET:IPV4;SOCK_DGRAM:UDP
  if (sock_fd == -1) {
    throw std::runtime_error("Transport: Failed to create local socket.");
  }

  struct sockaddr_in serveraddr;
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons(static_cast<unsigned short>(data_udp_port));

  int r = bind(sock_fd, reinterpret_cast<struct sockaddr *>(&serveraddr), sizeof(serveraddr));
  if (r != 0) {
    throw std::runtime_error("Transport: Failed to bind socket to port " + std::to_string(data_udp_port));
  }

  int flags = fcntl(sock_fd, F_GETFL, 0);
  int r2 = fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);
  if (r2 != 0) {
    throw std::runtime_error("Transport: Failed to set O_NONBLOCK");
  }

  send_buf = allocator.allocate(kMaxDataPerPkt + sizeof(pkthdr_t));
  if (!send_buf) {
    throw std::runtime_error("Transport: Failed of OOM");
  }
  recv_buf = allocator.allocate(kMaxDataPerPkt + sizeof(pkthdr_t));
  if (!recv_buf) {
    throw std::runtime_error("Transport: Failed of OOM");
  }

  ERPC_INFO("eRPC Transport: Created with transport UDP port %u.\n", data_udp_port);
}

void Transport::init_mem(uint8_t** rx_ring)
{
  this->rx_ring = rx_ring;
  this->rx_ring_head = 0;
  this->rx_ring_tail = kNumRxRingEntries - 1;
}

Transport::~Transport()
{
  if (sock_fd != -1) close(sock_fd);
}

void Transport::tx_burst(const tx_burst_item_t* tx_burst_arr, size_t num_pkts)
{
  for (size_t i = 0; i < num_pkts; i ++) {
    const tx_burst_item_t &item = tx_burst_arr[i];
    const MsgBuffer *msg_buffer = item.msg_buffer;

    if (item.pkt_idx == 0) {
      const pkthdr_t *pkthdr = msg_buffer->get_pkthdr_0();
      const size_t pkt_size = msg_buffer->get_pkt_size<kMaxDataPerPkt>(0);

      socklen_t ai_addrlen = *reinterpret_cast<const socklen_t*>(item.routing_info->buf);
      const struct sockaddr *ai_addr = reinterpret_cast<const struct sockaddr *>(item.routing_info->buf + sizeof(ai_addrlen));
      ssize_t ret = sendto(sock_fd, pkthdr, pkt_size, 0, ai_addr, ai_addrlen);
      if (ret != static_cast<ssize_t>(pkt_size)) {
        throw std::runtime_error("sendto() failed. errno = " + std::string(strerror(errno)));
      }
    } else {
      const pkthdr_t *pkthdr = msg_buffer->get_pkthdr_n(item.pkt_idx);
      const size_t pkt_size = msg_buffer->get_pkt_size<kMaxDataPerPkt>(item.pkt_idx);
      memcpy(send_buf, pkthdr, sizeof(pkthdr_t));
      memcpy(send_buf + sizeof(pkthdr_t), &msg_buffer->buf[item.pkt_idx * kMaxDataPerPkt], pkt_size - sizeof(pkthdr_t));

      socklen_t ai_addrlen = *reinterpret_cast<const socklen_t*>(item.routing_info->buf);
      const struct sockaddr *ai_addr = reinterpret_cast<const struct sockaddr *>(item.routing_info->buf + sizeof(ai_addrlen));
      ssize_t ret = sendto(sock_fd, send_buf, pkt_size, 0, ai_addr, ai_addrlen);
      if (ret != static_cast<ssize_t>(pkt_size)) {
        throw std::runtime_error("sendto() failed. errno = " + std::string(strerror(errno)));
      }
    }
  }
}

void Transport::tx_flush()
{
  // nothing
}

size_t Transport::rx_burst()
{
  size_t cnt = 0;
  while (rx_ring_head != rx_ring_tail) {
    ssize_t size = recv(sock_fd, recv_buf, kMaxDataPerPkt + sizeof(pkthdr_t), 0);
    if (size == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return cnt;
      } else {
        throw std::runtime_error("recv() failed. errno = " + std::string(strerror(errno)));
      }
    } else {
      rx_ring[rx_ring_head] = allocator.allocate(kMaxDataPerPkt + sizeof(pkthdr_t));
      memcpy(rx_ring[rx_ring_head], recv_buf, static_cast<size_t>(size));
      cnt ++;
      rx_ring_head = (rx_ring_head + 1) % kNumRxRingEntries;
    }
  }
  return cnt;
}

void Transport::post_recvs(size_t num_recvs)
{
  for (size_t i = 0; i < num_recvs; i++) {
    rx_ring_tail = (rx_ring_tail + 1) % kNumRxRingEntries;
    allocator.deallocate(rx_ring[rx_ring_tail], kMaxDataPerPkt + sizeof(pkthdr_t));
  }
}

}  // namespace erpc
