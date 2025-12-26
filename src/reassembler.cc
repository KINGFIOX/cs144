#include "reassembler.hh"

#include <algorithm>
#include <iterator>

void Reassembler::insert( uint64_t first_index, std::string data, bool is_last_substring )
{
  if ( is_last_substring ) {
    eof_index_ = first_index + data.size();
  }

  const uint64_t first_unassembled = next_index();
  const uint64_t first_unacceptable = first_unassembled + output_.writer().available_capacity();

  uint64_t start = first_index;
  uint64_t end = first_index + data.size();

  if ( end <= first_unassembled || start >= first_unacceptable ) {
    if ( eof_index_.has_value() && next_index() == *eof_index_ ) {
      output_.writer().close();
    }
    return;
  }

  start = std::max( start, first_unassembled );
  end = std::min( end, first_unacceptable );

  if ( end <= start ) {
    if ( eof_index_.has_value() && next_index() == *eof_index_ ) {
      output_.writer().close();
    }
    return;
  }

  Segment seg { start, data.substr( start - first_index, end - start ) };

  auto it = segments_.begin();
  while ( it != segments_.end() && it->start < seg.start ) {
    ++it;
  }

  if ( it != segments_.begin() ) {
    auto prev_it = std::prev( it );
    if ( prev_it->end() >= seg.start ) {
      seg.merge( *prev_it );
      it = segments_.erase( prev_it );
    }
  }

  while ( it != segments_.end() && it->start <= seg.end() ) {
    seg.merge( *it );
    it = segments_.erase( it );
  }

  segments_.insert( it, std::move( seg ) );

  while ( !segments_.empty() && segments_.front().start == next_index() ) {
    auto& front = segments_.front();
    output_.writer().push( front.data );
    segments_.pop_front();
  }

  if ( eof_index_.has_value() && next_index() == *eof_index_ ) {
    output_.writer().close();
  }
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const
{
  uint64_t pending_bytes = 0;
  for ( const auto& seg : segments_ ) {
    pending_bytes += seg.data.size();
  }
  return pending_bytes;
}

void Reassembler::Segment::merge( const Segment& other )
{
  const uint64_t merged_start = std::min( this->start, other.start );
  const uint64_t merged_end = std::max( this->end(), other.end() );

  std::string merged( merged_end - merged_start, '\0' );
  merged.replace( this->start - merged_start, this->data.size(), this->data );
  merged.replace( other.start - merged_start, other.data.size(), other.data );

  this->start = merged_start;
  this->data.swap( merged );
}
