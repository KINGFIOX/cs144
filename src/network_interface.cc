#include <iostream>
#include <utility>

#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "exception.hh"
#include "helpers.hh"
#include "network_interface.hh"

namespace {
constexpr size_t ARP_ENTRY_TTL_MS = 30'000;       // cache lifetime
constexpr size_t ARP_REQUEST_INTERVAL_MS = 5'000; // resend throttle
} // namespace

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( std::string_view name,
                                    std::shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  std::cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ )
            << " and IP address " << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  const uint32_t target_ip = next_hop.ipv4_numeric();

  const auto cache_it = arp_cache_.find( target_ip );
  if ( cache_it != arp_cache_.end() /*已经缓存*/
       && time_ms_ - cache_it->second.time_ms < ARP_ENTRY_TTL_MS /*且没有过期*/ ) {
    EthernetFrame frame;
    frame.header.dst = cache_it->second.ethernet_address;
    frame.header.src = ethernet_address_;
    frame.header.type = EthernetHeader::TYPE_IPv4;
    frame.payload = serialize( dgram );
    transmit( frame );
    return;
  }

  // --------- 可能需要发送 ARP 请求 (要不是没有缓存, 或者是缓存还没有到, 要不就是过期了) ---------

  const auto req_it = arp_request_time_.find( target_ip );
  const bool is_new_request = req_it == arp_request_time_.end(); // 没有请求过
  const bool request_expired
    = !is_new_request && time_ms_ - req_it->second >= ARP_REQUEST_INTERVAL_MS; // 上一次的ARP请求超时了
  const bool need_request = is_new_request || request_expired;                 // 一定需要新的ARP请求

  if ( request_expired ) {
    waiting_dgrams_.erase( target_ip );
  }

  // Remember this datagram for later transmission.
  waiting_dgrams_[target_ip].push_back( clone( dgram ) );

  if ( need_request ) {
    ARPMessage arp_req = {
      .opcode = ARPMessage::OPCODE_REQUEST,
      .sender_ethernet_address = ethernet_address_,
      .sender_ip_address = ip_address_.ipv4_numeric(),
      .target_ip_address = target_ip,
    };

    EthernetFrame frame = {
      .header = {
        .dst =  ETHERNET_BROADCAST,
        .src = ethernet_address_,
        .type = EthernetHeader::TYPE_ARP,
      },
      .payload = serialize( arp_req ),
    };
    transmit( frame );

    arp_request_time_[target_ip] = time_ms_;
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( EthernetFrame frame )
{
  const bool for_me = frame.header.dst == ethernet_address_ || frame.header.dst == ETHERNET_BROADCAST;
  if ( !for_me ) {
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) { // IPv4
    InternetDatagram dgram;
    if ( parse( dgram, frame.payload ) ) {
      datagrams_received_.push( std::move( dgram ) ); // 交给上层协议处理
    }
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage arp;
    if ( !parse( arp, frame.payload ) || !arp.supported() ) {
      return; // failed
    }

    // 更新ARP缓存
    arp_cache_[arp.sender_ip_address] = { .ethernet_address = arp.sender_ethernet_address, .time_ms = time_ms_ };

    // 是对端发来的ARP请求，先回ARP reply（这样符合测试的首选顺序）
    if ( arp.opcode == ARPMessage::OPCODE_REQUEST && arp.target_ip_address == ip_address_.ipv4_numeric() ) {
      ARPMessage reply = {
        .opcode = ARPMessage::OPCODE_REPLY,
        .sender_ethernet_address = ethernet_address_,
        .sender_ip_address = ip_address_.ipv4_numeric(),
        .target_ethernet_address = arp.sender_ethernet_address,
        .target_ip_address = arp.sender_ip_address,
      };

      EthernetFrame out = {
        .header = {
          .dst = arp.sender_ethernet_address,
          .src = ethernet_address_,
          .type = EthernetHeader::TYPE_ARP,
        },
        .payload = serialize( reply ),
      };
      transmit( out );
    }

    // 有要发送的datagram, 发送
    auto waiting_it = waiting_dgrams_.find( arp.sender_ip_address );
    if ( waiting_it != waiting_dgrams_.end() ) {
      for ( const auto& pending : waiting_it->second ) {
        EthernetFrame out = {
          .header = {
            .dst = arp.sender_ethernet_address,
            .src = ethernet_address_,
            .type = EthernetHeader::TYPE_IPv4,
          },
          .payload = serialize( pending ),
        };
        transmit( out );
      }
      waiting_dgrams_.erase( waiting_it );
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  time_ms_ += ms_since_last_tick;

  // Drop expired pending ARP requests and their queued datagrams.
  auto req_it = arp_request_time_.begin();
  while ( req_it != arp_request_time_.end() ) {
    if ( time_ms_ - req_it->second >= ARP_REQUEST_INTERVAL_MS ) {
      waiting_dgrams_.erase( req_it->first );
      req_it = arp_request_time_.erase( req_it );
    } else {
      ++req_it;
    }
  }

  // Expire ARP cache entries.
  auto it = arp_cache_.begin();
  while ( it != arp_cache_.end() ) {
    if ( time_ms_ - it->second.time_ms >= ARP_ENTRY_TTL_MS ) {
      it = arp_cache_.erase( it );
    } else {
      ++it;
    }
  }
}
