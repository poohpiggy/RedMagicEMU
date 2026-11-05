package app.aemu.ui

import app.aemu.importer.ImportCancelledException
import org.junit.Assert.*
import org.junit.Test
import java.io.IOException

class ImportOutcomeTest {
    @Test fun explicitCancellationIsNotAnError() {
        assertTrue(isUserCancellation(ImportCancelledException()))
    }
    @Test fun unrelatedErrorAndCleanupFailureAreNotHidden() {
        assertFalse(isUserCancellation(IOException("cancelled")))
        assertFalse(isUserCancellation(IOException("disk full")))
        assertFalse(isUserCancellation(ImportCancelledException().apply { addSuppressed(IOException("cleanup failed")) }))
        assertFalse(isUserCancellation(IOException("cleanup failed", ImportCancelledException())))
    }
}
