package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class TargetQspaPropertyTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun admittedTargetGetsMissingModemProperty() {
        PropArea.create(temp.newFile(), 36).use { area ->
            area.applyTargetBootProperties()
            assertEquals("enabled", area.get("ro.boot.vendor.qspa.modem"))
        }
    }

    @Test fun explicitModemPropertyIsPreserved() {
        PropArea.create(temp.newFile(), 36).use { area ->
            area.put("ro.boot.vendor.qspa.modem", "disabled")
            area.applyTargetBootProperties()
            assertEquals("disabled", area.get("ro.boot.vendor.qspa.modem"))
        }
    }

    @Test fun explicitEmptyModemPropertyIsPreserved() {
        PropArea.create(temp.newFile(), 36).use { area ->
            area.put("ro.boot.vendor.qspa.modem", "")
            area.applyTargetBootProperties()
            assertEquals("", area.get("ro.boot.vendor.qspa.modem"))
        }
    }

    @Test fun nonDefaultQspaVariantIsNotOverridden() {
        PropArea.create(temp.newFile(), 36).use { area ->
            area.put("ro.boot.vendor.qspa", "other")
            area.applyTargetBootProperties()
            assertNull(area.get("ro.boot.vendor.qspa.modem"))
        }
    }

    @Test fun explicitlySelectedDefaultQspaGetsModemProperty() {
        PropArea.create(temp.newFile(), 36).use { area ->
            area.put("ro.boot.vendor.qspa", "default")
            area.applyTargetBootProperties()
            assertEquals("enabled", area.get("ro.boot.vendor.qspa.modem"))
        }
    }
}
