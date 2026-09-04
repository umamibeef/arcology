/*  log.cpp -- the C face over spdlog.  See log.h.
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
#include "log.h"

#include <spdlog/cfg/env.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <chrono>
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
        /*  Nine colours, as far apart as the wheel allows once the
         *  level's own are kept clear -- spdlog paints debug cyan, info
         *  green, warn yellow and error red, and nothing here is near
         *  any of those.  Nine rather than two dozen: the point of a
         *  source colour is to tell the sources apart at a glance (the
         *  user: "so that we can identify the messages more easily"),
         *  and two dozen blues do not do that, while nine spread from
         *  blue through violet and magenta to pink, with one orange, do.
         *  Consecutive entries alternate hue families, so the first few
         *  sources to appear -- the ones every run has -- contrast most.
         *  The source sits in its own bracket now, so an orange source
         *  is not mistaken for a warning the way it could be when the
         *  two stood side by side unlabelled.
         *
         *  These are 256-colour codes.  A terminal old enough not to
         *  know them ignores the escape rather than printing rubbish,
         *  and NO_COLOR skips them entirely. */
        static const char *const PALETTE[] = {
            "\033[38;5;33m",  /* blue          */
            "\033[38;5;201m", /* magenta       */
            "\033[38;5;208m", /* orange        */
            "\033[38;5;135m", /* purple        */
            "\033[38;5;117m", /* light sky     */
            "\033[38;5;213m", /* pink          */
            "\033[38;5;63m",  /* royal blue    */
            "\033[38;5;171m", /* orchid        */
            "\033[38;5;147m", /* light slate   */
        };
        const size_t N = sizeof PALETTE / sizeof *PALETTE;

        std::string name(msg.logger_name.data(), msg.logger_name.size());

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

/*  The stamp: the date, then the hour and minute, then the seconds to
 *  the hundredth after the point --
 *
 *      [20260801:1349.3455]
 *
 *  -- the form the user set out.  Local time, no zone, and no
 *  separators inside a field, so it sorts as text and is one token to a
 *  parser.  spdlog has flags for the parts but none for hundredths (%e
 *  is thousandths), and the dot in the middle of a field is not
 *  something its pattern language can put there, so the whole thing is
 *  one flag. */
class stamp_flag : public spdlog::custom_flag_formatter
{
  public:
    void format(const spdlog::details::log_msg &msg, const std::tm &tm, spdlog::memory_buf_t &dest) override
    {
        using namespace std::chrono;
        const auto ms = duration_cast<milliseconds>(msg.time.time_since_epoch()) % 1000;
        char       buf[24];
        const int  n = std::snprintf(buf, sizeof buf, "%04d%02d%02d:%02d%02d.%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count() / 10));
        dest.append(buf, buf + n);
    }

    std::unique_ptr<spdlog::custom_flag_formatter> clone() const override
    {
        return spdlog::details::make_unique<stamp_flag>();
    }
};

/*  The level in capitals, padded to five so the brackets line up down
 *  a page -- [INFO ] over [DEBUG] (the user: "I want the levels to be
 *  the same width").  spdlog's %l is lower case and %L a single letter;
 *  neither is that.  CRITICAL is cut to CRIT for the width; nothing
 *  here emits it. */
class level_flag : public spdlog::custom_flag_formatter
{
  public:
    void format(const spdlog::details::log_msg &msg, const std::tm &, spdlog::memory_buf_t &dest) override
    {
        static const char *const NAME[] = {"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "CRIT ", "OFF  "};
        int                      l      = static_cast<int>(msg.level);
        if (l < 0 || l > 6)
            l = 6;
        dest.append(NAME[l], NAME[l] + std::strlen(NAME[l]));
    }

    std::unique_ptr<spdlog::custom_flag_formatter> clone() const override
    {
        return spdlog::details::make_unique<level_flag>();
    }
};

/*  [stamp][LEVEL][source] message.  Each field in its own brackets, the
 *  stamp first because that is where every log reader looks for one.
 *  The colour, where there is any, stays inside the brackets: the level
 *  in spdlog's own severity colour, the source in its palette entry. */
const char *const PATTERN = "[%~][%^%_%$][%*] %v";

std::unique_ptr<spdlog::pattern_formatter> make_formatter()
{
    auto f = spdlog::details::make_unique<spdlog::pattern_formatter>();
    f->add_flag<stamp_flag>('~').add_flag<level_flag>('_').add_flag<source_flag>('*').set_pattern(PATTERN);
    /*  set_pattern works out whether the pattern needs the local time
     *  from spdlog's own flags only; a custom flag does not count, and
     *  without this the stamp is handed an all-zero tm and prints the
     *  year 1900. */
    f->need_localtime(true);
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

void log_init(void) { std::call_once(g_once, setup); }

void log_set_level(RLogLevel max)
{
    std::call_once(g_once, setup);
    /*  set_level on the registry so loggers made later inherit it too;
     *  an explicit --verbose is meant to beat SPDLOG_LEVEL. */
    spdlog::set_level(to_spdlog(max));
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto &kv : g_loggers)
        kv.second->set_level(to_spdlog(max));
}

void log_set_colour(int on)
{
    std::call_once(g_once, setup);
    /*  Rebuilding the sink is the only way to change spdlog's mind, and
     *  the loggers have to be pointed at the new one. */
    auto sink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>(
        on ? spdlog::color_mode::always : spdlog::color_mode::never);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink   = sink;
    g_colour = sink->should_color();
    /*  A new sink comes with spdlog's default pattern; without this the
     *  stamp, the brackets and the source colour all vanish the moment
     *  colour is toggled. */
    sink->set_formatter(make_formatter());
    for (auto &kv : g_loggers)
        kv.second->sinks() = {g_sink};
}

int log_colour(void)
{
    std::call_once(g_once, setup);
    return g_colour ? 1 : 0;
}

/*  The banner's gradient runs across its width, character by
 *  character, through the first three colours the sources are painted
 *  in -- blue, magenta, orange -- so the art and the log lines under it
 *  are one palette.  A terminal that announces truecolor in COLORTERM
 *  gets the blend exact; any other gets the nearest of the 256-colour
 *  cube's 216, which is banded but never wrong, and macOS's own
 *  Terminal is one of those.  Blanks are left bare: colouring a space
 *  is bytes for nothing. */
static void banner_colour(float t, bool truecolor, char *out, size_t n)
{
    static const int STOP[3][3] = {
        {0,   135, 255}, /* blue    */
        {255, 0,   255}, /* magenta */
        {255, 135, 0  }, /* orange  */
    };
    const float u = t * 2.0f;
    const int   k = u < 1.0f ? 0 : 1;
    const float f = u - static_cast<float>(k);
    int         rgb[3];
    for (int c = 0; c < 3; ++c)
        rgb[c] = static_cast<int>(static_cast<float>(STOP[k][c]) + (static_cast<float>(STOP[k + 1][c]) - static_cast<float>(STOP[k][c])) * f + 0.5f);
    if (truecolor)
    {
        std::snprintf(out, n, "\033[38;2;%d;%d;%dm", rgb[0], rgb[1], rgb[2]);
        return;
    }
    /*  the cube's levels are 0, 95, 135, 175, 215, 255 */
    int q[3];
    for (int c = 0; c < 3; ++c)
        q[c] = rgb[c] < 48 ? 0 : rgb[c] < 115 ? 1
                             : rgb[c] < 155   ? 2
                             : rgb[c] < 195   ? 3
                             : rgb[c] < 235   ? 4
                                              : 5;
    std::snprintf(out, n, "\033[38;5;%dm", 16 + 36 * q[0] + 6 * q[1] + q[2]);
}

extern "C" void log_banner(const char *const *lines, int n)
{
    std::call_once(g_once, setup);
    size_t width = 0;
    for (int i = 0; i < n; ++i)
        if (std::strlen(lines[i]) > width)
            width = std::strlen(lines[i]);
    const char *ct        = std::getenv("COLORTERM");
    const bool  truecolor = ct && (std::strstr(ct, "truecolor") || std::strstr(ct, "24bit"));
    std::string buf;
    for (int i = 0; i < n; ++i)
    {
        const char *l = lines[i];
        for (size_t x = 0; l[x]; ++x)
        {
            if (l[x] == ' ' || !g_colour)
            {
                buf += l[x];
                continue;
            }
            char esc[32];
            banner_colour(width > 1 ? static_cast<float>(x) / static_cast<float>(width - 1) : 0.0f, truecolor, esc, sizeof esc);
            buf += esc;
            buf += l[x];
        }
        if (g_colour)
            buf += "\033[m";
        buf += '\n';
    }
    std::fputs(buf.c_str(), stderr);
}

extern "C" void log_raw(const char *text)
{
    std::call_once(g_once, setup);
    std::fputs(text, stderr);
}

void log_msg(RLogLevel lvl, const char *source, const char *fmt, ...)
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
