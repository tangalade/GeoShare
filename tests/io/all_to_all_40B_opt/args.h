#include "optionparser.h"

struct Arg: public option::Arg
{
  static void printError(const char* msg1, const option::Option& opt, const char* msg2)
  {
    fprintf(stderr, "%s", msg1);
    fwrite(opt.name, opt.namelen, 1, stderr);
    fprintf(stderr, "%s", msg2);
  }

  static option::ArgStatus Unknown(const option::Option& option, bool msg)
  {
    if (msg) printError("Unknown option '", option, "'\n");
    return option::ARG_ILLEGAL;
  }

  static option::ArgStatus NonEmpty(const option::Option& option, bool msg)
  {
    if (option.arg != 0 && option.arg[0] != 0)
      return option::ARG_OK;

    if (msg) printError("Option '", option, "' requires a non-empty argument\n");
    return option::ARG_ILLEGAL;
  }

  static option::ArgStatus Numeric(const option::Option& option, bool msg)
  {
    char* endptr = 0;
    if (option.arg != 0 && strtol(option.arg, &endptr, 10)){};
    if (endptr != option.arg && *endptr == 0)
      return option::ARG_OK;

    if (msg) printError("Option '", option, "' requires a numeric argument\n");
    return option::ARG_ILLEGAL;
  }
};

enum optionIndex { ARGS_UNKNOWN, ARGS_HELP, ARGS_VERBOSE,
		   ARGS_REQ_TYPE, ARGS_ENC_TYPE,
		   ARGS_OBJECT_SIZE,
		   ARGS_BUCKET_NAME, ARGS_OBJECT_NAME,
		   ARGS_DESTINATIONS,
		   ARGS_PRIME,
		   ARGS_NETWORK_LOOPS, ARGS_ENCODE_LOOPS, ARGS_TOTAL_LOOPS };

const optionIndex args_required_g[] = {
  ARGS_REQ_TYPE,
  ARGS_ENC_TYPE,
  ARGS_OBJECT_SIZE,
  ARGS_OBJECT_NAME,
  ARGS_DESTINATIONS,
};

const option::Descriptor args_usage_g[] = {
  { ARGS_UNKNOWN,       0,"" ,"",             Arg::Unknown, "USAGE: ./test [options]\n\nOptions:"},
  { ARGS_HELP,          0,"" ,"help",         Arg::None,    "  \t--help  \tPrint usage and exit." },
  { ARGS_VERBOSE,       0,"v","verbose",      Arg::None,    "  -v        \t--verbose" },
  { ARGS_REQ_TYPE,      0,"r","req-type",     Arg::NonEmpty,"  -r <arg>, \t--request-type=<arg>" },
  { ARGS_ENC_TYPE,      0,"e","enc-type",     Arg::NonEmpty,"  -e <arg>, \t--enc-type=<arg>" },
  { ARGS_OBJECT_SIZE,   0,"s","object-size",  Arg::Numeric, "  -s <arg>, \t--object-size=<arg>" },
  { ARGS_BUCKET_NAME,   0,"b","bucket-name",  Arg::NonEmpty,"  -b <arg>, \t--bucket-name=<arg>" },
  { ARGS_OBJECT_NAME,   0,"o","object-name",  Arg::NonEmpty,"  -o <arg>, \t--object-name=<arg>" },
  { ARGS_DESTINATIONS,  0,"d","destinations", Arg::NonEmpty,"  -d <arg>, \t--destinations=<arg>" },
  { ARGS_PRIME,         0,"p","prime-network",Arg::None,    "  -p        \t--prime-network." },
  { ARGS_NETWORK_LOOPS, 0,"" ,"network-loops",Arg::Numeric, "  \t--network-loops=<arg>" },
  { ARGS_ENCODE_LOOPS,  0,"" ,"encode-loops", Arg::Numeric, "  \t--encode-loops=<arg> [TODO]" },
  { ARGS_TOTAL_LOOPS,   0,"" ,"total-loops",  Arg::Numeric, "  \t--total-loops=<arg> [TODO]" },
};
