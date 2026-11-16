#!/usr/bin/env python3
"""Select complete, unchanged OpenRM policy functions for the XP adapter.

No source rewriting: preserve the upstream license and use #line for diagnostics.
The adapter supplies only the object views these functions read. This is not a
build of the full NVOC resource manager. Unknown/changed dependencies fail in C.
"""
import pathlib
import re
import sys

root, output = map(pathlib.Path, sys.argv[1:])
selections = {
    'src/nvidia/src/kernel/gpu/fifo/arch/ampere/kernel_fifo_ga100.c': [
        'kfifoGenerateWorkSubmitToken_GA100'],
    'src/nvidia/src/kernel/gpu/fifo/arch/turing/kernel_fifo_tu102.c': [
        'kfifoUpdateUsermodeDoorbell_TU102'],
    'src/nvidia/src/kernel/gpu/gr/kernel_graphics_context.c': [
        'kgrctxGetGlobalContextBufferInternalId_IMPL',
        'kgrctxPrepareInitializeCtxBuffer_IMPL'],
}
parts = ['/* Generated from the pinned OpenRM submodule. Do not edit. */\n',
         '#include "nvx_openrm_policy_view.h"\n',
         '#if defined(__GNUC__)\n#pragma GCC diagnostic ignored "-Wunused-parameter"\n#endif\n']
# Tokenize comments/strings so their braces cannot terminate a function.
tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
channel_header = (root / 'src/nvidia/generated/g_kernel_channel_nvoc.h').read_text()
notifier = re.search(r'typedef enum\s*\{[^{}]*\} ErrorNotifierType;', channel_header)
if notifier is None:
    raise SystemExit('OpenRM ErrorNotifierType definition changed')
constants = '\n'.join(line for line in channel_header.splitlines()
                      if line.startswith('#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_'))
if not constants:
    raise SystemExit('OpenRM internal channel flags missing')
# A separate generated header keeps the original enum/bitfields out of NVOC.
flags_output = output.with_name('nvx_openrm_channel_flags_generated.h')
flags_result = (channel_header[channel_header.index('/*'):channel_header.index('*/') + 2] + '\n' +
                notifier.group() + '\n' + constants + '\n')
if not flags_output.exists() or flags_output.read_text() != flags_result:
    flags_output.write_text(flags_result)
for relative, names in selections.items():
    path = root / relative
    source = path.read_text()
    license_end = source.index('*/') + 2
    parts.append(source[:license_end] + '\n')
    for name in names:
        matches = list(re.finditer(r'^NV_STATUS\s*\n' + re.escape(name) + r'\s*\(', source, re.M))
        if len(matches) != 1:
            raise SystemExit(f'{path}: expected one complete definition of {name}')
        start = matches[0].start()
        body = source.index('{', matches[0].end())
        depth = 0
        for token in tokens.finditer(source, body):
            if token.group() == '{':
                depth += 1
            elif token.group() == '}':
                depth -= 1
                if depth == 0:
                    end = token.end()
                    break
        else:
            raise SystemExit(f'{path}: unterminated {name}')
        line = source.count('\n', 0, start) + 1
        parts.extend([f'#line {line} "{path.as_posix()}"\n', source[start:end], '\n'])
result = ''.join(parts)
if not output.exists() or output.read_text() != result:
    output.write_text(result)
