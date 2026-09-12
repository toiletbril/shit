/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements POSIX path, metadata, directory, glob, link,
 * permission, and file-operation wrappers. It isolates filesystem headers and
 * pathname semantics from descriptor and process-launch code.
 */

#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#if defined __linux__
#include <linux/io_uring.h>
#include <mntent.h>
#include <sys/mman.h>
#elif defined __APPLE__ || defined BSD
#include <sys/mount.h>
#endif

namespace koshka {

namespace os {

pure fn path_is_absolute(StringView path) wontthrow -> bool
{
  if (path.length == 0) return false;
  return is_directory_separator(path.data[0]);
}

pure fn path_is_drive_relative(StringView path) wontthrow -> bool
{
  unused(path);
  return false;
}

fn resolve_drive_relative_path(StringView path) throws -> Maybe<Path>
{
  unused(path);
  return None;
}

pure fn path_root_length(StringView path) wontthrow -> usize
{
  return path_is_absolute(path) ? 1 : 0;
}

fn temp_directory_path() throws -> String
{
  if (const char *from_env = std::getenv("TMPDIR"); from_env != nullptr)
    return String{from_env};
  return String{"/tmp"};
}

cold fn path_exists(StringView path) wontthrow -> bool
{
  const String path_string{path};
  LOG(Debug, "probing whether '%s' exists", path_string.c_str());
  struct stat info{};
  return ::stat(path_string.c_str(), &info) == 0;
}

/* A failed stat reads as the type not matching. */
static fn stat_matches_type(const char *path, mode_t expected_type) wontthrow
    -> bool
{
  struct stat info{};
  if (::stat(path, &info) != 0) return false;
  return (info.st_mode & S_IFMT) == expected_type;
}

cold fn path_is_directory(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return stat_matches_type(path_string.c_str(), S_IFDIR);
}

fn path_is_regular_file(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return stat_matches_type(path_string.c_str(), S_IFREG);
}

fn path_is_symbolic_link(StringView path) wontthrow -> bool
{
  const String path_string{path};
  struct stat info{};
  if (::lstat(path_string.c_str(), &info) != 0) return false;
  return S_ISLNK(info.st_mode);
}

fn path_is_block_device(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return stat_matches_type(path_string.c_str(), S_IFBLK);
}

fn path_is_character_device(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return stat_matches_type(path_string.c_str(), S_IFCHR);
}

fn path_is_fifo(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return stat_matches_type(path_string.c_str(), S_IFIFO);
}

fn path_is_socket(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return stat_matches_type(path_string.c_str(), S_IFSOCK);
}

static fn stat_mode_has_bits(const char *path, mode_t bits) wontthrow -> bool
{
  struct stat info{};
  if (::stat(path, &info) != 0) return false;
  return (info.st_mode & bits) != 0;
}

fn path_has_setuid_bit(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return stat_mode_has_bits(path_string.c_str(), S_ISUID);
}

fn path_has_setgid_bit(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return stat_mode_has_bits(path_string.c_str(), S_ISGID);
}

fn path_has_sticky_bit(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return stat_mode_has_bits(path_string.c_str(), S_ISVTX);
}

fn path_is_owned_by_effective_user(StringView path) wontthrow -> bool
{
  const String path_string{path};
  struct stat info{};
  if (::stat(path_string.c_str(), &info) != 0) return false;
  return info.st_uid == ::geteuid();
}

fn path_is_owned_by_effective_group(StringView path) wontthrow -> bool
{
  const String path_string{path};
  struct stat info{};
  if (::stat(path_string.c_str(), &info) != 0) return false;
  return info.st_gid == ::getegid();
}

fn path_file_size(StringView path) wontthrow -> Maybe<u64>
{
  const String path_string{path};
  struct stat info{};
  if (::stat(path_string.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
    return None;
  }
  return static_cast<u64>(info.st_size);
}

fn path_modification_time(StringView path) wontthrow -> Maybe<i64>
{
  const String path_string{path};
  struct stat info{};
  if (::stat(path_string.c_str(), &info) != 0) return None;
  return static_cast<i64>(info.st_mtime);
}

fn paths_are_same_file(StringView first, StringView second) wontthrow -> bool
{
  const String first_string{first};
  const String second_string{second};
  struct stat first_info{}, second_info{};
  if (::stat(first_string.c_str(), &first_info) != 0) return false;
  if (::stat(second_string.c_str(), &second_info) != 0) return false;
  return first_info.st_dev == second_info.st_dev &&
         first_info.st_ino == second_info.st_ino;
}

fn paths_match_for_history(StringView first, StringView second) wontthrow
    -> bool
{
  return first == second;
}

fn path_is_newer_than(StringView first, StringView second) wontthrow -> bool
{
  const String first_string{first};
  const String second_string{second};
  struct stat first_info{}, second_info{};
  if (::stat(first_string.c_str(), &first_info) != 0) return false;
  if (::stat(second_string.c_str(), &second_info) != 0) return false;
  /* The nanoseconds break a same-second tie. */
  if (first_info.st_mtim.tv_sec != second_info.st_mtim.tv_sec)
    return first_info.st_mtim.tv_sec > second_info.st_mtim.tv_sec;
  return first_info.st_mtim.tv_nsec > second_info.st_mtim.tv_nsec;
}

fn path_is_older_than(StringView first, StringView second) wontthrow -> bool
{
  const String first_string{first};
  const String second_string{second};
  struct stat first_info{}, second_info{};
  if (::stat(first_string.c_str(), &first_info) != 0) return false;
  if (::stat(second_string.c_str(), &second_info) != 0) return false;
  if (first_info.st_mtim.tv_sec != second_info.st_mtim.tv_sec)
    return first_info.st_mtim.tv_sec < second_info.st_mtim.tv_sec;
  return first_info.st_mtim.tv_nsec < second_info.st_mtim.tv_nsec;
}

fn path_is_readable(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return ::access(path_string.c_str(), R_OK) == 0;
}

fn path_is_writable(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return ::access(path_string.c_str(), W_OK) == 0;
}

fn path_is_executable(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return ::access(path_string.c_str(), X_OK) == 0;
}

cold fn read_current_directory() throws -> Path
{
  LOG(Debug, "reading the current working directory");
  char local_buffer[PATH_MAX];
  errno = 0;
  if (::getcwd(local_buffer, sizeof(local_buffer)) != nullptr)
    return Path{StringView{local_buffer}};
  if (errno != ERANGE) return Path{};

  let buffer = ArrayList<char>{heap_allocator()};
  usize buffer_size = sizeof(local_buffer) * 2;
  loop
  {
    buffer.reserve(buffer_size);
    errno = 0;
    if (::getcwd(buffer.begin(), buffer_size) != nullptr)
      return Path{StringView{buffer.begin()}};
    if (errno != ERANGE) return Path{};
    buffer_size *= 2;
  }
}

fn change_current_directory(StringView path) throws -> ErrorOr<Ok>
{
  const String path_string{path};
  LOG(Info, "changing the current directory to '%s'", path_string.c_str());
  if (::chdir(path_string.c_str()) != 0)
    return Error{"Could not change directory to '" + path_string + "'"};
  return Success;
}

fn reference_current_directory() wontthrow -> DirectoryReference
{
  return DirectoryReference{open_current_directory_reference()};
}

fn restore_current_directory(const DirectoryReference &reference) wontthrow
    -> bool
{
  return reference.is_valid() && ::fchdir(reference.get()) == 0;
}

static fn fill_file_status(const struct stat &info,
                           file_status &status) wontthrow -> void;

cold fn list_directory(StringView dir) throws -> Maybe<ArrayList<String>>
{
  const String dir_string{dir};
  let entries = list_directory_typed(dir);
  if (!entries.has_value()) return None;
  let names = ArrayList<String>{heap_allocator()};
  names.reserve(entries->count());
  for (let &entry : *entries)
    names.push(steal(entry.name));
  LOG(All, "read %zu entries from the directory '%s'", names.count(),
      dir_string.c_str());
  return names;
}

cold fn list_directory_typed(StringView dir) throws
    -> Maybe<ArrayList<Path::directory_child>>
{
  const String dir_string{dir};
  let const handle = ::opendir(dir_string.c_str());
  if (handle == nullptr) return None;

  let entries = ArrayList<Path::directory_child>{heap_allocator()};
  loop
  {
    errno = 0;
    let const entry = ::readdir(handle);
    if (entry == nullptr) {
      if (errno != 0) {
        ::closedir(handle);
        return None;
      }
      break;
    }

    let const name = StringView{entry->d_name};
    if (name == StringView{"."} || name == StringView{".."}) {
      continue;
    }

    Path::entry_kind kind = Path::entry_kind::Unknown;
    switch (entry->d_type) {
    case DT_DIR: kind = Path::entry_kind::Directory; break;
    case DT_REG: kind = Path::entry_kind::Regular; break;
    case DT_LNK: kind = Path::entry_kind::Symlink; break;
    case DT_UNKNOWN: kind = Path::entry_kind::Unknown; break;
    default: kind = Path::entry_kind::Other; break;
    }

    entries.push(Path::directory_child{String{name}, kind});
  }

  ::closedir(handle);
  return entries;
}

cold fn list_directory_status(StringView dir, Allocator allocator) throws
    -> Maybe<ArrayList<directory_status_entry>>
{
  const String dir_string{dir};
  let const handle = ::opendir(dir_string.c_str());
  if (handle == nullptr) return None;

  let entries = ArrayList<directory_status_entry>{allocator};
  let const directory_descriptor = ::dirfd(handle);
  loop
  {
    errno = 0;
    let const native_entry = ::readdir(handle);
    if (native_entry == nullptr) {
      if (errno != 0) {
        ::closedir(handle);
        return None;
      }
      break;
    }

    let const name = StringView{native_entry->d_name};
    if (name == StringView{"."} || name == StringView{".."}) continue;

    Path::entry_kind kind = Path::entry_kind::Unknown;
    switch (native_entry->d_type) {
    case DT_DIR: kind = Path::entry_kind::Directory; break;
    case DT_REG: kind = Path::entry_kind::Regular; break;
    case DT_LNK: kind = Path::entry_kind::Symlink; break;
    case DT_UNKNOWN: kind = Path::entry_kind::Unknown; break;
    default: kind = Path::entry_kind::Other; break;
    }

    directory_status_entry entry{
        Path::directory_child{String{allocator, name}, kind}
    };
    struct stat info{};
    if (directory_descriptor >= 0 &&
        ::fstatat(directory_descriptor, native_entry->d_name, &info,
                  AT_SYMLINK_NOFOLLOW) == 0)
    {
      fill_file_status(info, entry.status);
      entry.has_status = true;
    }
    entries.push(steal(entry));
  }

  ::closedir(handle);
  return entries;
}

fn canonical_path(const Path &path) wontthrow -> Maybe<Path>
{
  let const allocator = uncached_heap_allocator();
  let resolved_path = allocator.alloc_array<char>(PATH_MAX);
  if (resolved_path == nullptr) return None;
  defer { allocator.free_array(resolved_path, PATH_MAX); };
  if (realpath(path.c_str(), resolved_path) == nullptr) return None;

  return Path{StringView{resolved_path}};
}

fn glob_matches(StringView pattern, Allocator allocator) throws
    -> ArrayList<String>
{
  let matches = ArrayList<String>{allocator};

  const String pattern_string{allocator, pattern};
  glob_t glob_result{};
  if (glob(pattern_string.c_str(), 0, nullptr, &glob_result) == 0) {
    for (usize i = 0; i < glob_result.gl_pathc; i++)
      matches.push(String{allocator, StringView{glob_result.gl_pathv[i]}});
  }
  globfree(&glob_result);

  return matches;
}

fn directory_is_trusted_for_exec(const Path &directory) wontthrow -> bool
{
  struct stat directory_stat;
  if (stat(directory.c_str(), &directory_stat) != 0) {
    LOG(Debug, "trust check failed because stat failed on '%s'",
        directory.c_str());
    return false;
  }
  let const is_owner_trusted =
      directory_stat.st_uid == 0 || directory_stat.st_uid == geteuid();
  if (!is_owner_trusted) {
    LOG(Debug,
        "trust check failed because '%s' is owned by uid %d, not root "
        "or the current user",
        directory.c_str(), directory_stat.st_uid);
    return false;
  }
  if ((directory_stat.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
    LOG(Debug, "trust check failed because '%s' permits shared writes",
        directory.c_str());
    return false;
  }

  LOG(Debug, "trust check passed for '%s'", directory.c_str());
  return true;
}

fn open_file_descriptor(StringView path, file_open_mode mode) throws
    -> Maybe<descriptor>
{
  LOG(Debug, "opening '%.*s'", static_cast<int>(path.length), path.data);

  /* Left inheritable on purpose, exec 3>file keeps the fd open across an exec.
   */
  int flags = 0;
  switch (mode) {
  case file_open_mode::Truncate: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
  case file_open_mode::TruncateNoClobber:
    /* O_EXCL fails atomically when the file exists, the way noclobber requires.
     */
    flags = O_WRONLY | O_CREAT | O_EXCL;
    break;
  case file_open_mode::Append: flags = O_WRONLY | O_CREAT | O_APPEND; break;
  case file_open_mode::Read: flags = O_RDONLY; break;
  case file_open_mode::ReadWrite: flags = O_RDWR | O_CREAT; break;
  }

  const String path_string{path};
  loop
  {
    const int fd = ::open(path_string.c_str(), flags, 0666);
    /* An open of a named pipe blocks until its peer arrives. A Ctrl-C returns
       to the caller. Any other interrupting signal retries the open. */
    if (fd < 0 && errno == EINTR) {
      if (INTERRUPT_REQUESTED) return koshka::None;
      continue;
    }

    if (fd < 0) return koshka::None;
    return fd;
  }
}

fn acquire_process_lock(StringView path) throws -> Maybe<descriptor>
{
  const String path_string{path};
  let const lock = ::open(path_string.c_str(), O_RDONLY | O_DIRECTORY);
  if (lock < 0) return None;
  if (::flock(lock, LOCK_EX) == 0) return lock;
  ::close(lock);
  return None;
}

fn release_process_lock(descriptor lock) wontthrow -> void
{
  unused(::flock(lock, LOCK_UN));
  unused(::close(lock));
}

fn write_to_temp_file(StringView content) throws -> Maybe<descriptor>
{
  LOG(Debug, "writing %zu bytes into an anonymous temp file", content.count());

  let const temp_dir = Path::temp_directory();

  let const path_template_path =
      PathBuilder{temp_dir.text()}.append("kosh_heredoc_XXXXXX").build();

  /* mkstemp rewrites the XXXXXX suffix in place, so the template is mutable. */
  const String &path_template_text = path_template_path.text();
  ArrayList<char> path_template{heap_allocator()};
  path_template.reserve(path_template_text.count() + 1);
  for (usize i = 0; i < path_template_text.count(); i++)
    path_template.push(path_template_text.c_str()[i]);
  path_template.push('\0');

  const int fd = mkstemp(path_template.begin());
  if (fd < 0) return koshka::None;

  unlink(path_template.begin());

  if (!write_all(fd, content.data, content.count())) {
    close(fd);
    return koshka::None;
  }

  if (lseek(fd, 0, SEEK_SET) < 0) {
    close(fd);
    return koshka::None;
  }
  return fd;
}

fn write_to_named_temp_file(const Path &directory, StringView prefix,
                            StringView content) throws -> Maybe<Path>
{
  if (prefix.find_character('/').has_value()) return None;
  let file_name = String{heap_allocator(), prefix};
  file_name += "_XXXXXX";
  let const path_template_path =
      PathBuilder{directory.text()}.append(file_name).build();
  let path_template = ArrayList<char>{heap_allocator()};
  path_template.reserve(path_template_path.count() + 1);
  for (usize index = 0; index < path_template_path.count(); index++)
    path_template.push(path_template_path.c_str()[index]);
  path_template.push('\0');

  const int fd = ::mkstemp(path_template.begin());
  if (fd < 0) return None;

  if (!write_all(fd, content.data, content.count())) {
    unused(::close(fd));
    unused(::unlink(path_template.begin()));
    return None;
  }

  if (::close(fd) != 0) {
    unused(::unlink(path_template.begin()));
    return None;
  }

  return Path{StringView{path_template.begin()}};
}

/* The String destructor may clobber errno, so each helper saves it across the
   inner scope that ends the String first. */
fn make_directory(StringView path, u32 mode) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    did_succeed = ::mkdir(path_string.c_str(), mode) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn set_file_mode(StringView path, u32 mode) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    did_succeed = ::chmod(path_string.c_str(), mode) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn set_file_owner(StringView path, i64 owner_id, i64 group_id,
                  bool should_follow_symlink) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    let const owner =
        owner_id < 0 ? static_cast<uid_t>(-1) : static_cast<uid_t>(owner_id);
    let const group =
        group_id < 0 ? static_cast<gid_t>(-1) : static_cast<gid_t>(group_id);
    did_succeed = should_follow_symlink
                      ? ::chown(path_string.c_str(), owner, group) == 0
                      : ::lchown(path_string.c_str(), owner, group) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn create_hard_link(StringView target, StringView link_path) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String target_string{target};
    const String link_string{link_path};
    did_succeed = ::link(target_string.c_str(), link_string.c_str()) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn make_fifo(StringView path, u32 mode) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    did_succeed = ::mkfifo(path_string.c_str(), static_cast<mode_t>(mode)) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn touch_file_times(StringView path) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    did_succeed = ::utimensat(AT_FDCWD, path_string.c_str(), nullptr, 0) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn set_file_times(StringView path, i64 access_time, u32 access_nanoseconds,
                  i64 modification_time, u32 modification_nanoseconds) wontthrow
    -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    const struct timespec times[2] = {
        {static_cast<time_t>(access_time),
         static_cast<long>(access_nanoseconds)      },
        {static_cast<time_t>(modification_time),
         static_cast<long>(modification_nanoseconds)}
    };
    did_succeed = ::utimensat(AT_FDCWD, path_string.c_str(), times, 0) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn remove_directory(StringView path) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    did_succeed = ::rmdir(path_string.c_str()) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn remove_file(StringView path) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    did_succeed = ::unlink(path_string.c_str()) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn rename_path(StringView from, StringView to) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String from_string{from};
    const String to_string{to};
    did_succeed = ::rename(from_string.c_str(), to_string.c_str()) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn create_symlink(StringView target, StringView link_path) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String target_string{target};
    const String link_string{link_path};
    did_succeed = ::symlink(target_string.c_str(), link_string.c_str()) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn read_symlink(StringView path, Allocator allocator) wontthrow -> Maybe<String>
{
  const String path_string{path};
  char inline_buffer[256];
  let length =
      ::readlink(path_string.c_str(), inline_buffer, sizeof(inline_buffer));
  if (length < 0) return koshka::None;
  if (static_cast<usize>(length) < sizeof(inline_buffer))
    return String{
        allocator, StringView{inline_buffer, static_cast<usize>(length)}
    };

  usize capacity = sizeof(inline_buffer) * 2;
  ArrayList<char> buffer{allocator};
  loop
  {
    buffer.reserve(capacity);
    length = ::readlink(path_string.c_str(), buffer.begin(), capacity);
    if (length < 0) return koshka::None;
    if (static_cast<usize>(length) < capacity)
      return String{
          allocator, StringView{buffer.begin(), static_cast<usize>(length)}
      };

    if (capacity >= (1U << 20)) {
      errno = ENAMETOOLONG;
      return koshka::None;
    }

    capacity *= 2;
  }
}

pure fn device_major(u64 device_id) wontthrow -> u32
{
  return static_cast<u32>(major(static_cast<dev_t>(device_id)));
}

pure fn device_minor(u64 device_id) wontthrow -> u32
{
  return static_cast<u32>(minor(static_cast<dev_t>(device_id)));
}

static fn copy_type_name(filesystem_status &status, StringView name) wontthrow
    -> void
{
  let const copied_length = name.length < sizeof(status.type_name) - 1
                                ? name.length
                                : sizeof(status.type_name) - 1;
  for (usize index = 0; index < copied_length; index++)
    status.type_name[index] = name[index];

  status.type_name[copied_length] = '\0';
}

#if defined __linux__

static pure fn linux_filesystem_type_name(u64 type_id) wontthrow -> StringView
{
  switch (type_id) {
  case 0x0000adf5u: return "adfs";
  case 0x00001cd1u: return "devpts";
  case 0x00001373u: return "devfs";
  case 0x00002011u: return "bpf";
  case 0x00004d44u: return "msdos";
  case 0x00006969u: return "nfs";
  case 0x00009fa0u: return "proc";
  case 0x00009fa2u: return "usbdevfs";
  case 0x0000ef53u: return "ext2/ext3/ext4";
  case 0x00726f6fu: return "ramfs";
  case 0x0027e0ebu: return "cgroupfs";
  case 0x01021994u: return "tmpfs";
  case 0x01021997u: return "v9fs";
  case 0x0bad1deau: return "fuse";
  case 0x1badface: return "bfs";
  case 0x2fc12fc1u: return "zfs";
  case 0x4244u: return "hfs";
  case 0x52654973u: return "reiserfs";
  case 0x5346414fu: return "afs";
  case 0x53464846u: return "wslfs";
  case 0x5346544eu: return "ntfs";
  case 0x58465342u: return "xfs";
  case 0x62656572u: return "sysfs";
  case 0x63677270u: return "cgroup2fs";
  case 0x64626720u: return "debugfs";
  case 0x65735546u: return "fuseblk";
  case 0x6e736673u: return "nsfs";
  case 0x73717368u: return "squashfs";
  case 0x794c7630u: return "overlayfs";
  case 0x9123683eu: return "btrfs";
  case 0xf2f52010u: return "f2fs";
  case 0xf995e849u: return "hpfs";
  default: break;
  }

  return "";
}

#endif

fn stat_filesystem(StringView path, filesystem_status &status) wontthrow -> bool
{
  const String path_string{path};
  struct statvfs info{};
  if (::statvfs(path_string.c_str(), &info) != 0) return false;
  status.block_size = info.f_bsize;
  status.fundamental_block_size =
      info.f_frsize == 0 ? info.f_bsize : info.f_frsize;
  status.total_blocks = info.f_blocks;
  status.free_blocks = info.f_bfree;
  status.available_blocks = info.f_bavail;
  status.total_files = info.f_files;
  status.free_files = info.f_ffree;
  status.name_max = info.f_namemax;
  status.filesystem_id = info.f_fsid;

#if defined __linux__
  struct statfs type_info{};
  if (::statfs(path_string.c_str(), &type_info) == 0) {
    status.type_id = static_cast<u64>(type_info.f_type);
    status.filesystem_id =
        (static_cast<u64>(static_cast<u32>(type_info.f_fsid.__val[0])) << 32u) |
        static_cast<u32>(type_info.f_fsid.__val[1]);
    copy_type_name(status, linux_filesystem_type_name(status.type_id));
  }
#elif defined __APPLE__ || defined BSD
  struct statfs type_info{};
  if (::statfs(path_string.c_str(), &type_info) == 0) {
    copy_type_name(status, StringView{type_info.f_fstypename});
  }
#endif

  return true;
}

#if defined __APPLE__ || defined BSD

static fn describe_mount_flags(u64 flags) throws -> String
{
  struct named_mount_flag
  {
    u64 bit;
    StringView name;
  };

  static constexpr named_mount_flag NAMED_FLAGS[] = {
      {MNT_SYNCHRONOUS, "sync"       },
      {MNT_NOEXEC,      "noexec"     },
      {MNT_NOSUID,      "nosuid"     },
      {MNT_NODEV,       "nodev"      },
      {MNT_UNION,       "union"      },
      {MNT_ASYNC,       "async"      },
      {MNT_NOATIME,     "noatime"    },
      {MNT_LOCAL,       "local"      },
      {MNT_QUOTA,       "quota"      },
      {MNT_ROOTFS,      "rootfs"     },
      {MNT_DONTBROWSE,  "nobrowse"   },
      {MNT_AUTOMOUNTED, "automounted"},
      {MNT_JOURNALED,   "journaled"  },
  };

  let options = String{(flags & MNT_RDONLY) != 0 ? "ro" : "rw"};
  for (let const &named : NAMED_FLAGS) {
    if ((flags & named.bit) == 0) continue;

    options += ",";
    options += named.name;
  }

  return options;
}

#endif

fn mounted_filesystems() throws -> ArrayList<mounted_filesystem>
{
  let result = ArrayList<mounted_filesystem>{heap_allocator()};
#if defined __linux__
  let *mount_file = setmntent("/proc/self/mounts", "r");
  if (mount_file == nullptr) mount_file = setmntent("/etc/mtab", "r");
  if (mount_file == nullptr) return result;
  defer { endmntent(mount_file); };
  mntent entry{};
  char buffer[8192];

  while (getmntent_r(mount_file, &entry, buffer, sizeof(buffer)) != nullptr)
    result.push(
        mounted_filesystem{String{entry.mnt_fsname}, String{entry.mnt_dir},
                           String{entry.mnt_type}, String{entry.mnt_opts}});
#elif defined __APPLE__ || defined BSD
  struct statfs *entries = nullptr;
  let const entry_count = getmntinfo(&entries, MNT_NOWAIT);

  for (int index = 0; index < entry_count; index++)
    result.push(mounted_filesystem{
        String{entries[index].f_mntfromname},
        String{entries[index].f_mntonname}, String{entries[index].f_fstypename},
        describe_mount_flags(static_cast<u64>(entries[index].f_flags))});
#else
  result.push(mounted_filesystem{String{"."}, String{"."},
                                 String{heap_allocator()},
                                 String{heap_allocator()}});
#endif

  return result;
}

fn sync_filesystems() wontthrow -> bool
{
  ::sync();
  return true;
}

fn sync_path(StringView path, bool is_data_only) wontthrow -> bool
{
  const String path_string{path};
  let const path_fd = ::open(path_string.c_str(), O_RDONLY | O_CLOEXEC);
  if (path_fd < 0) return false;
  defer { ::close(path_fd); };

#if defined __APPLE__
  unused(is_data_only);
  return ::fsync(path_fd) == 0;
#else
  return (is_data_only ? ::fdatasync(path_fd) : ::fsync(path_fd)) == 0;
#endif
}

static fn fill_file_status(const struct stat &info,
                           file_status &status) wontthrow -> void
{
  status.device_id = static_cast<u64>(info.st_dev);
  status.special_device_id = static_cast<u64>(info.st_rdev);
  status.file_id = static_cast<u64>(info.st_ino);
  status.has_file_identity = true;
  status.mode = static_cast<u32>(info.st_mode);
  status.link_count = static_cast<u64>(info.st_nlink);
  status.owner_id = static_cast<u32>(info.st_uid);
  status.group_id = static_cast<u32>(info.st_gid);
  status.size = static_cast<u64>(info.st_size);
  status.access_time = static_cast<i64>(info.st_atime);
  status.access_nanoseconds = static_cast<u32>(info.st_atim.tv_nsec);
  status.modification_time = static_cast<i64>(info.st_mtime);
  status.modification_nanoseconds = static_cast<u32>(info.st_mtim.tv_nsec);
  status.change_time = static_cast<i64>(info.st_ctime);
  status.change_nanoseconds = static_cast<u32>(info.st_ctim.tv_nsec);
  status.blocks = static_cast<u64>(info.st_blocks);
}

static fn validate_batched_syscall(const batched_syscall &operation) wontthrow
    -> i32
{
  if (operation.byte_count > static_cast<usize>(0xffffffffu) ||
      operation.byte_offset > 0x7fffffffffffffffULL)
  {
    return EINVAL;
  }

  switch (operation.syscall_id) {
  case batched_syscall_id::Read:
    if (operation.fd == KOSH_INVALID_FD ||
        (operation.output_buffer == nullptr && operation.byte_count != 0))
    {
      return EINVAL;
    }
    return 0;
  case batched_syscall_id::Write:
    if (operation.fd == KOSH_INVALID_FD ||
        (operation.input_buffer == nullptr && operation.byte_count != 0))
    {
      return EINVAL;
    }
    return 0;
  case batched_syscall_id::Lstat:
  case batched_syscall_id::Stat:
    return operation.path == nullptr || operation.status == nullptr ? EINVAL
                                                                    : 0;
  }

  return EINVAL;
}

static fn
execute_batched_syscall_direct(const batched_syscall &operation,
                               batched_syscall_result &result) wontthrow -> void
{
  result = {operation.request_id, 0, validate_batched_syscall(operation)};
  if (result.error_number != 0) return;

  switch (operation.syscall_id) {
  case batched_syscall_id::Read:
    loop
    {
      let const transferred_byte_count =
          ::pread(operation.fd, operation.output_buffer, operation.byte_count,
                  static_cast<off_t>(operation.byte_offset));
      if (transferred_byte_count >= 0) {
        result.transferred_byte_count =
            static_cast<usize>(transferred_byte_count);
        return;
      }
      if (errno != EINTR || INTERRUPT_REQUESTED) {
        result.error_number = errno;
        return;
      }
    }
  case batched_syscall_id::Write:
    loop
    {
      let const transferred_byte_count =
          ::pwrite(operation.fd, operation.input_buffer, operation.byte_count,
                   static_cast<off_t>(operation.byte_offset));
      if (transferred_byte_count >= 0) {
        result.transferred_byte_count =
            static_cast<usize>(transferred_byte_count);
        return;
      }
      if (errno != EINTR || INTERRUPT_REQUESTED) {
        result.error_number = errno;
        return;
      }
    }
  case batched_syscall_id::Lstat:
    if (!stat_path(operation.path->text().view(), *operation.status))
      result.error_number = errno;
    return;
  case batched_syscall_id::Stat:
    if (!stat_path_following(operation.path->text().view(), *operation.status))
      result.error_number = errno;
    return;
  }
}

#if defined __linux__

static fn fill_file_status(const struct statx &info,
                           file_status &status) wontthrow -> void
{
  status.device_id =
      static_cast<u64>(makedev(info.stx_dev_major, info.stx_dev_minor));
  status.special_device_id =
      static_cast<u64>(makedev(info.stx_rdev_major, info.stx_rdev_minor));
  status.file_id = info.stx_ino;
  status.link_count = info.stx_nlink;
  status.size = info.stx_size;
  status.access_time = info.stx_atime.tv_sec;
  status.modification_time = info.stx_mtime.tv_sec;
  status.change_time = info.stx_ctime.tv_sec;
  status.blocks = info.stx_blocks;
  status.mode = info.stx_mode;
  status.owner_id = info.stx_uid;
  status.group_id = info.stx_gid;
  status.access_nanoseconds = info.stx_atime.tv_nsec;
  status.modification_nanoseconds = info.stx_mtime.tv_nsec;
  status.change_nanoseconds = info.stx_ctime.tv_nsec;
  status.has_file_identity = true;
}

struct io_uring_batch
{
  io_uring_params parameters{};
  io_uring_sqe *submission_entries{nullptr};
  io_uring_cqe *completion_entries{nullptr};
  u32 *submission_head{nullptr};
  u32 *submission_tail{nullptr};
  u32 *submission_mask{nullptr};
  u32 *submission_count{nullptr};
  u32 *submission_array{nullptr};
  u32 *completion_head{nullptr};
  u32 *completion_tail{nullptr};
  u32 *completion_mask{nullptr};
  u32 *completion_count{nullptr};
  opaque *submission_mapping{MAP_FAILED};
  opaque *completion_mapping{MAP_FAILED};
  opaque *entry_mapping{MAP_FAILED};
  usize submission_mapping_size{0};
  usize completion_mapping_size{0};
  usize entry_mapping_size{0};
  i32 descriptor{-1};
  bool has_shared_mapping{false};
};

static fn close_io_uring_batch(io_uring_batch &ring) wontthrow -> void
{
  if (ring.descriptor >= 0) {
    ::close(ring.descriptor);
    ring.descriptor = -1;
  }
  if (ring.entry_mapping != MAP_FAILED) {
    ::munmap(ring.entry_mapping, ring.entry_mapping_size);
    ring.entry_mapping = MAP_FAILED;
  }
  if (ring.completion_mapping != MAP_FAILED && !ring.has_shared_mapping) {
    ::munmap(ring.completion_mapping, ring.completion_mapping_size);
    ring.completion_mapping = MAP_FAILED;
  }
  if (ring.submission_mapping != MAP_FAILED) {
    ::munmap(ring.submission_mapping, ring.submission_mapping_size);
    ring.submission_mapping = MAP_FAILED;
  }
}

static fn io_uring_supports(const io_uring_probe &probe, u8 opcode) wontthrow
    -> bool
{
  for (u8 index = 0; index < probe.ops_len; index++) {
    let const &operation = probe.ops[index];
    if (operation.op == opcode &&
        (operation.flags & IO_URING_OP_SUPPORTED) != 0)
    {
      return true;
    }
  }

  return false;
}

static fn open_io_uring_batch(const batched_syscall *operations,
                              usize requested_entry_count,
                              io_uring_batch &ring) wontthrow -> bool
{
  let const entry_count = requested_entry_count > 32
                              ? 32u
                              : static_cast<u32>(requested_entry_count);
  ring.descriptor = static_cast<i32>(
      ::syscall(SYS_io_uring_setup, entry_count, &ring.parameters));
  if (ring.descriptor < 0) return false;

  alignas(io_uring_probe)
      u8 probe_storage[sizeof(io_uring_probe) +
                       (IORING_OP_LAST + 1) * sizeof(io_uring_probe_op)]{};
  let &probe = *reinterpret_cast<io_uring_probe *>(probe_storage);
  let const probe_result =
      ::syscall(SYS_io_uring_register, ring.descriptor, IORING_REGISTER_PROBE,
                &probe, static_cast<u32>(IORING_OP_LAST + 1));
  bool needs_read = false;
  bool needs_write = false;
  bool needs_stat = false;
  for (usize index = 0; index < requested_entry_count; index++) {
    switch (operations[index].syscall_id) {
    case batched_syscall_id::Read: needs_read = true; break;
    case batched_syscall_id::Write: needs_write = true; break;
    case batched_syscall_id::Lstat:
    case batched_syscall_id::Stat: needs_stat = true; break;
    }
  }
  if (probe_result < 0 ||
      (needs_read && !io_uring_supports(probe, IORING_OP_READ)) ||
      (needs_write && !io_uring_supports(probe, IORING_OP_WRITE)) ||
      (needs_stat && !io_uring_supports(probe, IORING_OP_STATX)))
  {
    close_io_uring_batch(ring);
    return false;
  }

  ring.submission_mapping_size =
      ring.parameters.sq_off.array + ring.parameters.sq_entries * sizeof(u32);
  ring.completion_mapping_size =
      ring.parameters.cq_off.cqes +
      ring.parameters.cq_entries * sizeof(io_uring_cqe);
  ring.entry_mapping_size = ring.parameters.sq_entries * sizeof(io_uring_sqe);
  ring.has_shared_mapping =
      (ring.parameters.features & IORING_FEAT_SINGLE_MMAP) != 0;
  if (ring.has_shared_mapping &&
      ring.completion_mapping_size > ring.submission_mapping_size)
  {
    ring.submission_mapping_size = ring.completion_mapping_size;
  }

  ring.submission_mapping =
      ::mmap(nullptr, ring.submission_mapping_size, PROT_READ | PROT_WRITE,
             MAP_SHARED, ring.descriptor, IORING_OFF_SQ_RING);
  if (ring.submission_mapping == MAP_FAILED) {
    close_io_uring_batch(ring);
    return false;
  }

  if (ring.has_shared_mapping) {
    ring.completion_mapping = ring.submission_mapping;
  } else {
    ring.completion_mapping =
        ::mmap(nullptr, ring.completion_mapping_size, PROT_READ | PROT_WRITE,
               MAP_SHARED, ring.descriptor, IORING_OFF_CQ_RING);
    if (ring.completion_mapping == MAP_FAILED) {
      close_io_uring_batch(ring);
      return false;
    }
  }

  ring.entry_mapping =
      ::mmap(nullptr, ring.entry_mapping_size, PROT_READ | PROT_WRITE,
             MAP_SHARED, ring.descriptor, IORING_OFF_SQES);
  if (ring.entry_mapping == MAP_FAILED) {
    close_io_uring_batch(ring);
    return false;
  }

  let *submission_bytes = static_cast<u8 *>(ring.submission_mapping);
  let *completion_bytes = static_cast<u8 *>(ring.completion_mapping);
  ring.submission_head =
      reinterpret_cast<u32 *>(submission_bytes + ring.parameters.sq_off.head);
  ring.submission_tail =
      reinterpret_cast<u32 *>(submission_bytes + ring.parameters.sq_off.tail);
  ring.submission_mask = reinterpret_cast<u32 *>(
      submission_bytes + ring.parameters.sq_off.ring_mask);
  ring.submission_count = reinterpret_cast<u32 *>(
      submission_bytes + ring.parameters.sq_off.ring_entries);
  ring.submission_array =
      reinterpret_cast<u32 *>(submission_bytes + ring.parameters.sq_off.array);
  ring.completion_head =
      reinterpret_cast<u32 *>(completion_bytes + ring.parameters.cq_off.head);
  ring.completion_tail =
      reinterpret_cast<u32 *>(completion_bytes + ring.parameters.cq_off.tail);
  ring.completion_mask = reinterpret_cast<u32 *>(
      completion_bytes + ring.parameters.cq_off.ring_mask);
  ring.completion_count = reinterpret_cast<u32 *>(
      completion_bytes + ring.parameters.cq_off.ring_entries);
  ring.completion_entries = reinterpret_cast<io_uring_cqe *>(
      completion_bytes + ring.parameters.cq_off.cqes);
  ring.submission_entries = static_cast<io_uring_sqe *>(ring.entry_mapping);

  let const is_usable = *ring.submission_count >= entry_count &&
                        *ring.completion_count >= entry_count;
  if (!is_usable) close_io_uring_batch(ring);
  return is_usable;
}

static fn execute_io_uring_batch(const batched_syscall *operations,
                                 usize operation_count,
                                 batched_syscall_result *results) wontthrow
    -> bool
{
  if (operation_count < 2) return false;

  io_uring_batch ring{};
  if (!open_io_uring_batch(operations, operation_count, ring)) return false;
  defer { close_io_uring_batch(ring); };

  usize operation_start = 0;
  while (operation_start < operation_count) {
    let const available_count = static_cast<usize>(*ring.submission_count);
    let const chunk_count = operation_count - operation_start > available_count
                                ? available_count
                                : operation_count - operation_start;
    struct statx status_records[32]{};
    bool was_queued[32]{};
    bool was_completed[32]{};
    let const initial_tail =
        __atomic_load_n(ring.submission_tail, __ATOMIC_ACQUIRE);
    u32 queued_count = 0;

    for (usize chunk_index = 0; chunk_index < chunk_count; chunk_index++) {
      let const operation_index = operation_start + chunk_index;
      let const &operation = operations[operation_index];
      results[operation_index] = {operation.request_id, 0,
                                  validate_batched_syscall(operation)};
      if (results[operation_index].error_number != 0) continue;

      let const submission_index =
          (initial_tail + queued_count) & *ring.submission_mask;
      let &entry = ring.submission_entries[submission_index];
      entry = {};
      entry.user_data = operation_index;
      switch (operation.syscall_id) {
      case batched_syscall_id::Read:
        entry.opcode = IORING_OP_READ;
        entry.fd = operation.fd;
        entry.off = operation.byte_offset;
        entry.addr = reinterpret_cast<u64>(operation.output_buffer);
        entry.len = static_cast<u32>(operation.byte_count);
        break;
      case batched_syscall_id::Write:
        entry.opcode = IORING_OP_WRITE;
        entry.fd = operation.fd;
        entry.off = operation.byte_offset;
        entry.addr = reinterpret_cast<u64>(operation.input_buffer);
        entry.len = static_cast<u32>(operation.byte_count);
        break;
      case batched_syscall_id::Lstat:
      case batched_syscall_id::Stat:
        entry.opcode = IORING_OP_STATX;
        entry.fd = AT_FDCWD;
        entry.addr = reinterpret_cast<u64>(operation.path->c_str());
        entry.len = STATX_BASIC_STATS;
        entry.statx_flags = operation.syscall_id == batched_syscall_id::Lstat
                                ? AT_SYMLINK_NOFOLLOW
                                : 0;
        entry.addr2 = reinterpret_cast<u64>(&status_records[chunk_index]);
        break;
      }
      ring.submission_array[submission_index] = submission_index;
      was_queued[chunk_index] = true;
      queued_count++;
    }

    if (queued_count == 0) {
      operation_start += chunk_count;
      continue;
    }

    let const published_tail = initial_tail + queued_count;
    __atomic_store_n(ring.submission_tail, published_tail, __ATOMIC_RELEASE);
    let const do_fail_pending = [&](i32 error_number) wontthrow -> void {
      close_io_uring_batch(ring);
      for (usize chunk_index = 0; chunk_index < chunk_count; chunk_index++) {
        if (!was_queued[chunk_index] || was_completed[chunk_index]) continue;
        results[operation_start + chunk_index].error_number = error_number;
      }
    };
    while (__atomic_load_n(ring.submission_head, __ATOMIC_ACQUIRE) !=
           published_tail)
    {
      let const current_head =
          __atomic_load_n(ring.submission_head, __ATOMIC_ACQUIRE);
      let const pending_count = published_tail - current_head;
      let const submitted_count = ::syscall(SYS_io_uring_enter, ring.descriptor,
                                            pending_count, 0, 0, nullptr, 0);
      if (submitted_count < 0 && errno == EINTR) continue;
      if (submitted_count < 0) {
        let const error_number = errno;
        do_fail_pending(error_number);
        return true;
      }
    }

    u32 completed_count = 0;
    while (completed_count < queued_count) {
      let completion_head =
          __atomic_load_n(ring.completion_head, __ATOMIC_ACQUIRE);
      let const completion_tail =
          __atomic_load_n(ring.completion_tail, __ATOMIC_ACQUIRE);
      if (completion_head == completion_tail) {
        let const wait_result =
            ::syscall(SYS_io_uring_enter, ring.descriptor, 0, 1,
                      IORING_ENTER_GETEVENTS, nullptr, 0);
        if (wait_result < 0 && errno == EINTR) continue;
        if (wait_result < 0) {
          let const error_number = errno;
          do_fail_pending(error_number);
          return true;
        }
        continue;
      }

      while (completion_head != completion_tail &&
             completed_count < queued_count)
      {
        let const &completion =
            ring.completion_entries[completion_head & *ring.completion_mask];
        let const operation_index = static_cast<usize>(completion.user_data);
        let const chunk_index = operation_index - operation_start;
        let &result = results[operation_index];
        if (completion.res < 0) {
          result.error_number = -completion.res;
        } else if (operations[operation_index].syscall_id ==
                       batched_syscall_id::Lstat ||
                   operations[operation_index].syscall_id ==
                       batched_syscall_id::Stat)
        {
          fill_file_status(status_records[chunk_index],
                           *operations[operation_index].status);
        } else {
          result.transferred_byte_count = static_cast<usize>(completion.res);
        }
        was_completed[chunk_index] = true;
        completion_head++;
        completed_count++;
      }
      __atomic_store_n(ring.completion_head, completion_head, __ATOMIC_RELEASE);
    }

    operation_start += chunk_count;
  }

  return true;
}

#endif

fn execute_batched_syscalls(const batched_syscall *operations,
                            usize operation_count,
                            batched_syscall_result *results) wontthrow -> void
{
  if (operation_count == 0) return;
  if (operations == nullptr || results == nullptr) return;

  let const saved_error = errno;
  defer { errno = saved_error; };

#if defined __linux__
  if (execute_io_uring_batch(operations, operation_count, results)) return;
#endif

  for (usize index = 0; index < operation_count; index++) {
    execute_batched_syscall_direct(operations[index], results[index]);
    if (INTERRUPT_REQUESTED) {
      for (usize remaining_index = index + 1; remaining_index < operation_count;
           remaining_index++)
      {
        results[remaining_index] = {operations[remaining_index].request_id, 0,
                                    EINTR};
      }
      return;
    }
  }
}

fn stat_path(StringView path, file_status &status) wontthrow -> bool
{
  const String path_string{path};
  struct stat info{};
  /* lstat does not follow the symlink, so ls shows the l type without -L. */
  if (::lstat(path_string.c_str(), &info) != 0) return false;
  fill_file_status(info, status);
  return true;
}

fn stat_path_following(StringView path, file_status &status) wontthrow -> bool
{
  const String path_string{path};
  struct stat info{};
  if (::stat(path_string.c_str(), &info) != 0) return false;
  fill_file_status(info, status);
  return true;
}

fn file_type_letter(u32 mode) wontthrow -> char
{
  const mode_t bits = static_cast<mode_t>(mode);
  if (S_ISDIR(bits)) return 'd';
  if (S_ISLNK(bits)) return 'l';
  if (S_ISCHR(bits)) return 'c';
  if (S_ISBLK(bits)) return 'b';
  if (S_ISFIFO(bits)) return 'p';
  if (S_ISSOCK(bits)) return 's';
  return '-';
}

fn format_mode_string(u32 mode) throws -> String
{
  const mode_t bits = static_cast<mode_t>(mode);
  String result{heap_allocator()};
  result.push(file_type_letter(mode));
  result.push((bits & S_IRUSR) != 0 ? 'r' : '-');
  result.push((bits & S_IWUSR) != 0 ? 'w' : '-');
  result.push((bits & S_ISUID) != 0 ? ((bits & S_IXUSR) != 0 ? 's' : 'S')
                                    : ((bits & S_IXUSR) != 0 ? 'x' : '-'));
  result.push((bits & S_IRGRP) != 0 ? 'r' : '-');
  result.push((bits & S_IWGRP) != 0 ? 'w' : '-');
  result.push((bits & S_ISGID) != 0 ? ((bits & S_IXGRP) != 0 ? 's' : 'S')
                                    : ((bits & S_IXGRP) != 0 ? 'x' : '-'));
  result.push((bits & S_IROTH) != 0 ? 'r' : '-');
  result.push((bits & S_IWOTH) != 0 ? 'w' : '-');
  result.push((bits & S_ISVTX) != 0 ? ((bits & S_IXOTH) != 0 ? 't' : 'T')
                                    : ((bits & S_IXOTH) != 0 ? 'x' : '-'));
  return result;
}

} /* namespace os */

} /* namespace koshka */
