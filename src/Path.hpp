#pragma once

#include "ArrayList.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Maybe.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

/* The owning filesystem path this shell uses in place of std::filesystem::path.
   The text is a String, so a path carries no std::string and no heavyweight
   library. Pure-text questions, such as the parent or the filename, are answered
   by scanning the bytes. Questions that touch the disk, such as exists or
   is_directory, go through one syscall each rather than the std::filesystem
   machinery, which the shell measured as slow. The separator is the platform
   one, and a forward slash is always accepted on input so a script written for
   POSIX keeps working on Windows. */
struct Path
{
  Path() = default;
  explicit Path(StringView text);

  [[nodiscard]] const String &text() const;
  [[nodiscard]] const char *c_str() const;
  [[nodiscard]] usize size() const;
  [[nodiscard]] bool empty() const;

  /* The text without a trailing separator, the directory holding this path. An
     empty or root path yields itself. */
  [[nodiscard]] Path parent() const;
  /* The final component, after the last separator. */
  [[nodiscard]] StringView filename() const;
  /* The trailing extension of the filename including the dot, or empty when the
     filename has none. A leading dot does not start an extension. */
  [[nodiscard]] StringView extension() const;

  /* True when the path starts at a root, a leading separator on POSIX or a drive
     on Windows. */
  [[nodiscard]] bool is_absolute() const;
  [[nodiscard]] bool is_relative() const;

  /* Resolve . and .. components and collapse repeated separators, the
     std::filesystem lexically_normal without touching the disk. */
  [[nodiscard]] Path normalized() const;

  /* This path joined onto a base when it is relative, the base being the current
     working directory unless one is given. */
  [[nodiscard]] Path to_absolute() const;

  /* Append a component with a separator between, so a builder grows a path one
     piece at a time. */
  Path &push_component(StringView component);

  /* Replace the extension of the filename, adding one when absent. */
  [[nodiscard]] Path with_extension(StringView new_extension) const;

  /* Disk queries, one syscall each. file_size is None when the path is missing
     or is not a regular file. */
  [[nodiscard]] bool exists() const;
  [[nodiscard]] bool is_directory() const;
  [[nodiscard]] bool is_regular_file() const;
  [[nodiscard]] Maybe<u64> file_size() const;

  /* The access checks the test builtin asks for, each one access() call. */
  [[nodiscard]] bool is_readable() const;
  [[nodiscard]] bool is_writable() const;
  [[nodiscard]] bool is_executable() const;

  [[nodiscard]] bool operator==(const Path &other) const;

  /* The working directory of the process. */
  [[nodiscard]] static Path current_directory();
  static ErrorOr<Ok> set_current_directory(const Path &path);
  /* The directory for temporary files, from TMPDIR or a platform default. */
  [[nodiscard]] static Path temp_directory();

  /* The immediate children of a directory, filenames only, without . and .. .
     None when the path is not a readable directory. */
  [[nodiscard]] static Maybe<ArrayList<String>> read_directory(const Path &dir);

private:
  String m_text{};
};

/* A small builder for assembling a path from a root and a run of components,
   so a caller spells the intent rather than juggling separators by hand. */
struct PathBuilder
{
  PathBuilder() = default;
  explicit PathBuilder(StringView root);

  /* Add a component, inserting a separator unless the builder is empty or the
     component itself starts at a root. */
  PathBuilder &append(StringView component);
  /* Add raw bytes with no separator, for extending the last component. */
  PathBuilder &append_raw(StringView bytes);

  [[nodiscard]] Path build() const;

private:
  String m_text{};
};

} /* namespace shit */
