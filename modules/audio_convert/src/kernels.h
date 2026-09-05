#pragma once

#include <avs/types.h>
#include <avs/config.h>

#define CONVERT_DECLARE(func) void (func)(void *, void *, int);

typedef CONVERT_DECLARE(*convert_proc);

CONVERT_DECLARE(convert32To16);
CONVERT_DECLARE(convert16To32);
CONVERT_DECLARE(convert32To8);
CONVERT_DECLARE(convert8To32);
CONVERT_DECLARE(convert16To8);
CONVERT_DECLARE(convert8To16);
CONVERT_DECLARE(convert32To24);
CONVERT_DECLARE(convert24To32);
CONVERT_DECLARE(convert24To16);
CONVERT_DECLARE(convert16To24);
CONVERT_DECLARE(convert24To8);
CONVERT_DECLARE(convert8To24);
CONVERT_DECLARE(convert8ToFLT);
CONVERT_DECLARE(convertFLTTo8);
CONVERT_DECLARE(convert16ToFLT);
CONVERT_DECLARE(convertFLTTo16);
CONVERT_DECLARE(convert32ToFLT);
CONVERT_DECLARE(convertFLTTo32);


#undef CONVERT_DECLARE
