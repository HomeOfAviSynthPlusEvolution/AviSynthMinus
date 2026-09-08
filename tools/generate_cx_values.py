#!/usr/bin/env python3
"""Regenerate SDK value methods from core semantics. No compiler needed.

Use --check in validation; without it, rewrite the generated include.
Formatting and comments are ignored by --check, but C++ tokens must match.
Only value/refcount transformations belong here; transport stays in legacy.cpp.
"""
import argparse
from pathlib import Path
import re
repo=Path(__file__).resolve().parents[1]
root=repo/'avs_core/include'
src=(repo/'avs_core/core/interface.cpp').read_text()
# Reuse the core's value and video-info semantics. No core C++ symbol is linked
# into a plugin: all implementation methods have distinct CX_ names.
parts=[src[src.index('bool VideoInfo::HasVideo'):src.index('// class VideoFrameBuffer')],
       src[src.index('IClip* PClip::GetPointerWithAddRef'):src.index('// end class PClip')],
       src[src.index('void PVideoFrame::Init'):src.index('// end class PVideoFrame')],
       src[src.index('AVSValue::AVSValue()'):src.index('// end class AVSValue')]]
text='\n'.join(parts)
text=re.sub(r'/\*.*?\*/','',text,flags=re.S)
# Drop constructors, destructors and operators; the public inline methods stay
# unchanged and select the host's linkage or our local linkage at initialization.
pattern=r'(?m)^([^\n{};]*?\b(VideoInfo|PClip|PVideoFrame|AVSValue)::([~\w]+|operator[^ (]+)\s*\([^;{}]*?\)\s*(?:const\s*)?)\{'
defs=[]
for m in re.finditer(pattern,text):
    depth=1; end=m.end()
    while depth:
        if text[end]=='{': depth+=1
        if text[end]=='}': depth-=1
        end+=1
    cls,name=m.group(2,3)
    if name==cls or name.startswith(('~','operator')): continue
    defs.append((cls,name,m.group(1),text[m.end():end-1]))
names={c:{n for cc,n,_,_ in defs if cc==c} for c in ['VideoInfo','PClip','PVideoFrame','AVSValue']}
out=[]
for c,n,sig,body in defs:
    sig=sig.replace(c+'::'+n,c+'::CX_'+n)
    for name in sorted(names[c],key=len,reverse=True):
        body=re.sub(r'(?<![\w])'+re.escape(name)+r'\s*\(', 'CX_'+name+'(',body)
    body=body.replace('c.GetPointerWithAddRef()', 'c.CX_GetPointerWithAddRef()')
    body=body.replace('n.GetPointerWithAddRef()', 'nullptr; throw AvisynthError("CX SDK: function values are not supported")')
    body=re.sub(r'(\w+(?:->\w+)*)->AddRef\(\)',r'AvsCxSdkAccess::Retain(\1)',body)
    body=re.sub(r'(\w+(?:->\w+)*)->Release\(\)',r'AvsCxSdkAccess::Release(\1)',body)
    body=body.replace('((IClip*)prev_pointer_to_release)->Release()', 'AvsCxSdkAccess::Release((IClip*)prev_pointer_to_release)')
    body=body.replace('((IFunction*)prev_pointer_to_release)->Release()', 'AvsCxSdkAccess::Release((IFunction*)prev_pointer_to_release)')
    out.append(sig+'{'+body+'}\n')

target=root/'avs/cx/sdk/value_methods.inc'
output='// Derived from core/interface.cpp; same AviSynth license and linking exception.\n'+'\n'.join(out)
def tokens(source):
    source=re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    return re.findall(r'\w+|[^\w\s]', source)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check',action='store_true')
if parser.parse_args().check:
    if tokens(target.read_text()) != tokens(output):
        raise SystemExit('CX value methods differ from core; run tools/generate_cx_values.py and review the diff')
    print('CX value methods match core semantics')
else:
    target.write_text(output)
