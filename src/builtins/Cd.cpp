#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[dir]");

HELP_DESCRIPTION_DECL("The cd builtin changes the working directory.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Cd);

namespace shit {

Cd::Cd() = default;

pure fn Cd::kind() const wontthrow -> Builtin::Kind { return Kind::Cd; }

/* An absolute operand, or one led by dot or dot-dot, skips the CDPATH search.
 */
static fn cdpath_search_applies(const String &operand) throws -> bool
{
  if (operand.is_empty() || os::path_is_absolute(operand.view()) ||
      os::path_is_drive_relative(operand.view()))
  {
    return false;
  }
  if (operand == "." || operand == "..") return false;
  if (operand.length() >= 2 && operand[0] == '.' &&
      os::is_directory_separator(operand[1]))
  {
    return false;
  }
  if (operand.length() >= 3 && operand[0] == '.' && operand[1] == '.' &&
      os::is_directory_separator(operand[2]))
  {
    return false;
  }
  return true;
}

struct cd_path_component
{
  StringView text;
  usize decoded_start;
  usize decoded_end;
  bool is_opaque;
};

struct cd_component_owner_range
{
  usize first;
  usize end;
};

struct cd_unavailable_component
{
  Path prefix;
  usize component_index;
  usize component_count;
  bool is_not_directory;
};

static fn split_cd_path_components(StringView path,
                                   const utils::decoded_shell_word *decoded,
                                   Allocator allocator) throws
    -> ArrayList<cd_path_component>
{
  let components = ArrayList<cd_path_component>{allocator};
  usize position = os::path_root_length(path);
  while (position < path.length) {
    while (position < path.length && os::is_directory_separator(path[position]))
      position++;
    if (position >= path.length) break;

    let const start = position;
    while (position < path.length &&
           !os::is_directory_separator(path[position]))
      position++;

    let is_opaque = false;
    if (decoded != nullptr) {
      for (let const &range : decoded->opaque_ranges) {
        let const range_end = range.decoded_start + range.decoded_length;
        if (range.decoded_start < position && range_end > start) {
          is_opaque = true;
          break;
        }
      }
    }

    components.push(
        cd_path_component{path.substring_of_length(start, position - start),
                          start, position, is_opaque});
  }
  return components;
}

static fn first_unavailable_cd_component(const Path &target,
                                         Allocator allocator) throws
    -> Maybe<cd_unavailable_component>
{
  let const components =
      split_cd_path_components(target.text().view(), nullptr, allocator);
  for (usize component_index = 0; component_index < components.count();
       component_index++)
  {
    let const &component = components[component_index];
    let const prefix = Path{
        target.text().view().substring_of_length(0, component.decoded_end)};
    if (!prefix.exists())
      return cd_unavailable_component{prefix, component_index,
                                      components.count(), false};
    if (!prefix.is_directory())
      return cd_unavailable_component{prefix, component_index,
                                      components.count(), true};
  }
  return None;
}

static fn
cd_component_owner(const ArrayList<cd_path_component> &raw_components,
                   const ArrayList<cd_path_component> &expanded_components,
                   usize expanded_component_index) wontthrow
    -> Maybe<cd_component_owner_range>
{
  usize raw_left = 0;
  usize expanded_left = 0;
  while (raw_left < raw_components.count() &&
         expanded_left < expanded_components.count() &&
         !raw_components[raw_left].is_opaque &&
         raw_components[raw_left].text ==
             expanded_components[expanded_left].text)
  {
    if (expanded_left == expanded_component_index)
      return cd_component_owner_range{raw_left, raw_left + 1};
    raw_left++;
    expanded_left++;
  }

  usize raw_right = raw_components.count();
  usize expanded_right = expanded_components.count();
  while (raw_right > raw_left && expanded_right > expanded_left &&
         !raw_components[raw_right - 1].is_opaque &&
         raw_components[raw_right - 1].text ==
             expanded_components[expanded_right - 1].text)
  {
    if (expanded_right - 1 == expanded_component_index)
      return cd_component_owner_range{raw_right - 1, raw_right};
    raw_right--;
    expanded_right--;
  }

  if (expanded_component_index >= expanded_left &&
      expanded_component_index < expanded_right && raw_left < raw_right)
  {
    return cd_component_owner_range{raw_left, raw_right};
  }
  return None;
}

fn Cd::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);
  if (cxt.restricted_enforcement_active())
    throw ErrorWithLocation{ec.source_location(),
                            "cd is forbidden in a restricted shell"};

  let is_physical = cxt.shell_option_state(shell_option_id::Physical);
  usize operand_index = 1;
  while (operand_index < ec.args().count()) {
    const StringView option = ec.args()[operand_index].view();
    if (option == "--") {
      operand_index++;
      break;
    }

    if (option.length < 2 || option[0] != '-') break;
    let is_options = true;
    for (usize k = 1; k < option.length; k++)
      if (option[k] != 'L' && option[k] != 'P') {
        is_options = false;
        break;
      }
    if (!is_options) break;

    is_physical = option[option.length - 1] == 'P';
    operand_index++;
  }

  let const operand_count = ec.args().count() - operand_index;

  if (operand_count > 1) {
    let too_many = ErrorWithLocationAndDetails{
        ec.source_location(), "cd accepts only a single directory operand",
        "Quote a path that contains spaces"};
    too_many.set_command_status(2);
    throw too_many;
  }

  let arg_path = String{cxt.scratch_allocator()};

  let const is_to_previous =
      operand_count == 1 && ec.args()[operand_index] == "-";

  if (is_to_previous) {
    let const old_directory = cxt.get_variable_value("OLDPWD");
    if (!old_directory.has_value() || old_directory->is_empty()) {
      throw ErrorWithLocationAndDetails{
          ec.source_location(),
          "Unable to return to the previous directory because OLDPWD is not "
          "set",
          "Change directory at least once before `cd -`"};
    }
    arg_path.append(old_directory->view());
  } else if (operand_count > 0) {
    arg_path.append(ec.args()[operand_index]);
  } else {
    let const home_directory = os::get_home_directory();
    if (!home_directory.has_value())
      throw ErrorWithLocationAndDetails{
          ec.source_location(), "Unable to determine the home directory",
          "Set `HOME` to a valid path"};
    arg_path.append(home_directory->text());
  }

  LOG(Info, "cd changing directory to '%s'", arg_path.c_str());

  let target = Path{arg_path};
  let old_directory = Path{};

  /* An empty CDPATH entry, including one a leading, trailing, or doubled colon
     makes, names the current directory. */
  let was_reached_through_cdpath = false;
  if (!is_to_previous && operand_count > 0 && cdpath_search_applies(arg_path)) {
    if (let const cdpath = cxt.get_variable_value("CDPATH")) {
      const StringView entries = cdpath->view();
      usize start = 0;
      while (start <= entries.length) {
        usize end = start;
        while (end < entries.length && entries.data[end] != os::PATH_DELIMITER)
          end++;
        const StringView entry =
            entries.substring_of_length(start, end - start);
        Path candidate = entry.is_empty()
                             ? Path{arg_path}
                             : Path{entry}.push_component(arg_path.view());
        let resolved = candidate;
        if (is_physical) {
          if (resolved.is_relative()) {
            let current_directory = Path::current_directory();
            if (current_directory.is_empty()) break;
            resolved = current_directory.push_component(resolved.text().view());
          }
          if (let canonical = os::canonical_path(resolved))
            resolved = canonical.take();
        } else {
          resolved = resolved.to_absolute().normalized();
        }
        if (resolved.is_directory()) {
          LOG(Info, "cd resolved '%s' through CDPATH entry '%.*s'",
              arg_path.c_str(), static_cast<int>(entry.length), entry.data);
          target = steal(resolved);
          was_reached_through_cdpath = !entry.is_empty();
          break;
        }
        if (end >= entries.length) break;
        start = end + 1;
      }
    }
  }

  /* A relative operand joins onto the logical PWD when that names a directory,
     the bash -L default, so cd .. out of a symlinked directory returns to the
     symlink's parent. */
  if (is_physical) {
    if (target.is_relative()) {
      target = target.to_absolute();
      if (!target.is_absolute())
        throw ErrorWithLocation{
            ec.source_location(),
            StringView{"Unable to resolve '"} + arg_path +
                "' because the current directory is unavailable"};
    }

    if (let resolved = os::canonical_path(target)) target = resolved.take();
  } else if (target.is_absolute() ||
             os::path_is_drive_relative(target.text().view()))
  {
    target = target.to_absolute().normalized();
  } else {
    old_directory = logical_working_directory(cxt);
    let logical_target = Path{old_directory.text().view()};
    logical_target.push_component(target.text().view());
    logical_target = logical_target.normalized();

    if (!logical_target.is_empty() && logical_target.is_directory()) {
      target = steal(logical_target);
    } else {
      target = target.to_absolute().normalized();
      /* getcwd yields an empty path when the current directory was removed, so
         the result stays relative and the throw names that failure. */
      if (!target.is_absolute())
        throw ErrorWithLocation{
            ec.source_location(),
            StringView{"Unable to resolve '"} + arg_path +
                "' because the current directory is unavailable"};
    }
  }

  if (target.exists()) {
    if (!target.is_directory())
      throw ErrorWithLocation{ec.arg_location_at(operand_index),
                              StringView{"The path '"} + arg_path +
                                  "' is not a directory"};

    if (old_directory.is_empty())
      old_directory = logical_working_directory(cxt);
    /* A path that exists can still refuse the move, so the chdir failure throws
       before PWD and OLDPWD are rewritten, leaving them untouched like dash. */
    if (Path::set_current_directory(target).is_error()) {
      throw ErrorWithLocation{
          ec.source_location(),
          StringView{"Unable to change to the directory '"} + arg_path +
              "': " + os::last_system_error_message()};
    }
    /* A relative or empty PATH entry now names a different directory, so a
       cached resolution is marked stale for the next command to re-resolve. */
    cxt.get_program_resolver().working_directory_changed();
    if (!old_directory.is_empty())
      cxt.set_shell_variable("OLDPWD", old_directory.text());
    cxt.set_shell_variable("PWD", target.text());
    record_directory_access(target.text().view(), cxt.scratch_allocator());
    if (is_to_previous || was_reached_through_cdpath) {
      ec.print_to_stdout(target.text() + "\n");
    }
    return 0;
  }

  let const unavailable =
      first_unavailable_cd_component(target, cxt.scratch_allocator());
  if (!unavailable.has_value() || is_to_previous || operand_count == 0) {
    throw ErrorWithLocationAndDetails{
        ec.source_location(),
        StringView{"The directory '"} + arg_path + "' does not exist",
        "Check the spelling or create it with `mkdir -p`"};
  }

  let operand_location = ec.arg_location_at(operand_index);
  let raw_operand = arg_path.view();
  if (operand_index < ec.arg_locations().count()) {
    if (let const source = cxt.current_source();
        source != nullptr &&
        operand_location.position + operand_location.length <= source->length())
    {
      raw_operand = source->view().substring_of_length(
          operand_location.position, operand_location.length);
    }
  }

  let const decoded =
      utils::decode_shell_word(raw_operand, cxt.scratch_allocator());
  let const raw_components = split_cd_path_components(
      decoded.text.view(), &decoded, cxt.scratch_allocator());
  let const normalized_operand = Path{arg_path.view()}.normalized();
  let const expanded_components = split_cd_path_components(
      normalized_operand.text().view(), nullptr, cxt.scratch_allocator());
  let const remaining_component_count =
      unavailable->component_count - unavailable->component_index - 1;
  let const expanded_component_index =
      expanded_components.count() > remaining_component_count
          ? expanded_components.count() - remaining_component_count - 1
          : 0;
  let const owner = cd_component_owner(raw_components, expanded_components,
                                       expanded_component_index);

  let typed_prefix = arg_path.view();
  if (owner.has_value()) {
    let const &first = raw_components[owner->first];
    let const &last = raw_components[owner->end - 1];
    typed_prefix = decoded.text.view().substring_of_length(0, last.decoded_end);
    operand_location.position += decoded.raw_positions[first.decoded_start];
    operand_location.length = decoded.raw_positions[last.decoded_end] -
                              decoded.raw_positions[first.decoded_start];
  }

  if (unavailable->is_not_directory) {
    throw ErrorWithLocation{operand_location, StringView{"The path '"} +
                                                  typed_prefix +
                                                  "' is not a directory"};
  }

  let details = String{cxt.scratch_allocator(),
                       "Check the spelling or create it with `mkdir -p`"};
  if (owner.has_value() && owner->end == owner->first + 1) {
    if (let const suggested_name = utils::suggest_directory_entry(
            unavailable->prefix.parent(), unavailable->prefix.filename()))
    {
      let suggested_path = String{cxt.scratch_allocator()};
      suggested_path.append(decoded.text.view().substring_of_length(
          0, raw_components[owner->first].decoded_start));
      suggested_path.append(suggested_name->view());
      let quoted_path = String{cxt.scratch_allocator()};
      append_shell_quoted_arg(quoted_path, suggested_path.view());
      details = "Did you mean `" + quoted_path + "`?";
    }
  }

  throw ErrorWithLocationAndDetails{operand_location,
                                    StringView{"The directory '"} +
                                        typed_prefix + "' does not exist",
                                    details};
}

} // namespace shit
