#pragma once
#include <stdarg.h>
#include <string>
#include <stdint.h>

//super logger.
//has lgr name in order to avoid collision with log function and around
//it's intentionally not in any namespace to easier refer to it during logging & during invocation right from lldb/gdb console
//please add to lgr.descr.cpp c_lgr_areas_strs map a corresponding string value for a proper config.json parsing
enum lgr_area_e : uint32_t
{
        lgra_info               = 0x0000001,     //general info, output on startup
        lgra_curl               = 0x0000002,     //http/https REST subsystem
        lgra_websocket          = 0x0000004,     //websocketpp & related
        lgra_audio              = 0x0000008,     //audio flow
        lgra_ui                 = 0x0000010,     //User Interface that is
        lgra_posix              = 0x0000020,     //posix errors which are passed via errno variable, Berkley Sockets I/O, system calls, raw IO so on
        lgra_exception          = 0x0000040,     //c++ exception, special flag for logging!
        lgra_fcgi               = 0x0000080,     //libfcgi related stuff
        lgra_coord              = 0x0000100,     //listento-coord component
        lgra_server             = 0x0000200,     //listento-server component
        lgra_broadcaster        = 0x0000400,     //listento-broadcaster component
        lgra_balancer           = 0x0000800,     //listento-balancer component
        lgra_geoip              = 0x0001000,     //geoip service and around
        lgra_fs                 = 0x0002000,     //filesystem
        lgra_db                 = 0x0004000,     //database and sql issues
        lgra_license            = 0x0008000,     //license/access key/authorization issues (desktop & server side)
        lgra_chat               = 0x00010000,     //text chat related
        lgra_br_console         = 0x00020000,    //embedded in the app browser, console.log from them (right now only WkWebView, i.e. mac)
        lgra_br_js_error        = 0x00040000,    //java script erorrs. Right now they are visibled to Xcode console
        lgra_global_event       = 0x00080000,    //global events (unix domain sockets on MAC)
        lgra_midi               = 0x00100000,
        lgra_sync               = 0x00200000,    //synchronization issues (playing several streams in a time in a right moment)
        lgra_user_auth          = 0x00400000,    //user authorization issues
        lgra_api_auth           = 0x00800000,    //internal authorization issues
        lgra_rpc                = 0x01000000,    //remote procedure call subsystem, IPC also goes here
        lgra_win_service        = 0x02000000,    //windows service
        lgra_talkback           = 0x04000000,    //listento-talkback plug-in subsystem (both client & server side)
        lgra_video              = 0x08000000,
        lgra_udp                = 0x10000000,    //udp server or client related (for talkback currently)
        lgra_all                = 0xFFFFFFFF,     //all areas
        lgra_av                 = lgra_audio | lgra_video
};

typedef uint32_t lgr_area_t;

//lgr level of detail
//if you output the message it will be shown for all level greater or equal in this enum
//i.e. if you output lgre_death then it will be visible at all logging levels
//in turn if you set that you want to see messages only from lgre_error level then
//you will see lgre_error, lgre_death, but not lgre_debug
enum lgr_lod_e
{
        lgrl_no = -1,           //do not show any message; is not valid when you log something
        lgrl_death = 0,         //program will die after the message with probability above 100%; OR the information which has to be shared always (like app version or OS)
        lgrl_error,             //error, but app will try to work further
        lgrl_msg,               //the app wants to share some information with us
        lgrl_debug,             //show all messages
        lgrl_size               //must be last in the list; not valid for anything else except indicating an error
};

enum lgr_record_options_e
{
        lgrr_time =      0x01,      //record time in ns and in human readable form
        lgrr_thread =    0x02,      //record thread
        lgrr_backtrace = 0x04,      //record backtrace
        lgrr_all       = 0xffffff
};
typedef unsigned lgr_record_options_t;

struct lgr_init_t
{
        //"" (empty) - means no output
        //"stderr" or "stdout" - special values, are understood
        //else - filesystem path to log file
        std::string             log_file;                       //main human readable log file
        std::string             aux_log_file;                   //auxilary log file for backtraces
        lgr_lod_e               lod = lgrl_error;               //logs-reader level-of-detail interest
        lgr_area_t              area = (lgr_area_t)lgra_all;    //logs-reader area/feature/facitlity interest mask
        lgr_record_options_t    rec_options = 0;
        bool                    init_message = true;            //if to display initial message on start with details how the logger is configured
};

//by default logger takes parameters from config.hpp, but they can be changed on the fly
void lgr_init(lgr_init_t const & init);
void lgr_sync();        //force all logs to be flushed

//fast check if the area/lod will be output somewhere at all and it's worth to try to call output to log function
//here for performance/optimization reasons
bool lgr_has_area(lgr_area_t const area);
bool lgr_has_lod(lgr_lod_e const lod);
bool lgr_has_area_and_lod(lgr_area_t const area, lgr_lod_e const lod);

void lgr(lgr_area_t const area, lgr_lod_e const lod, char const * const format, ...);
void lgr_v(lgr_area_t const area, lgr_lod_e const lod, char const * const format, va_list ap);

//short version for lgr(area, lgre_debug, format, ...) and lgr(area, lgre_error, fromat, ...) correspondently
void lgr_dbg(lgr_area_t const area, char const * const format, ...);
void lgr_msg(lgr_area_t const area, char const * const format, ...);
void lgr_err(lgr_area_t const area, char const * const format, ...);

//these options will be added (ORed) with these set as a current default
void lgr_o(lgr_area_t const area, lgr_lod_e const lod, lgr_record_options_t const options, char const * const format, ...);
void lgr_ov(lgr_area_t const area, lgr_lod_e const lod, lgr_record_options_t const options, char const * const format, va_list ap);

//in case you need to dump a long string; most full version capable to dump anything
//str_len - if set to -1 will be computed
void lgr_str_o(lgr_area_t const area, lgr_lod_e const lod, lgr_record_options_t const options, char const * str, size_t str_len);

//if you need to log something once per launch (to avoid log pollution), first argument is function to call, others are arguments
//notice: it is made via static variable and constructor to achieve thread-safety using compiler facilities. I.e. the function is thread safe.
//usage: lgr_once(lgr_err, lgra_audio, "Mew %d times", 10);
#define lgr_once(func, ...) \
    { \
        static auto lmb = [=](){ func(__VA_ARGS__); }; \
        static struct SZ{           \
        SZ() {                      \
                lmb();              \
             }                      \
        } sZ;                       \
    }                               


//YS: you normally don't want to use these two functions below
//lgr_t which can be passed to a dll to initialise its logger using this one (I need it for http3 client, but may be useful for plug-ins too)
//it's needed on windows. On mac only if visibility==hidden
//make sure the project uses /MD (Multti Threaded Dll) Runtime and not MT if you use the functions
//lgr has to be inited alreadsy!
struct lgr_t* lgr_struct_for_dll_init();

//lgr_t object is not owned by the underlying dll, nor is reference counted!
void lgr_init_via_struct(lgr_t* other);
