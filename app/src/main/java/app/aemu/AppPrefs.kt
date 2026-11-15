package app.aemu

import android.content.Context
import android.content.res.Configuration
import app.aemu.core.VmSettings
import java.util.Locale

/** Настройки самого приложения (не отдельной системы): язык, тема, умолчания для новых систем. */
object AppPrefs {
    private const val FILE = "app"
    private const val DEFAULT_LANGUAGE = "en-US"

    /** Языки интерфейса: код BCP-47 → самоназвание. Пустой код — как в системе. */
    val LANGUAGES = listOf(
        "" to "",
        DEFAULT_LANGUAGE to "English (United States)",
        "en-GB" to "English (United Kingdom)",
        "zh-CN" to "简体中文",
        "zh-TW" to "繁體中文",
        "zh-HK" to "繁體中文（香港）",
    )
    private val SUPPORTED_LANGUAGES = LANGUAGES.mapTo(mutableSetOf()) { it.first }.apply { remove("") }

    const val THEME_SYSTEM = 0
    const val THEME_LIGHT = 1
    const val THEME_DARK = 2

    private fun sp(ctx: Context) = ctx.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    fun prefs(ctx: Context) = sp(ctx)

    /** order of the machines on the home screen: one of SORT_* (remembered) */
    const val SORT_VERSION = 0
    const val SORT_NAME = 1
    const val SORT_SKIN = 2
    const val SORT_LAST_RUN = 3
    const val SORT_SIZE = 4
    fun sortMode(ctx: Context): Int = sp(ctx).getInt("sort_mode", SORT_VERSION)
    fun setSortMode(ctx: Context, v: Int) = sp(ctx).edit().putInt("sort_mode", v).apply()
    /** when a machine was last started (0 = never since this was kept) */
    fun lastRun(ctx: Context, id: String): Long = sp(ctx).getLong("last_run_$id", 0L)
    fun setLastRun(ctx: Context, id: String, v: Long) = sp(ctx).edit().putLong("last_run_$id", v).apply()

    private fun canonicalLanguageTag(tag: String): String {
        val locale = Locale.forLanguageTag(tag.trim())
        val language = locale.language.lowercase(Locale.ROOT)
        if (language.isEmpty()) return ""
        val country = locale.country.uppercase(Locale.ROOT)
        return if (country.isEmpty()) language else "$language-$country"
    }

    private fun selectedLanguageTag(tag: String): String {
        val canonical = canonicalLanguageTag(tag)
        return when {
            canonical.isEmpty() -> ""
            canonical == "en" -> DEFAULT_LANGUAGE
            canonical in SUPPORTED_LANGUAGES -> canonical
            else -> DEFAULT_LANGUAGE
        }
    }

    internal fun resolveLanguageTag(selectedTag: String, systemTag: String): String {
        val selected = selectedLanguageTag(selectedTag)
        if (selected.isNotEmpty()) return selected
        val system = canonicalLanguageTag(systemTag)
        return if (system in SUPPORTED_LANGUAGES) system else DEFAULT_LANGUAGE
    }

    private fun rawLanguage(ctx: Context): String = sp(ctx).getString("lang", "") ?: ""

    fun language(ctx: Context): String = selectedLanguageTag(rawLanguage(ctx))
    fun setLanguage(ctx: Context, tag: String) = sp(ctx).edit().putString("lang", selectedLanguageTag(tag)).apply()

    private fun effectiveLanguage(ctx: Context): String {
        val locales = ctx.resources.configuration.locales
        val systemTag = if (locales.size() == 0) "" else locales[0].toLanguageTag()
        return resolveLanguageTag(rawLanguage(ctx), systemTag)
    }

    fun theme(ctx: Context): Int = sp(ctx).getInt("theme", THEME_SYSTEM)
    fun setTheme(ctx: Context, v: Int) = sp(ctx).edit().putInt("theme", v).apply()

    fun dynamicColor(ctx: Context): Boolean = sp(ctx).getBoolean("dynamic", false)
    fun setDynamicColor(ctx: Context, v: Boolean) = sp(ctx).edit().putBoolean("dynamic", v).apply()

    /** Умолчания, которые получает только что импортированная система. */
    fun defaults(ctx: Context): VmSettings {
        val p = sp(ctx)
        val d = VmSettings()
        return d.copy(
            gpu = p.getBoolean("def_gpu", d.gpu),
            jit = p.getBoolean("def_jit", d.jit),
            netProxy = p.getBoolean("def_proxy", d.netProxy),
            showNavBar = p.getBoolean("def_nav", d.showNavBar),
            keepScreenOn = p.getBoolean("def_awake", d.keepScreenOn),
            showLed = p.getBoolean("def_led", d.showLed),
        )
    }

    fun setDefaults(ctx: Context, s: VmSettings) = sp(ctx).edit()
        .putBoolean("def_gpu", s.gpu).putBoolean("def_jit", s.jit).putBoolean("def_proxy", s.netProxy)
        .putBoolean("def_nav", s.showNavBar).putBoolean("def_awake", s.keepScreenOn).putBoolean("def_led", s.showLed).apply()

    /** Контекст с выбранным языком — для attachBaseContext каждой активности. */
    /**
     * Language and theme of the app applied to an activity's resources. The theme goes into the night bits of
     * uiMode: values-night (window background), isSystemInDarkTheme and enableEdgeToEdge's status bar icons then
     * follow the app's choice — before, a dark app theme on a light system kept a white window background and dark,
     * invisible status bar icons.
     */
    fun wrap(base: Context): Context {
        val tag = effectiveLanguage(base)
        val night = when (theme(base)) {
            THEME_LIGHT -> Configuration.UI_MODE_NIGHT_NO
            THEME_DARK -> Configuration.UI_MODE_NIGHT_YES
            else -> -1
        }
        if (tag.isEmpty() && night < 0) return base
        val cfg = Configuration(base.resources.configuration)
        if (tag.isNotEmpty()) {
            val loc = Locale.forLanguageTag(tag)
            Locale.setDefault(loc)
            cfg.setLocale(loc)
            cfg.setLayoutDirection(loc)
        }
        if (night >= 0) cfg.uiMode = (cfg.uiMode and Configuration.UI_MODE_NIGHT_MASK.inv()) or night
        return base.createConfigurationContext(cfg)
    }
}
