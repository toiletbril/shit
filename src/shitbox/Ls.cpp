#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Shitbox.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-aA1lh] [path ...]");

HELP_DESCRIPTION_DECL(
    "The ls utility lists the names in each directory operand, or the current "
    "directory when none is given. The default lays the names out in columns "
    "sized to the terminal, and falls back to one name per line when the "
    "output "
    "is not a terminal.");

FLAG(LS_ALL, Bool, 'a', "", "List entries whose name starts with a dot.");
FLAG(LS_ALMOST_ALL, Bool, 'A', "",
     "List dot entries but not the . and .. directory entries.");
FLAG(LS_ONE, Bool, '1', "", "List one entry per line.");
FLAG(LS_LONG, Bool, 'l', "",
     "Print the mode, owner, group, size, and time before each name.");
FLAG(LS_HUMAN, Bool, 'h', "",
     "With -l, print the size in a human-readable form such as 4.0K.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Ls);

namespace shit {

namespace shitbox {

/* The gap between two columns in the default grid, the two trailing spaces
   coreutils leaves after each name. */
static constexpr usize COLUMN_GAP = 2;

/* The blocks ride along so the total line sums them without a second stat. */
struct long_entry
{
  String mode_string{};
  String link_count{};
  String owner{};
  String group{};
  String size{};
  String time{};
  String name{};
  u64 blocks{0};
};

/* One numeric id resolved to its name once, so a listing with many entries
   owned by the same user or group reads the passwd or group file once per
   distinct id rather than once per entry. */
struct id_name_entry
{
  u32 id{0};
  String name{};
};

static fn cached_owner_name(u32 uid, ArrayList<id_name_entry> &cache) throws
    -> String
{
  for (const id_name_entry &entry : cache)
    if (entry.id == uid) return entry.name.clone();
  let const looked_up = os::uid_to_username(uid);
  String name = looked_up.has_value() ? *looked_up : utils::uint_to_text(uid);
  cache.push(id_name_entry{uid, name.clone()});
  return name;
}

static fn cached_group_name(u32 gid, ArrayList<id_name_entry> &cache) throws
    -> String
{
  for (const id_name_entry &entry : cache)
    if (entry.id == gid) return entry.name.clone();
  let const looked_up = os::gid_to_groupname(gid);
  String name = looked_up.has_value() ? *looked_up : utils::uint_to_text(gid);
  cache.push(id_name_entry{gid, name.clone()});
  return name;
}

static fn append_padded(String &output, StringView field, usize width,
                        bool pad_on_left) throws -> void
{
  if (pad_on_left)
    for (usize i = field.length; i < width; i++)
      output += ' ';
  output += field;
  if (!pad_on_left)
    for (usize i = field.length; i < width; i++)
      output += ' ';
}

/* A path that cannot be stat'd renders a sparse row so the listing still names
   it. */
static fn build_long_entry(const Path &path, StringView name,
                           ArrayList<id_name_entry> &uid_cache,
                           ArrayList<id_name_entry> &gid_cache) throws
    -> long_entry
{
  long_entry entry{};
  entry.name = String{name};

  os::file_status status{};
  if (!os::stat_path(path.text().view(), status)) {
    entry.mode_string = "??????????";
    entry.link_count = "?";
    entry.owner = "?";
    entry.group = "?";
    entry.size = "?";
    entry.time = "?";
    return entry;
  }

  entry.mode_string = os::format_mode_string(status.mode);
  entry.link_count = utils::uint_to_text(status.link_count);
  entry.owner = cached_owner_name(status.owner_id, uid_cache);
  entry.group = cached_group_name(status.group_id, gid_cache);
  entry.size = FLAG_LS_HUMAN.is_enabled() ? format_human_size(status.size)
                                          : utils::uint_to_text(status.size);
  entry.time =
      utils::format_unix_timestamp(status.modification_time, "%b %e %H:%M");
  entry.blocks = status.blocks;
  return entry;
}

/* The widths are computed across the whole set first, so every row lines up the
   way the coreutils long listing does. */
static fn render_long_entries(const ArrayList<long_entry> &entries,
                              String &output) throws -> void
{
  usize link_width = 0;
  usize owner_width = 0;
  usize group_width = 0;
  usize size_width = 0;
  for (const long_entry &entry : entries) {
    if (entry.link_count.count() > link_width)
      link_width = entry.link_count.count();
    if (entry.owner.count() > owner_width) owner_width = entry.owner.count();
    if (entry.group.count() > group_width) group_width = entry.group.count();
    if (entry.size.count() > size_width) size_width = entry.size.count();
  }

  for (const long_entry &entry : entries) {
    output += entry.mode_string.view();
    output += ' ';
    append_padded(output, entry.link_count.view(), link_width, true);
    output += ' ';
    append_padded(output, entry.owner.view(), owner_width, false);
    output += ' ';
    append_padded(output, entry.group.view(), group_width, false);
    output += ' ';
    append_padded(output, entry.size.view(), size_width, true);
    output += ' ';
    output += entry.time.view();
    output += ' ';
    output += entry.name.view();
    output += '\n';
  }
}

static fn column_width(const ArrayList<usize> &widths, usize column_index,
                       usize rows) wontthrow -> usize
{
  const usize count = widths.count();
  usize widest = 0;
  for (usize r = 0; r < rows; r++) {
    const usize index = column_index * rows + r;
    if (index < count && widths[index] > widest) widest = widths[index];
  }
  return widest;
}

/* The grid packs column by column the way coreutils fills it down then across.
   One name per line when the output is not a terminal or -1 is given. */
static fn render_columns(const ArrayList<StringView> &names,
                         String &output) throws -> void
{
  const usize count = names.count();
  if (count == 0) return;

  u32 terminal_columns = 0;
  u32 terminal_rows = 0;
  const bool is_terminal = os::terminal_size(terminal_columns, terminal_rows);
  if (FLAG_LS_ONE.is_enabled() || !is_terminal) {
    for (const StringView &name : names) {
      output += name;
      output += '\n';
    }
    return;
  }
  const usize terminal_width = terminal_columns;

  ArrayList<usize> widths{};
  widths.reserve(count);
  for (const StringView &name : names)
    widths.push(name.length);

  /* The largest column count whose total width still fits wins, found by trying
     each count from many down to one. A column-major grid puts the entry at
     column*rows+row, so the search reads the widths in that order. */
  const usize max_columns = count < terminal_width ? count : terminal_width;
  usize best_columns = 1;
  for (usize columns = max_columns; columns >= 1; columns--) {
    const usize rows = (count + columns - 1) / columns;
    usize total = 0;
    for (usize c = 0; c < columns; c++) {
      total += column_width(widths, c, rows);
      if (c + 1 < columns) total += COLUMN_GAP;
    }
    if (total <= terminal_width) {
      best_columns = columns;
      break;
    }
    if (columns == 1) break;
  }

  const usize rows = (count + best_columns - 1) / best_columns;
  ArrayList<usize> column_widths{};
  column_widths.reserve(best_columns);
  for (usize c = 0; c < best_columns; c++)
    column_widths.push(column_width(widths, c, rows));

  for (usize r = 0; r < rows; r++) {
    for (usize c = 0; c < best_columns; c++) {
      const usize index = c * rows + r;
      if (index >= count) continue;
      output += names[index];
      /* The last filled column on the row needs no trailing pad, so the
         padding is added only when a further column holds an entry. */
      const bool has_next =
          (c + 1 < best_columns) && ((c + 1) * rows + r < count);
      if (has_next)
        for (usize p = widths[index]; p < column_widths[c] + COLUMN_GAP; p++)
          output += ' ';
    }
    output += '\n';
  }
}

/* coreutils prints the total in 1K blocks, summed from the 512-byte block
   counts the entries carry, hence the divide by two. */
static fn long_total_blocks(const ArrayList<long_entry> &entries) throws
    -> String
{
  u64 total_512_blocks = 0;
  for (const long_entry &entry : entries)
    total_512_blocks += entry.blocks;
  return "total " + utils::uint_to_text(total_512_blocks / 2);
}

Ls::Ls() = default;

pure fn Ls::kind() const wontthrow -> Utility::Kind { return Kind::Ls; }

fn Ls::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args) const throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  ArrayList<StringView> targets{};
  if (operands.is_empty())
    targets.push(StringView{"."});
  else
    for (const String &operand : operands)
      targets.push(operand.view());

  let output = String{};
  let const should_print_headers = targets.count() > 1;
  /* The owner and the group caches outlive the target loop, so a multi-target
     long listing resolves each distinct id once across every directory. */
  ArrayList<id_name_entry> uid_cache{};
  ArrayList<id_name_entry> gid_cache{};
  i32 status = 0;
  for (usize t = 0; t < targets.count(); t++) {
    let const target = targets[t];
    let const path = Path{target};
    if (!path.exists()) {
      report_soft_shitbox_error(ec, cxt,
                                "ls: cannot access '" + String{target} +
                                    "': no such file or directory");
      status = 2;
      continue;
    }

    /* A file operand prints its own row rather than its contents, the long row
       under -l and the bare name otherwise. */
    if (!path.is_directory()) {
      if (FLAG_LS_LONG.is_enabled()) {
        ArrayList<long_entry> file_entries{};
        file_entries.push(build_long_entry(path, target, uid_cache, gid_cache));
        render_long_entries(file_entries, output);
      } else {
        output += target;
        output += '\n';
      }
      continue;
    }

    Maybe<ArrayList<String>> names = Path::read_directory(path);
    if (!names.has_value()) {
      report_soft_shitbox_error(
          ec, cxt, "ls: cannot open directory '" + String{target} + "'");
      status = 2;
      continue;
    }
    sort_string_list(*names);

    if (should_print_headers) {
      if (t > 0) output += '\n';
      output += target;
      output += ":\n";
    }

    /* -a shows every entry including the dot files, -A shows the dot files but
       not the . and .. directory entries, and the plain listing hides any dot
       name. */
    let const is_showing_all = FLAG_LS_ALL.is_enabled();
    let const is_showing_dot_names =
        is_showing_all || FLAG_LS_ALMOST_ALL.is_enabled();

    ArrayList<StringView> visible_names{};
    /* The dot and dot-dot entries are not in the directory read, so -a adds
       them up front the way coreutils lists them before the rest, while -A
       leaves them out. */
    if (is_showing_all) {
      visible_names.push(StringView{"."});
      visible_names.push(StringView{".."});
    }
    for (const String &name : *names) {
      if (!is_showing_dot_names && !name.is_empty() && name.view()[0] == '.')
        continue;
      visible_names.push(name.view());
    }

    if (FLAG_LS_LONG.is_enabled()) {
      ArrayList<long_entry> entries{};
      entries.reserve(visible_names.count());
      for (const StringView &name : visible_names) {
        let const child = PathBuilder{target}.append(name).build();
        entries.push(build_long_entry(child, name, uid_cache, gid_cache));
      }
      output += long_total_blocks(entries);
      output += '\n';
      render_long_entries(entries, output);
    } else {
      render_columns(visible_names, output);
    }
  }

  ec.print_to_stdout(output);
  return status;
}

} // namespace shitbox

} // namespace shit
