package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files

class TargetRadioLaunchTest {
    private fun select(api: Int, model: String, abi: String, engine: Engine, enabled: Boolean): String {
        return TargetRadioLaunch.select(api, model, abi, engine, enabled).name
    }

    @Test fun onlyEnabledTargetUsesAidl() {
        assertEquals("AIDL", select(36, "NX809J", "arm64-v8a", Engine.N64, true))
        assertEquals("DISABLED", select(36, "NX809J", "arm64-v8a", Engine.N64, false))
        assertEquals("LEGACY", select(35, "NX809J", "arm64-v8a", Engine.N64, true))
        assertEquals("LEGACY", select(36, "other", "arm64-v8a", Engine.N64, true))
        assertEquals("LEGACY", select(36, "NX809J", "armeabi-v7a", Engine.N64, true))
        assertEquals("LEGACY", select(36, "NX809J", "arm64-v8a", Engine.KK, true))
        assertEquals("DISABLED", select(35, "other", "arm64-v8a", Engine.N64, false))
    }

    @Test fun onlyAidlOverridesMultiSimProperty() {
        val key = "persist.radio.multisim.config"
        val aidl = mutableMapOf(key to "dsda", "unrelated" to "preserve")
        TargetRadioLaunch.applyBootProperties(TargetRadioLaunch.Backend.AIDL, aidl)
        assertEquals("", aidl[key])
        assertEquals("preserve", aidl["unrelated"])
        val missing = mutableMapOf<String, String>()
        TargetRadioLaunch.applyBootProperties(TargetRadioLaunch.Backend.AIDL, missing)
        assertEquals("", missing[key])
        for (backend in listOf(TargetRadioLaunch.Backend.LEGACY, TargetRadioLaunch.Backend.DISABLED)) {
            val properties = mutableMapOf(key to "dsda", "unrelated" to "preserve")
            val before = properties.toMap()
            TargetRadioLaunch.applyBootProperties(backend, properties)
            assertEquals(before, properties)
            val empty = mutableMapOf<String, String>()
            TargetRadioLaunch.applyBootProperties(backend, empty)
            assertTrue(empty.isEmpty())
        }
    }

    @Test fun emptyMultiSimOverrideReplacesExistingAreaValue() {
        val file = Files.createTempFile("target-radio-props", ".bin").toFile()
        try {
            PropArea.create(file, 36).use { area ->
                val key = "persist.radio.multisim.config"
                assertTrue(area.put(key, "dsda"))
                val overrides = mutableMapOf<String, String>()
                TargetRadioLaunch.applyBootProperties(TargetRadioLaunch.Backend.AIDL, overrides)
                for ((name, value) in overrides) assertTrue(area.put(name, value))
                assertEquals("", area.get(key))
            }
        } finally { file.delete() }
    }

    @Test fun aidlRequiresItsJarAndUsesGuestProcessEntryPoint() {
        val root = Files.createTempDirectory("target-radio-launch").toFile()
        try {
            try {
                TargetRadioLaunch.service(root)
                fail("missing Radio JAR accepted")
            } catch (error: IllegalStateException) {
                assertTrue(error.message!!.contains("redmagic-radio.jar"))
            }
            val jar = root.resolve("system/framework/redmagic-radio.jar")
            checkNotNull(jar.parentFile).mkdirs()
            jar.writeBytes(byteArrayOf(1))
            val service = TargetRadioLaunch.service(root)
            assertEquals("aemu-radio", service.name)
            assertEquals(listOf("/system/bin/app_process", "-Djava.class.path=/system/framework/redmagic-radio.jar",
                "/system/bin", "app.aemu.stub.RadioServiceMain", "--experimental-slot1"), service.argv)
            assertFalse(service.onDemand)
        } finally { root.deleteRecursively() }
    }

    @Test fun registrationIsPlannedBetweenServiceManagerAndZygote() {
        fun svc(name: String, onDemand: Boolean = false) = GuestService(name, listOf("/system/bin/$name"), onDemand = onDemand)
        TargetRadioLaunch.requireServiceOrder(listOf(svc("servicemanager"), svc("surfaceflinger"), svc("zygote")))
        for (plan in listOf(emptyList(), listOf(svc("zygote"), svc("servicemanager")),
            listOf(svc("servicemanager", true), svc("zygote")), listOf(svc("servicemanager"), svc("zygote", true)))) {
            try { TargetRadioLaunch.requireServiceOrder(plan); fail("invalid service order accepted") }
            catch (_: IllegalStateException) { }
        }
    }

    @Test fun authenticatedEventArgumentsReachRadioProcess() {
        val root = Files.createTempDirectory("target-radio-event").toFile()
        try {
            val jar = root.resolve("system/framework/redmagic-radio.jar")
            checkNotNull(jar.parentFile).mkdirs()
            jar.writeBytes(byteArrayOf(1))
            val service = TargetRadioLaunch.service(root, "rme-test", 1234, 10042, 42)
            assertEquals(listOf("--experimental-slot1", "--event-socket", "rme-test", "1234", "10042", "42"), service.argv.takeLast(6))
        } finally { root.deleteRecursively() }
    }

    @Test fun inputRoutesOnlyToSelectedBackend() {
        var aidl = 0
        var legacy = 0
        fun send(backend: TargetRadioLaunch.Backend) = TargetRadioLaunch.routeEvent(backend,
            { aidl++; RadioSubmitResult.SUBMITTED }, { legacy++; true })
        assertEquals(RadioSubmitResult.SUBMITTED, send(TargetRadioLaunch.Backend.AIDL))
        assertEquals(1, aidl)
        assertEquals(0, legacy)
        assertEquals(RadioSubmitResult.SUBMITTED, send(TargetRadioLaunch.Backend.LEGACY))
        assertEquals(1, legacy)
        assertEquals(RadioSubmitResult.UNAVAILABLE, send(TargetRadioLaunch.Backend.DISABLED))
        assertEquals(1, aidl)
        assertEquals(1, legacy)
        assertEquals(RadioSubmitResult.REJECTED,
            TargetRadioLaunch.routeEvent(TargetRadioLaunch.Backend.LEGACY, { error("wrong backend") }, { false }))
    }
}
