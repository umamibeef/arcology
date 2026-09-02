/*  r_log.cpp -- the C face over spdlog.  See r_log.h.
 *
 *  spdlog handles the parts that are tedious and easy to get subtly
 *  wrong: colour sinks that ask the terminal rather than guessing, the
 *  Windows console, locking, and levels from the environment.  What is
 *  added here is the NO_COLOR family, which spdlog does not read, and
 *  the stream split.
 *
 *  Streams.  Diagnostics go to stderr and stay there.  This program's
 *  stdout carries measured output -- the --run summary, --check counts,
 *  --pick coordinates -- which people pipe into other things, so a log
 *  line on stdout would corrupt data rather than merely annoy.  The
 *  sinks are separate so that decision is one line to revisit.
 */
#include "r_log.h"

#include <spdlog/cfg/env.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{

std::once_flag                                                   g_once;
std::mutex                                                       g_mutex;
std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> g_loggers;
spdlog::sink_ptr                                                 g_sink;
/*  Which palette entry each source got.  Filled in logger_for, read by
 *  source_flag, both under g_mutex. */
std::unordered_map<std::string, size_t> g_colour_of;
bool                                    g_colour = false;

/*  no-color.org: present AND non-empty.  An empty NO_COLOR means
 *  nothing, which is the part that is usually got wrong. */
bool no_color_set()
{
    const char *v = std::getenv("NO_COLOR");
    return v && *v;
}

/*  FORCE_COLOR is npm's, CLICOLOR_FORCE is BSD's; either disables when
 *  set to "0".  CLICOLOR=0 asks for none even on a terminal. */
bool force_colour()
{
    const char *f = std::getenv("FORCE_COLOR");
    const char *c = std::getenv("CLICOLOR_FORCE");
    return (f && std::strcmp(f, "0") != 0) || (c && std::strcmp(c, "0") != 0);
}

bool clicolor_off()
{
    const char *v = std::getenv("CLICOLOR");
    return v && std::strcmp(v, "0") == 0;
}

/*  A source's own colour.
 *
 *  spdlog's "%^...%$" colours by LEVEL, so wrapping the source name in
 *  it paints every source the same and only the severity varies -- which
 *  is not what a source label is for.  You want to find the gpu lines by
 *  colour while the level still tells you how bad they are, so the two
 *  have to be coloured independently.  spdlog has no flag for that; this
 *  is one.
 *
 *  The palette deliberately excludes red and yellow: those belong to
 *  warn and error, and a source painted red would read as a severity.
 *  The colour comes from a hash of the name, so a source keeps the same
 *  colour between runs and across machines without a registry to
 *  maintain -- and adding a source needs no change here. */
class source_flag : public spdlog::custom_flag_formatter
{
  public:
    void format(const spdlog::details::log_msg &msg, const std::tm &, spdlog::memory_buf_t &dest) override
    {
        /*  Blues, purples and magentas, and nothing else.  spdlog
         *  paints debug cyan, info green, warn yellow and error red, so
         *  the whole warm half of the wheel plus green and cyan is left
         *  alone -- a source in one of those reads as a severity at a
         *  glance, which is the confusion this change exists to remove.
         *
         *  These are 256-colour codes.  A terminal old enough not to
         *  know them ignores the escape rather than printing rubbish,
         *  and NO_COLOR skips them entirely, so the cost of asking is
         *  nil and twenty-four hues beat the eight the basic set can
         *  spare.  With a dozen sources a collision is still possible;
         *  it costs a moment's confusion, not correctness. */
        static const char *const PALETTE[] = {
            "\033[38;5;39m",
            "\033[38;5;45m",
            "\033[38;5;63m",
            "\033[38;5;69m",
            "\033[38;5;75m",
            "\033[38;5;81m",
            "\033[38;5;99m",
            "\033[38;5;105m",
            "\033[38;5;111m",
            "\033[38;5;135m",
            "\033[38;5;141m",
            "\033[38;5;147m",
            "\033[38;5;165m",
            "\033[38;5;170m",
            "\033[38;5;177m",
            "\033[38;5;183m",
            "\033[38;5;189m",
            "\033[38;5;201m",
            "\033[38;5;207m",
            "\033[38;5;213m",
            "\033[38;5;219m",
            "\033[38;5;33m",
            "\033[38;5;57m",
            "\033[38;5;93m",
        };
        const size_t N = sizeof PALETTE / sizeof *PALETTE;

        std::string name(msg.logger_name.data(), msg.logger_name.size());
        if (name.size() < 9)
            name.append(9 - name.size(), ' ');

        if (!g_colour)
        {
            dest.append(name.data(), name.data() + name.size());
            return;
        }
        /*  Colours are handed out in the order sources first appear,
         *  not hashed from the name.  A hash is tempting -- it needs no
         *  state and is stable between runs -- but with a dozen sources
         *  and any workable palette it collides, and the pair it
         *  collided on was `city` and `cities`, which are precisely the
         *  two a reader must not confuse.  Order guarantees the first
         *  two dozen sources are all different, which is the property
         *  actually wanted. */
        size_t idx;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            std::string                 key(msg.logger_name.data(),
                                            msg.logger_name.size());
            auto                        it = g_colour_of.find(key);
            idx                            = it == g_colour_of.end() ? g_colour_of.size() : it->second;
        }
        const char       *c       = PALETTE[idx % N];
        static const char reset[] = "\033[m";
        dest.append(c, c + std::strlen(c));
        dest.append(name.data(), name.data() + name.size());
        dest.append(reset, reset + sizeof reset - 1);
    }

    std::unique_ptr<spdlog::custom_flag_formatter> clone() const override
    {
        return spdlog::details::make_unique<source_flag>();
    }
};

/*  An ISO 8601 timestamp, the level, then the source.  The timestamp is
 *  first because that is where every log reader looks for one, and it
 *  carries the offset so lines from two machines can be interleaved
 *  without guessing a zone. */
const char *const PATTERN = "%Y-%m-%dT%H:%M:%S.%e%z %^%-5l%$ %* %v";

std::unique_ptr<spdlog::pattern_formatter> make_formatter()
{
    auto f = spdlog::details::make_unique<spdlog::pattern_formatter>();
    f->add_flag<source_flag>('*').set_pattern(PATTERN);
    return f;
}

void setup()
{
    /*  A refusal beats a request, a request beats spdlog's own guess --
     *  and its guess already covers isatty, TERM and the Windows
     *  console, so there is nothing to reimplement here. */
    spdlog::color_mode mode = spdlog::color_mode::automatic;
    if (no_color_set() || clicolor_off())
        mode = spdlog::color_mode::never;
    else if (force_colour())
        mode = spdlog::color_mode::always;

    auto sink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>(mode);
    g_sink    = sink;
    /*  Ask the sink whether colour survived BEFORE building a formatter:
     *  source_flag reads g_colour to decide whether to emit escapes at
     *  all, so under NO_COLOR the lines come out clean rather than
     *  stripped afterwards. */
    g_colour = sink->should_color();
    sink->set_formatter(make_formatter());

    /*  The formatter goes on the registry too, not just the sink:
     *  loggers made later are given the REGISTRY's formatter, which
     *  would otherwise put spdlog's default pattern back and lose both
     *  the timestamp and the source colour. */
    spdlog::set_formatter(make_formatter());
    spdlog::set_level(spdlog::level::info);
    /*  SPDLOG_LEVEL=debug, or SPDLOG_LEVEL=gpu=debug,atlas=off */
    spdlog::cfg::load_env_levels();
}

spdlog::logger *logger_for(const char *source)
{
    std::call_once(g_once, setup);
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string                 key(source ? source : "");
    auto                        it = g_loggers.find(key);
    if (it != g_loggers.end())
        return it->second.get();
    g_colour_of.emplace(key, g_colour_of.size());
    auto lg = std::make_shared<spdlog::logger>(key, g_sink);
    lg->flush_on(spdlog::level::warn);
    /*  initialize_logger applies whatever SPDLOG_LEVEL asked for -- the
     *  global level, or a per-source one like `gpu=debug`.  Setting the
     *  level here by hand instead would quietly throw that away, which
     *  is what made per-source filtering look broken. */
    spdlog::initialize_logger(lg);
    g_loggers.emplace(key, lg);
    return g_loggers[key].get();
}

spdlog::level::level_enum to_spdlog(RLogLevel l)
{
    switch (l)
    {
        case R_LOG_ERROR:
            return spdlog::level::err;
        case R_LOG_WARN:
            return spdlog::level::warn;
        case R_LOG_NOTE:
            return spdlog::level::info;
        default:
            return spdlog::level::debug;
    }
}

} /* namespace */

extern "C" {

void r_log_init(void) { std::call_once(g_once, setup); }

void r_log_set_level(RLogLevel max)
{
    std::call_once(g_once, setup);
    /*  set_level on the registry so loggers made later inherit it too;
     *  an explicit --verbose is meant to beat SPDLOG_LEVEL. */
    spdlog::set_level(to_spdlog(max));
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto &kv : g_loggers)
        kv.second->set_level(to_spdlog(max));
}

void r_log_set_colour(int on)
{
    std::call_once(g_once, setup);
    /*  Rebuilding the sink is the only way to change spdlog's mind, and
     *  the loggers have to be pointed at the new one. */
    auto sink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>(
        on ? spdlog::color_mode::always : spdlog::color_mode::never);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink   = sink;
    g_colour = sink->should_color();
    for (auto &kv : g_loggers)
        kv.second->sinks() = {g_sink};
}

int r_log_colour(void)
{
    std::call_once(g_once, setup);
    return g_colour ? 1 : 0;
}

void r_log(RLogLevel lvl, const char *source, const char *fmt, ...)
{
    char    buf[1024];
    va_list ap;

    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    /*  The message is already formatted, so it is passed as a plain
     *  string: a stray brace in a path must not reach spdlog's own
     *  formatter and throw. */
    logger_for(source)->log(to_spdlog(lvl), "{}", buf);
}

} /* extern "C" */
