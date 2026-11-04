package app.aemu.core

import java.net.ServerSocket
import java.net.Socket
import org.junit.Assert.assertEquals
import org.junit.Test

class InputHandshakeTest {
    @Test fun explicitDeviceHandshakeResetsTimeout() {
        ServerSocket(0).use { server ->
            Socket("127.0.0.1", server.localPort).use { peer ->
                server.accept().use { socket ->
                    peer.getOutputStream().write(1)
                    assertEquals(1, readInputDevice({ socket.soTimeout = it }, { socket.getInputStream().read() }))
                    assertEquals(0, socket.soTimeout)
                }
            }
        }
    }

    @Test fun legacyPeerCanRemainIdleAfterHandshakeTimeout() {
        ServerSocket(0).use { server ->
            Socket("127.0.0.1", server.localPort).use { peer ->
                server.accept().use { socket ->
                    assertEquals(-1, readInputDevice({ socket.soTimeout = it }, { socket.getInputStream().read() }))
                    assertEquals(0, socket.soTimeout)
                    peer.getOutputStream().write(42)
                    assertEquals(42, socket.getInputStream().read())
                }
            }
        }
    }
}
