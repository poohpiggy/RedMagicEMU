#!/usr/bin/env python3
"""Apply the isolated, per-decoder WSI negotiation to freshly generated code."""
import argparse
from pathlib import Path
import shutil

p = argparse.ArgumentParser()
p.add_argument('generated', type=Path)
p.add_argument('host_connection', type=Path)
p.add_argument('output', type=Path, help='new directory for HostConnection source')
a = p.parse_args()
generated = a.generated.resolve(strict=True)
a.output.mkdir(parents=True, exist_ok=False)
here = Path(__file__).resolve().parent

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit('unexpected source marker: ' + repr(old[:100]))
    return text.replace(old, new, 1)

def edit(path, transform):
    path.write_text(transform(path.read_text()))

header = generated / 'guest/VkEncoder.h'
def edit_header(text):
    text = replace_once(text, '  ~VkEncoder();\n',
        '  ~VkEncoder();\n  void rmeSetWsiEnabled(bool enabled) { m_rmeWsiEnabled = enabled; }\n'
        '  bool rmeWsiEnabled() const { return m_rmeWsiEnabled; }\n')
    return replace_once(text, '  std::unique_ptr<Impl> mImpl;',
        '  bool m_rmeWsiEnabled = false;\n  std::unique_ptr<Impl> mImpl;')
edit(header, edit_header)

guest = generated / 'guest/VkEncoder.cpp'
def edit_guest(text):
    for name in ('vkCreateEmuSurfaceRME', 'vkQueuePresentKHR'):
        start = text.index('VkEncoder::' + name + '(')
        brace = text.index('{', start)
        text = text[:brace + 1] + '\n  if (!m_rmeWsiEnabled) return VK_ERROR_EXTENSION_NOT_PRESENT;' + text[brace + 1:]
    return text
edit(guest, edit_guest)

host = generated / 'host/VkDecoder.cpp'
def edit_host(text):
    text = replace_once(text, '#include "VkDecoder.h"',
        '#include "VkDecoder.h"\n#include "rme_wsi_protocol.h"')
    text = replace_once(text, '  bool m_forSnapshotLoad = false;',
        '  uint32_t m_rmeWsiVersion = 0;\n  bool m_forSnapshotLoad = false;')
    text = replace_once(text, 'class VkDecoder::Impl {',
        'bool rmeWsiProtocolCompiled() { return true; }\n\nclass VkDecoder::Impl {')
    text = replace_once(text, '    switch (opcode) {', '''    switch (opcode) {
    case RME_WSI_NEGOTIATE_OPCODE: {
      uint32_t requested = 0;
      if (packetLen == 3 * sizeof(uint32_t))
        memcpy(&requested, *readStreamPtrPtr, sizeof(requested));
      m_rmeWsiVersion = rme_wsi_select_version(packetLen, requested);
      vkStream->write(&m_rmeWsiVersion, sizeof(m_rmeWsiVersion));
      vkStream->commitWrite();
      break;
    }
''')
    text = replace_once(text, 'rme_vk_guest_surface_create(unboxed_instance, vk->vkGetInstanceProcAddr, pSurface)',
        '(m_rmeWsiVersion ? rme_vk_guest_surface_create(unboxed_instance, vk->vkGetInstanceProcAddr, pSurface) : VK_ERROR_EXTENSION_NOT_PRESENT)')
    text = replace_once(text, 'if (pPresentInfo && pPresentInfo->pResults)\n        vkStream->write(pPresentInfo->pResults,',
        'if (m_rmeWsiVersion && pPresentInfo && pPresentInfo->pResults)\n        vkStream->write(pPresentInfo->pResults,')
    return text
edit(host, edit_host)

connection = a.host_connection.read_text()
old_process_init = '    processPipeInit(fd, connType, noRenderControlEnc);'
new_process_init = '''    if (!processPipeInit(fd, connType, noRenderControlEnc)) {
        ALOGE("Failed to initialize process pipe\\n");
        return nullptr;
    }'''
if old_process_init in connection:
    connection = replace_once(connection, old_process_init, new_process_init)
elif new_process_init not in connection:
    raise SystemExit('missing the existing process-pipe failure handling')
connection = replace_once(connection, '#include "HostConnection.h"',
    '#include "HostConnection.h"\n#include "rme_wsi_protocol.h"')
connection = replace_once(connection,
    '        m_vkEnc = new VkEncoder(m_stream, getGlobalHealthMonitor());',
    '        m_vkEnc = new VkEncoder(m_stream, getGlobalHealthMonitor());\n'
    '        if (m_rcEnc) m_vkEnc->rmeSetWsiEnabled(rme_wsi_negotiate(m_stream, m_hostExtensions));')
(a.output / 'HostConnection.cpp').write_text(connection)
for directory in (a.output, generated / 'host'):
    shutil.copy2(here / 'rme_wsi_protocol.h', directory / 'rme_wsi_protocol.h')
print('WSI_NEGOTIATION_PATCHED', generated)
