package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class TargetSocPropertyTest {
    @get:Rule val temp = TemporaryFolder()

    @Test fun admittedTargetGetsMissingSocModel() {
        PropArea.create(temp.newFile(), 36).use { area ->
            area.applyTargetBootProperties()
            assertEquals("SM8850", area.get("ro.soc.model"))
        }
    }

    @Test fun emptySocModelIsFilled() {
        PropArea.create(temp.newFile(), 36).use { area ->
            area.put("ro.soc.model", "")
            area.applyTargetBootProperties()
            assertEquals("SM8850", area.get("ro.soc.model"))
        }
    }

    @Test fun declaredSocModelIsPreserved() {
        PropArea.create(temp.newFile(), 36).use { area ->
            area.put("ro.soc.model", "existing")
            area.applyTargetBootProperties()
            assertEquals("existing", area.get("ro.soc.model"))
        }
    }
}
