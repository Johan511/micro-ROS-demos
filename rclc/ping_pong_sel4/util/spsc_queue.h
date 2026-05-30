#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

extern void seL4_Yield(void); 

static inline void pause_instr(void) {
  seL4_Yield();
}

typedef struct spsc_queue_t {
  _Alignas(64) uint64_t front;
  _Alignas(64) uint64_t back;

  _Alignas(64) uint64_t consumerCachedFront;
  uint64_t consumerCachedBack;

  _Alignas(64) uint64_t producerCachedFront;
  uint64_t producerCachedBack;

  _Alignas(64) char *begin;
  char *end;
  uint8_t log2BlockSize;
  uint8_t log2Capacity;

} spsc_queue_t;

static inline bool spsc_init(spsc_queue_t *q, char *begin, char *end, uint8_t log2BlockSize) {
  uint64_t blockSize = (1ull << log2BlockSize);
  end = begin + ((end - begin) / blockSize) * blockSize;
  if (begin == end)
    return false;

  q->begin = begin;
  q->end = end;
  q->front = q->consumerCachedFront = q->producerCachedFront = 0;
  q->back = q->consumerCachedBack = q->producerCachedBack = 0;
  q->log2BlockSize = log2BlockSize;
  q->log2Capacity = __builtin_ctzll((end - begin) / blockSize);
  return true;
}

static inline void spsc_push(spsc_queue_t *q) {
  __atomic_store_n(&q->back, ++q->producerCachedBack, __ATOMIC_RELEASE);
}

static inline char *spsc_new_block(spsc_queue_t *q) {
  while (q->producerCachedBack - q->producerCachedFront ==
         (1ull << q->log2Capacity)) {
    pause_instr();
    q->producerCachedFront = __atomic_load_n(&q->front, __ATOMIC_RELAXED);
  }
  __atomic_thread_fence(__ATOMIC_ACQUIRE);

  uint64_t back = q->producerCachedBack;
  back &= (1ull << q->log2Capacity) - 1ull;
  return q->begin + back * (1ull << q->log2BlockSize);
}

static inline bool spsc_empty(spsc_queue_t *q)
{
  q->consumerCachedBack = __atomic_load_n(&q->back, __ATOMIC_ACQUIRE);
  return q->consumerCachedBack == q->consumerCachedFront;
}

static inline void spsc_pop(spsc_queue_t *q) {
  __atomic_store_n(&q->front, ++q->consumerCachedFront, __ATOMIC_RELEASE);
}

static inline char *spsc_front_block(spsc_queue_t *q) {
  while (q->consumerCachedBack == q->consumerCachedFront) {
    pause_instr();
    q->consumerCachedBack = __atomic_load_n(&q->back, __ATOMIC_RELAXED);
  }
  __atomic_thread_fence(__ATOMIC_ACQUIRE);

  uint64_t front = q->consumerCachedFront;
  front &= (1ull << q->log2Capacity) - 1ull;
  return q->begin + front * (1ull << q->log2BlockSize);
}
