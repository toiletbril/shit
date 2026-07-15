#include "Path.hpp"

#include "PackedStringKey.hpp"
#include "Platform.hpp"
#include "StaticStringMap.hpp"
#include "Trace.hpp"

namespace shit {

Path::Path(StringView text) : m_text(text) {}

hot fn Path::text() const wontthrow -> const String & { return m_text; }

hot fn Path::c_str() const wontthrow -> const char * { return m_text.c_str(); }

hot fn Path::count() const wontthrow -> usize { return m_text.count(); }

hot fn Path::is_empty() const wontthrow -> bool { return m_text.is_empty(); }

hot fn Path::has_trailing_separator() const wontthrow -> bool
{
  return !m_text.is_empty() && os::is_directory_separator(m_text.back());
}

fn Path::is_absolute() const wontthrow -> bool
{
  return os::path_is_absolute(m_text.view());
}

fn Path::is_relative() const wontthrow -> bool { return !is_absolute(); }

static pure fn filename_offset(const String &text) wontthrow -> usize
{
  for (usize i = text.count(); i > 0; i--)
    if (os::is_directory_separator(text[i - 1])) return i;
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
  if (name == StringView{"."} || name == StringView{".."}) {
    return StringView{name.data + name.length, 0};
  }
  /* A leading dot names a hidden file, so the scan stops before the first byte.
   */
  for (usize i = name.length; i > 1; i--)
    if (name.data[i - 1] == '.') return name.substring(i - 1);
  return StringView{name.data + name.length, 0};
}

fn Path::parent() const throws -> Path
{
  let const end = filename_offset(m_text);
  if (end == 0) return Path{};
  if (end == 1) return Path{m_text.substring_of_length(0, 1)};
  return Path{m_text.substring_of_length(0, end - 1)};
}

static fn append_path_component(String &text, StringView component) throws
    -> void
{
  if (component.length == 0) return;
  if (!text.is_empty() && !os::is_directory_separator(text.back()) &&
      !os::is_directory_separator(component.data[0]))
  {
    text.push(os::DIRECTORY_SEPARATOR);
  }
  text.append(component);
}

fn Path::push_component(StringView component) throws -> Path &
{
  append_path_component(m_text, component);
  return *this;
}

fn Path::with_extension(StringView new_extension) const throws -> Path
{
  let const current_extension = extension();

  ASSERT(current_extension.length <= m_text.count(),
         "extension is a suffix of the path text");
  let const prefix_length = m_text.count() - current_extension.length;

  let result = Path{m_text.substring_of_length(0, prefix_length)};
  if (new_extension.length > 0 && new_extension.data[0] != '.') {
    result.m_text.push('.');
  }
  result.m_text.append(new_extension);

  return result;
}

cold fn Path::normalized() const throws -> Path
{
  LOG(Debug, "normalizing the path '%s'", m_text.c_str());

  let const root_length = os::path_root_length(m_text.view());
  let const is_absolute_path = root_length > 0;

  let components = ArrayList<StringView>{heap_allocator()};
  usize i = root_length;
  while (i < m_text.count()) {
    if (os::is_directory_separator(m_text[i])) {
      i++;
      continue;
    }
    let const component_start = i;
    while (i < m_text.count() && !os::is_directory_separator(m_text[i]))
      i++;
    let const component =
        m_text.substring_of_length(component_start, i - component_start);
    if (component == StringView{"."}) continue;
    if (component == StringView{".."}) {
      /* A relative path keeps a leading .. because it cannot climb past its own
         start. */
      if (components.count() > 0 && !(components.back() == StringView{".."})) {
        components.pop_back();
      } else if (!is_absolute_path) {
        components.push(component);
      }
      continue;
    }
    components.push(component);
  }

  let normalized_text = String{heap_allocator()};
  normalized_text.append(m_text.substring_of_length(0, root_length));
  if (!normalized_text.is_empty() && !components.is_empty() &&
      !os::is_directory_separator(normalized_text.back()))
  {
    normalized_text.push(os::DIRECTORY_SEPARATOR);
  }
  for (usize i = 0; i < components.count(); i++) {
    if (i > 0) normalized_text.push(os::DIRECTORY_SEPARATOR);
    normalized_text.append(components[i]);
  }
  if (normalized_text.is_empty())
    normalized_text.append(is_absolute_path
                               ? StringView{&os::DIRECTORY_SEPARATOR, 1}
                               : StringView{"."});
  return Path{normalized_text};
}

fn Path::to_absolute() const throws -> Path
{
  if (is_absolute()) return normalized();
  if (let native = os::resolve_drive_relative_path(m_text.view()))
    return native->normalized();
  let result = current_directory();
  result.push_component(m_text);
  return result.normalized();
}

hot fn Path::operator==(const Path &other) const wontthrow -> bool
{
  return m_text == other.m_text;
}

cold fn Path::exists() const wontthrow -> bool
{
  return os::path_exists(m_text.view());
}

cold fn Path::is_directory() const wontthrow -> bool
{
  return os::path_is_directory(m_text.view());
}

fn Path::is_regular_file() const wontthrow -> bool
{
  return os::path_is_regular_file(m_text.view());
}

fn Path::is_symbolic_link() const wontthrow -> bool
{
  return os::path_is_symbolic_link(m_text.view());
}

fn Path::is_block_device() const wontthrow -> bool
{
  return os::path_is_block_device(m_text.view());
}

fn Path::is_character_device() const wontthrow -> bool
{
  return os::path_is_character_device(m_text.view());
}

fn Path::is_fifo() const wontthrow -> bool
{
  return os::path_is_fifo(m_text.view());
}

fn Path::is_socket() const wontthrow -> bool
{
  return os::path_is_socket(m_text.view());
}

fn Path::has_setuid_bit() const wontthrow -> bool
{
  return os::path_has_setuid_bit(m_text.view());
}

fn Path::has_setgid_bit() const wontthrow -> bool
{
  return os::path_has_setgid_bit(m_text.view());
}

fn Path::has_sticky_bit() const wontthrow -> bool
{
  return os::path_has_sticky_bit(m_text.view());
}

fn Path::is_owned_by_effective_user() const wontthrow -> bool
{
  return os::path_is_owned_by_effective_user(m_text.view());
}

fn Path::is_owned_by_effective_group() const wontthrow -> bool
{
  return os::path_is_owned_by_effective_group(m_text.view());
}

fn Path::file_size() const wontthrow -> Maybe<u64>
{
  return os::path_file_size(m_text.view());
}

fn Path::modification_time() const wontthrow -> Maybe<i64>
{
  return os::path_modification_time(m_text.view());
}

fn Path::is_same_file_as(const Path &other) const wontthrow -> bool
{
  return os::paths_are_same_file(m_text.view(), other.m_text.view());
}

fn Path::is_newer_than(const Path &other) const wontthrow -> bool
{
  return os::path_is_newer_than(m_text.view(), other.m_text.view());
}

fn Path::is_older_than(const Path &other) const wontthrow -> bool
{
  return os::path_is_older_than(m_text.view(), other.m_text.view());
}

fn Path::is_readable() const wontthrow -> bool
{
  return os::path_is_readable(m_text.view());
}

fn Path::is_writable() const wontthrow -> bool
{
  return os::path_is_writable(m_text.view());
}

fn Path::is_executable() const wontthrow -> bool
{
  return os::path_is_executable(m_text.view());
}

cold fn Path::current_directory() throws -> Path
{
  return os::read_current_directory();
}

fn Path::set_current_directory(const Path &path) throws -> ErrorOr<Ok>
{
  return os::change_current_directory(path.text().view());
}

cold fn Path::read_directory(const Path &dir) throws -> Maybe<ArrayList<String>>
{
  return os::list_directory(dir.text().view());
}

cold fn Path::read_directory_typed(const Path &dir) throws
    -> Maybe<ArrayList<directory_child>>
{
  return os::list_directory_typed(dir.text().view());
}

fn Path::temp_directory() throws -> Path
{
  return Path{os::temp_directory_path().view()};
}

fn Path::read_entire_file() const throws -> Maybe<String>
{
  LOG(Debug, "reading the entire file '%s'", c_str());

  let const file =
      os::open_file_descriptor(text().view(), os::file_open_mode::Read);
  if (!file) return None;
  defer { os::close_fd(*file); };

  return os::read_fd_to_string(*file, heap_allocator());
}

fn Path::canonicalize(StringView path) throws -> Maybe<Path>
{
  LOG(Debug, "canonicalizing the path '%.*s'", static_cast<int>(path.length),
      path.data);

  let candidate = Path{path};

  if (candidate.is_relative() && os::has_directory_separator(path)) {
    candidate = candidate.to_absolute();
  }

  candidate = candidate.normalized();

  /* A name written with a trailing dot gets no suffix added. */
  const bool ends_with_dot =
      path.length > 0 && path.data[path.length - 1] == '.';
  if (candidate.extension().is_empty() && !ends_with_dot) {
    usize suffix_index = 0;
    while (!candidate.exists() && suffix_index < os::PROGRAM_SUFFIXES.count()) {
      let const &suffix = os::PROGRAM_SUFFIXES[suffix_index++];
      candidate = candidate.with_extension(suffix.text);
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

  usize line_end = 2;
  while (line_end < head.length && head[line_end] != '\n')
    line_end++;
  let const line = head.substring_of_length(2, line_end - 2);

  let const do_basename_of = [](StringView token) -> StringView {
    usize last_slash = token.length;
    for (usize i = 0; i < token.length; i++)
      if (token[i] == '/') last_slash = i;
    return last_slash == token.length ? token : token.substring(last_slash + 1);
  };
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
  /* The env form names the shell as the first non-option token after env. */
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

  static constexpr static_string_entry<mimic_mood> SHELL_ENTRIES[] = {
      {SSK("sh"),   mimic_mood::Posix  },
      {SSK("dash"), mimic_mood::Posix  },
      {SSK("bash"), mimic_mood::Bash   },
      {SSK("shit"), mimic_mood::Default},
  };
  static constexpr StaticStringMap SHELL_MOODS{SHELL_ENTRIES};
  return SHELL_MOODS.find(shell);
}

PathBuilder::PathBuilder(StringView root) : m_text(root) {}

fn PathBuilder::append(StringView component) throws -> PathBuilder &
{
  append_path_component(m_text, component);
  return *this;
}

fn PathBuilder::append_raw(StringView bytes) throws -> PathBuilder &
{
  m_text.append(bytes);
  return *this;
}

fn PathBuilder::build() const throws -> Path { return Path{m_text}; }

} /* namespace shit */
