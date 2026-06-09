#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

#include <ctime>

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[query ...]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace {

/* The store keeps at most this many directories, so a long-lived shell does not
   grow ~/.shit_dirs without bound and the per-cd rewrite stays cheap. */
constexpr usize Z_FRECENCY_MAX = 500;

/* One directory in the frecency store, its visit count and the epoch second of
   the last visit. The score weights the count by how recently it was seen. */
struct frecency_entry
{
  String path;
  i64 rank;
  i64 last_access;
};

/* The store lives at ~/.shit_dirs. None when the home directory is unknown. */
static fn frecency_store_path() throws -> Maybe<Path>
{
  Maybe<Path> home = os::get_home_directory();
  if (!home) return None;
  Path path = *home;
  path.push_component(".shit_dirs");
  return path;
}

static fn now_epoch_seconds() wontthrow -> i64
{
  return static_cast<i64>(std::time(nullptr));
}

/* The frecency weight zoxide uses, a recent visit counts for much more than an
   old one, so a directory seen this hour outranks one seen last month. */
static fn recency_weight(i64 age_seconds) wontthrow -> double
{
  if (age_seconds < 3600) return 4.0;
  if (age_seconds < 86400) return 2.0;
  if (age_seconds < 604800) return 0.5;
  return 0.25;
}

static fn read_frecency_store() throws -> ArrayList<frecency_entry>
{
  ArrayList<frecency_entry> entries{};
  Maybe<Path> path = frecency_store_path();
  if (!path) return entries;
  Maybe<String> content = utils::read_entire_file(path->text().view());
  if (!content) return entries;

  let const text = content->view();
  usize line_start = 0;
  for (usize i = 0; i <= text.length; i++) {
    if (i != text.length && text[i] != '\n') continue;
    let const line = text.substring_of_length(line_start, i - line_start);
    line_start = i + 1;
    if (line.is_empty()) continue;

    /* Each line is path, rank, and last-access, tab separated. A malformed line
       is skipped rather than failing the whole read. */
    Maybe<usize> first_tab = line.find_character('\t');
    if (!first_tab) continue;
    let const after_path = line.substring(*first_tab + 1);
    Maybe<usize> second_tab = after_path.find_character('\t');
    if (!second_tab) continue;

    let const path_field = line.substring_of_length(0, *first_tab);
    let const rank_field = after_path.substring_of_length(0, *second_tab);
    let const time_field = after_path.substring(*second_tab + 1);
    let const rank = utils::parse_decimal_integer(rank_field);
    let const last = utils::parse_decimal_integer(time_field);
    if (rank.is_error() || last.is_error()) continue;
    entries.push(frecency_entry{String{path_field}, rank.value(), last.value()});
  }
  return entries;
}

static fn write_frecency_store(const ArrayList<frecency_entry> &entries) throws
    -> void
{
  Maybe<Path> path = frecency_store_path();
  if (!path) return;

  String out{};
  for (const frecency_entry &entry : entries) {
    out.append(entry.path.view());
    out += '\t';
    out.append(utils::int_to_text(entry.rank));
    out += '\t';
    out.append(utils::int_to_text(entry.last_access));
    out += '\n';
  }

  Maybe<os::descriptor> fd =
      os::open_file_descriptor(path->text().view(), os::file_open_mode::Truncate);
  if (!fd) return;
  /* The store was just truncated, so a short write would drop entries. The write
     loops until the whole buffer lands or the descriptor stops accepting it. */
  usize total_written = 0;
  while (total_written < out.count()) {
    Maybe<usize> written =
        os::write_fd(*fd, out.c_str() + total_written, out.count() - total_written);
    if (!written || *written == 0) break;
    total_written += *written;
  }
  os::close_fd(*fd);
}

/* A case-insensitive substring test, so z dot matches Dotfiles. */
static fn contains_ignore_case(StringView haystack, StringView needle) wontthrow
    -> bool
{
  if (needle.is_empty()) return true;
  if (needle.length > haystack.length) return false;
  let const lower = [](char c) wontthrow -> char {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
  };
  for (usize i = 0; i + needle.length <= haystack.length; i++) {
    bool matched = true;
    for (usize j = 0; j < needle.length; j++) {
      if (lower(haystack[i + j]) != lower(needle[j])) {
        matched = false;
        break;
      }
    }
    if (matched) return true;
  }
  return false;
}

} /* namespace */

fn record_directory_access(StringView directory) throws -> void
{
  if (directory.is_empty()) return;

  ArrayList<frecency_entry> entries = read_frecency_store();
  const i64 now = now_epoch_seconds();
  bool found = false;
  for (frecency_entry &entry : entries) {
    if (entry.path.view() == directory) {
      entry.rank += 1;
      entry.last_access = now;
      found = true;
      break;
    }
  }
  if (!found) {
    entries.push(frecency_entry{String{directory}, 1, now});
    /* When the store overflows its bound, the least-visited directory is
       dropped by overwriting it with the last entry and shrinking, since the
       order of the store does not matter to the ranking. */
    if (entries.count() > Z_FRECENCY_MAX) {
      usize weakest = 0;
      for (usize i = 1; i < entries.count(); i++)
        if (entries[i].rank < entries[weakest].rank) weakest = i;
      entries[weakest] = steal(entries[entries.count() - 1]);
      entries.pop_back();
    }
  }
  write_frecency_store(entries);
}

Z::Z() = default;

pure fn Z::kind() const wontthrow -> Builtin::Kind { return Kind::Z; }

fn Z::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* The query is every operand joined by a space, so z proj src matches a path
     that holds both, the way zoxide takes several keywords. */
  String query{};
  for (usize i = 1; i < ec.args().count(); i++) {
    if (i > 1) query += ' ';
    query.append(ec.args()[i]);
  }

  ArrayList<frecency_entry> entries = read_frecency_store();
  const i64 now = now_epoch_seconds();

  const frecency_entry *best = nullptr;
  double best_score = -1.0;
  for (const frecency_entry &entry : entries) {
    if (!query.is_empty() && !contains_ignore_case(entry.path.view(), query.view()))
      continue;
    const double score = static_cast<double>(entry.rank) *
                         recency_weight(now - entry.last_access);
    if (score > best_score) {
      best_score = score;
      best = &entry;
    }
  }

  if (best == nullptr)
    throw Error{StringView{"z: no matching directory for '"} + query + "'"};

  Path target = Path{best->path.view()}.to_absolute().normalized();
  if (!target.is_directory())
    throw Error{StringView{"z: '"} + best->path + "' is no longer a directory"};

  const Path old_directory = Path::current_directory();
  if (Path::set_current_directory(target).is_error())
    throw Error{StringView{"z: could not cd to '"} + target.text() + "'"};
  utils::invalidate_path_cache();
  if (!old_directory.is_empty())
    cxt.set_shell_variable("OLDPWD", old_directory.text());
  cxt.set_shell_variable("PWD", target.text());
  record_directory_access(target.text().view());

  ec.print_to_stdout(target.text() + "\n");
  return 0;
}

} /* namespace shit */
