#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <functional>
#include <list>

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms ), RTO_ms_( initial_RTO_ms )
  {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // For testing: how many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // For testing: how many consecutive retransmissions have happened?
  const Writer& writer() const { return input_.writer(); }
  const Reader& reader() const { return input_.reader(); }
  Writer& writer() { return input_.writer(); }

private:
  Reader& reader() { return input_.reader(); }
  void fill_window( const TransmitFunction& transmit );

  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;

  uint64_t next_seqno_abs_ { 0 };        // 下一次发送的绝对序号
  uint64_t last_ack_abs_ { 0 };          // 已确认的最后一个序号（开区间）
  uint64_t bytes_in_flight_ { 0 };       // 未被确认的序号数量
  uint64_t RTO_ms_;                      // 当前 RTO
  uint64_t time_since_last_tx_ms_ { 0 }; // 距离上次（重）传的时间
  uint64_t consecutive_retx_ { 0 };      // 连续重传次数
  uint16_t window_size_ { 1 };           // 最近一次通告窗口，0 按 1 处理
  bool timer_running_ { false };
  bool syn_sent_ { false };
  bool fin_sent_ { false };

  struct Outstanding
  {
    TCPSenderMessage msg;
    uint64_t abs_seqno {};
  };

  std::list<Outstanding> outstanding_ {};
};
