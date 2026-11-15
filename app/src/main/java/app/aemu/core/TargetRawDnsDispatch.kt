package app.aemu.core

/** Preserve the legacy unsupported response until the target verifier is available. */
internal fun targetRawDnsReply(
    api: Int,
    args: List<String>,
    terminated: Boolean,
    resolver: ((Long, ByteArray, Int) -> DnsRawProtocol.Result)?,
): ByteArray? {
    if (api != 36) return null
    if (resolver == null) return unsupportedTargetRawDns(api, args)
    if (!terminated) return DnsRawProtocol.error(90 /* EMSGSIZE */)
    return DnsRawProtocol.reply(args, resolver)
}
