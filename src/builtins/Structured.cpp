#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("from json | to json | to table | get path");

HELP_DESCRIPTION_DECL(
    "The from builtin parses the JSON text on its input into a value and "
    "prints "
    "it back as JSON. The to builtin reads JSON on its input and renders it as "
    "JSON or as an aligned table. The get builtin walks a dotted path into the "
    "JSON value and prints the part it reaches. Every stage reads text and "
    "writes text, so it composes with the rest of a pipeline.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace {

/* The structured-data builtins keep pipes plain text. from parses JSON text
   into a value tree, to renders the tree back to JSON or to an aligned table,
   and get walks a dotted path into the tree. Every stage reads text and writes
   text, so it composes with grep and the rest of the POSIX world. */

constexpr usize JSON_MAX_DEPTH = 200;

/* A parsed JSON value. A number keeps its source text so a long integer or a
   high-precision float survives the round trip unchanged. A string holds its
   decoded bytes. */
struct json_value
{
  enum class Kind : u8
  {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
  };

  Kind kind{Kind::Null};
  bool boolean{false};
  String scalar{};
  ArrayList<json_value> items{};
  ArrayList<String> keys{};
  ArrayList<json_value> values{};
};

class JsonParser
{
public:
  explicit JsonParser(StringView text) : m_text(text) {}

  fn parse() throws -> json_value
  {
    skip_whitespace();
    json_value value = parse_value(0);
    skip_whitespace();
    if (m_pos != m_text.length)
      fail("unexpected trailing content after the value");
    return value;
  }

private:
  StringView m_text;
  usize m_pos{0};

  cold fn fail(StringView what) const throws -> void
  {
    throw Error{StringView{"Invalid JSON, "} + what};
  }

  fn peek() const wontthrow -> char
  {
    return m_pos < m_text.length ? m_text[m_pos] : '\0';
  }

  fn skip_whitespace() wontthrow -> void
  {
    while (m_pos < m_text.length) {
      const char c = m_text[m_pos];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        m_pos++;
      else
        break;
    }
  }

  fn parse_value(usize depth) throws -> json_value
  {
    if (depth > JSON_MAX_DEPTH) fail("the value nests too deeply");
    skip_whitespace();
    const char c = peek();
    switch (c) {
    case '{': return parse_object(depth);
    case '[': return parse_array(depth);
    case '"': {
      json_value value;
      value.kind = json_value::Kind::String;
      value.scalar = parse_string();
      return value;
    }
    case 't':
    case 'f': return parse_bool();
    case 'n': return parse_null();
    default:
      if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
      fail("an unexpected character begins a value");
      return json_value{};
    }
  }

  fn parse_object(usize depth) throws -> json_value
  {
    json_value value;
    value.kind = json_value::Kind::Object;
    m_pos++;
    skip_whitespace();
    if (peek() == '}') {
      m_pos++;
      return value;
    }
    for (;;) {
      skip_whitespace();
      if (peek() != '"') fail("expected a string key in the object");
      String key = parse_string();
      skip_whitespace();
      if (peek() != ':') fail("expected ':' after an object key");
      m_pos++;
      json_value member = parse_value(depth + 1);
      value.keys.push(steal(key));
      value.values.push(steal(member));
      skip_whitespace();
      const char c = peek();
      if (c == ',') {
        m_pos++;
        continue;
      }
      if (c == '}') {
        m_pos++;
        break;
      }
      fail("expected ',' or '}' in the object");
    }
    return value;
  }

  fn parse_array(usize depth) throws -> json_value
  {
    json_value value;
    value.kind = json_value::Kind::Array;
    m_pos++;
    skip_whitespace();
    if (peek() == ']') {
      m_pos++;
      return value;
    }
    for (;;) {
      json_value item = parse_value(depth + 1);
      value.items.push(steal(item));
      skip_whitespace();
      const char c = peek();
      if (c == ',') {
        m_pos++;
        continue;
      }
      if (c == ']') {
        m_pos++;
        break;
      }
      fail("expected ',' or ']' in the array");
    }
    return value;
  }

  fn parse_string() throws -> String
  {
    String out{};
    m_pos++;
    while (m_pos < m_text.length) {
      const char c = m_text[m_pos++];
      if (c == '"') return out;
      if (c == '\\') {
        if (m_pos >= m_text.length) fail("a string ends inside an escape");
        const char escaped = m_text[m_pos++];
        switch (escaped) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': append_unicode_escape(out); break;
        default: fail("an unknown escape follows a backslash");
        }
      } else {
        out += c;
      }
    }
    fail("a string has no closing quote");
    return out;
  }

  fn read_hex4() throws -> u32
  {
    if (m_pos + 4 > m_text.length) fail("a \\u escape is too short");
    u32 code = 0;
    for (usize i = 0; i < 4; i++) {
      const char hex = m_text[m_pos++];
      code <<= 4;
      if (hex >= '0' && hex <= '9')
        code |= static_cast<u32>(hex - '0');
      else if (hex >= 'a' && hex <= 'f')
        code |= static_cast<u32>(hex - 'a' + 10);
      else if (hex >= 'A' && hex <= 'F')
        code |= static_cast<u32>(hex - 'A' + 10);
      else
        fail("a \\u escape has a bad hex digit");
    }
    return code;
  }

  static fn encode_utf8(u32 code, String &out) throws -> void
  {
    if (code < 0x80) {
      out += static_cast<char>(code);
    } else if (code < 0x800) {
      out += static_cast<char>(0xC0 | (code >> 6));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
      out += static_cast<char>(0xE0 | (code >> 12));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (code >> 18));
      out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    }
  }

  /* Decode a \uXXXX escape to UTF-8, combining a high and low surrogate pair
     into the single astral code point it encodes so an emoji round-trips as
     valid UTF-8 rather than two invalid surrogate halves. */
  fn append_unicode_escape(String &out) throws -> void
  {
    u32 code = read_hex4();
    if (code >= 0xD800 && code <= 0xDBFF && m_pos + 1 < m_text.length &&
        m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u')
    {
      m_pos += 2;
      const u32 low = read_hex4();
      if (low >= 0xDC00 && low <= 0xDFFF) {
        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
      } else {
        /* The second escape is not a low surrogate, so the high half stands on
           its own and the second value is encoded after it. */
        encode_utf8(code, out);
        code = low;
      }
    }
    encode_utf8(code, out);
  }

  fn at_digit() const wontthrow -> bool
  {
    const char c = peek();
    return c >= '0' && c <= '9';
  }

  /* Scan a JSON number by the grammar, so a malformed run such as a lone '-',
     '1.2.3', or '1e' is rejected rather than stored and re-emitted as invalid
     JSON. The source text is kept verbatim to preserve precision. */
  fn parse_number() throws -> json_value
  {
    const usize start = m_pos;
    if (peek() == '-') m_pos++;
    if (peek() == '0') {
      m_pos++;
    } else if (at_digit()) {
      while (at_digit())
        m_pos++;
    } else {
      fail("a number has no integer digits");
    }
    if (peek() == '.') {
      m_pos++;
      if (!at_digit()) fail("a number has no fraction digits");
      while (at_digit())
        m_pos++;
    }
    if (peek() == 'e' || peek() == 'E') {
      m_pos++;
      if (peek() == '+' || peek() == '-') m_pos++;
      if (!at_digit()) fail("a number has no exponent digits");
      while (at_digit())
        m_pos++;
    }
    json_value value;
    value.kind = json_value::Kind::Number;
    value.scalar = String{m_text.substring_of_length(start, m_pos - start)};
    return value;
  }

  fn parse_bool() throws -> json_value
  {
    json_value value;
    value.kind = json_value::Kind::Bool;
    if (m_text.substring(m_pos).starts_with("true")) {
      value.boolean = true;
      m_pos += 4;
    } else if (m_text.substring(m_pos).starts_with("false")) {
      value.boolean = false;
      m_pos += 5;
    } else {
      fail("an invalid literal begins with 't' or 'f'");
    }
    return value;
  }

  fn parse_null() throws -> json_value
  {
    if (!m_text.substring(m_pos).starts_with("null"))
      fail("an invalid literal begins with 'n'");
    m_pos += 4;
    return json_value{};
  }
};

static fn json_escape_into(StringView text, String &out) throws -> void
{
  out += '"';
  for (usize i = 0; i < text.length; i++) {
    const char c = text[i];
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\t': out += "\\t"; break;
    case '\r': out += "\\r"; break;
    default: out += c;
    }
  }
  out += '"';
}

static fn serialize_json(const json_value &value, String &out) throws -> void
{
  switch (value.kind) {
  case json_value::Kind::Null: out += "null"; break;
  case json_value::Kind::Bool: out += value.boolean ? "true" : "false"; break;
  case json_value::Kind::Number: out.append(value.scalar.view()); break;
  case json_value::Kind::String:
    json_escape_into(value.scalar.view(), out);
    break;
  case json_value::Kind::Array:
    out += '[';
    for (usize i = 0; i < value.items.count(); i++) {
      if (i > 0) out += ',';
      serialize_json(value.items[i], out);
    }
    out += ']';
    break;
  case json_value::Kind::Object:
    out += '{';
    for (usize i = 0; i < value.keys.count(); i++) {
      if (i > 0) out += ',';
      json_escape_into(value.keys[i].view(), out);
      out += ':';
      serialize_json(value.values[i], out);
    }
    out += '}';
    break;
  }
}

/* The plain text of a scalar, for a table cell or a get result. A nested object
   or array renders as compact JSON so a cell still shows something. */
static fn scalar_text(const json_value &value) throws -> String
{
  switch (value.kind) {
  case json_value::Kind::Null: return String{};
  case json_value::Kind::Bool: return String{value.boolean ? "true" : "false"};
  case json_value::Kind::Number: return String{value.scalar.view()};
  case json_value::Kind::String: return String{value.scalar.view()};
  default: {
    String out{};
    serialize_json(value, out);
    return out;
  }
  }
}

/* A scalar rendered for a table cell, with the control characters flattened to
   a space so a newline or tab in a string value cannot break the row alignment.
   get keeps the raw value, only the table view sanitizes. */
static fn table_cell_text(const json_value &value) throws -> String
{
  let const raw = scalar_text(value);
  String out{};
  for (usize i = 0; i < raw.count(); i++) {
    let const character = raw.view()[i];
    out += (character == '\n' || character == '\t' || character == '\r')
               ? ' '
               : character;
  }
  return out;
}

static fn pad_to(String &out, StringView cell, usize width) throws -> void
{
  out.append(cell);
  for (usize i = cell.length; i < width; i++)
    out += ' ';
}

/* Render the column headers, a separator, and each row aligned to the widest
   cell per column, an ASCII table the way nushell prints one. */
static fn render_aligned(const ArrayList<String> &columns,
                         const ArrayList<ArrayList<String>> &rows) throws
    -> String
{
  ArrayList<usize> widths{};
  widths.reserve(columns.count());
  for (const String &column : columns)
    widths.push(column.view().length);
  for (const ArrayList<String> &row : rows)
    for (usize i = 0; i < row.count() && i < widths.count(); i++)
      if (row[i].view().length > widths[i]) widths[i] = row[i].view().length;

  String out{};
  for (usize i = 0; i < columns.count(); i++) {
    if (i > 0) out += " | ";
    pad_to(out, columns[i].view(), widths[i]);
  }
  out += '\n';
  for (usize i = 0; i < columns.count(); i++) {
    if (i > 0) out += "-+-";
    for (usize j = 0; j < widths[i]; j++)
      out += '-';
  }
  out += '\n';
  for (const ArrayList<String> &row : rows) {
    for (usize i = 0; i < columns.count(); i++) {
      if (i > 0) out += " | ";
      const StringView cell = i < row.count() ? row[i].view() : StringView{""};
      pad_to(out, cell, widths[i]);
    }
    out += '\n';
  }
  return out;
}

static fn render_table(const json_value &value) throws -> String
{
  /* An array of objects becomes a row per element with a column per key. An
     array of scalars becomes a single value column. An object becomes a
     key-and-value table, and a bare scalar prints on its own. */
  if (value.kind == json_value::Kind::Array) {
    bool all_objects = !value.items.is_empty();
    for (const json_value &item : value.items)
      if (item.kind != json_value::Kind::Object) all_objects = false;

    if (all_objects) {
      ArrayList<String> columns{};
      for (const json_value &row : value.items)
        for (const String &key : row.keys) {
          bool seen = false;
          for (const String &column : columns)
            if (column.view() == key.view()) {
              seen = true;
              break;
            }
          if (!seen) columns.push(String{key.view()});
        }
      ArrayList<ArrayList<String>> rows{};
      for (const json_value &row : value.items) {
        ArrayList<String> cells{};
        cells.reserve(columns.count());
        for (const String &column : columns) {
          const json_value *cell = nullptr;
          for (usize k = 0; k < row.keys.count(); k++)
            if (row.keys[k].view() == column.view()) {
              cell = &row.values[k];
              break;
            }
          cells.push(cell != nullptr ? table_cell_text(*cell) : String{});
        }
        rows.push(steal(cells));
      }
      return render_aligned(columns, rows);
    }

    ArrayList<String> columns{};
    columns.push(String{"value"});
    ArrayList<ArrayList<String>> rows{};
    for (const json_value &item : value.items) {
      ArrayList<String> cells{};
      cells.push(table_cell_text(item));
      rows.push(steal(cells));
    }
    return render_aligned(columns, rows);
  }

  if (value.kind == json_value::Kind::Object) {
    ArrayList<String> columns{};
    columns.push(String{"key"});
    columns.push(String{"value"});
    ArrayList<ArrayList<String>> rows{};
    for (usize i = 0; i < value.keys.count(); i++) {
      ArrayList<String> cells{};
      cells.push(String{value.keys[i].view()});
      cells.push(table_cell_text(value.values[i]));
      rows.push(steal(cells));
    }
    return render_aligned(columns, rows);
  }

  return scalar_text(value) + "\n";
}

/* Read everything available on the builtin's input descriptor, the pipe in a
   pipeline or the terminal otherwise. */
static fn read_all_input(ExecContext &ec) throws -> String
{
  const os::descriptor fd = ec.in_fd.value_or(SHIT_STDIN);
  String out{};
  char buffer[4096];
  for (;;) {
    Maybe<usize> read_count = os::read_fd(fd, buffer, sizeof(buffer));
    if (!read_count || *read_count == 0) break;
    out.append(StringView{buffer, *read_count});
  }
  return out;
}

static fn parse_input_json(ExecContext &ec) throws -> json_value
{
  let const input = read_all_input(ec);
  JsonParser parser{input.view()};
  return parser.parse();
}

} /* namespace */

From::From() = default;

pure fn From::kind() const wontthrow -> Builtin::Kind { return Kind::From; }

fn From::execute(ExecContext &ec, EvalContext &) const throws -> i32
{
  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);
  if (ec.args().count() < 2)
    throw Error{"from: expected a format, such as 'from json'"};

  const StringView format = ec.args()[1];
  if (format != "json")
    throw Error{StringView{"from: unsupported format '"} + ec.args()[1] +
                "', only 'json' is supported"};

  json_value value = parse_input_json(ec);
  String out{};
  serialize_json(value, out);
  out += '\n';
  ec.print_to_stdout(out);
  return 0;
}

To::To() = default;

pure fn To::kind() const wontthrow -> Builtin::Kind { return Kind::To; }

fn To::execute(ExecContext &ec, EvalContext &) const throws -> i32
{
  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);
  if (ec.args().count() < 2)
    throw Error{"to: expected a format, such as 'to table' or 'to json'"};

  const StringView format = ec.args()[1];
  json_value value = parse_input_json(ec);

  if (format == "table") {
    ec.print_to_stdout(render_table(value));
    return 0;
  }
  if (format == "json") {
    String out{};
    serialize_json(value, out);
    out += '\n';
    ec.print_to_stdout(out);
    return 0;
  }
  throw Error{StringView{"to: unsupported format '"} + ec.args()[1] +
              "', only 'table' and 'json' are supported"};
}

Get::Get() = default;

pure fn Get::kind() const wontthrow -> Builtin::Kind { return Kind::Get; }

fn Get::execute(ExecContext &ec, EvalContext &) const throws -> i32
{
  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);
  if (ec.args().count() < 2)
    throw Error{"get: expected a dotted path, such as 'get users.0.name'"};

  json_value root = parse_input_json(ec);
  const StringView path = ec.args()[1];

  const json_value *current = &root;
  usize start = 0;
  for (usize i = 0; i <= path.length; i++) {
    if (i != path.length && path[i] != '.') continue;
    const StringView component = path.substring_of_length(start, i - start);
    start = i + 1;
    if (component.is_empty()) continue;

    if (current->kind == json_value::Kind::Object) {
      const json_value *next = nullptr;
      for (usize k = 0; k < current->keys.count(); k++)
        if (current->keys[k].view() == component) {
          next = &current->values[k];
          break;
        }
      if (next == nullptr)
        throw Error{StringView{"get: no field '"} + component +
                    "' in the object"};
      current = next;
    } else if (current->kind == json_value::Kind::Array) {
      const ErrorOr<i64> index = utils::parse_decimal_integer(component);
      if (index.is_error() || index.value() < 0 ||
          static_cast<usize>(index.value()) >= current->items.count())
        throw Error{StringView{"get: '"} + component +
                    "' is not a valid index into the array"};
      current = &current->items[static_cast<usize>(index.value())];
    } else {
      throw Error{StringView{"get: cannot index into a scalar with '"} +
                  component + "'"};
    }
  }

  String out{};
  if (current->kind == json_value::Kind::String ||
      current->kind == json_value::Kind::Number ||
      current->kind == json_value::Kind::Bool ||
      current->kind == json_value::Kind::Null)
  {
    out = scalar_text(*current);
  } else {
    serialize_json(*current, out);
  }
  out += '\n';
  ec.print_to_stdout(out);
  return 0;
}

} /* namespace shit */
