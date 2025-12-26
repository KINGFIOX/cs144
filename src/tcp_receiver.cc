#include "tcp_receiver.hh"

#include <algorithm>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( message.RST ) {
    reassembler_.reader().set_error();
    return;
  }

  if ( !isn_.has_value() ) {
    if ( !message.SYN ) { // 尚未收到 SYN，除 SYN 以外的报文直接丢弃
      return;
    }
    isn_ = message.seqno; // 收到的是 SYN, 设置 ISN
  } else if ( message.SYN ) {
    return; // 已经建立了 isn，再次收到 SYN 直接忽略
  }

  const Writer& writer = reassembler_.writer();
  const uint64_t checkpoint = writer.bytes_pushed() + 1 /*SYN*/ + ( writer.is_closed() ? 1 /*FIN*/ : 0 );
  const uint64_t abs_seqno = message.seqno.unwrap( *isn_, checkpoint );

  const uint64_t stream_index = message.SYN ? 0 : abs_seqno - 1;

  reassembler_.insert( stream_index, std::move( message.payload ), message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{

  const Writer& writer = reassembler_.writer();

  TCPReceiverMessage msg {
    .window_size = static_cast<uint16_t>( min<uint64_t>( UINT16_MAX, writer.available_capacity() ) ),
    .RST = reassembler_.reader().has_error(),
  };

  if ( !isn_.has_value() ) {
    return msg;
  }

  uint64_t ack_abs = writer.bytes_pushed() /*stream_index*/ + 1 /*SYN*/;
  if ( writer.is_closed() ) {
    ack_abs += 1 /*FIN*/;
  }

  msg.ackno = Wrap32::wrap( ack_abs, *isn_ );
  return msg;
}
