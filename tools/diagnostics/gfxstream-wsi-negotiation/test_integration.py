#!/usr/bin/env python3
"""Check that a generated client and decoder use the same WSI negotiation."""
import argparse
import subprocess
import tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('generated', type=Path)
p.add_argument('host_connection', type=Path)
a = p.parse_args()
guest = (a.generated / 'guest/VkEncoder.cpp').read_text()
host = (a.generated / 'host/VkDecoder.cpp').read_text()
connection = a.host_connection.read_text()

requirements = {
    'guest connection negotiates only after extension discovery':
        'rme_wsi_negotiate(m_stream, m_hostExtensions)' in connection,
    'surface-create is gated by the negotiated encoder':
        'if (!m_rmeWsiEnabled) return VK_ERROR_EXTENSION_NOT_PRESENT;' in guest,
    'host consumes negotiation on its decoder instance':
        'case RME_WSI_NEGOTIATE_OPCODE:' in host and 'm_rmeWsiVersion' in host,
    'host surface creation is gated by the decoder version':
        'm_rmeWsiVersion ? rme_vk_guest_surface_create' in host,
    'host uses negotiated version when writing present results':
        'if (m_rmeWsiVersion && pPresentInfo && pPresentInfo->pResults)' in host,
}
for name, ok in requirements.items():
    if not ok:
        raise SystemExit('FAIL: ' + name)

here = Path(__file__).resolve().parent
subprocess.run(['sh', str(here / 'test.sh')], check=True)
start = host.index('    case RME_WSI_NEGOTIATE_OPCODE:')
end = host.index('\n    }', start) + len('\n    }')
case = host[start:end]
start = host.index('if (m_rmeWsiVersion && pPresentInfo && pPresentInfo->pResults)')
end = host.index('      vkStream->commitWrite();', start)
present = host[start:end]
program = r'''
#include "rme_wsi_protocol.h"
#include <cassert>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
struct Stream {
    std::vector<uint32_t> values;
    void write(const void* p, size_t size) {
        assert(size % sizeof(uint32_t) == 0);
        const auto* words = static_cast<const uint32_t*>(p);
        values.insert(values.end(), words, words + size / sizeof(uint32_t));
    }
    void commitWrite() {}
};
struct PresentInfo { uint32_t swapchainCount; uint32_t* pResults; };
struct SocketStream {
    int fd;
    int flush() { return 0; }
    int writeFully(const void* p, size_t size) {
        return send(fd, p, size, MSG_NOSIGNAL) == static_cast<ssize_t>(size) ? 0 : -1;
    }
    const unsigned char* readFully(void* p, size_t size) {
        return recv(fd, p, size, MSG_WAITALL) == static_cast<ssize_t>(size)
            ? static_cast<const unsigned char*>(p) : nullptr;
    }
};
struct Decoder {
    uint32_t m_rmeWsiVersion = 0;
    Stream output;
    void negotiate(uint32_t packetLen, uint32_t version) {
        uint8_t* data = reinterpret_cast<uint8_t*>(&version);
        uint8_t** readStreamPtrPtr = &data;
        Stream* vkStream = &output;
        switch (RME_WSI_NEGOTIATE_OPCODE) {
''' + case + r'''
        }
    }
    void present(PresentInfo* pPresentInfo) {
        Stream* vkStream = &output;
        using VkResult = uint32_t;
        VkResult vkQueuePresentKHR_VkResult_return = 33;
''' + present + r'''
    }
};
int main() {
    Decoder oldChannel, newChannel;
    uint32_t results[] = {11, 22};
    PresentInfo present{2, results};
    oldChannel.present(&present);
    assert((oldChannel.output.values == std::vector<uint32_t>{33}));
    newChannel.negotiate(12, 1);
    assert(newChannel.m_rmeWsiVersion == 1 && oldChannel.m_rmeWsiVersion == 0);
    newChannel.output.values.clear();
    newChannel.present(&present);
    assert((newChannel.output.values == std::vector<uint32_t>{11, 22, 33}));
    newChannel.negotiate(12, 2);
    assert(newChannel.m_rmeWsiVersion == 0);
    newChannel.output.values.clear();
    newChannel.present(&present);
    assert((newChannel.output.values == std::vector<uint32_t>{33}));
    newChannel.negotiate(8, 1);
    assert(newChannel.m_rmeWsiVersion == 0);

    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    SocketStream stream{sockets[0]};
    assert(!rme_wsi_negotiate(&stream, "old_service"));
    pollfd pending{sockets[1], POLLIN, 0};
    assert(poll(&pending, 1, 0) == 0);
    std::thread server([&] {
        uint32_t packet[3];
        assert(recv(sockets[1], packet, sizeof(packet), MSG_WAITALL) == sizeof(packet));
        assert(packet[0] == RME_WSI_NEGOTIATE_OPCODE);
        Decoder decoder;
        decoder.negotiate(packet[1], packet[2]);
        assert(decoder.m_rmeWsiVersion == 1 && decoder.output.values.size() == 1);
        assert(send(sockets[1], decoder.output.values.data(), sizeof(uint32_t), MSG_NOSIGNAL) == sizeof(uint32_t));
    });
    assert(rme_wsi_negotiate(&stream, RME_WSI_EXTENSION));
    server.join();
    close(sockets[0]); close(sockets[1]);
}
'''
with tempfile.TemporaryDirectory(prefix='rme-wsi-decoder-test-') as tmp:
    root = Path(tmp)
    source = root / 'test.cpp'
    source.write_text(program)
    subprocess.run(['/var/tmp/rme-gfxstream-build.hTnuwsWV/bin/cxx', '-std=c++17', '-Wall', '-Wextra',
                    '-Werror', '-pthread', '-I' + str(here), str(source), '-o', str(root / 'test')], check=True)
    subprocess.run([str(root / 'test')], check=True)
print('WSI_NEGOTIATION_CONNECTED_TO_GENERATED_DECODER_OK')
