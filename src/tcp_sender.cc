#include "tcp_sender.hh"
#include "byte_stream.hh"
#include "tcp_config.hh"
#include <algorithm>

void TCPSender::fill_window( const TransmitFunction& transmit )
{
  // 如果流已经出错，直接发送带 RST 的空段
  if ( writer().has_error() || reader().has_error() ) {
    auto msg = make_empty_message();
    msg.RST = true;
    transmit( msg );
    return;
  }

  const uint64_t effective_window = window_size_ == 0 ? 1 : window_size_; // 对端如果提示0窗口，发送零窗口探测

  // 窗口没有满，还能发送数据
  while ( bytes_in_flight_ < effective_window ) {
    TCPSenderMessage msg {
      .seqno = Wrap32::wrap( next_seqno_abs_, isn_ ),
    };

    uint64_t remaining = effective_window - bytes_in_flight_; // 剩余窗口大小(包含SYN, FIN)

    if ( !syn_sent_ ) { // 第一次发送数据
      if ( remaining == 0 ) {
        break;
      }
      msg.SYN = true;
      syn_sent_ = true;
      remaining -= 1;
    }

    // 确定发送 Segment 的长度
    const size_t payload_len = std::min( { remaining, TCPConfig::MAX_PAYLOAD_SIZE, reader().bytes_buffered() } );

    if ( payload_len > 0 ) { // 装填 Segment
      read( reader(), payload_len, msg.payload );
      remaining -= payload_len;
    }

    if ( !fin_sent_ && reader().is_finished() /*stream结束了*/ && remaining > 0 /*窗口还有空间*/ ) {
      msg.FIN = true;
      fin_sent_ = true;
      remaining -= 1;
    }

    (void)remaining; // FIXME: suppress unused warning

    const auto seg_len = msg.sequence_length();

    if ( seg_len == 0 ) {
      break; // nothing to send
    }

    transmit( msg );
    // outstanding_ 里面的元素一定是按顺序的, 因为就是这么添加进去的
    outstanding_.push_back( { msg, next_seqno_abs_ /*当前segment的abs_seqno*/ } );

    // update state
    next_seqno_abs_ += seg_len;
    bytes_in_flight_ += seg_len;

    if ( !timer_running_ ) { // 重传计时器
      timer_running_ = true;
      time_since_last_tx_ms_ = 0;
    }

    if ( fin_sent_ || ( reader().bytes_buffered() == 0 && !reader().is_finished() ) ) {
      break; // 已经发了 FIN，或当前没有更多数据可发
    }
  }
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return bytes_in_flight_;
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retx_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  fill_window( transmit );
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  TCPSenderMessage msg = {
    .seqno = Wrap32::wrap( next_seqno_abs_, isn_ ),
    .RST = reader().has_error() || writer().has_error(),
  };
  return msg;
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( msg.RST ) {
    reader().set_error();
    writer().set_error();
    return;
  }

  window_size_ = msg.window_size;

  if ( !msg.ackno.has_value() ) { // TCP 的第一次握手，不是 ACK segment, 因此没有 ackno
    return;
  }

  const uint64_t ack_abs = msg.ackno->unwrap( isn_, next_seqno_abs_ );

  if ( ack_abs > next_seqno_abs_ ) { /*将要发送的序号, ack_abs至少是已经发送的序号, 时间顺序上不可能存在这种情况*/
    return;                          // impossible ack, ignore
  }

  if ( ack_abs <= last_ack_abs_ ) { // 重复确认(TCP是累计确认)
    return;                         // duplicate or old ack
  }

  last_ack_abs_ = ack_abs;                            // 更新已确认的最后一个序号（开区间）
  bytes_in_flight_ = next_seqno_abs_ - last_ack_abs_; // outstanding

  while ( !outstanding_.empty() ) {
    const auto& front = outstanding_.front();
    const uint64_t seg_end = front.abs_seqno + front.msg.sequence_length();
    if ( seg_end <= last_ack_abs_ ) {
      outstanding_.pop_front(); // 已经确认了, 删除
    } else {
      break;
    }
  }

  // reset
  consecutive_retx_ = 0;
  RTO_ms_ = initial_RTO_ms_;
  time_since_last_tx_ms_ = 0;
  timer_running_ = bytes_in_flight_ > 0;
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( !timer_running_ || bytes_in_flight_ == 0 ) {
    return;
  }

  time_since_last_tx_ms_ += ms_since_last_tick;

  if ( time_since_last_tx_ms_ < RTO_ms_ /*还没到重传的时间*/ || outstanding_.empty() ) {
    return;
  }

  // 重传第一个outstanding的segment
  // TODO: 改成选择重传，目前只是重传第一个outstanding的segment(能通过测试)

  transmit( outstanding_.front().msg );
  time_since_last_tx_ms_ = 0;

  consecutive_retx_ += 1;
  if ( window_size_ > 0 ) {
    RTO_ms_ <<= 1;
  }

  if ( consecutive_retx_ > TCPConfig::MAX_RETX_ATTEMPTS ) {
    reader().set_error();
    writer().set_error();
  }
}
