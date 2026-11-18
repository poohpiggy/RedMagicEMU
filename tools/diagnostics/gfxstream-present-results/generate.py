#!/usr/bin/env python3
"""Generate an isolated d7a swapchain encoder for protocol inspection only."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, help="must be a new directory")
    parser.add_argument("--clang-bin", type=Path, help="directory containing clang-format")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--candidate", action="store_true", help="generate paired experimental response encoding")
    mode.add_argument("--single-only", action="store_true", help="generate single-present experiment using the original wire")
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    registry = source / "codegen/vulkan/vulkan-docs-next"
    shutil.copytree(registry / "scripts", output / "scripts")
    shutil.copytree(registry / "xml", output / "xml")
    config = output / "scripts/cerealgenerator.py"
    text = config.read_text()
    old = '"VK_KHR_swapchain" : HOST_MODULES,'
    new = ('"VK_KHR_swapchain" : HOST_MODULES + ["VkEncoder", '
           '"goldfish_vk_reserved_marshaling_guest", "goldfish_vk_deepcopy_guest", '
           '"goldfish_vk_counting_guest", "goldfish_vk_transform_guest", '
           '"goldfish_vk_marshaling_guest", "goldfish_vk_extension_structs_guest"],')
    if text.count(old) != 1:
        raise ValueError("unexpected generator configuration")
    config.write_text(text.replace(old, new, 1))
    if args.single_only:
        encoder = output / "scripts/cereal/encoder.py"
        text = encoder.read_text()
        anchor = 'def emit_default_encoding(typeInfo, api, cgen):\n'
        insert = '''    if api.name == "vkQueuePresentKHR":
        cgen.line("if (pPresentInfo->swapchainCount != 1 || pPresentInfo->pResults) {")
        cgen.line("    if (pPresentInfo->pResults) for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) pPresentInfo->pResults[i] = VK_ERROR_FEATURE_NOT_PRESENT;")
        cgen.line("    return VK_ERROR_FEATURE_NOT_PRESENT;")
        cgen.line("}")
'''
        if text.count(anchor) != 1:
            raise ValueError("unexpected encoder function")
        encoder.write_text(text.replace(anchor, anchor + insert, 1))
    if args.candidate:
        encoder = output / "scripts/cereal/encoder.py"
        text = encoder.read_text()
        start = text.index("def emit_default_encoding(typeInfo, api, cgen):")
        end = text.index("\n## Custom encoding definitions", start)
        prefix, body, suffix = text[:start], text[start:end], text[end:]
        anchor = '    emit_parameter_encode_preamble_write(typeInfo, api, cgen)\n'
        insert = '''    if api.name == "vkQueuePresentKHR":
        cgen.stmt("VkResult* rmeOriginalResults = pPresentInfo->pResults")
        cgen.stmt("VkPresentInfoKHR rmePresentInfo = *pPresentInfo")
        cgen.stmt("std::vector<VkResult> rmeResultStorage(rmeOriginalResults ? pPresentInfo->swapchainCount : 0, VK_SUCCESS)")
        cgen.stmt("if (rmeOriginalResults) rmePresentInfo.pResults = rmeResultStorage.data()")
        cgen.stmt("pPresentInfo = &rmePresentInfo")
'''
        if body.count(anchor) != 1:
            raise ValueError("unexpected encoder preamble")
        body = body.replace(anchor, insert + anchor, 1)
        anchor = '    emit_return_unmarshal(typeInfo, api, cgen)\n'
        insert = '''    if api.name == "vkQueuePresentKHR":
        cgen.stmt("if (rmeOriginalResults) stream->read(rmeOriginalResults, pPresentInfo->swapchainCount * sizeof(VkResult))")
'''
        if body.count(anchor) != 1:
            raise ValueError("unexpected encoder response")
        encoder.write_text(prefix + body.replace(anchor, insert + anchor, 1) + suffix)
        decoder = output / "scripts/cereal/decoder.py"
        text = decoder.read_text()
        anchor = 'def emit_decode_return_writeback(api, cgen):\n'
        insert = '''    if api.name == "vkQueuePresentKHR":
        cgen.stmt("if (pPresentInfo && pPresentInfo->pResults) vkStream->write(pPresentInfo->pResults, pPresentInfo->swapchainCount * sizeof(VkResult))")
'''
        if text.count(anchor) != 1:
            raise ValueError("unexpected decoder response")
        decoder.write_text(text.replace(anchor, anchor + insert, 1))
    guest = output / "guest"
    host = output / "host"
    cereal = host / "cereal"
    guest.mkdir()
    (cereal / "common").mkdir(parents=True)
    env = dict(os.environ, GFXSTREAM_GUEST_ENCODER_DIR=str(guest),
               GFXSTREAM_HOST_DECODER_DIR=str(host), GFXSTREAM_OUTPUT_DIR=str(cereal),
               GFXSTREAM_SCRIPTS_DIR=str(source / "scripts"))
    if args.clang_bin:
        env["PATH"] = str(args.clang_bin.resolve(strict=True)) + os.pathsep + env["PATH"]
    subprocess.run(["python3", str(output / "scripts/genvk.py"),
                    "-registry", str(output / "xml/vk.xml"), "-registryGfxstream",
                    str(output / "xml/vk_gfxstream.xml"), "cereal", "-o", str(cereal)],
                   env=env, check=True)
    print("Isolated output:", output)


if __name__ == "__main__":
    main()
