#include "router.hh"

#include <iostream>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  if (prefix_length > 32) { // ignore invalid routes
    return;
  }

  // 按前缀长度从大到小插入，保持路由表有序（最长前缀优先）
  auto it = route_table_.begin();
  while ( it != route_table_.end() && it->prefix_length > prefix_length ) {
    ++it;
  }
  route_table_.insert( it, { route_prefix, prefix_length, next_hop, interface_num } );
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  // 遍历每个接口，转发其收到的所有报文
  for ( const auto& iface : interfaces_ ) {
    auto& queue = iface->datagrams_received();

    while ( !queue.empty() ) {
      InternetDatagram dgram = std::move( queue.front() );
      queue.pop();

      // TTL<=1 无法继续转发，直接丢弃
      if ( dgram.header.ttl <= 1 ) {
        continue;
      }

      // 最长前缀匹配
      optional<RouteEntry> best_route {};
      for ( const auto& route : route_table_ ) { // 最长前缀匹配
        const uint32_t mask = route.prefix_length == 0 ? 0 : 0xffffffffu << ( 32 - route.prefix_length );
        if ( ( dgram.header.dst & mask ) == ( route.prefix & mask ) ) {
          best_route = route;
          break;
        }
      }

      if ( !best_route.has_value() ) { // not found
        continue;
      }

      // update TTL and checksum
      dgram.header.ttl -= 1;
      dgram.header.compute_checksum();

      const Address next_hop = best_route->next_hop.has_value()
                                 ? best_route->next_hop.value()                    // 下一条
                                 : Address::from_ipv4_numeric( dgram.header.dst ); // 直连

      interfaces_.at( best_route->interface_num )->send_datagram( dgram, next_hop );
    }
  }
}
