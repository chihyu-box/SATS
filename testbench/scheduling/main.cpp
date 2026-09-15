#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <systemc>
#include "include/config.h"
#include "module/top_module.h"
#include "scheduler_a.h"
#include "scheduler_b.h"
#include "scheduler_c.h"

struct Options
{
    std::string scheduler;
    DramLayout layout = DramLayout::RowMajor;
    std::string trace_path;   // empty disables the trace
    std::string dramsys_config = sats::config::DRAMSYS_CONFIG_PATH;
    bool print_matrices = false;

    std::string name() const { return "scheduler_" + scheduler + (layout == DramLayout::TileMajor ? "_tile_major" : ""); }
};

static void usage(const char *prog)
{
    std::cerr << "usage: " << prog << " --scheduler <a|b|c> [options]\n"
              << "  --scheduler <a|b|c>              which schedule to run\n"
              << "  --layout <row_major|tile_major>  how A and B are laid out in DRAM (default: row_major)\n"
              << "  --trace <path>                   trace CSV to write (default: trace_scheduler_<x>[_tile_major].csv)\n"
              << "  --no-trace                       do not write a trace\n"
              << "  --dramsys-config <path>          DRAMSys configuration (default: " << sats::config::DRAMSYS_CONFIG_PATH << ")\n"
              << "  --print                          print the input and output matrices" << std::endl;
}

static int fail(const char *prog, const std::string &message)
{
    std::cerr << message << std::endl;
    usage(prog);
    return 1;
}

static std::optional<int> parse_options(int argc, char *argv[], Options &opts)
{
    std::string layout = "row_major";
    bool no_trace = false;
    const std::map<std::string, std::string *> valued = {
        {"--scheduler", &opts.scheduler}, {"--layout", &layout}, {"--trace", &opts.trace_path}, {"--dramsys-config", &opts.dramsys_config}};
    const std::map<std::string, bool *> flags = {{"--no-trace", &no_trace}, {"--print", &opts.print_matrices}};

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        std::string key = arg, value;
        bool has_value = false;
        if (auto eq = arg.find('='); eq != std::string::npos)
        {
            key = arg.substr(0, eq);
            value = arg.substr(eq + 1);
            has_value = true;
        }

        if (auto it = valued.find(key); it != valued.end())
        {
            if (!has_value && i + 1 >= argc)
                return fail(argv[0], key + " needs a value");
            *it->second = has_value ? value : argv[++i];
        }
        else if (auto it = flags.find(key); it != flags.end())
            *it->second = true;
        else if (key == "--help" || key == "-h")
        {
            usage(argv[0]);
            return 0;
        }
        else
            return fail(argv[0], "unknown option '" + arg + "'");
    }

    if (opts.scheduler.empty())
        return fail(argv[0], "--scheduler is required");
    if (opts.scheduler != "a" && opts.scheduler != "b" && opts.scheduler != "c")
        return fail(argv[0], "unknown scheduler '" + opts.scheduler + "' (expected a, b or c)");
    if (layout != "row_major" && layout != "tile_major")
        return fail(argv[0], "unknown layout '" + layout + "' (expected row_major or tile_major)");

    opts.layout = layout == "tile_major" ? DramLayout::TileMajor : DramLayout::RowMajor;
    if (no_trace)
        opts.trace_path.clear();
    else if (opts.trace_path.empty())
        opts.trace_path = "trace_" + opts.name() + ".csv";
    return std::nullopt;
}

static std::unique_ptr<SchedulerBase> make_scheduler(const Options &opts, sats::TopModule &top)
{
    std::string name = opts.name();
    if (opts.scheduler == "a")
        return std::make_unique<SchedulerA>(name.c_str(), top, opts.print_matrices, opts.layout);
    if (opts.scheduler == "b")
        return std::make_unique<SchedulerB>(name.c_str(), top, opts.print_matrices, opts.layout);
    return std::make_unique<SchedulerC>(name.c_str(), top, opts.print_matrices, opts.layout);
}

int sc_main(int argc, char *argv[])
{
    Options opts;
    if (auto exit_code = parse_options(argc, argv, opts))
        return *exit_code;

    sats::TopModule top{"top", opts.trace_path, opts.dramsys_config};
    auto scheduler = make_scheduler(opts, top);
    sc_core::sc_start();
    return 0;
}
