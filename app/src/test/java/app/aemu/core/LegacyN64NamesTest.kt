package app.aemu.core

import org.junit.Assert.assertEquals
import org.junit.Test

class LegacyN64NamesTest {
    @Test fun oldKernelAndGsiNamesRemainDistinctForRestoration() {
        val kernel = LegacyN64Names.parse("system#lib64#old.so\nvendor#etc#old.rc\n")
        val gsi = LegacyN64Names.parse("vendor#etc#gsi.rc\n")
        val current = setOf("system#lib64#current.so", "system#lib64#old.so")
        val anyKernel = LegacyN64Names.kernel(current, kernel)
        assertEquals(setOf("system#lib64#old.so", "system#lib64#current.so", "vendor#etc#old.rc"), anyKernel)
        assertEquals(anyKernel + "vendor#etc#gsi.rc", LegacyN64Names.layer(anyKernel, gsi))
    }
}
