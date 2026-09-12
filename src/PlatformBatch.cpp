/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the portable Batch operation queue. It stores requests
 * in insertion order, assigns their stable result identifiers, and submits the
 * complete vector to the active platform backend in one operation.
 */

#include "Platform.hpp"

namespace koshka::os {

fn BatchOperation::read(descriptor fd, char *buffer, usize byte_count,
                        u64 byte_offset) wontthrow -> BatchOperation
{
  BatchOperation operation;
  operation.m_kind = Kind::Read;
  operation.m_descriptor = fd;
  operation.m_output_buffer = buffer;
  operation.m_byte_count = byte_count;
  operation.m_byte_offset = byte_offset;
  return operation;
}

fn BatchOperation::write(descriptor fd, const char *buffer, usize byte_count,
                         u64 byte_offset) wontthrow -> BatchOperation
{
  BatchOperation operation;
  operation.m_kind = Kind::Write;
  operation.m_descriptor = fd;
  operation.m_input_buffer = buffer;
  operation.m_byte_count = byte_count;
  operation.m_byte_offset = byte_offset;
  return operation;
}

fn BatchOperation::lstat(const Path &path, file_status &status) wontthrow
    -> BatchOperation
{
  BatchOperation operation;
  operation.m_kind = Kind::Lstat;
  operation.m_path = &path;
  operation.m_status = &status;
  return operation;
}

fn BatchOperation::stat(const Path &path, file_status &status) wontthrow
    -> BatchOperation
{
  BatchOperation operation;
  operation.m_kind = Kind::Stat;
  operation.m_path = &path;
  operation.m_status = &status;
  return operation;
}

pure fn BatchOperation::get_kind() const wontthrow -> Kind { return m_kind; }

pure fn BatchOperation::get_descriptor() const wontthrow -> descriptor
{
  return m_descriptor;
}

pure fn BatchOperation::get_input_buffer() const wontthrow -> const char *
{
  return m_input_buffer;
}

pure fn BatchOperation::get_output_buffer() const wontthrow -> char *
{
  return m_output_buffer;
}

pure fn BatchOperation::get_byte_count() const wontthrow -> usize
{
  return m_byte_count;
}

pure fn BatchOperation::get_byte_offset() const wontthrow -> u64
{
  return m_byte_offset;
}

pure fn BatchOperation::get_path() const wontthrow -> const Path &
{
  ASSERT(m_path != nullptr, "batch operation has no path");
  return *m_path;
}

pure fn BatchOperation::get_status() const wontthrow -> file_status &
{
  ASSERT(m_status != nullptr, "batch operation has no status output");
  return *m_status;
}

Batch::Batch(Allocator allocator) : m_operations(allocator) {}

fn Batch::reserve(usize operation_count) throws -> void
{
  m_operations.reserve(operation_count);
}

fn Batch::add(BatchOperation operation) throws -> void
{
  internal::batched_syscall record{};
  record.path = operation.m_path;
  record.input_buffer = operation.m_input_buffer;
  record.output_buffer = operation.m_output_buffer;
  record.status = operation.m_status;
  record.request_id = m_operations.count();
  record.byte_offset = operation.m_byte_offset;
  record.byte_count = operation.m_byte_count;
  record.fd = operation.m_descriptor;
  record.syscall_id = operation.m_kind;
  m_operations.push(record);
}

fn Batch::clear() wontthrow -> void { m_operations.clear(); }

static pure fn is_same_metadata_request(
    const internal::batched_syscall &left,
    const internal::batched_syscall &right) wontthrow -> bool
{
  if (left.syscall_id != right.syscall_id) return false;
  if (left.syscall_id != BatchOperation::Kind::Lstat &&
      left.syscall_id != BatchOperation::Kind::Stat)
  {
    return false;
  }
  if (left.path == nullptr || right.path == nullptr) return false;

  return left.path->text().view() == right.path->text().view();
}

static pure fn is_metadata_request(
    const internal::batched_syscall &operation) wontthrow -> bool
{
  return operation.path != nullptr &&
         (operation.syscall_id == BatchOperation::Kind::Lstat ||
          operation.syscall_id == BatchOperation::Kind::Stat);
}

static fn find_canonical_operation_positions(
    const ArrayList<internal::batched_syscall> &operations,
    ArrayList<usize> &canonical_positions) throws -> bool
{
  usize metadata_count = 0;
  for (let const &operation : operations)
    if (is_metadata_request(operation)) metadata_count++;

  if (metadata_count < 2 ||
      metadata_count > ArrayList<usize>::MAXIMUM_ELEMENT_COUNT / 4)
  {
    return false;
  }

  canonical_positions.clear();
  canonical_positions.reserve(operations.count());
  for (usize index = 0; index < operations.count(); index++)
    canonical_positions.push(index);

  usize bucket_count = 4;
  while (bucket_count < metadata_count * 2)
    bucket_count *= 2;

  let buckets = ArrayList<usize>{operations.allocator()};
  buckets.reserve(bucket_count);
  for (usize index = 0; index < bucket_count; index++)
    buckets.push(SIZE_MAX);

  bool has_repeated_request = false;
  for (usize index = 0; index < operations.count(); index++) {
    let const &operation = operations[index];
    if (!is_metadata_request(operation)) continue;

    let const path = operation.path->text().view();
    let const kind_hash =
        static_cast<u64>(operation.syscall_id) * 0x9e3779b97f4a7c15ull;
    usize bucket =
        static_cast<usize>(hash_bytes(path) ^ kind_hash) & (bucket_count - 1);
    loop
    {
      let const existing_position = buckets[bucket];
      if (existing_position == SIZE_MAX) {
        buckets[bucket] = index;
        break;
      }
      if (is_same_metadata_request(operations[existing_position], operation)) {
        canonical_positions[index] = existing_position;
        has_repeated_request = true;
        break;
      }

      bucket = (bucket + 1) & (bucket_count - 1);
    }
  }

  return has_repeated_request;
}

fn Batch::execute(ArrayList<BatchResult> &results) const throws -> void
{
  let canonical_positions = ArrayList<usize>{m_operations.allocator()};
  if (!find_canonical_operation_positions(m_operations, canonical_positions)) {
    results.clear();
    results.reserve(m_operations.count());
    for (usize index = 0; index < m_operations.count(); index++)
      results.push({});

    internal::execute_batch_operations(m_operations.begin(),
                                       m_operations.count(), results.begin());
    return;
  }

  let optimized_operations =
      ArrayList<internal::batched_syscall>{m_operations.allocator()};
  let optimized_positions = ArrayList<usize>{m_operations.allocator()};
  optimized_operations.reserve(m_operations.count());
  optimized_positions.reserve(m_operations.count());
  for (usize index = 0; index < m_operations.count(); index++) {
    let const canonical_position = canonical_positions[index];
    if (canonical_position != index) {
      optimized_positions.push(optimized_positions[canonical_position]);
      continue;
    }

    optimized_positions.push(optimized_operations.count());
    optimized_operations.push(m_operations[index]);
  }

  let optimized_results = ArrayList<BatchResult>{m_operations.allocator()};
  optimized_results.reserve(optimized_operations.count());
  for (usize index = 0; index < optimized_operations.count(); index++)
    optimized_results.push({});

  internal::execute_batch_operations(optimized_operations.begin(),
                                     optimized_operations.count(),
                                     optimized_results.begin());

  results.clear();
  results.reserve(m_operations.count());
  for (usize index = 0; index < m_operations.count(); index++) {
    let const optimized_position = optimized_positions[index];
    let result = optimized_results[optimized_position];
    result.request_id = index;
    results.push(result);

    let const &operation = m_operations[index];
    let const &optimized_operation = optimized_operations[optimized_position];
    if (result.error_number == 0 && operation.status != nullptr &&
        optimized_operation.status != nullptr &&
        operation.status != optimized_operation.status)
    {
      *operation.status = *optimized_operation.status;
    }
  }
}

fn Batch::execute() const throws -> ArrayList<BatchResult>
{
  let results = ArrayList<BatchResult>{m_operations.allocator()};
  execute(results);
  return results;
}

pure fn Batch::count() const wontthrow -> usize { return m_operations.count(); }

} /* namespace koshka::os */
