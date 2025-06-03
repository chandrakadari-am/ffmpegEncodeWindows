#include "lgr.descr.hpp"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <chrono>
#include <map>

//specially for Slava and his old MacOS 10.14
#if __has_include(<ranges>) && __cplusplus >= 202002L
#include <ranges>
#define RANGES_PRESENT 1
#endif

using namespace std::string_literals;

//where is you compile time reflection ??? even magic_enum library requires a dependency which I don't want to introduce for the logger
static std::map<std::string, lgr_area_e> const c_lgr_areas_strs = {
    {"info"s,         lgra_info        },
    {"curl"s,         lgra_curl        },
    {"websocket"s,    lgra_websocket   },
    {"audio"s,        lgra_audio       },
    {"ui"s,           lgra_ui          },
    {"posix"s,        lgra_posix       },
    {"exception"s,    lgra_exception   },
    {"fcgi"s,         lgra_fcgi        },
    {"coord"s,        lgra_coord       },
    {"server"s,       lgra_server      },
    {"broadcaster"s,  lgra_broadcaster },
    {"balancer"s,     lgra_balancer    },
    {"geoip"s,        lgra_geoip       },
    {"fs"s,           lgra_fs          },
    {"db"s,           lgra_db          },
    {"license"s,      lgra_license     },
    {"chat"s,         lgra_chat        },
    {"br_console"s,   lgra_br_console  },
    {"br_js_error"s,  lgra_br_js_error },
    {"global_event"s, lgra_global_event},
    {"midi"s,         lgra_midi        },
    {"sync"s,         lgra_sync        },
    {"user_auth"s,    lgra_user_auth   },
    {"api_auth"s,     lgra_api_auth    },
    {"rpc"s,          lgra_rpc         },
    {"win_service"s,  lgra_win_service },
    {"talkback"s,     lgra_talkback    },
    {"video"s,        lgra_video       },
    {"udp"s,          lgra_udp         },
    {"all"s,          lgra_all         },
    {"av"s,           lgra_av          }
};

std::string lgr_area_str(lgr_area_t const a)
{
        char buf[11];
        int n = snprintf(buf, sizeof(buf), "0x%08x", a);
        return std::string(buf, n);
}

lgr_area_t lgr_area_from_str(char const * str)
{
    lgr_area_t ret_area = (lgr_area_t)0;
    if (strncmp(str, "0x", 2) == 0) {
        uint32_t ret = 0;
        //sscanf(str,"0x%x", &ret);
    }
    else {
#if RANGES_PRESENT
        //parse string from config
        //just drop all spaces first
        int const str_len = (int)strlen(str);
        std::string str_s;
        str_s.reserve(str_len);
        
        //remove spaces if any
        for (int i = 0; i < str_len; ++i)
            if (str[i] != ' ' && str[i] != '\t')
                str_s.push_back(str[i]);
                
        for (auto w_subrange : std::ranges::views::split(str_s, ',')) {
            std::string_view w_view{w_subrange.begin(), w_subrange.end()};
            std::string const w_str(w_view);
            auto f = c_lgr_areas_strs.find(w_str);
            if (f != c_lgr_areas_strs.end()) [[likely]] {
                ret_area |= f->second;
            }
            else {
                fprintf(stderr, "lgr_area_from_str: lgr_area contains unrecognized token: %s, ignoring\n", w_str.c_str());
            }
        }
#endif
    }
    return ret_area;
}


char const * lgr_lod_str(lgr_lod_e const l)
{
        char const * r;
        switch(l)
        {
                case lgrl_no: r = "no"; break;
                case lgrl_death: r = "dth"; break;
                case lgrl_error: r = "err"; break;
                case lgrl_msg: r = "msg"; break;
                case lgrl_debug: r = "dbg"; break;
                default:
                        r = "";
                        assert(false);
        }
        return r;
}

lgr_lod_e    lgr_lod_from_str(char const * str)
{
        lgr_lod_e ret = lgrl_size;
        if (!strcmp(str, "no")) ret = lgrl_no;
        else if (!strcmp(str, "dth")) ret = lgrl_death;
        else if (!strcmp(str, "death")) ret = lgrl_death;
        else if (!strcmp(str, "err")) ret = lgrl_error;
        else if (!strcmp(str, "error")) ret = lgrl_error;
        else if (!strcmp(str, "msg")) ret = lgrl_msg;
        else if (!strcmp(str, "message")) ret = lgrl_msg;
        else if (!strcmp(str, "dbg")) ret = lgrl_debug;
        else if (!strcmp(str, "debug")) ret = lgrl_debug;
        return ret;
}

uint64_t     lgr_timestamp_ms()
{
        std::chrono::system_clock::time_point start;
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        uint64_t const ret_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        return ret_ms;
}
