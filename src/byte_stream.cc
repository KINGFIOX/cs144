#include "byte_stream.hh"
#include <cassert>

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity )
{
  buffer_.resize( capacity );
}

void Writer::push( string data )
{
  if ( closed_ ) {
    return; // wrong, but silence
  }

  auto to_write = min( data.size(), available_capacity() );
  auto rear = bytes_pushed_ % capacity_; // 指向队尾元素的下一个位置
  auto pos = 0;                          // position in data

  if ( rear + to_write > capacity_ ) {
    // wrap-around: write tail, then head
    const size_t first_part = capacity_ - rear;
    data.copy( &buffer_[rear], first_part, pos );
    rear = 0;
    to_write -= first_part;
    pos += first_part;
    bytes_pushed_ += first_part;
  }

  data.copy( &buffer_[rear], to_write, pos );
  bytes_pushed_ += to_write;
}

void Writer::close()
{
  // Your code here.
  closed_ = true;
}

bool Writer::is_closed() const
{
  return closed_; // Your code here.
}

uint64_t Writer::available_capacity() const
{
  const auto used_capacity = bytes_pushed_ - bytes_popped_;
  return capacity_ - used_capacity; // Your code here.
}

uint64_t Writer::bytes_pushed() const
{
  return bytes_pushed_; // Your code here.
}

std::string_view Reader::peek() const
{
  const auto buffered = bytes_buffered();
  if ( buffered == 0 ) {
    return {};
  }

  const auto front = bytes_popped_ % capacity_;
  const auto first_len = std::min<uint64_t>( buffered, capacity_ - front );
  return { buffer_.data() + front, static_cast<size_t>( first_len ) };
}

void Reader::pop( uint64_t len )
{
  const auto buffered_bytes = bytes_buffered();
  if ( len > buffered_bytes ) {
    assert( 0 && "not enough bytes to pop" );
  }
  bytes_popped_ += len; // Your code here.
}

bool Reader::is_finished() const
{
  return closed_ and bytes_pushed_ == bytes_popped_; // Your code here.
}

uint64_t Reader::bytes_buffered() const
{
  return bytes_pushed_ - bytes_popped_; // Your code here.
}

uint64_t Reader::bytes_popped() const
{
  return bytes_popped_; // Your code here.
}
