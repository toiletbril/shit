/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This header defines portable batched filesystem and descriptor operations.
 * Platform.hpp includes it after the native descriptor and file status types
 * are available. Batch owns the operation vector and returns ordered results.
 */

#pragma once

#include "Common.hpp"
#include "Platform.hpp"

namespace koshka::os {

struct batch_operation
{
  enum class Kind : u8
  {
    Read = 0,
    Write = 1,
    WriteCurrent = 2,
    Lstat = 3,
    Stat = 4,
    Exists = 5,
  };

  static fn read(descriptor fd, char *buffer, usize byte_count,
                 u64 byte_offset = 0) wontthrow -> batch_operation;
  static fn write(descriptor fd, const char *buffer, usize byte_count,
                  u64 byte_offset = 0) wontthrow -> batch_operation;
  static fn write_current(descriptor fd, const char *buffer,
                          usize byte_count) wontthrow -> batch_operation;
  static fn lstat(const Path &path, file_status &status) wontthrow
      -> batch_operation;
  static fn stat(const Path &path, file_status &status) wontthrow
      -> batch_operation;
  static fn exists(const Path &path) wontthrow -> batch_operation;
  static fn lstat(Path &&path, file_status &status) wontthrow
      -> batch_operation = delete;
  static fn lstat(const Path &&path, file_status &status) wontthrow
      -> batch_operation = delete;
  static fn stat(Path &&path, file_status &status) wontthrow
      -> batch_operation = delete;
  static fn stat(const Path &&path, file_status &status) wontthrow
      -> batch_operation = delete;
  static fn exists(Path &&path) wontthrow -> batch_operation = delete;
  static fn exists(const Path &&path) wontthrow -> batch_operation = delete;

  const Path *path{nullptr};
  const char *input_buffer{nullptr};
  char *output_buffer{nullptr};
  file_status *status{nullptr};
  u64 request_id{0};
  u64 byte_offset{0};
  usize byte_count{0};
  descriptor fd{KOSH_INVALID_FD};
  Kind syscall_id{Kind::Read};

private:
  batch_operation() = default;
  friend class Batch;
};

struct batch_result
{
  u64 request_id{0};
  usize transferred_byte_count{0};
  i32 error_number{0};
  bool is_existing{false};
};

static_assert(sizeof(usize) != 8 || sizeof(batch_result) == 24);

namespace batch_internal {

using batched_syscall_id = batch_operation::Kind;
using batched_syscall_result = batch_result;
using batched_syscall = batch_operation;

fn execute_batch_operations(const batched_syscall *operations,
                            usize operation_count,
                            batch_result *results) wontthrow -> void;

} /* namespace batch_internal */

class Batch
{
public:
  explicit Batch(Allocator allocator);

  fn reserve(usize operation_count) throws -> void;
  fn add(batch_operation operation) throws -> void;
  fn clear() wontthrow -> void;
  fn execute(ArrayList<batch_result> &results) const throws -> void;
  fn execute() const throws -> ArrayList<batch_result>;

  pure fn count() const wontthrow -> usize;

private:
  ArrayList<batch_operation> m_operations;
};

} /* namespace koshka::os */
