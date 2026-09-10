/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file measures the C library routines the shell leans on. The same
 * binary is built against every candidate library so that a per-routine
 * timing gap between two libraries can be read directly. The reported number
 * is nanoseconds for each operation.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile uint64_t sink;

static uint64_t now_nanoseconds(void)
{
  struct timespec moment;
  clock_gettime(CLOCK_MONOTONIC, &moment);
  return (uint64_t) moment.tv_sec * 1000000000ULL + (uint64_t) moment.tv_nsec;
}

static void report(const char *name, size_t size, uint64_t elapsed,
                   uint64_t operation_count)
{
  double each = (double) elapsed / (double) operation_count;
  printf("%-18s %8zu %12.3f\n", name, size, each);
}

static char *source_buffer;
static char *target_buffer;

#define BUFFER_LENGTH (1u << 20)

static void measure_memcpy(size_t size, uint64_t operation_count)
{
  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    memcpy(target_buffer + (index & 63), source_buffer + (index & 63), size);
    sink += (uint64_t) target_buffer[index & 63];
  }
  report("memcpy", size, now_nanoseconds() - started, operation_count);
}

static void measure_memset(size_t size, uint64_t operation_count)
{
  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    memset(target_buffer + (index & 63), (int) (index & 255), size);
    sink += (uint64_t) target_buffer[index & 63];
  }
  report("memset", size, now_nanoseconds() - started, operation_count);
}

static void measure_memchr(size_t size, uint64_t operation_count)
{
  memset(source_buffer, 'a', size);
  source_buffer[size - 1] = 'z';

  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    const char *found = memchr(source_buffer, 'z', size);
    sink += (uint64_t) (uintptr_t) found;
  }
  report("memchr", size, now_nanoseconds() - started, operation_count);
}

static void measure_strlen(size_t size, uint64_t operation_count)
{
  memset(source_buffer, 'a', size);
  source_buffer[size] = '\0';

  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    sink += strlen(source_buffer);
  }
  report("strlen", size, now_nanoseconds() - started, operation_count);
}

static void measure_memcmp(size_t size, uint64_t operation_count)
{
  memset(source_buffer, 'a', size + 1);
  memset(target_buffer, 'a', size + 1);
  target_buffer[size - 1] = 'b';

  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    sink += (uint64_t) (memcmp(source_buffer, target_buffer, size) != 0);
  }
  report("memcmp", size, now_nanoseconds() - started, operation_count);
}

static void measure_strcmp(size_t size, uint64_t operation_count)
{
  memset(source_buffer, 'a', size);
  source_buffer[size] = '\0';
  memset(target_buffer, 'a', size);
  target_buffer[size - 1] = 'b';
  target_buffer[size] = '\0';

  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    sink += (uint64_t) (strcmp(source_buffer, target_buffer) != 0);
  }
  report("strcmp", size, now_nanoseconds() - started, operation_count);
}

static void measure_malloc_churn(size_t size, uint64_t operation_count)
{
  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    void *block = malloc(size);
    if (block == NULL) {
      exit(1);
    }

    ((char *) block)[0] = (char) index;
    sink += (uint64_t) ((char *) block)[0];
    free(block);
  }
  report("malloc-churn", size, now_nanoseconds() - started, operation_count);
}

#define LIVE_BLOCK_COUNT 512u

static void measure_malloc_live(size_t size, uint64_t operation_count)
{
  void *live[LIVE_BLOCK_COUNT];
  for (unsigned index = 0; index < LIVE_BLOCK_COUNT; index += 1) {
    live[index] = malloc(size);
    if (live[index] == NULL) {
      exit(1);
    }
  }

  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    unsigned slot = (unsigned) (index % LIVE_BLOCK_COUNT);
    free(live[slot]);
    live[slot] = malloc(size);
    if (live[slot] == NULL) {
      exit(1);
    }

    ((char *) live[slot])[0] = (char) index;
    sink += (uint64_t) ((char *) live[slot])[0];
  }
  report("malloc-live", size, now_nanoseconds() - started, operation_count);

  for (unsigned index = 0; index < LIVE_BLOCK_COUNT; index += 1) {
    free(live[index]);
  }
}

static void measure_realloc_growth(size_t size, uint64_t operation_count)
{
  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    char *block = malloc(16);
    for (size_t length = 32; length <= size; length *= 2) {
      char *grown = realloc(block, length);
      if (grown == NULL) {
        exit(1);
      }

      block = grown;
      block[0] = (char) index;
    }

    sink += (uint64_t) block[0];
    free(block);
  }
  report("realloc-growth", size, now_nanoseconds() - started, operation_count);
}

static void measure_snprintf_number(uint64_t operation_count)
{
  char line[64];
  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    sink += (uint64_t) snprintf(line, sizeof line, "%d", (int) index);
  }
  report("snprintf-int", 0, now_nanoseconds() - started, operation_count);
}

static void measure_snprintf_string(uint64_t operation_count)
{
  char line[128];
  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    sink += (uint64_t) snprintf(line, sizeof line, "%s=%s/%u", "PATH",
                                "/usr/local/bin", (unsigned) index);
  }
  report("snprintf-str", 0, now_nanoseconds() - started, operation_count);
}

static void measure_strstr(size_t size, uint64_t operation_count)
{
  memset(source_buffer, 'a', size);
  memcpy(source_buffer + size - 8, "needle_z", 8);
  source_buffer[size] = '\0';

  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    sink += (uint64_t) (uintptr_t) strstr(source_buffer, "needle_z");
  }
  report("strstr", size, now_nanoseconds() - started, operation_count);
}

static int compare_unsigned(const void *left, const void *right)
{
  unsigned first = *(const unsigned *) left;
  unsigned second = *(const unsigned *) right;
  if (first < second) {
    return -1;
  }

  return first > second;
}

static void measure_qsort(size_t count, uint64_t operation_count)
{
  unsigned *values = malloc(count * sizeof *values);
  if (values == NULL) {
    exit(1);
  }

  uint64_t started = now_nanoseconds();
  for (uint64_t index = 0; index < operation_count; index += 1) {
    for (size_t slot = 0; slot < count; slot += 1) {
      values[slot] = (unsigned) ((slot * 2654435761u) ^ (unsigned) index);
    }

    qsort(values, count, sizeof *values, compare_unsigned);
    sink += values[0];
  }
  report("qsort", count, now_nanoseconds() - started, operation_count);

  free(values);
}

int main(void)
{
  source_buffer = malloc(BUFFER_LENGTH);
  target_buffer = malloc(BUFFER_LENGTH);
  if (source_buffer == NULL || target_buffer == NULL) {
    return 1;
  }

  memset(source_buffer, 'a', BUFFER_LENGTH);
  memset(target_buffer, 'b', BUFFER_LENGTH);

  printf("%-18s %8s %12s\n", "routine", "size", "ns/op");

  measure_memcpy(8, 20000000);
  measure_memcpy(32, 20000000);
  measure_memcpy(128, 10000000);
  measure_memcpy(1024, 4000000);
  measure_memcpy(16384, 300000);

  measure_memset(8, 20000000);
  measure_memset(32, 20000000);
  measure_memset(128, 10000000);
  measure_memset(1024, 4000000);
  measure_memset(16384, 300000);

  measure_memchr(64, 10000000);
  measure_memchr(1024, 2000000);
  measure_memchr(65536, 50000);

  measure_strlen(8, 20000000);
  measure_strlen(64, 10000000);
  measure_strlen(1024, 2000000);

  measure_memcmp(8, 20000000);
  measure_memcmp(32, 20000000);
  measure_memcmp(256, 8000000);

  measure_strcmp(8, 20000000);
  measure_strcmp(64, 10000000);
  measure_strcmp(1024, 2000000);

  measure_strstr(1024, 500000);

  measure_malloc_churn(16, 5000000);
  measure_malloc_churn(64, 5000000);
  measure_malloc_churn(256, 5000000);
  measure_malloc_churn(4096, 2000000);

  measure_malloc_live(16, 5000000);
  measure_malloc_live(64, 5000000);
  measure_malloc_live(256, 5000000);

  measure_realloc_growth(4096, 500000);

  measure_snprintf_number(3000000);
  measure_snprintf_string(2000000);

  measure_qsort(64, 200000);
  measure_qsort(1024, 10000);

  free(source_buffer);
  free(target_buffer);

  return 0;
}
