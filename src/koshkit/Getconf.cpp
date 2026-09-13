/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the getconf utility. It resolves supported system and
 * pathname configuration names through the platform configuration interfaces.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-v specification] system-variable | path-variable path");

HELP_DESCRIPTION_DECL("The getconf utility writes configuration values.");

FLAG(GETCONF_SPECIFICATION, String, 'v', "specification",
     "Use this POSIX specification.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Getconf);

namespace koshka::koshkit {

inline constexpr static_string_entry<os::system_configuration_key>
    SYSTEM_CONFIGURATION_ENTRIES[] = {
        {SSK("AIO_LISTIO_MAX"),                os::system_configuration_key::AioListIoMax       },
        {SSK("AIO_MAX"),                       os::system_configuration_key::AioMax             },
        {SSK("AIO_PRIO_DELTA_MAX"),
         os::system_configuration_key::AioPriorityDeltaMax                                      },
        {SSK("ARG_MAX"),                       os::system_configuration_key::ArgMax             },
        {SSK("ATEXIT_MAX"),                    os::system_configuration_key::AtExitMax          },
        {SSK("BC_BASE_MAX"),                   os::system_configuration_key::BcBaseMax          },
        {SSK("BC_DIM_MAX"),                    os::system_configuration_key::BcDimensionMax     },
        {SSK("BC_SCALE_MAX"),                  os::system_configuration_key::BcScaleMax         },
        {SSK("BC_STRING_MAX"),                 os::system_configuration_key::BcStringMax        },
        {SSK("CHILD_MAX"),                     os::system_configuration_key::ChildMax           },
        {SSK("CLK_TCK"),                       os::system_configuration_key::ClockTicks         },
        {SSK("COLL_WEIGHTS_MAX"),
         os::system_configuration_key::CollationWeightsMax                                      },
        {SSK("DELAYTIMER_MAX"),                os::system_configuration_key::DelayTimerMax      },
        {SSK("EXPR_NEST_MAX"),                 os::system_configuration_key::ExpressionNestMax  },
        {SSK("GETGR_R_SIZE_MAX"),
         os::system_configuration_key::GroupBufferSizeMax                                       },
        {SSK("GETPW_R_SIZE_MAX"),
         os::system_configuration_key::PasswordBufferSizeMax                                    },
        {SSK("HOST_NAME_MAX"),                 os::system_configuration_key::HostNameMax        },
        {SSK("IOV_MAX"),                       os::system_configuration_key::IoVectorMax        },
        {SSK("LINE_MAX"),                      os::system_configuration_key::LineMax            },
        {SSK("LOGIN_NAME_MAX"),                os::system_configuration_key::LoginNameMax       },
        {SSK("MQ_OPEN_MAX"),                   os::system_configuration_key::MessageQueueOpenMax},
        {SSK("MQ_PRIO_MAX"),
         os::system_configuration_key::MessageQueuePriorityMax                                  },
        {SSK("NGROUPS_MAX"),                   os::system_configuration_key::GroupsMax          },
        {SSK("NPROCESSORS_CONF"),
         os::system_configuration_key::ProcessorConfigured                                      },
        {SSK("NPROCESSORS_ONLN"),
         os::system_configuration_key::ProcessorOnline                                          },
        {SSK("OPEN_MAX"),                      os::system_configuration_key::OpenMax            },
        {SSK("PAGESIZE"),                      os::system_configuration_key::PageSize           },
        {SSK("PAGE_SIZE"),                     os::system_configuration_key::PageSize           },
        {SSK("PASS_MAX"),                      os::system_configuration_key::PasswordMax        },
        {SSK("PHYS_PAGES"),                    os::system_configuration_key::PhysicalPages      },
        {SSK("PTHREAD_DESTRUCTOR_ITERATIONS"),
         os::system_configuration_key::ThreadDestructorIterations                               },
        {SSK("PTHREAD_KEYS_MAX"),              os::system_configuration_key::ThreadKeysMax      },
        {SSK("PTHREAD_STACK_MIN"),
         os::system_configuration_key::ThreadStackMin                                           },
        {SSK("PTHREAD_THREADS_MAX"),
         os::system_configuration_key::ThreadCountMax                                           },
        {SSK("RE_DUP_MAX"),                    os::system_configuration_key::RegexDupMax        },
        {SSK("RTSIG_MAX"),                     os::system_configuration_key::RealtimeSignalMax  },
        {SSK("SEM_NSEMS_MAX"),                 os::system_configuration_key::SemaphoreCountMax  },
        {SSK("SEM_VALUE_MAX"),                 os::system_configuration_key::SemaphoreValueMax  },
        {SSK("SIGQUEUE_MAX"),                  os::system_configuration_key::SignalQueueMax     },
        {SSK("STREAM_MAX"),                    os::system_configuration_key::StreamMax          },
        {SSK("SYMLOOP_MAX"),                   os::system_configuration_key::SymbolicLinkLoopMax},
        {SSK("TIMER_MAX"),                     os::system_configuration_key::TimerMax           },
        {SSK("TTY_NAME_MAX"),                  os::system_configuration_key::TtyNameMax         },
        {SSK("TZNAME_MAX"),                    os::system_configuration_key::TimeZoneNameMax    },
        {SSK("_POSIX_VERSION"),                os::system_configuration_key::PosixVersion       },
};
inline constexpr StaticStringMap SYSTEM_CONFIGURATIONS{
    SYSTEM_CONFIGURATION_ENTRIES};

inline constexpr static_string_entry<os::path_configuration_key>
    PATH_CONFIGURATION_ENTRIES[] = {
        {SSK("FILESIZEBITS"),             os::path_configuration_key::FileSizeBits    },
        {SSK("LINK_MAX"),                 os::path_configuration_key::LinkMax         },
        {SSK("MAX_CANON"),                os::path_configuration_key::MaxCanonical    },
        {SSK("MAX_INPUT"),                os::path_configuration_key::MaxInput        },
        {SSK("NAME_MAX"),                 os::path_configuration_key::NameMax         },
        {SSK("PATH_MAX"),                 os::path_configuration_key::PathMax         },
        {SSK("PIPE_BUF"),                 os::path_configuration_key::PipeBuffer      },
        {SSK("POSIX2_SYMLINKS"),          os::path_configuration_key::TwoSymbolicLinks},
        {SSK("POSIX_ALLOC_SIZE_MIN"),
         os::path_configuration_key::AllocationSizeMin                                },
        {SSK("POSIX_REC_INCR_XFER_SIZE"),
         os::path_configuration_key::RecommendedIncrementTransferSize                 },
        {SSK("POSIX_REC_MAX_XFER_SIZE"),
         os::path_configuration_key::RecommendedMaxTransferSize                       },
        {SSK("POSIX_REC_MIN_XFER_SIZE"),
         os::path_configuration_key::RecommendedMinTransferSize                       },
        {SSK("POSIX_REC_XFER_ALIGN"),
         os::path_configuration_key::RecommendedTransferAlignment                     },
        {SSK("SYMLINK_MAX"),              os::path_configuration_key::SymbolicLinkMax },
        {SSK("_POSIX_ASYNC_IO"),          os::path_configuration_key::AsyncIo         },
        {SSK("_POSIX_CHOWN_RESTRICTED"),
         os::path_configuration_key::ChownRestricted                                  },
        {SSK("_POSIX_NO_TRUNC"),          os::path_configuration_key::NoTrunc         },
        {SSK("_POSIX_PRIO_IO"),           os::path_configuration_key::PriorityIo      },
        {SSK("_POSIX_SYNC_IO"),           os::path_configuration_key::SyncIo          },
        {SSK("_POSIX_VDISABLE"),          os::path_configuration_key::DisableCharacter},
};
inline constexpr StaticStringMap PATH_CONFIGURATIONS{
    PATH_CONFIGURATION_ENTRIES};

Getconf::Getconf() = default;

pure fn Getconf::kind() const wontthrow -> Utility::Kind
{
  return Kind::Getconf;
}

fn Getconf::execute(const ExecContext &ec, EvalContext &cxt,
                    const ArrayList<String> &args,
                    const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const operands =
      parse_util_operands(FLAG_LIST, args, &arg_locations, &operand_locations);
  defer { reset_flags(FLAG_LIST); };

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty() || operands.count() > 2)
    return report_usage_error(ec, cxt, args[0].view());
  if (FLAG_GETCONF_SPECIFICATION.is_set() &&
      FLAG_GETCONF_SPECIFICATION.value() != "POSIX_V7_LP64_OFF64" &&
      FLAG_GETCONF_SPECIFICATION.value() != "POSIX_V7_ILP32_OFFBIG")
  {
    report_soft_koshkit_util_error(
        ec, cxt, FLAG_GETCONF_SPECIFICATION.value_location(), args[0].view(),
        "unsupported specification '" +
            String{FLAG_GETCONF_SPECIFICATION.value()} + "'");
    return 2;
  }

  Maybe<i64> value;
  if (let const system_key = SYSTEM_CONFIGURATIONS.find(operands[0].view());
      system_key.has_value())
  {
    if (operands.count() != 1)
      return report_usage_error(ec, cxt, args[0].view());
    value = os::system_configuration(*system_key);
  } else if (let const path_key = PATH_CONFIGURATIONS.find(operands[0].view());
             path_key.has_value())
  {
    if (operands.count() != 2)
      return report_usage_error(ec, cxt, args[0].view());
    value = os::path_configuration(operands[1].view(), *path_key);
  } else {
    report_soft_koshkit_util_error(ec, cxt, operand_locations[0],
                                   args[0].view(),
                                   "unknown variable '" + operands[0] + "'");
    return 1;
  }

  if (!value.has_value() || *value == -1)
    ec.print_to_stdout("undefined\n");
  else
    ec.print_to_stdout(String::from(*value, cxt.scratch_allocator()) + "\n");
  return 0;
}

} // namespace koshka::koshkit
