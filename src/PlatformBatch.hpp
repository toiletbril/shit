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

struct BatchOperation
{
  enum class Kind : u8
  {
    Read = 0,
    Write = 1,
    Lstat = 2,
    Stat = 3,
  };

  static fn read(descriptor fd, char *buffer, usize byte_count,
                 u64 byte_offset = 0) wontthrow -> BatchOperation;
  static fn write(descriptor fd, const char *buffer, usize byte_count,
                  u64 byte_offset = 0) wontthrow -> BatchOperation;
  static fn lstat(const Path &path, file_status &status) wontthrow
      -> BatchOperation;
  static fn stat(const Path &path, file_status &status) wontthrow
      -> BatchOperation;
  static fn lstat(Path &&path, file_status &status) wontthrow
      -> BatchOperation = delete;
  static fn lstat(const Path &&path, file_status &status) wontthrow
      -> BatchOperation = delete;
  static fn stat(Path &&path, file_status &status) wontthrow
      -> BatchOperation = delete;
  static fn stat(const Path &&path, file_status &status) wontthrow
      -> BatchOperation = delete;

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
  BatchOperation() = default;
  friend class Batch;
};

struct BatchResult
{
  u64 request_id{0};
  usize transferred_byte_count{0};
  i32 error_number{0};
};

namespace batch_internal {

using batched_syscall_id = BatchOperation::Kind;
using batched_syscall_result = BatchResult;
using batched_syscall = BatchOperation;

fn execute_batch_operations(const batched_syscall *operations,
                            usize operation_count,
                            BatchResult *results) wontthrow -> void;

} /* namespace batch_internal */

class Batch
{
public:
  explicit Batch(Allocator allocator);

  fn reserve(usize operation_count) throws -> void;
  fn add(BatchOperation operation) throws -> void;
  fn clear() wontthrow -> void;
  fn execute(ArrayList<BatchResult> &results) const throws -> void;
  fn execute() const throws -> ArrayList<BatchResult>;

  pure fn count() const wontthrow -> usize;

private:
  ArrayList<BatchOperation> m_operations;
};

} /* namespace koshka::os */
