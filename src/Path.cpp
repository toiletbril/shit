#include "Path.hpp"

#include "Platform.hpp"

#include <cerrno>
#include <cstdlib>

#if SHIT_PLATFORM_IS POSIX
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
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

fn Path::text() const wontthrow -> const String & { return m_text; }

fn Path::c_str() const wontthrow -> const char * { return m_text.c_str(); }

fn Path::count() const wontthrow -> usize { return m_text.count(); }

fn Path::is_empty() const wontthrow -> bool { return m_text.is_empty(); }

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
  let const existing = extension();

  ASSERT(existing.length <= m_text.count(),
         "extension is a suffix of the path text");
  let const keep = m_text.count() - existing.length;

  Path result{m_text.substring_of_length(0, keep)};
  if (new_extension.length > 0 && new_extension.data[0] != '.')
    result.m_text.push('.');
  result.m_text.append(new_extension);

  return result;
}

fn Path::normalized() const throws -> Path
{
  let const absolute = is_absolute();

  /* Each kept component is a view into the original text, valid for the life of
     this function while the result is assembled. */
  ArrayList<StringView> components{};
  usize i = 0;
  while (i < m_text.count()) {
    if (is_directory_separator(m_text[i])) {
      i++;
      continue;
    }
    const usize start = i;
    while (i < m_text.count() && !is_directory_separator(m_text[i]))
      i++;
    let const part = m_text.substring_of_length(start, i - start);
    if (part == StringView{"."}) continue;
    if (part == StringView{".."}) {
      /* A .. pops the last real component, unless none remains or the last was
         itself a .. kept because the path is relative and cannot climb past its
         own start. */
      if (components.count() > 0 && !(components.back() == StringView{".."})) {
        components.pop_back();
      } else if (!absolute) {
        components.push(part);
      }
      continue;
    }
    components.push(part);
  }

  String built{};
  if (absolute) built.push(DIRECTORY_SEPARATOR);
  for (usize c = 0; c < components.count(); c++) {
    if (c > 0) built.push(DIRECTORY_SEPARATOR);
    built.append(components[c]);
  }
  if (built.is_empty())
    built.append(absolute ? StringView{"/"} : StringView{"."});
  return Path{built};
}

fn Path::to_absolute() const throws -> Path
{
  if (is_absolute()) return normalized();
  let result = current_directory();
  result.push_component(m_text);
  return result.normalized();
}

fn Path::operator==(const Path &other) const wontthrow -> bool
{
  return m_text == other.m_text;
}

#if SHIT_PLATFORM_IS POSIX

fn Path::exists() const wontthrow -> bool
{
  struct stat info{};
  return ::stat(m_text.c_str(), &info) == 0;
}

fn Path::is_directory() const wontthrow -> bool
{
  struct stat info{};
  if (::stat(m_text.c_str(), &info) != 0) return false;
  return S_ISDIR(info.st_mode);
}

fn Path::is_regular_file() const wontthrow -> bool
{
  struct stat info{};
  if (::stat(m_text.c_str(), &info) != 0) return false;
  return S_ISREG(info.st_mode);
}

fn Path::is_symbolic_link() const wontthrow -> bool
{
  struct stat info{};
  if (::lstat(m_text.c_str(), &info) != 0) return false;
  return S_ISLNK(info.st_mode);
}

fn Path::file_size() const wontthrow -> Maybe<u64>
{
  struct stat info{};
  if (::stat(m_text.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return None;
  return static_cast<u64>(info.st_size);
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

fn Path::current_directory() throws -> Path
{
  /* getcwd fails with ERANGE when the working directory does not fit the
     buffer, so the buffer doubles until the path fits rather than silently
     returning an empty path for a deep directory. A real failure such as a
     removed working directory carries a different errno and ends the loop with
     an empty path. */
  ArrayList<char> buffer{};
  usize buffer_size = 4096;
  for (;;) {
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
  if (::chdir(path.c_str()) != 0)
    return Error{"Could not change directory to '" + path.text() + "'"};
  return Success;
}

fn Path::read_directory(const Path &dir) throws -> Maybe<ArrayList<String>>
{
  DIR *const handle = ::opendir(dir.c_str());
  if (handle == nullptr) return None;

  ArrayList<String> names{};
  /* readdir returns NULL on both a clean end of the directory and a read error,
     so errno is cleared before each call. A NULL with a changed errno is a real
     error, which returns None rather than a truncated list the caller would
     mistake for the whole directory. */
  for (;;) {
    errno = 0;
    struct dirent *const entry = ::readdir(handle);
    if (entry == nullptr) {
      if (errno != 0) {
        ::closedir(handle);
        return None;
      }
      break;
    }

    const StringView name{entry->d_name};
    if (name == StringView{"."} || name == StringView{".."}) continue;
    names.push(String{name});
  }

  ::closedir(handle);

  return names;
}

#elif SHIT_PLATFORM_IS WIN32

fn Path::exists() const -> bool
{
  return GetFileAttributesA(m_text.c_str()) != INVALID_FILE_ATTRIBUTES;
}

fn Path::is_directory() const -> bool
{
  DWORD attributes = GetFileAttributesA(m_text.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

fn Path::is_regular_file() const -> bool
{
  DWORD attributes = GetFileAttributesA(m_text.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

fn Path::is_symbolic_link() const -> bool
{
  DWORD attributes = GetFileAttributesA(m_text.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

fn Path::file_size() const -> Maybe<u64>
{
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (GetFileAttributesExA(m_text.c_str(), GetFileExInfoStandard, &data) == 0)
    return None;
  if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return None;
  return (static_cast<u64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

fn Path::is_same_file_as(const Path &other) const -> bool
{
  /* The volume serial and the file index together name one file the way a
     device and an inode do on POSIX, read from an opened handle. The backup
     semantics flag lets a directory open too, so a directory compares like any
     other path. */
  HANDLE first = CreateFileA(
      m_text.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (first == INVALID_HANDLE_VALUE) return false;
  HANDLE second =
      CreateFileA(other.m_text.c_str(), 0,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (second == INVALID_HANDLE_VALUE) {
    CloseHandle(first);
    return false;
  }
  BY_HANDLE_FILE_INFORMATION first_info{}, second_info{};
  const bool both_read = GetFileInformationByHandle(first, &first_info) != 0 &&
                         GetFileInformationByHandle(second, &second_info) != 0;
  CloseHandle(first);
  CloseHandle(second);
  if (!both_read) return false;
  return first_info.dwVolumeSerialNumber == second_info.dwVolumeSerialNumber &&
         first_info.nFileIndexHigh == second_info.nFileIndexHigh &&
         first_info.nFileIndexLow == second_info.nFileIndexLow;
}

fn Path::is_newer_than(const Path &other) const -> bool
{
  WIN32_FILE_ATTRIBUTE_DATA a{}, b{};
  if (GetFileAttributesExA(m_text.c_str(), GetFileExInfoStandard, &a) == 0)
    return false;
  if (GetFileAttributesExA(other.m_text.c_str(), GetFileExInfoStandard, &b) ==
      0)
    return false;
  return CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) > 0;
}

fn Path::is_older_than(const Path &other) const -> bool
{
  WIN32_FILE_ATTRIBUTE_DATA a{}, b{};
  if (GetFileAttributesExA(m_text.c_str(), GetFileExInfoStandard, &a) == 0)
    return false;
  if (GetFileAttributesExA(other.m_text.c_str(), GetFileExInfoStandard, &b) ==
      0)
    return false;
  return CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) < 0;
}

fn Path::is_readable() const -> bool { return _access(m_text.c_str(), 4) == 0; }

fn Path::is_writable() const -> bool { return _access(m_text.c_str(), 2) == 0; }

fn Path::is_executable() const -> bool
{
  /* Windows has no execute permission bit, so an existing file is treated as
     runnable, matching how the shell resolves a program there. */
  return exists();
}

fn Path::current_directory() -> Path
{
  char buffer[4096];
  if (_getcwd(buffer, sizeof(buffer)) != nullptr)
    return Path{StringView{buffer}};
  return Path{};
}

fn Path::set_current_directory(const Path &path) -> ErrorOr<Ok>
{
  if (_chdir(path.c_str()) != 0)
    return Error{"Could not change directory to '" + path.text() + "'"};
  return Success;
}

fn Path::read_directory(const Path &dir) -> Maybe<ArrayList<String>>
{
  String pattern{dir.text()};
  pattern.push(DIRECTORY_SEPARATOR);
  pattern.push('*');

  WIN32_FIND_DATAA data{};
  HANDLE handle = FindFirstFileA(pattern.c_str(), &data);
  if (handle == INVALID_HANDLE_VALUE) return None;

  ArrayList<String> names{};
  do {
    StringView name{data.cFileName};
    if (name == StringView{"."} || name == StringView{".."}) continue;
    names.push(String{name});
  } while (FindNextFileA(handle, &data) != 0);
  FindClose(handle);
  return names;
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
