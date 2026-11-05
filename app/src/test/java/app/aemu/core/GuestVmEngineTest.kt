package app.aemu.core

import android.content.ContextWrapper
import android.content.pm.ApplicationInfo
import java.io.File
import java.nio.file.Files
import org.junit.Assert.*
import org.junit.Test

class GuestVmEngineTest {
    @Test fun arm64InstanceWithLegacyEngineUsesNativeRunnerWithoutChangingStoredImage() {
        val dir = Files.createTempDirectory("guest-vm-engine").toFile()
        try {
            val ctx = object : ContextWrapper(null) {
                override fun getFilesDir(): File = dir
                override fun getApplicationInfo(): ApplicationInfo = ApplicationInfo().apply { nativeLibraryDir = dir.absolutePath }
            }
            val image = GuestImage(id="abcdef", name="test", release="16", api=36, brand="REDMAGIC", model="NX809J", skin="AOSP", engine=Engine.KK, abi="arm64-v8a", bootclasspath="", exports=emptyMap(), dirs=emptyList(), services=emptyList(), sdcardPath="/sdcard", settings=VmSettings(), createdAt=0, fingerprint=InstanceAdmission.SYSTEM_FINGERPRINT)
            val vm = GuestVm(ctx, image)
            assertEquals(Engine.N64, vm.engine)
            assertEquals(Engine.N64, vm.runner.engine)
            assertEquals("libaemu64_36.so", vm.runner.qemu.name)
            assertEquals(image.fingerprint, vm.img.fingerprint)
            assertEquals(Engine.KK, image.engine)
        } finally { dir.deleteRecursively() }
    }
}
