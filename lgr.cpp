#include "lgr.hpp"
#include "lgr.descr.hpp"
#include <stdio.h>
#include <vector>
#include <stdio.h>
#include <string.h>
#ifdef LGR_WIN_DEBUG_OUTPUT
#include <Windows.h>
#include <debugapi.h>
#endif

struct lgr_t
{
    FILE *                  f = nullptr;            //file where to output logs
    FILE *                  aux_f = nullptr;        //file where to output auxilary logs information [backtraces, thread info]
    lgr_area_t              area = lgra_all;        //flags from lgr_area_e, areas which have to be dumped
    lgr_lod_e               lod = lgrl_debug;       //level of detail in which user is interested
    lgr_record_options_t    options = lgrr_all;     //additional info to dump to aux_f file
    unsigned long long      lgr_init_time_ms = 0;
    bool                    imported = false;
};

static lgr_t s_lgr;     //global object to use
static lgr_t * s_lgr_p = &s_lgr;

static FILE * lgr_open_file(char const * const filename)
{
    FILE * ret;
    if (filename == nullptr || *filename == 0)
        ret = nullptr;
    else if (!strcmp(filename, "stderr"))
        ret = stderr;
    else if (!strcmp(filename, "stdout"))
        ret = stdout;
    else
        ret = NULL;//fopen(filename, "w");
    return ret;
}

static void lgr_close()
{
    if (s_lgr.f && s_lgr.f != stderr && s_lgr.f != stdout) { fclose(s_lgr.f); s_lgr.f = nullptr; }
    if (s_lgr.aux_f && s_lgr.aux_f != stderr && s_lgr.aux_f != stdout) { fclose(s_lgr.aux_f); s_lgr.aux_f = nullptr; }
}

void lgr_init(lgr_init_t const & init)
{
    lgr_close();
    s_lgr.f = lgr_open_file(init.log_file.c_str());
    s_lgr.aux_f = lgr_open_file(init.aux_log_file.c_str());
    s_lgr.lod = init.lod;
    s_lgr.area = init.area;
    s_lgr.lgr_init_time_ms = (unsigned long long)lgr_timestamp_ms();
    if (init.init_message)
        lgr(lgra_all, lgrl_death, "Logger created: area: %x, lod: %s, time: %llu", s_lgr.area, lgr_lod_str(s_lgr.lod), s_lgr.lgr_init_time_ms);
}

void lgr_sync()
{
    if (s_lgr_p->f) fflush(s_lgr_p->f);
    if (s_lgr_p->aux_f) fflush(s_lgr_p->aux_f);
}

struct lgr_t* lgr_struct_for_dll_init()
{
    return &s_lgr;
}

//lgr_t object is not owned by the underlying dll, nor is reference counted!
void lgr_init_via_struct(lgr_t* other)
{
    s_lgr_p = other;
}

bool lgr_has_area(lgr_area_t const area)
{
    return !!(s_lgr_p->area & area);
}

bool lgr_has_lod(lgr_lod_e const lod)
{
    return lod <= s_lgr_p->lod;
}

bool lgr_has_area_and_lod(lgr_area_t const area, lgr_lod_e const lod)
{
    return lgr_has_area(area) && lgr_has_lod(lod);
}

void lgr(lgr_area_t const area, lgr_lod_e const lod, char const * const format, ...)
{
    va_list ap;
    va_start(ap, format);
    lgr_v(area, lod, format, ap);
    va_end(ap);
}

void lgr_v(lgr_area_t const area, lgr_lod_e const lod, char const * const format, va_list ap)
{
    lgr_ov(area, lod, 0, format, ap);
}

//short version for lgr(area, lgre_debug, format, ...) and lgr(area, lgre_error, fromat, ...) correspondently
void lgr_dbg(lgr_area_t const area, char const * const format, ...)
{
    va_list ap;
    va_start(ap, format);
    lgr_v(area, lgrl_debug, format, ap);
    va_end(ap);
}

void lgr_msg(lgr_area_t const area, char const * const format, ...)
{
    va_list ap;
    va_start(ap, format);
    lgr_v(area, lgrl_msg, format, ap);
    va_end(ap);
}

void lgr_err(lgr_area_t const area, char const * const format, ...)
{
    va_list ap;
    va_start(ap, format);
    lgr_v(area, lgrl_error, format, ap);
    va_end(ap);
}


//these options will be added (ORed) with these set as a current default
void lgr_o(lgr_area_t const area, lgr_lod_e const lod, lgr_record_options_t const options, char const * const format, ...)
{
    va_list ap;
    va_start(ap, format);
    lgr_ov(area, lgrl_error, options, format, ap);
    va_end(ap);
    (void)lod;
}

void lgr_ov(lgr_area_t const area, lgr_lod_e const lod, lgr_record_options_t const options, char const * const format, va_list ap)
{
    char buf_stack[1024];
    buf_stack[0] = 0;       //who knows...
    
    va_list ap2;
    va_copy(ap2, ap);
    int would_print = vsnprintf(buf_stack, sizeof(buf_stack), format, ap);
    if (would_print >= ((int)sizeof(buf_stack) - 1)) {      //bad case.... need to use alloc
        std::vector<char> buf_heap(would_print + 1);
        would_print = vsnprintf(buf_heap.data(), buf_heap.size(), format, ap2);
        lgr_str_o(area, lod, options, buf_heap.data(), would_print);
    }
    else
        lgr_str_o(area, lod, options, buf_stack, would_print);
    va_end(ap2);
}

//in case you need to dump a long string; most full version capable to dump anything
//str_len - if set to -1 will be computed
void lgr_str_o(lgr_area_t const area, lgr_lod_e const lod, lgr_record_options_t const options, char const * str, size_t str_len)
{
    (void)options; (void)str_len;
    //should we output something ?
    if ((s_lgr_p->area & area) && lod <= s_lgr_p->lod)
    {
        unsigned long long const ts_ms = (unsigned long long)lgr_timestamp_ms();
        if (s_lgr_p->f) {
            unsigned long long const ts_ms = (unsigned long long)lgr_timestamp_ms();    //timestamp in milliseconnods since UNIX epoch time
            //unsigned long long ts_diff_ms = ts_ms - s_lgr_p->lgr_init_time_ms;
            
            fprintf(s_lgr_p->f, "%04x %s %016llu: %s\n", area, lgr_lod_str(lod), ts_ms, str);
#ifdef LGR_WIN_DEBUG_OUTPUT
            std::string dbg_str = str;
            dbg_str += "\n";
            OutputDebugStringA(dbg_str.c_str());
#endif
        }
        //TODO: add to s_lgr_p->aux_f meta-information (thread, backtrace, human readable time)
        printf("%04x %s %016llu: %s\n", area, lgr_lod_str(lod), ts_ms, str);
        if (lod <= lgrl_error) {
            //I'm sure it is worth to sync in order a user would see at least something in case of a crash.
            lgr_sync();
        }
    }
}
