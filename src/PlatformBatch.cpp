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

fn Batch::execute(ArrayList<BatchResult> &results) const throws -> void
{
  results.clear();
  results.reserve(m_operations.count());
  for (usize index = 0; index < m_operations.count(); index++)
    results.push({});

  internal::execute_batch_operations(m_operations.begin(),
                                     m_operations.count(), results.begin());
}

fn Batch::execute() const throws -> ArrayList<BatchResult>
{
  let results = ArrayList<BatchResult>{m_operations.allocator()};
  execute(results);
  return results;
}

pure fn Batch::count() const wontthrow -> usize { return m_operations.count(); }

}
