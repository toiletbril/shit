#include "Path.hpp"

#include "Platform.hpp"
#include "Trace.hpp"

#if SHIT_PLATFORM_IS POSIX
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#define st_mtim st_mtimespec
#define st_atim st_atimespec
#define st_ctim st_ctimespec
#endif
#elif SHIT_PLATFORM_IS WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#endif

namespace shit {

#if SHIT_PLATFORM_IS WIN32
static constexpr char DIRECTORY_SEPARATOR = '\\';
#else
static constexpr char DIRECTORY_SEPARATOR = '/';
#endif

/* A forward slash is always a separator so a POSIX-style path keeps working
   everywhere, and a backslash is one on Windows too. */
static pure fn is_directory_separator(char c) wontthrow -> bool
{
#if SHIT_PLATFORM_IS WIN32
  return c == '/' || c == '\\';
#else
  return c == '/';
#endif
}

Path::Path(StringView text) : m_text(text) {}

hot fn Path::text() const wontthrow -> const String & { return m_text; }

hot fn Path::c_str() const wontthrow -> const char * { return m_text.c_str(); }

hot fn Path::count() const wontthrow -> usize { return m_text.count(); }

hot fn Path::is_empty() const wontthrow -> bool { return m_text.is_empty(); }

fn Path::is_absolute() const wontthrow -> bool
{
  if (m_text.is_empty()) return false;
#if SHIT_PLATFORM_IS WIN32
  if (is_directory_separator(m_text[0])) return true;
  /* A drive-qualified path such as C:\ is absolute. */
  return m_text.count() >= 2 && m_text[1] == ':';
#else
  return is_directory_separator(m_text[0]);
#endif
}

fn Path::is_relative() const wontthrow -> bool { return !is_absolute(); }

/* The offset just past the last separator, so the filename starts there. Zero
   when there is no separator. */
static pure fn filename_offset(const String &text) wontthrow -> usize
{
  for (usize i = text.count(); i > 0; i--)
    if (is_directory_separator(text[i - 1])) return i;
  return 0;
}

fn Path::filename() const wontthrow -> StringView
{
  let const start = filename_offset(m_text);
  ASSERT(start <= m_text.count());
  return m_text.substring(start);
}

fn Path::extension() const wontthrow -> StringView
{
  let const name = filename();
  /* The . and .. directory entries have no extension, matching std::filesystem,
     so the trailing dot in .. is not read as one. */
  if (name == StringView{"."} || name == StringView{".."})
    return StringView{name.data + name.length, 0};
  /* A dot at the start names a hidden file rather than an extension, so the
     scan stops before the first byte. */
  for (usize i = name.length; i > 1; i--)
    if (name.data[i - 1] == '.') return name.substring(i - 1);
  return StringView{name.data + name.length, 0};
}

fn Path::parent() const throws -> Path
{
  let const end = filename_offset(m_text);
  if (end == 0) return Path{};
  /* Keep a lone root separator, otherwise drop the trailing one. */
  if (end == 1) return Path{m_text.substring_of_length(0, 1)};
  return Path{m_text.substring_of_length(0, end - 1)};
}

fn Path::push_component(StringView component) throws -> Path &
{
  if (component.length == 0) return *this;
  if (!m_text.is_empty() && !is_directory_separator(m_text.back()) &&
      !is_directory_separator(component.data[0]))
  {
    m_text.push(DIRECTORY_SEPARATOR);
  }
  m_text.append(component);
  return *this;
}

fn Path::with_extension(StringView new_extension) const throws -> Path
{
  let const current_extension = extension();

  ASSERT(current_extension.length <= m_text.count(),
         "extension is a suffix of the path text");
  let const prefix_length = m_text.count() - current_extension.length;

  let result = Path{m_text.substring_of_length(0, prefix_length)};
  if (new_extension.length > 0 && new_extension.data[0] != '.')
    result.m_text.push('.');
  result.m_text.append(new_extension);

  return result;
}

cold fn Path::normalized() const throws -> Path
{
  LOG(Debug, "normalizing the path '%s'", m_text.c_str());

  let const is_absolute_path = is_absolute();

  /* Each kept component is a view into the original text, valid for the life of
     this function while the result is assembled. */
  let components = ArrayList<StringView>{};
  usize i = 0;
  while (i < m_text.count()) {
    if (is_directory_separator(m_text[i])) {
      i++;
      continue;
    }
    let const component_start = i;
    while (i < m_text.count() && !is_directory_separator(m_text[i]))
      i++;
    let const component =
        m_text.substring_of_length(component_start, i - component_start);
    if (component == StringView{"."}) continue;
    if (component == StringView{".."}) {
      /* A .. pops the last real component, unless none remains or the last was
         itself a .. kept because the path is relative and cannot climb past its
         own start. */
      if (components.count() > 0 && !(components.back() == StringView{".."})) {
        components.pop_back();
      } else if (!is_absolute_path) {
        components.push(component);
      }
      continue;
    }
    components.push(component);
  }

  let normalized_text = String{};
  if (is_absolute_path) normalized_text.push(DIRECTORY_SEPARATOR);
  for (usize i = 0; i < components.count(); i++) {
    if (i > 0) normalized_text.push(DIRECTORY_SEPARATOR);
    normalized_text.append(components[i]);
  }
  if (normalized_text.is_empty())
    normalized_text.append(is_absolute_path ? StringView{"/"}
                                            : StringView{"."});
  return Path{normalized_text};
}

fn Path::to_absolute() const throws -> Path
{
  if (is_absolute()) return normalized();
  let result = current_directory();
  result.push_component(m_text);
  return result.normalized();
}

hot fn Path::operator==(const Path &other) const wontthrow -> bool
{
  return m_text == other.m_text;
}

#if SHIT_PLATFORM_IS POSIX

cold fn Path::exists() const wontthrow -> bool
{
  LOG(Debug, "probing whether '%s' exists", m_text.c_str());
  struct stat info{};
  return ::stat(m_text.c_str(), &info) == 0;
}

/* The stat-and-check the type predicates share. A failed stat reads as the type
   not matching rather than an error, matching every caller. The lstat-based
   symbolic-link test keeps its own body since it must not follow the link. */
static fn stat_matches_type(const char *path, mode_t expected_type) wontthrow
    -> bool
{
  struct stat info{};
  if (::stat(path, &info) != 0) return false;
  return (info.st_mode & S_IFMT) == expected_type;
}

cold fn Path::is_directory() const wontthrow -> bool
{
  return stat_matches_type(m_text.c_str(), S_IFDIR);
}

fn Path::is_regular_file() const wontthrow -> bool
{
  return stat_matches_type(m_text.c_str(), S_IFREG);
}

fn Path::is_symbolic_link() const wontthrow -> bool
{
  struct stat info{};
  if (::lstat(m_text.c_str(), &info) != 0) return false;
  return S_ISLNK(info.st_mode);
}

fn Path::is_block_device() const wontthrow -> bool
{
  return stat_matches_type(m_text.c_str(), S_IFBLK);
}

fn Path::is_character_device() const wontthrow -> bool
{
  return stat_matches_type(m_text.c_str(), S_IFCHR);
}

fn Path::is_fifo() const wontthrow -> bool
{
  return stat_matches_type(m_text.c_str(), S_IFIFO);
}

fn Path::is_socket() const wontthrow -> bool
{
  return stat_matches_type(m_text.c_str(), S_IFSOCK);
}

fn Path::file_size() const wontthrow -> Maybe<u64>
{
  struct stat info{};
  if (::stat(m_text.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return None;
  return static_cast<u64>(info.st_size);
}

fn Path::modification_time() const wontthrow -> Maybe<i64>
{
  struct stat info{};
  if (::stat(m_text.c_str(), &info) != 0) return None;
  return static_cast<i64>(info.st_mtime);
}

fn Path::is_same_file_as(const Path &other) const wontthrow -> bool
{
  struct stat a{}, b{};
  if (::stat(m_text.c_str(), &a) != 0) return false;
  if (::stat(other.m_text.c_str(), &b) != 0) return false;
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

fn Path::is_newer_than(const Path &other) const wontthrow -> bool
{
  struct stat a{}, b{};
  if (::stat(m_text.c_str(), &a) != 0) return false;
  if (::stat(other.m_text.c_str(), &b) != 0) return false;
  /* The seconds are compared first and the nanoseconds break a tie, the same
     st_mtim ordering dash compares so two files written in the same second
     still order by their finer timestamp. */
  if (a.st_mtim.tv_sec != b.st_mtim.tv_sec)
    return a.st_mtim.tv_sec > b.st_mtim.tv_sec;
  return a.st_mtim.tv_nsec > b.st_mtim.tv_nsec;
}

fn Path::is_older_than(const Path &other) const wontthrow -> bool
{
  struct stat a{}, b{};
  if (::stat(m_text.c_str(), &a) != 0) return false;
  if (::stat(other.m_text.c_str(), &b) != 0) return false;
  if (a.st_mtim.tv_sec != b.st_mtim.tv_sec)
    return a.st_mtim.tv_sec < b.st_mtim.tv_sec;
  return a.st_mtim.tv_nsec < b.st_mtim.tv_nsec;
}

fn Path::is_readable() const wontthrow -> bool
{
  return ::access(m_text.c_str(), R_OK) == 0;
}

fn Path::is_writable() const wontthrow -> bool
{
  return ::access(m_text.c_str(), W_OK) == 0;
}

fn Path::is_executable() const wontthrow -> bool
{
  return ::access(m_text.c_str(), X_OK) == 0;
}

cold fn Path::current_directory() throws -> Path
{
  /* getcwd fails with ERANGE when the working directory does not fit the
     buffer, so the buffer doubles until the path fits rather than silently
     returning an empty path for a deep directory. A real failure such as a
     removed working directory carries a different errno and ends the loop with
     an empty path. */
  LOG(Debug, "reading the current working directory");
  let buffer = ArrayList<char>{};
  usize buffer_size = 4096;
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

fn Path::set_current_directory(const Path &path) throws -> ErrorOr<Ok>
{
  LOG(Info, "changing the current directory to '%s'", path.c_str());
  if (::chdir(path.c_str()) != 0)
    return Error{"Could not change directory to '" + path.text() + "'"};
  return Success;
}

cold fn Path::read_directory(const Path &dir) throws -> Maybe<ArrayList<String>>
{
  let const handle = ::opendir(dir.c_str());
  if (handle == nullptr) {
    LOG(Debug, "could not open the directory '%s'", dir.c_str());
    return None;
  }

  let names = ArrayList<String>{};
  /* readdir returns NULL on both a clean end of the directory and a read error,
     so errno is cleared before each call. A NULL with a changed errno is a real
     error, which returns None rather than a truncated list the caller would
     mistake for the whole directory. */
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
    if (name == StringView{"."} || name == StringView{".."}) continue;
    names.push(String{name});
  }

  ::closedir(handle);

  LOG(All, "read %zu entries from the directory '%s'", names.count(),
      dir.c_str());

  return names;
}

cold fn Path::read_directory_typed(const Path &dir) throws
    -> Maybe<ArrayList<directory_child>>
{
  let const handle = ::opendir(dir.c_str());
  if (handle == nullptr) return None;

  let entries = ArrayList<directory_child>{};
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
    if (name == StringView{"."} || name == StringView{".."}) continue;

    /* readdir carries the type on most filesystems, so a directory or a regular
       file is known without a stat. A symlink and an unknown type still need a
       stat from the caller, the latter on a filesystem that does not fill
       d_type. */
    entry_kind kind = entry_kind::Unknown;
    switch (entry->d_type) {
    case DT_DIR: kind = entry_kind::Directory; break;
    case DT_REG: kind = entry_kind::Regular; break;
    case DT_LNK: kind = entry_kind::Symlink; break;
    case DT_UNKNOWN: kind = entry_kind::Unknown; break;
    default: kind = entry_kind::Other; break;
    }

    entries.push(directory_child{String{name}, kind});
  }

  ::closedir(handle);
  return entries;
}

#elif SHIT_PLATFORM_IS WIN32

cold fn Path::exists() const wontthrow -> bool
{
  return GetFileAttributesA(m_text.c_str()) != INVALID_FILE_ATTRIBUTES;
}

cold fn Path::is_directory() const wontthrow -> bool
{
  let const attributes = GetFileAttributesA(m_text.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

fn Path::is_regular_file() const wontthrow -> bool
{
  let const attributes = GetFileAttributesA(m_text.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

fn Path::is_symbolic_link() const wontthrow -> bool
{
  let const attributes = GetFileAttributesA(m_text.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

/* Windows has no POSIX block, character, FIFO, or socket file type, so these
   primaries are always false there. */
fn Path::is_block_device() const wontthrow -> bool { return false; }
fn Path::is_character_device() const wontthrow -> bool { return false; }
fn Path::is_fifo() const wontthrow -> bool { return false; }
fn Path::is_socket() const wontthrow -> bool { return false; }

fn Path::file_size() const wontthrow -> Maybe<u64>
{
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (GetFileAttributesExA(m_text.c_str(), GetFileExInfoStandard, &data) == 0)
    return None;
  if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return None;
  return (static_cast<u64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

fn Path::modification_time() const wontthrow -> Maybe<i64>
{
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (GetFileAttributesExA(m_text.c_str(), GetFileExInfoStandard, &data) == 0)
    return None;
  /* The write time packs as a 64-bit count of 100ns ticks, enough for the
     staleness compare without converting to the POSIX epoch. */
  return static_cast<i64>(
      (static_cast<u64>(data.ftLastWriteTime.dwHighDateTime) << 32) |
      data.ftLastWriteTime.dwLowDateTime);
}

fn Path::is_same_file_as(const Path &other) const wontthrow -> bool
{
  /* The volume serial and the file index together name one file the way a
     device and an inode do on POSIX, read from an opened handle. The backup
     semantics flag lets a directory open too, so a directory compares like any
     other path. */
  let const first = CreateFileA(
      m_text.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (first == INVALID_HANDLE_VALUE) return false;
  let const second =
      CreateFileA(other.m_text.c_str(), 0,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (second == INVALID_HANDLE_VALUE) {
    CloseHandle(first);
    return false;
  }
  BY_HANDLE_FILE_INFORMATION first_info{}, second_info{};
  let const both_read = GetFileInformationByHandle(first, &first_info) != 0 &&
                        GetFileInformationByHandle(second, &second_info) != 0;
  CloseHandle(first);
  CloseHandle(second);
  if (!both_read) return false;
  return first_info.dwVolumeSerialNumber == second_info.dwVolumeSerialNumber &&
         first_info.nFileIndexHigh == second_info.nFileIndexHigh &&
         first_info.nFileIndexLow == second_info.nFileIndexLow;
}

fn Path::is_newer_than(const Path &other) const wontthrow -> bool
{
  WIN32_FILE_ATTRIBUTE_DATA a{}, b{};
  if (GetFileAttributesExA(m_text.c_str(), GetFileExInfoStandard, &a) == 0)
    return false;
  if (GetFileAttributesExA(other.m_text.c_str(), GetFileExInfoStandard, &b) ==
      0)
    return false;
  return CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) > 0;
}

fn Path::is_older_than(const Path &other) const wontthrow -> bool
{
  WIN32_FILE_ATTRIBUTE_DATA a{}, b{};
  if (GetFileAttributesExA(m_text.c_str(), GetFileExInfoStandard, &a) == 0)
    return false;
  if (GetFileAttributesExA(other.m_text.c_str(), GetFileExInfoStandard, &b) ==
      0)
    return false;
  return CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) < 0;
}

fn Path::is_readable() const wontthrow -> bool
{
  return _access(m_text.c_str(), 4) == 0;
}

fn Path::is_writable() const wontthrow -> bool
{
  return _access(m_text.c_str(), 2) == 0;
}

fn Path::is_executable() const wontthrow -> bool
{
  /* Windows has no execute permission bit, so an existing file is treated as
     runnable, matching how the shell resolves a program there. */
  return exists();
}

cold fn Path::current_directory() throws -> Path
{
  char buffer[4096];
  if (_getcwd(buffer, sizeof(buffer)) != nullptr)
    return Path{StringView{buffer}};
  return Path{};
}

fn Path::set_current_directory(const Path &path) throws -> ErrorOr<Ok>
{
  if (_chdir(path.c_str()) != 0)
    return Error{"Could not change directory to '" + path.text() + "'"};
  return Success;
}

cold fn Path::read_directory(const Path &dir) throws -> Maybe<ArrayList<String>>
{
  let pattern = dir.text().clone();
  pattern.push(DIRECTORY_SEPARATOR);
  pattern.push('*');

  WIN32_FIND_DATAA data{};
  let const handle = FindFirstFileA(pattern.c_str(), &data);
  if (handle == INVALID_HANDLE_VALUE) return None;

  let names = ArrayList<String>{};
  do {
    let const name = StringView{data.cFileName};
    if (name == StringView{"."} || name == StringView{".."}) continue;
    names.push(String{name});
  } while (FindNextFileA(handle, &data) != 0);
  FindClose(handle);
  LOG(All, "read %zu entries from the directory '%s'", names.count(),
      dir.c_str());
  return names;
}

cold fn Path::read_directory_typed(const Path &dir) throws
    -> Maybe<ArrayList<directory_child>>
{
  /* Windows carries no readdir type, so the names are read the plain way and
     each child is left Unknown for the caller to stat. */
  Maybe<ArrayList<String>> names = read_directory(dir);
  if (!names.has_value()) return None;

  let entries = ArrayList<directory_child>{};
  entries.reserve(names->count());
  for (String &name : *names)
    entries.push(directory_child{steal(name), entry_kind::Unknown});
  return entries;
}

#endif

fn Path::temp_directory() throws -> Path
{
#if SHIT_PLATFORM_IS WIN32
  if (const char *from_env = std::getenv("TEMP"))
    return Path{StringView{from_env}};
  return Path{StringView{"C:\\Windows\\Temp"}};
#else
  if (const char *from_env = std::getenv("TMPDIR"))
    return Path{StringView{from_env}};
  return Path{StringView{"/tmp"}};
#endif
}

fn Path::read_entire_file(StringView path) throws -> Maybe<String>
{
  LOG(Debug, "reading the entire file '%.*s'", static_cast<int>(path.length),
      path.data);

  let const file = os::open_file_descriptor(path, os::file_open_mode::Read);
  if (!file) return None;

  let contents = String{};
  char buffer[4096];
  loop
  {
    Maybe<usize> read_count = os::read_fd(*file, buffer, sizeof(buffer));
    if (!read_count || *read_count == 0) break;
    contents.append(StringView{buffer, *read_count});
  }

  os::close_fd(*file);

  return contents;
}

fn Path::canonicalize(StringView path) throws -> Maybe<Path>
{
  LOG(Debug, "canonicalizing the path '%.*s'", static_cast<int>(path.length),
      path.data);

  let candidate = Path{path};

  if (candidate.is_relative() && path.find_character('/').has_value()) {
    candidate = candidate.to_absolute();
  }

  candidate = candidate.normalized();

  /* A name with no extension may need one of the omitted suffixes added. The
     ending dot is stripped by the path normalization, so a name written with a
     trailing dot is left as typed. */
  const bool ends_with_dot =
      path.length > 0 && path.data[path.length - 1] == '.';
  if (candidate.extension().is_empty() && !ends_with_dot) {
    usize suffix_index = 0;
    while (!candidate.exists() && suffix_index < os::OMITTED_SUFFIXES.count()) {
      const String &suffix = os::OMITTED_SUFFIXES[suffix_index++];
      candidate = candidate.with_extension(suffix.view());
    }
  }

  if (!candidate.exists()) return shit::None;

  return candidate;
}

fn Path::detect_mimic_shell() const throws -> Maybe<mimic_mood>
{
  LOG(Debug, "probing '%s' for a shell shebang to mimic", c_str());

  let const file =
      os::open_file_descriptor(text().view(), os::file_open_mode::Read);
  if (!file) return None;
  char buffer[256];
  let const read_count = os::read_fd(*file, buffer, sizeof(buffer));
  os::close_fd(*file);
  if (!read_count || *read_count < 3) return None;

  let const head = StringView{buffer, *read_count};
  if (!head.starts_with("#!")) return None;

  /* The shebang ends at the first newline, and only its first line is read. */
  usize line_end = 2;
  while (line_end < head.length && head[line_end] != '\n')
    line_end++;
  let const line = head.substring_of_length(2, line_end - 2);

  /* The basename of a whitespace-delimited token, dropping any directory path.
   */
  let const do_basename_of = [](StringView token) -> StringView {
    usize last_slash = token.length;
    for (usize i = 0; i < token.length; i++)
      if (token[i] == '/') last_slash = i;
    return last_slash == token.length ? token : token.substring(last_slash + 1);
  };
  /* Walk the line token by token, splitting on spaces and tabs. */
  usize i = 0;
  let const do_next_token = [&]() -> StringView {
    while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
      i++;
    usize const start = i;
    while (i < line.length && line[i] != ' ' && line[i] != '\t')
      i++;
    return line.substring_of_length(start, i - start);
  };

  StringView shell = do_basename_of(do_next_token());
  /* The /usr/bin/env form names the shell as the next token, after any env
     options, so the first non-option token is taken. */
  if (shell == "env") {
    loop
    {
      let const token = do_next_token();
      if (token.length == 0) return None;
      if (token[0] == '-') continue;
      shell = do_basename_of(token);
      break;
    }
  }

  if (shell == "sh" || shell == "dash") return mimic_mood::Posix;
  if (shell == "bash") return mimic_mood::Bash;
  if (shell == "shit") return mimic_mood::Default;
  return None;
}

PathBuilder::PathBuilder(StringView root) : m_text(root) {}

fn PathBuilder::append(StringView component) throws -> PathBuilder &
{
  if (component.length == 0) return *this;
  if (!m_text.is_empty() && !is_directory_separator(m_text.back()) &&
      !is_directory_separator(component.data[0]))
  {
    m_text.push(DIRECTORY_SEPARATOR);
  }
  m_text.append(component);
  return *this;
}

fn PathBuilder::append_raw(StringView bytes) throws -> PathBuilder &
{
  m_text.append(bytes);
  return *this;
}

fn PathBuilder::build() const throws -> Path { return Path{m_text}; }

} /* namespace shit */
