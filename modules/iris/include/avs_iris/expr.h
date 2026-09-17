#pragma once
#include <avisynth.h>
AVSValue __cdecl CreateExprCompat(AVSValue args, void *user,
                                  IScriptEnvironment *env);
AVSValue __cdecl CreateIrisExpr(AVSValue args, void *user,
                                IScriptEnvironment *env);
