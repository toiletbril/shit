#pragma once

#include "ArrayList.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Maybe.hpp"
#include "MimicMood.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

/* The owning filesystem path this shell uses. Pure-text questions, such as the
   parent or the filename, are answered by scanning the bytes, while questions
   that touch the disk go through one syscall each. The separator is the
   platform one, and a forward slash is always accepted on input so a script
   written for POSIX keeps working on Windows. */
class Path
{
public:
  Path() = default;
  explicit Path(StringView text);

  mustuse fn clone() const throws -> Path { return Path{*this}; }

  hot mustuse pure fn text() const wontthrow -> const String &;
  hot mustuse pure fn c_str() const wontthrow -> const char *;
  hot mustuse pure fn count() const wontthrow -> usize;
  hot mustuse pure fn is_empty() const wontthrow -> bool;

  /* The text without a trailing separator, the directory holding this path. An
     empty or root path yields itself. */
  mustuse fn parent() const throws -> Path;
  /* The final component, after the last separator. */
  mustuse pure fn filename() const wontthrow -> StringView;
  /* The trailing extension of the filename including the dot, or empty when
     none. A leading dot does not start an extension. */
  mustuse pure fn extension() const wontthrow -> StringView;

  /* True when the path starts at a root, a leading separator on POSIX or a
     drive on Windows. */
  mustuse pure fn is_absolute() const wontthrow -> bool;
  mustuse pure fn is_relative() const wontthrow -> bool;

  /* Resolve . and .. components and collapse repeated separators, without
     touching the disk. */
  cold mustuse fn normalized() const throws -> Path;

  /* This path joined onto a base when it is relative, the base being the
     current working directory unless one is given. */
  mustuse fn to_absolute() const throws -> Path;

  /* Append a component with a separator between, so a builder grows a path one
     piece at a time. */
  fn push_component(StringView component) throws -> Path &;

  /* Replace the extension of the filename, adding one when absent. */
  mustuse pure fn with_extension(StringView new_extension) const throws -> Path;

  /* Disk queries, one syscall each. file_size is None when the path is missing
     or is not a regular file. */
  cold mustuse fn exists() const wontthrow -> bool;
  cold mustuse fn is_directory() const wontthrow -> bool;
  mustuse fn is_regular_file() const wontthrow -> bool;
  /* True when the path itself is a symbolic link, tested without following it,
     for the test builtin's -L and -h primaries. */
  mustuse fn is_symbolic_link() const wontthrow -> bool;
  /* The POSIX file-type tests the test builtin's -b -c -p -S primaries need.
     Each is always false on Windows where the type has no equivalent. */
  mustuse fn is_block_device() const wontthrow -> bool;
  mustuse fn is_character_device() const wontthrow -> bool;
  mustuse fn is_fifo() const wontthrow -> bool;
  mustuse fn is_socket() const wontthrow -> bool;
  /* The permission and ownership tests the test builtin's -g -u -k -O -G
     primaries need. Each is always false on Windows where the bit has no
     equivalent. */
  mustuse fn has_setuid_bit() const wontthrow -> bool;
  mustuse fn has_setgid_bit() const wontthrow -> bool;
  mustuse fn has_sticky_bit() const wontthrow -> bool;
  mustuse fn is_owned_by_effective_user() const wontthrow -> bool;
  mustuse fn is_owned_by_effective_group() const wontthrow -> bool;
  mustuse fn file_size() const wontthrow -> Maybe<u64>;
  /* The mtime in whole seconds, None when the path is missing, the staleness
     key the completion target caches refresh on. */
  mustuse fn modification_time() const wontthrow -> Maybe<i64>;

  /* The access checks the test builtin asks for, each one access() call. */
  mustuse fn is_readable() const wontthrow -> bool;
  mustuse fn is_writable() const wontthrow -> bool;
  mustuse fn is_executable() const wontthrow -> bool;

  /* The two-file comparisons the test builtin's -ef, -nt, and -ot ask for. Each
     stats both paths and reads false when either path is missing.
     is_same_file_as matches when the two name one file, equal device and
     inode. */
  mustuse fn is_same_file_as(const Path &other) const wontthrow -> bool;
  mustuse fn is_newer_than(const Path &other) const wontthrow -> bool;
  mustuse fn is_older_than(const Path &other) const wontthrow -> bool;

  hot mustuse pure fn operator==(const Path &other) const wontthrow->bool;

  cold mustuse static fn current_directory() throws -> Path;
  static fn set_current_directory(const Path &path) throws -> ErrorOr<Ok>;
  /* The directory for temporary files, from TMPDIR or a platform default. */
  mustuse static fn temp_directory() throws -> Path;

  /* The immediate children of a directory, filenames only, without . and .. .
     None when the path is not a readable directory. */
  cold mustuse static fn read_directory(const Path &dir) throws
      -> Maybe<ArrayList<String>>;

  /* The type the directory read already knew for a child, so a caller skips a
     stat for an entry the filesystem typed. Unknown means the caller must stat
     to learn the type. */
  enum class entry_kind : u8
  {
    Unknown,
    Directory,
    Regular,
    Symlink,
    Other,
  };
  struct directory_child
  {
    String name;
    entry_kind kind;
  };
  /* Like read_directory but carrying each child's type from the read, for a
     caller such as the globstar walk that would otherwise stat every entry. */
  cold mustuse static fn read_directory_typed(const Path &dir) throws
      -> Maybe<ArrayList<directory_child>>;

  /* Read the whole file at this path into a string through the os descriptor
     layer, so no iostream file stream is pulled in. None when the open fails.
   */
  mustuse fn read_entire_file() const throws -> Maybe<String>;

  /* Resolve a path against the filesystem, rooting a relative path that holds a
     slash, normalizing it, and trying the omitted command suffixes on a name
     with no extension. */
  mustuse static fn canonicalize(StringView path) throws -> Maybe<Path>;

  /* The shell this file's shebang names, for the mimicry feature, or None when
     it is not a script shit can emulate. A sh or dash interpreter maps to
     Posix, bash to Bash, and shit to Default, including the /usr/bin/env form.
   */
  mustuse fn detect_mimic_shell() const throws -> Maybe<mimic_mood>;

private:
  String m_text{};
};

/* A small builder for assembling a path from a root and a run of components. */
class PathBuilder
{
public:
  PathBuilder() = default;
  explicit PathBuilder(StringView root);

  /* Add a component, inserting a separator unless the builder is empty or the
     component itself starts at a root. */
  fn append(StringView component) throws -> PathBuilder &;
  /* Add raw bytes with no separator, for extending the last component. */
  fn append_raw(StringView bytes) throws -> PathBuilder &;

  mustuse fn build() const throws -> Path;

private:
  String m_text{};
};

} // namespace shit
