#!/usr/bin/env python3
"""Regenerate a d7a decoder with isolated swapchain metadata/image hooks."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path, help='new directory')
parser.add_argument('--clang-bin', type=Path, required=True)
parser.add_argument('--surface-present', action='store_true', help='also include the isolated surface-query and paired present experiments')
parser.add_argument('--surface-create', action='store_true', help='also include the isolated private surface-creation experiment')
args = parser.parse_args()
if args.surface_create and not args.surface_present:
    parser.error('--surface-create requires --surface-present')
source = args.source.resolve(strict=True)
out = args.output.resolve()
base_gen = Path(__file__).resolve().parent.parent / 'gfxstream-present-results/generate.py'
command = ['python3', str(base_gen), str(source), str(out), '--clang-bin', str(args.clang_bin)]
if args.surface_present:
    command.append('--candidate')
subprocess.run(command, check=True)
encoder = out / 'scripts/cereal/encoder.py'
encoder_text = encoder.read_text()
start = encoder_text.index('def emit_parameter_encode_do_parameter_write(typeInfo, api, cgen):')
end = encoder_text.index('\ndef emit_parameter_encode_read(', start)
prefix, body, suffix = encoder_text[:start], encoder_text[start:end], encoder_text[end:]
anchor = '    for p in encodingParams.toWrite:\n'
insert = '''        if api.name == "vkCreateSwapchainKHR" and p.paramName == "pSwapchain":
            cgen.stmt("/* is handle, possibly out */")
            cgen.stmt("uint64_t rmeNullSwapchain = 0")
            cgen.stmt("memcpy(*streamPtrPtr, &rmeNullSwapchain, 8)")
            cgen.stmt("*streamPtrPtr += 8")
            cgen.stmt("/* is handle, possibly out */")
            dispatchDone = True
            continue
        if api.name == "vkGetSwapchainImagesKHR" and p.paramName == "pSwapchainImageCount":
            cgen.stmt("/* RME output count */")
            cgen.stmt("uint64_t rmeCountPointer = (uint64_t)(uintptr_t)pSwapchainImageCount")
            cgen.stmt("memcpy(*streamPtrPtr, &rmeCountPointer, 8)")
            cgen.stmt("gfxstream::guest::Stream::toBe64((uint8_t*)*streamPtrPtr)")
            cgen.stmt("*streamPtrPtr += 8")
            cgen.line("if (pSwapchainImageCount) {")
            cgen.line("    uint32_t rmeCount = pSwapchainImages ? *pSwapchainImageCount : 0;")
            cgen.line("    memcpy(*streamPtrPtr, &rmeCount, sizeof(rmeCount));")
            cgen.line("    *streamPtrPtr += sizeof(rmeCount);")
            cgen.line("}")
            dispatchDone = True
            continue
        if api.name == "vkGetSwapchainImagesKHR" and p.paramName == "pSwapchainImages":
            cgen.stmt("/* is handle, possibly out */")
            cgen.stmt("uint64_t rmeImagesPointer = (uint64_t)(uintptr_t)pSwapchainImages")
            cgen.stmt("memcpy(*streamPtrPtr, &rmeImagesPointer, 8)")
            cgen.stmt("gfxstream::guest::Stream::toBe64((uint8_t*)*streamPtrPtr)")
            cgen.stmt("*streamPtrPtr += 8")
            cgen.line("if (pSwapchainImages && *pSwapchainImageCount) {")
            cgen.line("    memset(*streamPtrPtr, 0, 8 * (*pSwapchainImageCount));")
            cgen.line("    *streamPtrPtr += 8 * (*pSwapchainImageCount);")
            cgen.line("}")
            cgen.stmt("/* is handle, possibly out */")
            dispatchDone = True
            continue
'''
if body.count(anchor) != 1:
    raise ValueError('unexpected swapchain parameter write generator')
encoder_text = prefix + body.replace(anchor, anchor + insert, 1) + suffix
start = encoder_text.index('def emit_default_encoding(typeInfo, api, cgen):')
end = encoder_text.index('\n## Custom encoding definitions', start)
prefix, body, suffix = encoder_text[:start], encoder_text[start:end], encoder_text[end:]
anchor = '    emit_parameter_encode_preamble_write(typeInfo, api, cgen)\n'
insert = '''    if api.name == "vkGetSwapchainImagesKHR":
        cgen.stmt("uint32_t rmeImageCapacity = pSwapchainImages ? *pSwapchainImageCount : 0")
'''
if body.count(anchor) != 1:
    raise ValueError('unexpected image enumeration preamble generator')
body = body.replace(anchor, anchor + insert, 1)
anchor = '    emit_parameter_encode_read(typeInfo, api, cgen)\n'
replacement = '''    if api.name == "vkCreateSwapchainKHR":
        cgen.stmt("uint64_t rmeSwapchainWire = 0")
        cgen.stmt("stream->read(&rmeSwapchainWire, 8)")
    elif api.name == "vkGetSwapchainImagesKHR":
        cgen.stmt("/* RME image response */")
        cgen.stmt("uint64_t rmeCheckCount = stream->getBe64()")
        cgen.line("if (pSwapchainImageCount) {")
        cgen.line("    if (!rmeCheckCount) fprintf(stderr, \\\"fatal: pSwapchainImageCount inconsistent between guest and host\\\\n\\\");")
        cgen.line("    stream->read(pSwapchainImageCount, sizeof(uint32_t));")
        cgen.line("}")
        cgen.stmt("uint64_t rmeCheckImages = stream->getBe64()")
        cgen.stmt("std::vector<uint64_t> rmeSwapchainImageWire")
        cgen.stmt("bool rmeTooManyImages = false")
        cgen.line("if (pSwapchainImages) {")
        cgen.line("    if (!rmeCheckImages) fprintf(stderr, \\\"fatal: pSwapchainImages inconsistent between guest and host\\\\n\\\");")
        cgen.line("    if (*pSwapchainImageCount) {")
        cgen.line("        if (*pSwapchainImageCount > rmeImageCapacity) {")
        cgen.line("            rmeTooManyImages = true;")
        cgen.line("            uint64_t rmeRemaining = uint64_t(*pSwapchainImageCount) * sizeof(uint64_t);")
        cgen.line("            uint8_t rmeDiscard[256];")
        cgen.line("            while (rmeRemaining) {")
        cgen.line("                size_t rmeChunk = rmeRemaining > sizeof(rmeDiscard) ? sizeof(rmeDiscard) : size_t(rmeRemaining);")
        cgen.line("                stream->read(rmeDiscard, rmeChunk);")
        cgen.line("                rmeRemaining -= rmeChunk;")
        cgen.line("            }")
        cgen.line("        } else {")
        cgen.line("            rmeSwapchainImageWire.resize(*pSwapchainImageCount);")
        cgen.line("            stream->read(rmeSwapchainImageWire.data(), size_t(*pSwapchainImageCount) * sizeof(uint64_t));")
        cgen.line("        }")
        cgen.line("    }")
        cgen.line("}")
    else:
        emit_parameter_encode_read(typeInfo, api, cgen)
'''
if body.count(anchor) != 1:
    raise ValueError('unexpected swapchain response generator')
body = body.replace(anchor, replacement, 1)
anchor = '    emit_return_unmarshal(typeInfo, api, cgen)\n'
insert = '''    if api.name == "vkCreateSwapchainKHR":
        cgen.line("if (vkCreateSwapchainKHR_VkResult_return == VK_SUCCESS && rmeSwapchainWire) {")
        cgen.line("    stream->setHandleMapping(sResourceTracker->createMapping());")
        cgen.line("    stream->handleMapping()->mapHandles_u64_VkSwapchainKHR(&rmeSwapchainWire, pSwapchain, 1);")
        cgen.line("    stream->unsetHandleMapping();")
        cgen.line("} else {")
        cgen.line("    *pSwapchain = VK_NULL_HANDLE;")
        cgen.line("    if (vkCreateSwapchainKHR_VkResult_return == VK_SUCCESS) vkCreateSwapchainKHR_VkResult_return = VK_ERROR_INITIALIZATION_FAILED;")
        cgen.line("}")
    if api.name == "vkGetSwapchainImagesKHR":
        cgen.line("if (rmeTooManyImages) vkGetSwapchainImagesKHR_VkResult_return = VK_ERROR_INITIALIZATION_FAILED;")
        cgen.line("else if ((vkGetSwapchainImagesKHR_VkResult_return == VK_SUCCESS || vkGetSwapchainImagesKHR_VkResult_return == VK_INCOMPLETE) && pSwapchainImages && *pSwapchainImageCount) {")
        cgen.line("    if (!rmeMapSwapchainImages(swapchain, rmeSwapchainImageWire.data(), *pSwapchainImageCount, pSwapchainImages, sResourceTracker->createMapping())) vkGetSwapchainImagesKHR_VkResult_return = VK_ERROR_INITIALIZATION_FAILED;")
        cgen.line("}")
'''
if body.count(anchor) != 1:
    raise ValueError('unexpected swapchain result generator')
anchor_post = '    emit_post(typeInfo, api, cgen)\n'
if body.count(anchor_post) != 1:
    raise ValueError('unexpected swapchain post generator')
body = body.replace(anchor_post, '''    if api.name == "vkDestroySwapchainKHR":
        cgen.stmt("rmeReleaseSwapchainImages(swapchain, sResourceTracker->destroyMapping())")
''' + anchor_post, 1)
encoder.write_text(prefix + body.replace(anchor, anchor + insert, 1) + suffix)
if args.surface_present:
    config = out / 'scripts/cerealgenerator.py'
    text = config.read_text()
    old = '"VK_KHR_surface": ["goldfish_vk_dispatch"],'
    new = ('"VK_KHR_surface": HOST_MODULES + ["VkEncoder", '
           '"goldfish_vk_reserved_marshaling_guest", "goldfish_vk_deepcopy_guest", '
           '"goldfish_vk_counting_guest", "goldfish_vk_transform_guest", '
           '"goldfish_vk_marshaling_guest", "goldfish_vk_extension_structs_guest"],')
    if text.count(old) != 1:
        raise ValueError('unexpected surface generator configuration')
    config.write_text(text.replace(old, new, 1))
if args.surface_create:
    xml = out / 'xml/vk_gfxstream.xml'
    data = xml.read_text()
    marker = '    </commands>'
    requirement = '                <command name="vkGetBlobGOOGLE"/>'
    command = '''        <command successcodes="VK_SUCCESS" errorcodes="VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_INITIALIZATION_FAILED">
            <proto><type>VkResult</type> <name>vkCreateEmuSurfaceRME</name></proto>
            <param><type>VkInstance</type> <name>instance</name></param>
            <param><type>VkSurfaceKHR</type>* <name>pSurface</name></param>
        </command>
'''
    if data.count(marker) != 1 or data.count(requirement) != 1:
        raise ValueError('unexpected gfxstream registry layout')
    data = data.replace(marker, command + marker, 1)
    data = data.replace(requirement, requirement + '\n                <command name="vkCreateEmuSurfaceRME"/>', 1)
    xml.write_text(data)
path = out / 'scripts/cereal/decoder.py'
text = path.read_text()

def replace(before, after):
    global text
    if text.count(before) != 1:
        raise ValueError(f'expected one generator anchor: {before!r}')
    text = text.replace(before, after, 1)

anchor = '            emit_unmarshal(typeInfo, p, cgen, output = p.possiblyOutput(), destroy = destroy, noUnbox = noUnbox)\n'
replace(anchor, '''            if api.name == "vkGetSwapchainImagesKHR" and p.paramName == "swapchain":
                cgen.stmt("VkSwapchainKHR boxed_swapchain")
                cgen.stmt("memcpy(&boxed_swapchain, *readStreamPtrPtr, sizeof(boxed_swapchain))")
''' + anchor)
anchor = 'def emit_dispatch_call(api, cgen):\n'
insert = '''
    if api.name == "vkDestroySwapchainKHR":
        cgen.stmt("m_state->on_forgetSwapchain(unboxed_device, boxed_swapchain_preserve)")
    if api.name == "vkGetSwapchainImagesKHR":
        cgen.line("""
        VkResult vkGetSwapchainImagesKHR_VkResult_return =
            m_state->on_validateSwapchain(unboxed_device, boxed_swapchain);
        if (vkGetSwapchainImagesKHR_VkResult_return == VK_SUCCESS) {
            vkGetSwapchainImagesKHR_VkResult_return = vk->vkGetSwapchainImagesKHR(
                unboxed_device, swapchain, pSwapchainImageCount, pSwapchainImages);
        }
        if ((vkGetSwapchainImagesKHR_VkResult_return == VK_SUCCESS ||
             vkGetSwapchainImagesKHR_VkResult_return == VK_INCOMPLETE) && pSwapchainImages) {
            VkResult registration = m_state->on_registerSwapchainImages(
                unboxed_device, boxed_swapchain, *pSwapchainImageCount, pSwapchainImages);
            if (registration != VK_SUCCESS) vkGetSwapchainImagesKHR_VkResult_return = registration;
        }
        if (vkGetSwapchainImagesKHR_VkResult_return != VK_SUCCESS &&
            vkGetSwapchainImagesKHR_VkResult_return != VK_INCOMPLETE &&
            pSwapchainImages && pSwapchainImageCount) *pSwapchainImageCount = 0;
        if ((vkGetSwapchainImagesKHR_VkResult_return) == VK_ERROR_DEVICE_LOST)
            m_state->on_DeviceLost();
        m_state->on_CheckOutOfMemory(vkGetSwapchainImagesKHR_VkResult_return, opcode, context);
        """)
        return
'''
replace(anchor, anchor + insert)
anchor = '    emit_decode_parameters_writeback(typeInfo, api, cgen, autobox=not globalWrapped)\n'
insert = '''    if api.name == "vkCreateSwapchainKHR":
        cgen.stmt("if (vkCreateSwapchainKHR_VkResult_return != VK_SUCCESS) *pSwapchain = VK_NULL_HANDLE")
'''
replace(anchor, insert + anchor)
insert = '''    if api.name == "vkCreateSwapchainKHR":
        cgen.stmt("if (vkCreateSwapchainKHR_VkResult_return == VK_SUCCESS) m_state->on_recordSwapchain(unboxed_device, *pSwapchain, pCreateInfo)")
'''
replace(anchor, anchor + insert)
path.write_text(text)
env = dict(os.environ, GFXSTREAM_GUEST_ENCODER_DIR=str(out / 'guest'),
           GFXSTREAM_HOST_DECODER_DIR=str(out / 'host'), GFXSTREAM_OUTPUT_DIR=str(out / 'host/cereal'),
           GFXSTREAM_SCRIPTS_DIR=str(source / 'scripts'))
env['PATH'] = str(args.clang_bin.resolve(strict=True)) + os.pathsep + env['PATH']
subprocess.run(['python3', str(out / 'scripts/genvk.py'), '-registry', str(out / 'xml/vk.xml'),
                '-registryGfxstream', str(out / 'xml/vk_gfxstream.xml'), 'cereal',
                '-o', str(out / 'host/cereal')], env=env, check=True)
generated_encoder = out / 'guest/VkEncoder.cpp'
generated_text = generated_encoder.read_text()
include = '#include <memory>\n'
marker = 'using gfxstream::guest::Lock;\n'
if generated_text.count(include) != 1 or generated_text.count(marker) != 1:
    raise ValueError('unexpected generated encoder preamble')
cache = Path(__file__).with_name('image_wrapper_cache.inc').read_text()
generated_text = generated_text.replace(include, '#include <mutex>\n' + include, 1)
generated_text = generated_text.replace(marker, marker + '\n' + cache + '\n', 1)
generated_encoder.write_text(generated_text)
if args.surface_present:
    fixes = Path(__file__).resolve().parent.parent / 'gfxstream-surface-wire/apply_fixes.py'
    subprocess.run(['python3', str(fixes), str(out)], check=True)
if args.surface_create:
    create = Path(__file__).resolve().parent.parent / 'gfxstream-surface-wire/apply_create.py'
    subprocess.run(['python3', str(create), str(out)], check=True)
print('GENERATED_SWAPCHAIN_DECODER', out / 'host/VkDecoder.cpp')
