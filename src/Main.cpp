#include "Arena.hpp"
#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-OPTIONS] [--] <file1> [file2, ...]", "[-OPTIONS] [-]",
                   "[-OPTIONS]");

FLAG(INTERACTIVE, Bool, 'i', "interactive",
     "Specify that the shell is interactive.");
FLAG(STDIN, Bool, 's', "stdin", "Execute command from stdin and exit.");
FLAG(COMMAND, ManyStrings, 'c', "command",
     "Execute specified command and exit. Can be used multiple times.");
FLAG(ERROR_EXIT, Bool, 'e', "error-exit", "Die on first error.");
FLAG(DISABLE_EXPANSION, Bool, 'f', "no-glob", "Disable path expansion.");
FLAG(ONE_COMMAND, Bool, 't', "one-command",
     "Exit after executing one command.");
FLAG(VERBOSE, Bool, 'v', "verbose",
     "Write input to standard error as it is read.");
FLAG(EXPAND_VERBOSE, Bool, 'x', "xtrace",
     "Write expanded input to standard error as it is read.");

FLAG(EXPORT_ALL, Bool, 'a', "export-all",
     "Mark every assigned variable for the environment.");
FLAG(NO_CLOBBER, Bool, 'C', "no-clobber",
     "Refuse to overwrite an existing file through '>'.");
FLAG(NO_EXEC, Bool, 'n', "no-exec",
     "Read and parse commands but do not run them.");
FLAG(NOUNSET, Bool, 'u', "nounset", "Treat an unset variable as an error.");
FLAG(LOGIN, Bool, 'l', "login",
     "Act as a login shell and source the profiles.");

FLAG(IGNORED1, Bool, 'h', "\0", "Ignored, left for compatibility.");
FLAG(IGNORED2, Bool, 'm', "\0", "Ignored, left for compatibility.");

FLAG(AST, Bool, 'A', "ast", "Print AST before executing each command.");
FLAG(ESCAPE_MAP, Bool, 'M', "escape-bitmap",
     "Print escape bitmap after each parsed command.");
FLAG(EXIT_CODE, Bool, 'E', "exit-code",
     "Print exit code after each executed command.");
FLAG(STATS, Bool, 'S', "stats",
     "Print statistics after each executed command.");

FLAG(VERSION, Bool, '\0', "version", "Display program version and notices.");
FLAG(SHORT_VERSION, Bool, 'V', "short-version",
     "Display version in a short form.");
FLAG(HELP, Bool, '\0', "help", "Display help message.");

#if SHIT_PLATFORM_IS COSMO
FLAG(COSMO_FTRACE, Bool, '\0', "ftrace", "Cosmopolitan: Trace functions.");
FLAG(COSMO_STRACE, Bool, '\0', "strace", "Cosmopolitan: Trace system calls.");
#endif

namespace shit {

/* Print the help or version text and return the exit code when one of those
   flags is set, otherwise None so the shell proceeds to normal startup. */
static fn print_help_or_version_status(const String &program_path)
    -> Maybe<int>
{
  if (FLAG_HELP.is_enabled()) {
    String h{};
    h += "shit, a pedantic, super-fast and awesome posix-compatible command "
         "line interpreter\nor a friendly interactive shell for gigachads.\n\n";
    /* make_synopsis remains on the std::string_view Cli boundary, so spell the
       view from the program path String here. */
    h += make_synopsis(
        std::string_view{program_path.c_str(), program_path.size()},
        HELP_SYNOPSIS);
    h += '\n';
    h += make_flag_help(FLAG_LIST);
    h += '\n';
    print_error(h);
    return EXIT_SUCCESS;
  } else if (FLAG_VERSION.is_enabled()) {
    show_version();
    return EXIT_SUCCESS;
  } else if (FLAG_SHORT_VERSION.is_enabled()) {
    show_short_version();
    return EXIT_SUCCESS;
  }

  return None;
}

/* Report a break, continue, or return that reached the top with no loop,
   function, or sourced script to consume it. The jump carries the source and
   the origin it was made in, so the caret points at the exact builtin and the
   note names where it ran. */
static fn report_escaped_control_flow(EvalContext &context,
                                      const String &fallback_source)
    -> void
{
  if (!context.has_pending_control_flow()) return;

  const control_flow &control = context.pending_control_flow();
  String what{};
  switch (control.kind) {
  case control_flow::Kind::Break:
    what = "'break' used outside of a loop";
    break;
  case control_flow::Kind::Continue:
    what = "'continue' used outside of a loop";
    break;
  case control_flow::Kind::Return: {
    /* A return that reaches the top of a non-interactive script ends the shell
       with its status, the way dash treats a top-level return. It stays an
       error at an interactive prompt, where there is nothing to return from. */
    if (!context.shell_is_interactive()) {
      i32 return_status = static_cast<i32>(control.value);
      context.clear_control_flow();
      context.run_exit_trap();
      utils::quit(return_status, true);
    }
    what = "'return' used outside of a function or a sourced script";
    break;
  }
  case control_flow::Kind::Exit:
  case control_flow::Kind::Normal: context.clear_control_flow(); return;
  }

  const String *source =
      control.source != nullptr ? control.source : &fallback_source;
  ErrorWithLocation located{control.location, what};
  show_message(located.to_string(*source));
  if (!control.origin.empty()) {
    show_message(
        Note{"this jump was reached while running " + control.origin}
            .to_string());
  }

  context.clear_control_flow();
}

/* Lex, parse, validate, and evaluate one chunk of shell source in the given
   context. The main loop and source_file share this so a sourced file runs the
   same pipeline as an interactive line. Returns the resulting exit code. */
static fn run_script_contents(const String &script_contents,
                              EvalContext &context,
                              BumpArena &ast_arena,
                              Maybe<StringView> filename = None)
    -> int
{
  int exit_code = EXIT_FAILURE;

  try {
    defer { context.end_command(); };

    /* Reclaim the previous command's arena storage before the next parse, and
       destroy the eval and dot ASTs that point into it. Function bodies live in
       the separate function arena, so they survive this reset and a function
       defined on one command stays callable on the next. */
    context.clear_retained_sources();
    ast_arena.reset();
    context.reset_scratch_arena();

    Parser p{
        Lexer{String{script_contents.view()}, ast_arena,
                    FLAG_ESCAPE_MAP.is_enabled(), filename}
    };
    Expression *ast = p.construct_ast();

    if (FLAG_AST.is_enabled()) {
      print(ast->to_ast_string());
      print("\n");
    }

    if (FLAG_ESCAPE_MAP.is_enabled()) {
      for (const auto &word : p.debug_words()) {
        print(word.to_pretty_string());
        print("\n");
      }
    }

    /* Validate the whole tree before running anything. An unconditional
       problem stops execution, a conditional one only warns. */
    if (!analyze_ast(ast, script_contents, context.function_names(),
                           context.alias_names()))
    {
      exit_code = EXIT_FAILURE;
    } else if (context.no_exec()) {
      /* Under -n the tree is parsed and validated but never run. */
      exit_code = EXIT_SUCCESS;
    } else {
      context.set_current_source(&script_contents, "the script");
      exit_code = static_cast<int>(ast->evaluate(context));
      report_escaped_control_flow(context, script_contents);
      /* script_contents is local to this call, so drop the frame before it goes
         out of scope and leaves a dangling pointer behind. */
      context.set_current_source(nullptr, "");
    }
    context.set_last_exit_status(static_cast<i32>(exit_code));

    if (FLAG_EXIT_CODE.is_enabled())
      print("[Code " + utils::integer_to_string(exit_code) + "]\n");

    if (FLAG_STATS.is_enabled()) {
      print(context.make_stats_string());
      print("\n");
    }
  } catch (const ErrorWithLocationAndDetails &e) {
    show_message(e.to_string(script_contents));
    show_message(e.details_to_string(script_contents));
  } catch (const ErrorWithLocation &e) {
    show_message(e.to_string(script_contents));
  } catch (const Error &e) {
    show_message(e.to_string());
  } catch (const std::exception &e) {
    show_message(
        "Uncaught exception while executing the AST. Aborting the command.");
    show_message("Last system message: '" +
                       os::last_system_error_message() + "'.");
    show_message("Context: '" + String{e.what()} + "'.");
  } catch (...) {
    show_message(
        "Unexpected system explosion while executing the AST. Exiting.");
    show_message("Last system message: " +
                       os::last_system_error_message());
    utils::quit(EXIT_FAILURE);
  }

  return exit_code;
}

/* Read a whole file and run it in the given context. A missing file is not an
   error, since a login shell sources profiles that may not exist. */
static fn source_file(const Path &path, EvalContext &context,
                      BumpArena &ast_arena) -> void
{
  Maybe<String> contents =
      utils::read_entire_file(path.text());
  if (!contents) return;

  /* The profile path names the source, so a parse error in it and a backtrace
     caret for a file it sources both carry the file rather than a bare
     line:col. */
  run_script_contents(*contents, context, ast_arena, path.text().view());
}

/* Expand the common prompt escapes in PS1 and PS2. */
static fn expand_prompt_escapes(StringView prompt, StringView user,
                                StringView working_directory)
    -> String
{
  String out{};
  for (usize i = 0; i < prompt.length; i++) {
    if (prompt[i] != '\\' || i + 1 >= prompt.length) {
      out += prompt[i];
      continue;
    }
    u8 escaped = prompt[++i];
    switch (escaped) {
    case 'u': out += user; break;
    case 'h':
      out +=
          os::get_environment_variable("HOSTNAME").value_or("localhost");
      break;
    case 'w': {
      String shown{working_directory};
      Maybe<Path> home = os::get_home_directory();
      if (home && shown.starts_with(home->text())) {
        String collapsed{};
        collapsed += "~";
        collapsed += shown.substring(home->size());
        shown = steal(collapsed);
      }
      out += shown;
    } break;
    case 'W': out += Path{working_directory}.filename(); break;
    case '$': out += (user == "root") ? '#' : '$'; break;
    case 'n': out += '\n'; break;
    case 't': out += '\t'; break;
    case '\\': out += '\\'; break;
    default:
      out += '\\';
      out += escaped;
      break;
    }
  }
  return out;
}

} /* namespace shit */

fn main(int argc, char **argv) -> int
{
#if SHIT_PLATFORM_IS COSMO
  ShowCrashReports();
  unused(FLAG_COSMO_FTRACE);
  unused(FLAG_COSMO_STRACE);
#endif

  bool is_login_shell = false;
  shit::ArrayList<shit::String> file_names{};

  try {
    file_names = shit::parse_flags(FLAG_LIST, argc, argv);
  } catch (const shit::Error &e) {
    shit::show_message(e.to_string());
    return EXIT_SUCCESS;
  }

  /* Program path is the first argument. Pull it out and get rid of it. */
  shit::String program_path{};

  if (file_names.size() > 0) {
    program_path = file_names[0];
    /* Drop the program path, the first element. The list has no erase, so the
       rest is rebuilt from the second element on. */
    shit::ArrayList<shit::String> rest{};
    for (usize i = 1; i < file_names.size(); i++)
      rest.push(shit::String{shit::heap_allocator(), file_names[i]});
    file_names = steal(rest);
  } else {
    program_path = "<unknown>";
  }

  if (shit::Maybe<int> code = shit::print_help_or_version_status(program_path))
    return *code;

  if (FLAG_LOGIN.is_enabled() || program_path == "-") is_login_shell = true;

  /* Both stdin and interactive flags are enabled, but there will be only the
   * last man standing. */
  if (FLAG_STDIN.is_enabled() && FLAG_INTERACTIVE.is_enabled()) {
    bool is_tty = shit::os::is_stdin_a_tty();

    shit::String s{};
    s += "Both '-s' and '-i' options were specified. Falling back to ";
    s += is_tty ? "'-i'" : "'-s' because stdin is not a tty.";
    shit::show_message(s);

    if (is_tty)
      FLAG_STDIN.toggle();
    else
      FLAG_INTERACTIVE.toggle();
  }

  bool should_read_stdin = false, should_execute_commands = false,
       should_read_files = false, should_be_interactive = false;

  /* Figure out what to do. Note that "-c" can be specified multiple times.
   * Option precedence should behave as follows: "-s", then "-c", then files
   * (arguments), then "-i" (or no arguments). */
  if (FLAG_STDIN.is_enabled()) {
    if (!FLAG_COMMAND.is_empty() || !file_names.empty() ||
        FLAG_INTERACTIVE.is_enabled())
    {
      shit::show_message(
          "Incompatible options or arguments were specified along "
          "with '-s' option. "
          "Falling back to '-s'.");
    }
    should_read_stdin = true;
  } else if (!FLAG_COMMAND.is_empty()) {
    if (!file_names.empty() || FLAG_INTERACTIVE.is_enabled()) {
      shit::show_message(
          "Incompatible options or arguments were specified along "
          "with '-c' options. "
          "Falling back to '-c'.");
    }
    should_execute_commands = true;
  } else if (!file_names.empty()) {
    if (FLAG_INTERACTIVE.is_enabled()) {
      shit::show_message("Both file argument and '-i' option were given. "
                         "Falling back to reading files.");
    }
    should_read_files = true;
  } else {
    should_be_interactive = true;
  }

  /* Main loop state. The program name is $0 and the remaining arguments are the
     positional parameters $1 upward, held in the list the context owns. */
  shit::ArrayList<shit::String> positional_params{};
  positional_params.reserve(file_names.size());
  for (const shit::String &file_name : file_names)
    positional_params.push(shit::String{
        shit::heap_allocator(),
        shit::StringView{file_name.data(), file_name.size()}
    });

  shit::EvalContext context{
      FLAG_DISABLE_EXPANSION.is_enabled(), FLAG_VERBOSE.is_enabled(),
      FLAG_EXPAND_VERBOSE.is_enabled(),    should_be_interactive,
      FLAG_ERROR_EXIT.is_enabled(),        shit::String{program_path},
      steal(positional_params)};

  /* Apply the remaining option flags that the constructor does not take. */
  context.set_error_unset(FLAG_NOUNSET.is_enabled());
  context.set_no_clobber(FLAG_NO_CLOBBER.is_enabled());
  context.set_export_all(FLAG_EXPORT_ALL.is_enabled());
  context.set_no_exec(FLAG_NO_EXEC.is_enabled());
  /* Monitor mode is on by default in an interactive shell, the way job control
     is enabled at a prompt. */
  context.set_monitor(should_be_interactive);

  /* Seed the standard and shell-specific variables a script may read. The
     version and runtime values come from the build. */
  context.set_shell_variable("SHELL", program_path);
  context.set_shell_variable("PWD", shit::Path::current_directory().text());
  shit::String version_string{};
  version_string += shit::utils::integer_to_string(SHIT_VER_MAJOR);
  version_string += ".";
  version_string += shit::utils::integer_to_string(SHIT_VER_MINOR);
  version_string += ".";
  version_string += shit::utils::integer_to_string(SHIT_VER_PATCH);
  version_string += "-" SHIT_VER_EXTRA;
  context.set_shell_variable("SHIT_VERSION", version_string);
  context.set_shell_variable("SHIT_COMMIT", SHIT_COMMIT_HASH);
  context.set_shell_variable("SHIT_BUILD_MODE", SHIT_BUILD_MODE);
  context.set_shell_variable("SHIT_OS", SHIT_OS_INFO);

  usize arg_index = 0;
  bool should_quit = FLAG_ONE_COMMAND.is_enabled() ? true : false;
  int exit_code = EXIT_SUCCESS;

  /* Clear and set up cache. Don't prematurely initialize the whole path map,
   * since it's only really noticeable in interactive mode. This way,
   * subsequent calls to the same program will still be cached in any mode,
   * but we won't waste any milliseconds traversing directories for very
   * simple scripts! */
  shit::utils::clear_path_map();
  shit::os::set_default_signal_handlers();

  /* The parse arena holds the AST and its tokens for one command, and is reset
     between commands. It outlives each tree it builds. */
  shit::BumpArena ast_arena{};
  shit::AST_ARENA = &ast_arena;

  /* The function arena holds function bodies, which outlive the command that
     defined them, so it is never reset during the run. */
  shit::BumpArena function_arena{};
  shit::FUNCTION_ARENA = &function_arena;

  /* A login shell reads /etc/profile and ~/.profile if they exist, then the
     file named by ENV when that is set. A missing file is silently skipped. */
  if (is_login_shell) {
    source_file(shit::Path{"/etc/profile"}, context, ast_arena);
    if (shit::Maybe<shit::Path> home = shit::os::get_home_directory();
        home.has_value())
    {
      shit::Path profile = *home;
      profile.push_component(".profile");
      source_file(profile, context, ast_arena);
    }
    if (shit::Maybe<shit::String> env = context.get_variable_value("ENV");
        env.has_value() && !env->empty())
    {
      source_file(shit::Path{env->view()}, context, ast_arena);
    }
  }

  /* A simple return cannot be used after this point, since we need a special
   * cleanup for toiletline. utils::quit() should be used instead. */
  for (;;) {
    ASSERT(!shit::os::is_child_process());

    shit::String script_contents{};

    /* Figure out what to do and retrieve the code. */
    try {
      if (should_read_files || should_read_stdin) {
        /* If "-s" is used, or when the file name is "-", read standard input,
           otherwise read the named file, both through the descriptor layer so
           no iostream file stream is pulled in. */
        if (should_read_stdin || file_names[arg_index] == "-") {
          should_quit = should_quit || should_read_stdin;
          script_contents = shit::utils::read_entire_standard_input();
        } else {
          const shit::String &file_name = file_names[arg_index];
          shit::Maybe<shit::String> contents =
              shit::utils::read_entire_file(file_name.view());
          if (!contents) {
            throw shit::Error{"Could not open '" + file_name.view() +
                              "': " + shit::os::last_system_error_message()};
          }
          script_contents = steal(*contents);
        }

        if ((arg_index += 1) == file_names.size()) {
          should_quit = true;
        }
      } else if (should_execute_commands) {
        shit::StringView command_view = FLAG_COMMAND.next();
        script_contents = shit::String{command_view};
        if (FLAG_COMMAND.at_end()) should_quit = true;
      } else if (should_be_interactive) {
        if (!toiletline::is_active()) {
          shit::utils::initialize_path_map();
          toiletline::initialize();
          shit::show_message("Welcome :3");
        } else {
          /* NOTE: avoid this branch if exit_raw_mode() wasn't called
           * previosly! */
          toiletline::enter_raw_mode();
        }

        static constexpr usize PWD_LENGTH = 24;

        shit::String full_pwd{shit::Path::current_directory().text()};
        toiletline::set_title("shit @ " + full_pwd);

        shit::String pwd{full_pwd};
        if (pwd.length() > PWD_LENGTH) {
          shit::String shortened{};
          shortened += "...";
          shortened += pwd.substring(pwd.length() - PWD_LENGTH + 3);
          pwd = steal(shortened);
        }

        shit::String u = shit::os::get_current_user().value_or("???");

        /* shit % ...wd1/pwd2/pwd3/pwd4/pwd5 $ command */
        shit::String prompt{};
        if (shit::Maybe<shit::String> ps1 = context.get_variable_value("PS1");
            ps1.has_value() && !ps1->empty())
        {
          /* A user-set PS1 expands its escape sequences, \u \h \w \W \$ and
             the like. */
          prompt =
              expand_prompt_escapes(ps1->view(), u.view(), full_pwd.view());
        } else {
          prompt += u;
          prompt += ' ';
          prompt += pwd;
          prompt += (u == "root") ? " # " : " $ ";
        }

        /* Ask for input until we get one. */
        for (;;) {
          auto [code, input] = toiletline::get_input(prompt);

          switch (code) {
          case TL_PRESSED_TAB:
            /* TODO. */
            shit::print("^I");
            shit::flush();
            toiletline::set_input(input);
            break;
          case TL_PRESSED_EOF:
            /* Exit on CTRL-D. */
            shit::print("^D");
            shit::flush();
            toiletline::emit_newlines(input);
            shit::utils::quit(exit_code, true);
            break;
          case TL_PRESSED_INTERRUPT:
            /* Ignore Ctrl-C. */
            shit::print("^C");
            shit::flush();
            break;
          case TL_PRESSED_SUSPEND:
            /* Ignore Ctrl-Z. */
            shit::print("^Z");
            shit::flush();
            break;
          default:;
          }

          toiletline::emit_newlines(input);

          /* Execute the command without raw mode. */
          if (code == TL_PRESSED_ENTER && !input.empty()) {
            script_contents = steal(input);
            break;
          }
        }

        toiletline::exit_raw_mode();
      } else {
        unreachable();
      }
    } catch (const shit::Error &e) {
      shit::show_message(e.to_string());
      shit::utils::quit(EXIT_FAILURE);
    } catch (const std::exception &e) {
      shit::show_message(
          "Uncaught exception while getting the input. Exiting.");
      shit::show_message("Context: '" + shit::String{e.what()} + "'.");
      shit::utils::quit(EXIT_FAILURE);
    } catch (...) {
      shit::show_message(
          "Unexpected system explosion while getting the input. Exiting.");
      shit::show_message("Last system message: " +
                         shit::os::last_system_error_message());
      shit::utils::quit(EXIT_FAILURE);
    }

    /* Drop any interrupt that landed while the prompt was waiting, so a Ctrl-C
       used to clear the input line does not abort the command about to run. */
    shit::os::INTERRUPT_REQUESTED = 0;

    /* Execute the contents through the shared pipeline. */
    exit_code = run_script_contents(script_contents, context, ast_arena);

    /* TODO: Make ExecutionErrorWithLocation to distinguish execution
     * errors? Or statically check commands before they are executed? */

    /* We can get here from child process if they didn't exec()
     * properly to print error. */
    if (should_quit || shit::os::is_child_process() ||
        (FLAG_ERROR_EXIT.is_enabled() && exit_code != 0))
    {
      if (!shit::os::is_child_process()) context.run_exit_trap();
      shit::utils::quit(exit_code, FLAG_ERROR_EXIT.is_enabled());
    }
  }

  unreachable();
}
