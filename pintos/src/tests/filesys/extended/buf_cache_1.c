/* Tests the effectiveness of the cache by first reading with a cold cache
and then re-read from the same file.
The access counts should be the same and the hit rates should improve. */
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "lib/string.h"
#include "lib/stdlib.h"
#include <debug.h>

void
test_main (void)
{
  int block_size = 512;
  /* Create a file to read. */
  create ("/test_data.txt", 0);
  int fd = open ("/test_data.txt");
  char buffer[block_size];
  memset(buffer, 'a', block_size);
  int i = 0;
  for (; i < 7; i ++) {
    write(fd, buffer, block_size);
  }
  close(fd);
  buffer_reset (); // We want to flush the buffer to get a cold buffer.
  buffer_stats_reset ();
  int misses;
  int accesses;
  int misses_cold;
  int accesses_cold;

  /* Read with a cold buffer cache. */
  fd = open ("/test_data.txt");
  for (i = 0; i < 7; i ++) {
    read(fd, buffer, block_size);
  }
  accesses_cold = buffer_accesses();
  misses_cold = buffer_miss_count();
  close(fd);
  msg ("Hit rate with a cold cache: %d / %d", accesses_cold - misses_cold, accesses_cold);

  /* Read with a buffer cache. */
  buffer_stats_reset();
  fd = open ("/test_data.txt");
  for (i = 0; i < 7; i ++) {
    read(fd, buffer, block_size);
  }
  accesses = buffer_accesses();
  misses = buffer_miss_count();
  close(fd);
  msg ("Hit rate for re-opened file: %d / %d", accesses - misses, accesses);

  ASSERT (accesses == accesses_cold);
  ASSERT (misses_cold > misses);
}
