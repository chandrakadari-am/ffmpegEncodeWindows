#pragma once
//for internal usage of logger (lgr) file
//converts enums to human readable string
//takes backtraces
#include "lgr.hpp"
#include <stdint.h>
#include <string>

//flags from lgr_area_e
std::string  lgr_area_str(lgr_area_t const a);
lgr_area_t   lgr_area_from_str(char const * str);

char const * lgr_lod_str(lgr_lod_e const l);
lgr_lod_e    lgr_lod_from_str(char const * str);        //return lgrl_size if str is invalid

uint64_t     lgr_timestamp_ms();


