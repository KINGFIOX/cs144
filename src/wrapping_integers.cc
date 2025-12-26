#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point /*initial value*/ )
{
  return { zero_point + static_cast<uint32_t>( n ) }; // 会调用参数构造函数，uint32_t 加法自然会回环
}

static uint64_t distance( uint64_t a, uint64_t b )
{
  return a > b ? a - b : b - a;
}

// 有许多 absolute sequence number 映射到同一个 sequence number, 我们需要找到距离 checkpoint 最接近的那一个
uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  const uint64_t MOD = 1ULL << 32;                            // 2^32
  const uint32_t offset = raw_value_ - zero_point.raw_value_; // 低 32 位
  const uint64_t base = checkpoint & ~( MOD - 1 );            // 取高 32 位
  uint64_t candidate = base + offset;                         // 可能是最终结果

  // 考虑 checkpoint 附近的两个可能结果，选择距离 checkpoint 最接近的那一个
  uint64_t best = candidate;
  uint64_t best_dist = distance( best, checkpoint );

  if ( candidate + MOD > candidate ) { // avoid up overflow
    const uint64_t up = candidate + MOD;
    const uint64_t dist_up = distance( up, checkpoint );
    if ( dist_up < best_dist || ( dist_up == best_dist && up < best ) ) {
      best = up;
      best_dist = dist_up;
    }
  }

  if ( candidate >= MOD ) { // avoid down overflow
    const uint64_t down = candidate - MOD;
    const uint64_t dist_down = distance( down, checkpoint );
    if ( dist_down < best_dist || ( dist_down == best_dist && down < best ) ) {
      best = down;
    }
  }

  return best;
}
