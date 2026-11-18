#!/usr/bin/env python3
"""Patch and compile an isolated Android Vulkan-only RenderThread candidate."""
import json
import argparse
from pathlib import Path
import shlex
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('database', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--wsi-negotiation', action='store_true')
parser.add_argument('--bind-process-pipe', action='store_true')
args = parser.parse_args()
source, database, output = args.source, args.database, args.output
output.mkdir(parents=True, exist_ok=False)
entries = json.loads(database.read_text())
text = (source / "host/RenderThread.cpp").read_text()
principal = Path(__file__).resolve().parents[1] / "gfxstream-principal-scope"
channel = principal.parent / "gfxstream-androidpipe-channel"

def replace_once(old, new):
    global text
    if text.count(old) != 1:
        raise ValueError(f"expected exactly one source marker: {old[:50]!r}")
    text = text.replace(old, new, 1)

replace_once('#include "RenderThread.h"', '''#include "RenderThread.h"
#if !GFXSTREAM_ENABLE_HOST_GLES
#include "rme_vk_rc_decoder.h"
#endif''')
if args.wsi_negotiation:
    replace_once('namespace gfxstream {',
                 'namespace gfxstream { namespace vk { bool rmeWsiProtocolCompiled(); } }\nnamespace gfxstream {')
replace_once('    bool anyProgress = false;\n    while (true) {', '''    bool anyProgress = false;
#if !GFXSTREAM_ENABLE_HOST_GLES
    // SocketSession has not yet supplied an authenticated process identity.
    // A zero identity causes rcSetPuid to fail closed.
    RcDecoder rmeRcDecoder(0);
    bool rmeRcInvalid = false;
#endif
    while (true) {''')
if args.wsi_negotiation:
    replace_once('    RcDecoder rmeRcDecoder(0);',
                 '    RcDecoder rmeRcDecoder(0, tInfo.m_vkInfo.has_value() && vk::rmeWsiProtocolCompiled());')
if args.bind_process_pipe:
    if args.wsi_negotiation:
        raise ValueError("process-pipe binding currently enables compute-only channels")
    start = text.index("    // SocketSession has not yet supplied")
    end = text.index("    bool rmeRcInvalid", start)
    text = (text[:start]
            + "    RcDecoder rmeRcDecoder(mRmePrincipal, tInfo.m_vkInfo.has_value(),\n"
              "        FrameBuffer::getFB()->getFeatures().VulkanShaderFloat16Int8.enabled);\n"
            + text[end:])
    replace_once('      mContextId(virtioGpuContextId)\n{',
                 '''      mContextId(virtioGpuContextId),
      mRmePrincipal(loadStream || !channel ? nullptr : rme::PrincipalScope::take())
{''')
    replace_once('            if (!tInfo.m_puid) {\n',
                 '            if (!tInfo.m_puid && !mChannel) {\n')
    replace_once('            if (tInfo.m_vkInfo) {\n                tInfo.m_vkInfo->ctx_id',
                 '''            const bool authorized = !mChannel ||
                (processResources && rmeRcDecoder.acceptsVulkan(tInfo.m_puid));
            if (tInfo.m_vkInfo && authorized) {
                tInfo.m_vkInfo->ctx_id''')
    header = (source / "host/RenderThread.h").read_text()
    marker = '    bool mNeedReloadProcessResources = false;'
    if header.count(marker) != 1:
        raise ValueError("unexpected RenderThread header")
    header_path = output / "RenderThread.bound.h"
    header_path.write_text(
        '#include "principal_scope.h"\n' + header.replace(
            marker, marker + "\n    std::shared_ptr<const rme::Principal> mRmePrincipal;"))
    (output / "headers.json").write_text(json.dumps({
        "version": 0,
        "roots": [{
            "type": "file",
            "name": str((source / "host/RenderThread.h").resolve()),
            "external-contents": str(header_path.resolve()),
        }],
    }))
marker = '''            //
            // try to process some of the command buffer using the Magma'''
replace_once(marker, '''#if !GFXSTREAM_ENABLE_HOST_GLES
            if (mChannel) {
                int used = rmeRcDecoder.decode(readBuf.buf(), readBuf.validData(),
                                               ioStream, &tInfo.m_puid);
                if (used < 0) rmeRcInvalid = true;
                else if (used > 0) {
                    readBuf.consume(static_cast<size_t>(used));
                    progress = true;
                }
            }
#endif

''' + marker)
replace_once('''        } while (progress);
    }

    if (dumpFP)''', '''#if !GFXSTREAM_ENABLE_HOST_GLES
            if (rmeRcInvalid) break;
#endif
        } while (progress);
#if !GFXSTREAM_ENABLE_HOST_GLES
        if (rmeRcInvalid) {
            mChannel->stopFromHost();
            break;
        }
#endif
    }

    if (dumpFP)''')
(output / "rme_vk_rc_decoder.h").write_bytes(Path(__file__).with_name("rc_decoder.h").read_bytes())
target = output / "RenderThread.cpp"
target.write_text(text)

def compile_copy(target, template_name, object_name, record_name):
    entry, = [e for e in entries if e["file"].endswith("/" + template_name)]
    cwd = Path(entry["directory"])
    original = (cwd / entry["file"]).resolve(strict=True)
    command = list(entry.get("arguments") or shlex.split(entry["command"]))
    if '-DGFXSTREAM_ENABLE_HOST_GLES=0' not in command:
        raise ValueError("expected Vulkan-only compile flags")
    command[1:1] = ["-I" + str(original.parent), "-I" + str(principal)]
    if args.bind_process_pipe:
        command[1:1] = ["-ivfsoverlay", str((output / "headers.json").resolve())]
    command[command.index(entry["file"])] = str(target.resolve())
    command[command.index("-o") + 1] = str((output / object_name).resolve())
    if "-MF" in command:
        command[command.index("-MF") + 1] = str((output / (object_name + ".d")).resolve())
    (output / (record_name + ".txt")).write_text(shlex.join(command) + "\n")
    with (output / (record_name + ".log")).open("w") as log:
        subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)

compile_copy(target, "RenderThread.cpp", "RenderThread.o", "command")
if args.bind_process_pipe:
    # Recompile every host translation unit that sees the changed class layout.
    for name in ("RenderChannelImpl", "RendererImpl", "RenderLibImpl", "RenderWindow"):
        copy = output / (name + ".cpp")
        copy.write_bytes((source / "host" / copy.name).read_bytes())
        compile_copy(copy, copy.name, name + ".o", name)
    for file in (principal / "principal_scope.cpp", channel / "channel.cpp",
                 channel / "socket_session.cpp", channel / "socket_server.cpp"):
        compile_copy(file, "FrameBuffer.cpp", file.stem + ".o", file.stem)
print("PASS: Android ARM64 Vulkan-only RenderThread with RC candidate", output / "RenderThread.o")
