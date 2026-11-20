package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Test

class InitPlanTargetHardwareTest {
    private val hardware = listOf(
        GuestService("perf2-hal-1-0", listOf("/vendor/bin/hw/vendor.qti.hardware.perf2-hal-service")),
        GuestService("vendor.sensors-hal-multihal", listOf("/vendor/bin/hw/android.hardware.sensors-service.multihal")),
        GuestService("vendor.thermal-hal", listOf("/vendor/bin/hw/android.hardware.thermal-service.qti")),
        GuestService("vendor.powerstats-hal", listOf("/vendor/bin/hw/android.hardware.powerstats-service.qti")),
        GuestService("vendor.ir-default", listOf("/vendor/bin/hw/android.hardware.ir-service")),
        GuestService("vendor.qti.hardware.memtrack", listOf("/vendor/bin/hw/vendor.qti.hardware.memtrack-service")),
        GuestService("vendor.boot-qti", listOf("/vendor/bin/hw/android.hardware.boot-service.qti")),
        GuestService("vendor.camera-provider", listOf("/vendor/bin/hw/vendor.qti.camera.provider-service_64")),
        GuestService("vendor.qti.hardware.display.allocator", listOf("/vendor/bin/hw/vendor.qti.hardware.display.allocator-service")),
        GuestService("vendor.qti.light", listOf("/vendor/bin/hw/vendor.qti.hardware.lights.service")),
        GuestService("vendor.audio-hal-aidl", listOf("/vendor/bin/hw/audiohalservice.qti")),
        GuestService("vendor-qti-media-c2-hal-1-0", listOf("/vendor/bin/hw/vendor.qti.media.c2@1.0-service")),
        GuestService("vendor.usb-hal", listOf("/vendor/bin/hw/android.hardware.usb-service.qti")),
        GuestService("vendor.wifi_hal_legacy", listOf("/vendor/bin/hw/android.hardware.wifi-service")),
        GuestService("nfc_hal_service", listOf("/vendor/bin/hw/android.hardware.nfc-service-aidl")),
        GuestService("vendor.qconfig", listOf("/vendor/bin/hw/qconfigservice")),
        GuestService("vendor.zte.ldd-default", listOf("/vendor/bin/hw/vendor.zte.ldd.cmd-service")),
        GuestService("vendor.capabilityconfigstore", listOf("/vendor/bin/hw/vendor.qti.hardware.capabilityconfigstore-service")),
        GuestService("vendor.servicetrackeraidl", listOf("/vendor/bin/hw/vendor.qti.hardware.servicetrackeraidl-service")),
        GuestService("feature_enabler_client", listOf("/vendor/bin/feature_enabler_client")),
        GuestService("vendor.zte.voice-service", listOf("/vendor/bin/hw/vendor.zte.voice-service")),
        GuestService("vendor.zte.llm-service", listOf("/vendor/bin/hw/vendor.zte.llm-service")),
        GuestService("vendor.zte.bge-service", listOf("/vendor/bin/hw/vendor.zte.bge-service")),
        GuestService("vendor.zte.bert-service", listOf("/vendor/bin/hw/vendor.zte.bert-service")),
        GuestService("vendor.trustedui", listOf("/vendor/bin/hw/vendor.qti.hardware.trustedui-aidl-service-qti")),
        GuestService("vendor.secureprocessor", listOf("/vendor/bin/hw/vendor.qti.hardware.secureprocessor")),
        GuestService("vendor.qti.snapdragonServices", listOf("/vendor/bin/snapdragon_services")),
        GuestService("vendor.qspa-service", listOf("/vendor/bin/vendor.qti.qspa-service")),
        GuestService("vendor.qms", listOf("/vendor/bin/qms")),
        GuestService("vendor.dspservice", listOf("/vendor/bin/dspservice")),
        GuestService("vendor.drm-clearkey-service", listOf("/vendor/bin/hw/android.hardware.drm-service.clearkey")),
        GuestService("vendor.c2pa", listOf("/vendor/bin/hw/vendor.qti.hardware.c2pa-aidl-service-qti")),
        GuestService("qspmhal", listOf("/vendor/bin/vendor.qti.qspmhal-service")),
        GuestService("poweropt-service", listOf("/vendor/bin/poweropt-service")),
        GuestService("lowi-server", listOf("/vendor/bin/lowi-server")),
        GuestService("vendordiag", listOf("/vendor/bin/vendordiag")),
        GuestService("xtra-daemon", listOf("/vendor/bin/xtra-daemon")),
        GuestService("vendor.keymint", listOf("/vendor/bin/hw/android.hardware.security.onekeymint-service-qti")),
        GuestService("vendor.secretkeeper", listOf("/vendor/bin/hw/android.hardware.security.secretkeeper-service-qti")),
    )

    private val kept = listOf(
        GuestService("servicemanager", listOf("/system/bin/servicemanager")),
        GuestService("sensors-hal-1-0", listOf("/vendor/bin/hw/android.hardware.sensors@1.0-service")),
        GuestService("vendor.power", listOf("/vendor/bin/hw/android.hardware.power-service")),
        GuestService("camera-provider-2-4", listOf("/vendor/bin/hw/android.hardware.camera.provider@2.4-service")),
        GuestService("vendor.gralloc-2-0", listOf("/vendor/bin/hw/android.hardware.graphics.allocator@2.0-service")),
        GuestService("vendor.audio-hal", listOf("/vendor/bin/hw/android.hardware.audio.service")),
        GuestService("media.swcodec", listOf("/apex/com.android.media.swcodec/bin/mediaswcodec")),
        GuestService("netd", listOf("/system/bin/netd")),
        GuestService("drm-hal-1-0", listOf("/vendor/bin/hw/android.hardware.drm@1.0-service")),
        GuestService("vendor.keymaster-3-0", listOf("/vendor/bin/hw/android.hardware.keymaster@3.0-service")),
        GuestService("keystore2", listOf("/system/bin/keystore2", "/data/misc/keystore")),
        GuestService("idmap2d", listOf("/system/bin/idmap2d")),
        GuestService("mediaserver", listOf("/system/bin/mediaserver")),
        GuestService("zygote", listOf("/system/bin/app_process64")),
    )

    @Test fun onlyStockHardwareServicesAreOmittedOnTarget() {
        val plan = kept.take(1) + hardware + kept.drop(1)
        assertEquals(kept, InitPlan.forGuestRuntime(plan, 36, "NX809J", "arm64-v8a", Engine.N64))
    }

    @Test fun otherFirmwareKeepsItsServicePlan() {
        val plan = hardware + kept
        assertEquals(plan, InitPlan.forGuestRuntime(plan, 36, "Other", "arm64-v8a", Engine.N64))
        assertEquals(plan, InitPlan.forGuestRuntime(plan, 35, "NX809J", "arm64-v8a", Engine.N64))
    }
}
