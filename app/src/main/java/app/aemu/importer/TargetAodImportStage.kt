package app.aemu.importer

import app.aemu.core.InstanceAdmission
import app.aemu.core.Engine
import app.aemu.core.TargetCarrierProperties
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardOpenOption.CREATE_NEW
import java.nio.file.StandardOpenOption.READ
import java.nio.file.StandardOpenOption.WRITE
import java.nio.file.attribute.BasicFileAttributes
import java.security.MessageDigest

/**
 * Stages compatibility files only for a fresh, unpublished instance. Callers
 * must supply an independently trusted proxy digest; preparation alone does not
 * prove that SystemUI loaded it. Inputs are copied before verification and no
 * original firmware file is overwritten.
 *
 * Like the existing extraction writer, the parent-path checks assume this
 * app-private lease is exclusively owned by cooperating processes. They do not
 * protect against an independently mutating process with the same UID.
 */
internal object TargetAodImportStage {
    private const val APK_SHA = "24c77f7fa300940133c4ab6fd35cf26d47021e04251e653fad56a86b9bd23c9d"
    private const val MODEL_SHA = "d2f72574ac8f68144e2be04f168ad1eafe45ef043a279388f0b2fd36d6cb695f"
    private const val MASK_SHA = "019bfeea5f8e5109befa231f8febce04732320ff1e256e51fa28b092e6231c7a"
    private const val MASK_SIZE = 1216 * 2688
    private const val MAX_APK = 512L * 1024 * 1024
    private const val MAX_MODEL = 256L * 1024 * 1024
    private const val MAX_PROXY = 4 * 1024 * 1024
    private val apkParts = listOf("system", "system_ext", "priv-app", "SystemUI_MFV", "SystemUI_MFV.apk")
    private val modelParts = listOf("system", "etc", "custom_config", "app", "systemUI", "depth", "wallpaperMattingCache.bin")

    data class Inputs(val proxy: ByteArray, val proxySha256: String, val mask: ByteArray)
    data class Target(val api: Int, val model: String, val abi: String, val engine: Engine)
    sealed interface Result {
        data object Prepared : Result
        data class NotApplicable(val reason: String) : Result
        /** Optional cause is retained for the caller to record without exposing file contents. */
        data class Unavailable(val reason: String, val cause: Exception? = null) : Result
    }

    fun prepareFixture(lease: PendingImports.Lease, id: String, target: Target, identity: TestIdentity,
                       cancelled: () -> Boolean, afterProxy: (File) -> Unit, source: () -> Inputs): Result =
        prepareInternal(lease, id, target, Identity(identity.apkSha256, identity.maskSha256,
            identity.maskSize, identity.modelSha256), cancelled, afterProxy, source)

    /** Caller must supply only a pinned, independently reviewed proxy digest. */
    fun prepare(lease: PendingImports.Lease, id: String, target: Target,
                cancelled: () -> Boolean = { false }, source: () -> Inputs): Result =
        prepareInternal(lease, id, target, Identity(APK_SHA, MASK_SHA, MASK_SIZE, MODEL_SHA), cancelled, {}, source)

    /** Synthetic hashes allow tests to exercise the exact transaction without firmware data. */
    data class TestIdentity(val apkSha256: String, val maskSha256: String, val maskSize: Int, val modelSha256: String)
    fun stageFixture(lease: PendingImports.Lease, id: String, inputs: Inputs, identity: TestIdentity,
                     cancelled: () -> Boolean = { false }) {
        val result = prepareFixture(lease, id, Target(36, "NX809J", "arm64-v8a", Engine.N64), identity, cancelled, {}) { inputs }
        if (result != Result.Prepared) throw IOException("AOD staging did not complete: $result")
    }

    private data class Identity(val apkSha: String, val maskSha: String, val maskSize: Int, val modelSha: String)

    private fun prepareInternal(lease: PendingImports.Lease, id: String, target: Target, identity: Identity,
                                cancelled: () -> Boolean, afterProxy: (File) -> Unit, source: () -> Inputs): Result {
        fun checkCancel() { if (cancelled()) throw ImportCancelledException() }
        checkCancel()
        return lease.withUnpublishedRoot(id) { root ->
            if (target.api != 36 || target.model != "NX809J" || target.abi != "arm64-v8a" || target.engine != Engine.N64)
                return@withUnpublishedRoot Result.NotApplicable("unsupported guest")
            val instance = root.parentFile ?: throw IOException("Missing pending instance")
            try { InstanceAdmission.requireFingerprint(InstanceAdmission.readFingerprint(instance)) }
            catch (_: IllegalStateException) { return@withUnpublishedRoot Result.NotApplicable("different firmware") }
            val carrier = TargetCarrierProperties.load(root, 36, "NX809J", "arm64-v8a")
            if (carrier is TargetCarrierProperties.Result.MissingImage || carrier is TargetCarrierProperties.Result.NotTarget)
                return@withUnpublishedRoot Result.NotApplicable("no matching carrier")
            if (carrier is TargetCarrierProperties.Result.Invalid)
                return@withUnpublishedRoot Result.Unavailable("invalid carrier configuration")
            if (carrier !is TargetCarrierProperties.Result.Loaded || carrier.fallback || carrier.variant != "IP_MC_W_CN" ||
                carrier.properties["ro.vendor.build.def_theme_name"] != "default_theme_64")
                return@withUnpublishedRoot Result.NotApplicable("different carrier theme")
            val base = root.toPath().toAbsolutePath().normalize()
            val apk: Path
            try {
                apk = checkedFile(base, apkParts)
                val model = checkedFile(base, modelParts)
                if (!matchesFile(apk, identity.apkSha, MAX_APK) || !matchesFile(model, identity.modelSha, MAX_MODEL))
                    return@withUnpublishedRoot Result.Unavailable("firmware files differ")
            } catch (e: IOException) { return@withUnpublishedRoot Result.Unavailable("firmware files unavailable", e) }
            checkCancel()
            val supplied = try { source() }
                catch (e: ImportCancelledException) { throw e }
                catch (e: IOException) { return@withUnpublishedRoot Result.Unavailable("assets unavailable", e) }
                catch (e: java.util.concurrent.CancellationException) { throw e }
                catch (e: RuntimeException) { return@withUnpublishedRoot Result.Unavailable("assets unavailable", e) }
            if (supplied.proxy.isEmpty() || supplied.proxy.size > MAX_PROXY ||
                supplied.mask.size != identity.maskSize || identity.maskSize !in 1..MAX_PROXY)
                return@withUnpublishedRoot Result.Unavailable("invalid asset size")
            val proxy = supplied.proxy.copyOf()
            val mask = supplied.mask.copyOf()
            if (!matches(proxy, supplied.proxySha256) || !matches(mask, identity.maskSha))
                return@withUnpublishedRoot Result.Unavailable("asset digest mismatch")
            val appDir = apk.parent
            val lib = appDir.resolve("lib")
            val arm = lib.resolve("arm64")
            val created = mutableListOf<Pair<Path, Any>>()
            fun mark(path: Path) {
                try {
                    val key = Files.readAttributes(path, BasicFileAttributes::class.java, NOFOLLOW_LINKS).fileKey()
                        ?: throw IOException("AOD created file identity unavailable")
                    created.add(path to key)
                } catch (e: IOException) {
                    // Without an identity it is unsafe to assume that the created entry was cleaned.
                    e.addSuppressed(IOException("AOD created path cannot be safely identified"))
                    throw e
                }
            }
            try {
                // Never reuse a firmware-supplied or preexisting lib directory.
                Files.createDirectory(lib); mark(lib)
                Files.createDirectory(arm); mark(arm)
                checkCancel()
                fun write(name: String, data: ByteArray) {
                    val path = arm.resolve(name)
                    Files.newByteChannel(path, setOf(CREATE_NEW, WRITE, NOFOLLOW_LINKS)).use { channel ->
                        mark(path)
                        val buffer = java.nio.ByteBuffer.wrap(data)
                        while (buffer.hasRemaining()) channel.write(buffer)
                    }
                }
                write("libzte_saliency.so", proxy)
                checkCancel()
                afterProxy(arm.toFile())
                write("rme_aod_mask.raw", mask)
                checkCancel()
                if (!matchesFile(arm.resolve("libzte_saliency.so"), supplied.proxySha256, MAX_PROXY.toLong()) ||
                    !matchesFile(arm.resolve("rme_aod_mask.raw"), identity.maskSha, MAX_PROXY.toLong()))
                    throw IOException("AOD staged data failed verification")
            } catch (failure: Throwable) {
                for ((path, key) in created.asReversed()) try {
                    val current = Files.readAttributes(path, BasicFileAttributes::class.java, NOFOLLOW_LINKS).fileKey()
                    if (current != key) throw IOException("AOD staging identity changed")
                    Files.delete(path)
                } catch (cleanup: Throwable) { failure.addSuppressed(cleanup) }
                if (failure is ImportCancelledException || failure.suppressed.isNotEmpty() || failure !is IOException)
                    throw failure
                return@withUnpublishedRoot Result.Unavailable("staging failed; no partial files retained", failure)
            }
            Result.Prepared
        }
    }

    private fun checkedFile(base: Path, parts: List<String>): Path {
        var path = base
        for ((index, part) in parts.withIndex()) {
            path = path.resolve(part)
            val good = if (index == parts.lastIndex) Files.isRegularFile(path, NOFOLLOW_LINKS)
                       else Files.isDirectory(path, NOFOLLOW_LINKS)
            if (!good) throw IOException("Invalid AOD firmware path")
        }
        return path
    }

    private fun matches(data: ByteArray, expected: String): Boolean =
        digestMatches(MessageDigest.getInstance("SHA-256").digest(data), expected)

    private fun matchesFile(path: Path, expected: String, max: Long): Boolean {
        if (Files.size(path) !in 1..max) return false
        val digest = MessageDigest.getInstance("SHA-256")
        var total = 0L
        Files.newInputStream(path, READ, NOFOLLOW_LINKS).use { input ->
            val bytes = ByteArray(1024 * 1024)
            while (true) {
                val n = input.read(bytes)
                if (n < 0) break
                if (n == 0) continue
                total += n
                if (total > max) return false
                digest.update(bytes, 0, n)
            }
        }
        return total > 0 && digestMatches(digest.digest(), expected)
    }

    private fun digestMatches(actual: ByteArray, expected: String): Boolean {
        if (expected.length != 64 || !expected.all { it in '0'..'9' || it in 'a'..'f' }) return false
        val bytes = ByteArray(32) { expected.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        return MessageDigest.isEqual(actual, bytes)
    }
}
