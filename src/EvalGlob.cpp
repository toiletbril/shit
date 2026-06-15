#include "Arena.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

/* The pathname expansion of the evaluator, the per-component glob walk, the
   globstar recursion, the tilde expansions, and the lenient compgen -G
   probe. Split out of Eval.cpp so the evaluator core stays the hot-path
   file. */

namespace shit {

fn EvalContext::expand_path_once(const glob_field &field,
                                 bool should_expand_files) throws
    -> ArrayList<glob_field>
{
  let const scratch = scratch_allocator();
  let expanded = ArrayList<glob_field>{scratch};

  /* This runs only for a field that holds a real glob, which is rare. The path
     text is split on its last separator into a parent directory and the glob
     stem. */
  let const path = field.text.view();
  LOG(All, "scanning a directory for the glob component '%.*s'",
      static_cast<int>(path.length), path.data);

  let last_slash = Maybe<usize>{};
  for (usize i = path.length; i > 0; i--)
    if (path[i - 1] == '/') {
      last_slash = i - 1;
      break;
    }
  let const has_slashes = last_slash.has_value();

  let parent_dir = Path{};
  if (has_slashes)
    parent_dir =
        Path{*last_slash != 0 ? path.substring_of_length(0, *last_slash)
                              : path.substring_of_length(0, 1)};
  else
    parent_dir = Path{StringView{"."}};

  /* Stem of the glob after the last slash. Its mask starts at stem_start in the
     field, so glob_matches reads field.glob_active from there. */
  let const stem_start = has_slashes ? *last_slash + 1 : 0;
  let const has_glob = stem_start < path.length;
  let glob = StringView{};
  if (has_glob) glob = path.substring(stem_start);

  /* A missing or unreadable parent directory yields no match the way dash
     treats it, so the caller applies the failglob policy and the pattern stays
     literal under failglob-off rather than raising an error here. */
  let const entries = Path::read_directory(parent_dir);
  if (!entries.has_value()) {
    LOG(Debug,
        "the parent directory is unreadable, the glob '%.*s' yields no match",
        static_cast<int>(path.length), path.data);
    return expanded;
  }

  if (!has_glob) {
    let copy = glob_field{scratch};
    copy.text.append(field.text.view());
    copy.glob_active = field.glob_active;
    expanded.push(steal(copy));
    return expanded;
  }

  /* The typed prefix is everything up to and through the last slash, such as
     dot-slash or a directory name with a slash. It is preserved on each match
     the way dash keeps the directory the user wrote, so a dot-slash glob yields
     a dot-slash result rather than a bare filename. A bare glob with no slash
     has an empty prefix and a synthetic dot parent, so the match carries the
     filename alone. */
  let const typed_prefix =
      has_slashes ? path.substring_of_length(0, stem_start) : StringView{};

  /* The no-glob field returned above, so the stem is a non-empty glob here and
     glob[0] reads a real byte. */
  ASSERT(has_glob);
  ASSERT(!glob.is_empty());

  for (const String &entry_name : *entries) {
    let const filename = entry_name.view();

    /* The full path joins the parent and the filename, the way the directory
       walk needs it for the is_directory test and the result text. */
    let full_path = parent_dir;
    full_path.push_component(filename);

    if (!should_expand_files && !full_path.is_directory()) continue;

    /* A hidden entry matches only a pattern that itself starts with a dot,
       the POSIX rule the dotglob tests record. */
    if (glob[0] != '.' && !filename.is_empty() && filename[0] == '.') continue;

    if (utils::glob_matches(glob, filename, field.glob_active, stem_start,
                            extglob_enabled()))
    {
      add_expansion();

      /* A real filename is literal, so the resulting field never globs again.
         The empty mask is the all-literal convention, so it carries no
         per-result allocation. The typed prefix joins the filename directly
         rather than through the Path join, so the user's exact "./" or "dir/"
         survives instead of a normalized form. */
      let result_field = glob_field{scratch};
      result_field.text.append(typed_prefix);
      result_field.text.append(filename);
      expanded.push(steal(result_field));
    }
  }

  return expanded;
}

namespace {

/* The index of the first active metacharacter that actually forms a glob. A '['
   without a later ']' is a literal bracket, not a glob, so a field such as the
   command word '[' needs no directory scan at all. Returns nullopt when the
   field is all literal. */
hot pure fn first_active_glob(StringView text, const ArrayList<bool> &mask,
                              bool extglob) wontthrow -> Maybe<usize>
{
  let open_bracket = Maybe<usize>{};
  for (usize i = 0; i < mask.count(); i++) {
    let const ch = text.data[i];
    /* An extended-glob opener such as @( forces a directory scan even though
       the opener byte is not a metacharacter in the mask, since the
       alternatives it holds match real names. The structure is read from the
       text the way the matcher reads it. */
    if (extglob && i + 1 < text.length &&
        (ch == '?' || ch == '*' || ch == '+' || ch == '@' || ch == '!') &&
        text.data[i + 1] == '(')
      return i;
    if (!mask[i]) continue;
    if (ch == '*' || ch == '?') return i;
    if (ch == '[') {
      if (!open_bracket) open_bracket = i;
    } else if (ch == ']' && open_bracket) {
      return open_bracket;
    }
  }
  return shit::None;
}

/* The recursion depth cap for a ** walk. A real directory tree is far
   shallower, so the cap only stops a symlink cycle from looping forever, since
   the path layer cannot tell a symlinked directory from a real one. */
constexpr usize GLOBSTAR_MAX_DEPTH = 256;

/* Collect the relative paths a ** component expands to, walking dir and its
   subdirectories. In directory position only subdirectories are collected and
   the base is added as the empty path so ** can match zero levels. As a
   trailing component every file and directory is collected, which are the final
   matches. A hidden entry is skipped the way bash globstar skips a dotfile. */
fn collect_globstar_paths(const Path &dir, StringView relative,
                          bool directories_only, bool include_base, usize depth,
                          Allocator allocator, ArrayList<String> &out) throws
    -> void
{
  LOG(All,
      "collecting globstar paths under the relative path '%.*s', depth %zu",
      static_cast<int>(relative.length), relative.data, depth);
  if (directories_only && include_base) out.push(String{allocator, relative});
  if (depth >= GLOBSTAR_MAX_DEPTH) return;

  let const entries = Path::read_directory(dir);
  if (!entries.has_value()) return;

  for (const String &entry : *entries) {
    let const name = entry.view();
    if (!name.is_empty() && name[0] == '.') continue;

    let child_dir = dir;
    child_dir.push_component(name);
    let const is_dir = child_dir.is_directory();

    let child_relative = String{allocator};
    if (!relative.is_empty()) {
      child_relative.append(relative);
      child_relative += '/';
    }
    child_relative.append(name);

    if (!directories_only || is_dir)
      out.push(String{allocator, child_relative.view()});
    if (is_dir)
      collect_globstar_paths(child_dir, child_relative.view(), directories_only,
                             false, depth + 1, allocator, out);
  }
}

} /* namespace */

fn EvalContext::expand_path_recurse(ArrayList<glob_field> fields) throws
    -> ArrayList<glob_field>
{
  let const scratch = scratch_allocator();
  let result = ArrayList<glob_field>{scratch};

  for (glob_field &field : fields) {
    let const text = field.text.view();

    /* An empty mask is the all-literal convention, so a field without one holds
       no live glob metacharacter. */
    let const expand_ch =
        first_active_glob(text, field.glob_active, extglob_enabled());

    if (!expand_ch) {
      /* No glob remains. This field is a literal suffix appended after an
         earlier glob, so keep it only when it actually exists. A path produced
         purely by globbing came from a directory read and always exists, so it
         never reaches here and pays no stat. */
      if (Path{field.text.view()}.exists()) result.push(steal(field));
      continue;
    }

    /* An active glob index came from the mask, so it points inside the text and
       the field carries a mask parallel to the text. */
    ASSERT(*expand_ch < text.length);
    ASSERT(field.glob_active.count() == text.length);

    let slash_after = Maybe<usize>{};
    for (usize k = *expand_ch; k < text.length; k++) {
      if (text.data[k] == '/') {
        slash_after = k;
        break;
      }
    }

    /* A ** component matches across directory levels when globstar is on. The
       component runs from the slash before the glob to the slash after it, and
       it is the globstar form only when it is exactly two active stars. */
    usize component_start = 0;
    for (usize k = *expand_ch; k > 0; k--)
      if (text.data[k - 1] == '/') {
        component_start = k;
        break;
      }
    let const component_end = slash_after.value_or(text.length);
    /* The two-byte shape test runs first, so the common single-star field
       never pays the shopt lookup. */
    let const is_globstar_component = component_end - component_start == 2 &&
                                      text.data[component_start] == '*' &&
                                      text.data[component_start + 1] == '*' &&
                                      field.glob_active[component_start] &&
                                      field.glob_active[component_start + 1] &&
                                      is_shopt_enabled("globstar");

    if (is_globstar_component) {
      LOG(All, "expanding a globstar component across directory levels");
      let const prefix = text.substring_of_length(0, component_start);
      let base = Path{StringView{"."}};
      if (component_start == 1)
        base = Path{StringView{"/"}};
      else if (component_start > 1)
        base = Path{text.substring_of_length(0, component_start - 1)};

      let const directory_position = slash_after.has_value();
      let relatives = ArrayList<String>{scratch};
      collect_globstar_paths(base, StringView{""}, directory_position, true, 0,
                             scratch, relatives);

      /* As a trailing ** each collected file or directory is a final match, the
         prefix the user wrote joined to the relative path. The base directory
         itself is the zero-level match, emitted as the bare prefix, and skipped
         when the prefix is empty so a bare ** does not yield the current
         directory. */
      if (!directory_position) {
        if (!prefix.is_empty()) {
          let base_field = glob_field{scratch};
          base_field.text.append(prefix);
          result.push(steal(base_field));
        }
        for (const String &relative : relatives) {
          let match_field = glob_field{scratch};
          match_field.text.append(prefix);
          match_field.text.append(relative.view());
          result.push(steal(match_field));
        }
        continue;
      }

      /* In a directory position the globstar stands in for zero or more levels,
         so the suffix after it is matched in the base directory and in every
         descendant directory. The empty relative is the zero-level case, which
         collapses the star pair and its slash to nothing rather than inserting
         a stray slash. */
      let const suffix = text.substring(*slash_after + 1);
      let rebuilt = ArrayList<glob_field>{scratch};
      for (const String &relative : relatives) {
        let candidate = glob_field{scratch};
        candidate.text.append(prefix);
        if (!relative.view().is_empty()) {
          candidate.text.append(relative.view());
          candidate.text += '/';
        }
        let const literal_length = candidate.text.count();
        candidate.text.append(suffix);
        if (candidate.text.is_empty()) continue;
        for (usize k = 0; k < literal_length; k++)
          candidate.glob_active.push(false);
        for (usize k = *slash_after + 1; k < field.glob_active.count(); k++)
          candidate.glob_active.push(field.glob_active[k]);
        rebuilt.push(steal(candidate));
      }
      let recursed = expand_path_recurse(steal(rebuilt));
      for (glob_field &f : recursed)
        result.push(steal(f));
      continue;
    }

    /* The glob is the last component, so expand it against files and emit the
       matches as is. */
    if (!slash_after) {
      let once = expand_path_once(field, true);
      for (glob_field &f : once)
        result.push(steal(f));
      continue;
    }

    /* Split off the first globbed directory component and the literal-or-glob
       suffix after it, building each from a substring rather than copying the
       whole field. */
    let const slash_offset = static_cast<std::ptrdiff_t>(*slash_after);
    let operating = glob_field{scratch};
    operating.text.append(StringView{text.data, *slash_after});
    for (std::ptrdiff_t k = 0; k < slash_offset; k++)
      operating.glob_active.push(field.glob_active[static_cast<usize>(k)]);
    let removed_suffix = glob_field{scratch};
    removed_suffix.text.append(
        StringView{text.data + *slash_after, text.length - *slash_after});
    for (usize k = static_cast<usize>(slash_offset);
         k < field.glob_active.count(); k++)
      removed_suffix.glob_active.push(field.glob_active[k]);

    let once = expand_path_once(operating, false);

    /* Bring back the removed suffix and recurse on the expanded entries. Each
       match came back all-literal with an empty mask, so restore its false
       entries before the suffix mask to keep the mask aligned with the text. */
    for (glob_field &f : once) {
      let const matched_length = f.text.count();
      f.text.append(removed_suffix.text.view());
      f.glob_active.clear();
      for (usize k = 0; k < matched_length; k++)
        f.glob_active.push(false);
      for (usize k = 0; k < removed_suffix.glob_active.count(); k++)
        f.glob_active.push(removed_suffix.glob_active[k]);
    }

    /* The recurse validates each level through the directory read or, for a
       literal suffix, the existence check above, so no extra stat is needed
       here. */
    let twice = expand_path_recurse(steal(once));
    for (glob_field &f : twice)
      result.push(steal(f));
  }

  return result;
}

fn EvalContext::expand_tilde(WordSegment &leading_segment,
                             bool word_continues) const throws -> void
{
  /* A tilde only expands when it is unquoted. An escaped or quoted tilde is a
     literal segment and stays as is. */
  if (!leading_segment.is_tilde_candidate()) return;

  let &text = leading_segment.text;
  if (text.is_empty() || text[0] != '~') return;

  /* The user name runs from after the ~ to the first / or the end of the word,
     so ~ and ~/path carry an empty name while ~user and ~user/path carry the
     name. */
  usize name_end = 1;
  while (name_end < text.length() && text[name_end] != '/')
    name_end++;
  let const name = text.view().substring_of_length(1, name_end - 1);

  /* A tilde prefix that runs to the segment's end while the word goes on in a
     later segment carries a quoted or escaped character inside the prefix, so
     bash leaves the whole word literal, the way ~ch\et and ~chet""/bar
     stay as written. */
  if (name_end == text.length() && word_continues) return;

  let const directory = resolve_tilde_prefix(name);
  if (name.is_empty() && !directory)
    throw Error{"Could not figure out home directory"};
  if (!directory) return;

  LOG(All, "the tilde prefix '~%.*s' expands to '%.*s'",
      static_cast<int>(name.length), name.data,
      static_cast<int>(directory->view().length), directory->view().data);
  /* String has no in-place erase or insert, so the directory and the
     remainder after the name are joined into a fresh buffer and moved back. */
  let expanded = String{heap_allocator()};
  expanded.append(directory->view());
  expanded.append(text.view().substring(name_end));
  text = steal(expanded);
}

fn EvalContext::resolve_tilde_prefix(StringView name) const throws
    -> Maybe<String>
{
  /* ~+ is PWD and ~- is OLDPWD the way bash reads them, with an unset name
     leaving the prefix literal. */
  if (name == "+" || name == "-")
    return get_variable_value(name == "+" ? StringView{"PWD"}
                                          : StringView{"OLDPWD"});
  /* An empty name is the bare ~, which resolves to the current home. A named
     user resolves through the system database, and an unknown name yields
     nothing the way bash leaves ~baduser literal. */
  let const home =
      name.is_empty() ? os::get_home_directory() : os::get_home_for_user(name);
  if (!home) return shit::None;
  return String{heap_allocator(), home->text().view()};
}

fn EvalContext::expand_colon_tildes(WordSegment &segment,
                                    bool word_continues) const throws -> void
{
  if (!segment.is_tilde_candidate()) return;
  let const view = segment.text.view();
  let rewritten = String{heap_allocator()};
  let changed = false;
  usize i = 0;
  while (i < view.length) {
    if (view[i] == ':' && i + 1 < view.length && view[i + 1] == '~') {
      usize prefix_end = i + 2;
      while (prefix_end < view.length && view[prefix_end] != '/' &&
             view[prefix_end] != ':')
        prefix_end++;
      /* A prefix that runs to the segment's end while the word goes on in a
         later segment carries a quoted character, so it stays literal the way
         the leading prefix does. */
      if (!(prefix_end == view.length && word_continues)) {
        let const name = view.substring_of_length(i + 2, prefix_end - i - 2);
        if (let const directory = resolve_tilde_prefix(name)) {
          rewritten += ':';
          rewritten.append(directory->view());
          i = prefix_end;
          changed = true;
          continue;
        }
      }
    }
    rewritten += view[i];
    i++;
  }
  if (changed) {
    LOG(All, "rewrote colon tilde prefixes in an assignment value");
    segment.text = steal(rewritten);
  }
}

hot fn EvalContext::expand_path(glob_field field,
                                SourceLocation location) throws
    -> ArrayList<String>
{
  let const scratch = scratch_allocator();

  /* Fast path. A field with no glob that actually matches paths is its own
     single result, so it skips the recursion, the directory scan, and every
     copy. A bare command word such as '[' lands here instead of scanning the
     current directory. */
  let const has_glob =
      m_enable_path_expansion &&
      first_active_glob(field.text.view(), field.glob_active, extglob_enabled())
          .has_value();

  if (!has_glob) {
    let single = ArrayList<String>{scratch};
    single.push(steal(field.text));
    return single;
  }

  /* The pattern is kept so a glob that matches None falls back to it, since
     the field moves into the recurse. */
  let pattern = String{scratch};
  pattern.append(field.text.view());

  let input = ArrayList<glob_field>{scratch};
  input.push(steal(field));
  let fields = expand_path_recurse(steal(input));

  let values = ArrayList<String>{scratch};
  for (glob_field &f : fields)
    values.push(steal(f.text));

  /* Sort the matches in byte order, which is the POSIX collating order in the C
     locale and what dash produces. A plain compare also keeps a large expansion
     from spending most of its time in the sort comparator. */
  utils::sort_ascending(values);

  LOG(All, "the glob pattern '%s' matched %zu paths", pattern.c_str(),
      values.count());

  /* A glob that matches no file is a hard error by default, the typo-catching
     behavior. With failglob off the shell takes the POSIX fallback and expands
     the glob to its literal pattern as a single field, the way dash does. The
     caret points at the offending word. A test or [ command is exempt, so a
     glob used to probe whether a file exists keeps its literal text in silence
     and the probe returns false rather than aborting the command. */
  if (values.count() == 0) {
    /* The test exemption keeps the literal fallback in silence. Otherwise the
       failglob strictness throws, or -W downgrades it to a warning unless the
       set -o failglob was the script's explicit ask. */
    if (!m_glob_exempt_for_test)
      warn_or_throw(m_runtime.failglob, m_runtime.failglob_explicit, location,
                    "The glob pattern '" + pattern +
                        "' matched no file, it expands to its literal text, "
                        "which is rarely intended. Probe for matches with "
                        "compgen -G '" +
                        pattern + "' or relax with set +o failglob");
    values.push(steal(pattern));
  }

  return values;
}

/* The compgen -G probe, a glob expansion that reports matches and never trips
   failglob. A pattern with no metacharacter names one file, so it reports
   that file only when it exists, the way bash compgen -G does. set -f does not
   apply, since the caller asks for the expansion explicitly. The matches land
   on the scratch arena and die with the command. */
fn EvalContext::expand_glob_lenient(StringView pattern) throws
    -> ArrayList<String>
{
  let const scratch = scratch_allocator();
  let values = ArrayList<String>{scratch};

  let field = glob_field{scratch};
  field.text.append(pattern);
  for (usize i = 0; i < pattern.length; i++)
    field.glob_active.push(true);

  if (!first_active_glob(field.text.view(), field.glob_active,
                         extglob_enabled())
           .has_value())
  {
    LOG(Debug, "compgen -G probe of '%.*s' has no glob, checking existence",
        static_cast<int>(pattern.length), pattern.data);
    if (Path{pattern}.exists()) values.push(String{scratch, pattern});
    return values;
  }

  let input = ArrayList<glob_field>{scratch};
  input.push(steal(field));
  for (glob_field &f : expand_path_recurse(steal(input)))
    values.push(steal(f.text));
  utils::sort_ascending(values);
  LOG(Debug, "compgen -G probe matched %zu paths", values.count());
  return values;
}

} /* namespace shit */
