// All addresses are data pointers or opaque host map tokens.
case AVS_CX_SDK_PUSHCONTEXT:
env->PushContext((int)q->integers[0]);
break;
case AVS_CX_SDK_POPCONTEXT:
env->PopContext();
break;
case AVS_CX_SDK_SETMEMORYMAX:
r->integer = env->SetMemoryMax((int)q->integers[0]);
break;
case AVS_CX_SDK_SETWORKINGDIR:
r->integer = env->SetWorkingDir((const char *)q->pointers[0]);
break;
case AVS_CX_SDK_PLANARCHROMAALIGNMENT:
r->integer =
    env->PlanarChromaAlignment((IScriptEnvironment::PlanarChromaAlignmentMode)q->integers[0]);
break;
case AVS_CX_SDK_PROPNUMKEYS:
r->integer = env->propNumKeys((const AVSMap *)q->pointers[0]);
break;
case AVS_CX_SDK_PROPGETKEY:
r->pointer = const_cast<void *>(static_cast<const void *>(
    env->propGetKey((const AVSMap *)q->pointers[0], (int)q->integers[0])));
break;
case AVS_CX_SDK_PROPNUMELEMENTS:
r->integer = env->propNumElements((const AVSMap *)q->pointers[0], (const char *)q->pointers[1]);
break;
case AVS_CX_SDK_PROPGETTYPE:
r->integer = env->propGetType((const AVSMap *)q->pointers[0], (const char *)q->pointers[1]);
break;
case AVS_CX_SDK_PROPGETINT:
r->integer = env->propGetInt((const AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                             (int)q->integers[0], (int *)q->pointers[2]);
break;
case AVS_CX_SDK_PROPGETFLOAT:
r->real = env->propGetFloat((const AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                            (int)q->integers[0], (int *)q->pointers[2]);
break;
case AVS_CX_SDK_PROPGETDATA:
r->pointer = const_cast<void *>(static_cast<const void *>(
    env->propGetData((const AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                     (int)q->integers[0], (int *)q->pointers[2])));
break;
case AVS_CX_SDK_PROPGETDATASIZE:
r->integer = env->propGetDataSize((const AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                                  (int)q->integers[0], (int *)q->pointers[2]);
break;
case AVS_CX_SDK_PROPDELETEKEY:
r->integer = env->propDeleteKey((AVSMap *)q->pointers[0], (const char *)q->pointers[1]);
break;
case AVS_CX_SDK_PROPSETINT:
r->integer = env->propSetInt((AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                             (int64_t)q->integers[0], (int)q->integers[1]);
break;
case AVS_CX_SDK_PROPSETFLOAT:
r->integer = env->propSetFloat((AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                               (double)q->real, (int)q->integers[0]);
break;
case AVS_CX_SDK_PROPSETDATA:
r->integer =
    env->propSetData((AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                     (const char *)q->pointers[2], (int)q->integers[0], (int)q->integers[1]);
break;
case AVS_CX_SDK_PROPGETINTARRAY:
r->pointer = const_cast<void *>(static_cast<const void *>(env->propGetIntArray(
    (const AVSMap *)q->pointers[0], (const char *)q->pointers[1], (int *)q->pointers[2])));
break;
case AVS_CX_SDK_PROPGETFLOATARRAY:
r->pointer = const_cast<void *>(static_cast<const void *>(env->propGetFloatArray(
    (const AVSMap *)q->pointers[0], (const char *)q->pointers[1], (int *)q->pointers[2])));
break;
case AVS_CX_SDK_PROPSETINTARRAY:
r->integer = env->propSetIntArray((AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                                  (const int64_t *)q->pointers[2], (int)q->integers[0]);
break;
case AVS_CX_SDK_PROPSETFLOATARRAY:
r->integer = env->propSetFloatArray((AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                                    (const double *)q->pointers[2], (int)q->integers[0]);
break;
case AVS_CX_SDK_CREATEMAP:
r->pointer = const_cast<void *>(static_cast<const void *>(env->createMap()));
break;
case AVS_CX_SDK_FREEMAP:
env->freeMap((AVSMap *)q->pointers[0]);
break;
case AVS_CX_SDK_CLEARMAP:
env->clearMap((AVSMap *)q->pointers[0]);
break;
case AVS_CX_SDK_ALLOCATE:
r->pointer = const_cast<void *>(static_cast<const void *>(
    env->Allocate((size_t)q->integers[0], (size_t)q->integers[1], (AvsAllocType)q->integers[2])));
break;
case AVS_CX_SDK_FREE:
env->Free((void *)q->pointers[0]);
break;
case AVS_CX_SDK_PROPGETINTSATURATED:
r->integer = env->propGetIntSaturated((const AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                                      (int)q->integers[0], (int *)q->pointers[2]);
break;
case AVS_CX_SDK_PROPGETFLOATSATURATED:
r->real = env->propGetFloatSaturated((const AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                                     (int)q->integers[0], (int *)q->pointers[2]);
break;
case AVS_CX_SDK_PROPGETDATATYPEHINT:
r->integer = env->propGetDataTypeHint((const AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                                      (int)q->integers[0], (int *)q->pointers[2]);
break;
case AVS_CX_SDK_PROPSETDATAH:
r->integer = env->propSetDataH((AVSMap *)q->pointers[0], (const char *)q->pointers[1],
                               (const char *)q->pointers[2], (int)q->integers[0],
                               (int)q->integers[1], (int)q->integers[2]);
break;
