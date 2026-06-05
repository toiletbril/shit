#include "Path.hpp"

#include "Platform.hpp"

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
static bool is_directory_separator(char c)
{
#if SHIT_PLATFORM_IS WIN32
  return c == '/' || c == '\\';
#else
  return c == '/';
#endif
}

Path::Path(StringView text) : m_text(text) {}

const String &Path::text() const { return m_text; }

const char *Path::c_str() const { return m_text.c_str(); }

usize Path::size() const { return m_text.size(); }

bool Path::empty() const { return m_text.empty(); }

bool Path::is_absolute() const
{
  if (m_text.empty()) return false;
#if SHIT_PLATFORM_IS WIN32
  if (is_directory_separator(m_text[0])) return true;
  /* A drive-qualified path such as C:\ is absolute. */
  return m_text.size() >= 2 && m_text[1] == ':';
#else
  return is_directory_separator(m_text[0]);
#endif
}

bool Path::is_relative() const { return !is_absolute(); }

/* The offset just past the last separator, so the filename starts there. Zero
   when there is no separator. */
static usize filename_offset(const String &text)
{
  for (usize i = text.size(); i > 0; i--)
    if (is_directory_separator(text[i - 1])) return i;
  return 0;
}

StringView Path::filename() const
{
  usize start = filename_offset(m_text);
  return m_text.substring(start);
}

StringView Path::extension() const
{
  StringView name = filename();
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

Path Path::parent() const
{
  usize end = filename_offset(m_text);
  if (end == 0) return Path{};
  /* Keep a lone root separator, otherwise drop the trailing one. */
  if (end == 1) return Path{m_text.substring_of_length(0, 1)};
  return Path{m_text.substring_of_length(0, end - 1)};
}

Path &Path::push_component(StringView component)
{
  if (component.length == 0) return *this;
  if (!m_text.empty() && !is_directory_separator(m_text.back()) &&
      !is_directory_separator(component.data[0]))
  {
    m_text.push(DIRECTORY_SEPARATOR);
  }
  m_text.append(component);
  return *this;
}

Path Path::with_extension(StringView new_extension) const
{
  StringView existing = extension();
  usize keep = m_text.size() - existing.length;
  Path result{m_text.substring_of_length(0, keep)};
  if (new_extension.length > 0 && new_extension.data[0] != '.')
    result.m_text.push('.');
  result.m_text.append(new_extension);
  return result;
}

Path Path::normalized() const
{
  bool absolute = is_absolute();

  /* Each kept component is a view into the original text, valid for the life of
     this function while the result is assembled. */
  ArrayList<StringView> components{};
  usize i = 0;
  while (i < m_text.size()) {
    if (is_directory_separator(m_text[i])) {
      i++;
      continue;
    }
    usize start = i;
    while (i < m_text.size() && !is_directory_separator(m_text[i]))
      i++;
    StringView part = m_text.substring_of_length(start, i - start);
    if (part == StringView{"."}) continue;
    if (part == StringView{".."}) {
      /* A .. pops the last real component, unless none remains or the last was
         itself a .. kept because the path is relative and cannot climb past its
         own start. */
      if (components.size() > 0 && !(components.back() == StringView{".."})) {
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
  for (usize c = 0; c < components.size(); c++) {
    if (c > 0) built.push(DIRECTORY_SEPARATOR);
    built.append(components[c]);
  }
  if (built.empty()) built.append(absolute ? StringView{"/"} : StringView{"."});
  return Path{built};
}

Path Path::to_absolute() const
{
  if (is_absolute()) return normalized();
  Path result = current_directory();
  result.push_component(m_text);
  return result.normalized();
}

bool Path::operator==(const Path &other) const
{
  return m_text == other.m_text;
}

#if SHIT_PLATFORM_IS POSIX

bool Path::exists() const
{
  struct stat info{};
  return ::stat(m_text.c_str(), &info) == 0;
}

bool Path::is_directory() const
{
  struct stat info{};
  if (::stat(m_text.c_str(), &info) != 0) return false;
  return S_ISDIR(info.st_mode);
}

bool Path::is_regular_file() const
{
  struct stat info{};
  if (::stat(m_text.c_str(), &info) != 0) return false;
  return S_ISREG(info.st_mode);
}

Maybe<u64> Path::file_size() const
{
  struct stat info{};
  if (::stat(m_text.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return None;
  return static_cast<u64>(info.st_size);
}

bool Path::is_readable() const { return ::access(m_text.c_str(), R_OK) == 0; }

bool Path::is_writable() const { return ::access(m_text.c_str(), W_OK) == 0; }

bool Path::is_executable() const { return ::access(m_text.c_str(), X_OK) == 0; }

Path Path::current_directory()
{
  char buffer[4096];
  if (::getcwd(buffer, sizeof(buffer)) != NULL) return Path{StringView{buffer}};
  return Path{};
}

ErrorOr<Ok> Path::set_current_directory(const Path &path)
{
  if (::chdir(path.c_str()) != 0)
    return Error{"Could not change directory to '" + path.text() + "'"};
  return Ok{};
}

Maybe<ArrayList<String>> Path::read_directory(const Path &dir)
{
  DIR *handle = ::opendir(dir.c_str());
  if (handle == NULL) return None;

  ArrayList<String> names{};
  for (struct dirent *entry = ::readdir(handle); entry != NULL;
       entry = ::readdir(handle))
  {
    StringView name{entry->d_name};
    if (name == StringView{"."} || name == StringView{".."}) continue;
    names.push(String{name});
  }
  ::closedir(handle);
  return names;
}

#elif SHIT_PLATFORM_IS WIN32

bool Path::exists() const
{
  return GetFileAttributesA(m_text.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool Path::is_directory() const
{
  DWORD attributes = GetFileAttributesA(m_text.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool Path::is_regular_file() const
{
  DWORD attributes = GetFileAttributesA(m_text.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

Maybe<u64> Path::file_size() const
{
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (GetFileAttributesExA(m_text.c_str(), GetFileExInfoStandard, &data) == 0)
    return None;
  if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return None;
  return (static_cast<u64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

bool Path::is_readable() const { return _access(m_text.c_str(), 4) == 0; }

bool Path::is_writable() const { return _access(m_text.c_str(), 2) == 0; }

bool Path::is_executable() const
{
  /* Windows has no execute permission bit, so an existing file is treated as
     runnable, matching how the shell resolves a program there. */
  return exists();
}

Path Path::current_directory()
{
  char buffer[4096];
  if (_getcwd(buffer, sizeof(buffer)) != NULL) return Path{StringView{buffer}};
  return Path{};
}

ErrorOr<Ok> Path::set_current_directory(const Path &path)
{
  if (_chdir(path.c_str()) != 0)
    return Error{"Could not change directory to '" + path.text() + "'"};
  return Ok{};
}

Maybe<ArrayList<String>> Path::read_directory(const Path &dir)
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

Path Path::temp_directory()
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

PathBuilder &PathBuilder::append(StringView component)
{
  if (component.length == 0) return *this;
  if (!m_text.empty() && !is_directory_separator(m_text.back()) &&
      !is_directory_separator(component.data[0]))
  {
    m_text.push(DIRECTORY_SEPARATOR);
  }
  m_text.append(component);
  return *this;
}

PathBuilder &PathBuilder::append_raw(StringView bytes)
{
  m_text.append(bytes);
  return *this;
}

Path PathBuilder::build() const { return Path{m_text}; }

} /* namespace shit */
