#ifndef RME_OPENCL_ABI_IMAGE_H
#define RME_OPENCL_ABI_IMAGE_H

struct image_layout { size_t row, rows, slices, row_pitch, slice_pitch, bytes; };

static size_t pixel_size(cl_image_format format) {
    size_t channels;
    switch (format.image_channel_order) {
    case CL_R: case CL_A: case CL_INTENSITY: case CL_LUMINANCE: channels = 1; break;
    case CL_RG: case CL_RA: case CL_Rx: channels = 2; break;
    case CL_RGB: case CL_RGx: channels = 3; break;
    case CL_RGBA: case CL_BGRA: case CL_ARGB: case CL_RGBx: channels = 4; break;
    default: return 0;
    }
    switch (format.image_channel_data_type) {
    case CL_UNORM_SHORT_565: case CL_UNORM_SHORT_555: return 2;
    case CL_UNORM_INT_101010: return 4;
    case CL_SNORM_INT8: case CL_UNORM_INT8: case CL_SIGNED_INT8: case CL_UNSIGNED_INT8: return channels;
    case CL_SNORM_INT16: case CL_UNORM_INT16: case CL_SIGNED_INT16: case CL_UNSIGNED_INT16:
    case CL_HALF_FLOAT: return 2 * channels;
    case CL_SIGNED_INT32: case CL_UNSIGNED_INT32: case CL_FLOAT: return 4 * channels;
    default: return 0;
    }
}

static int layout_image(struct image_layout *layout, cl_mem_object_type type, const size_t region[3],
                        size_t pixel, size_t row_pitch, size_t slice_pitch) {
    if (!pixel || !region[0] || region[0] > RME_CL_ABI_MAX / pixel || !region[1] || !region[2]) return 0;
    layout->row = region[0] * pixel;
    layout->rows = type == CL_MEM_OBJECT_IMAGE1D_ARRAY ? 1 : region[1];
    layout->slices = type == CL_MEM_OBJECT_IMAGE1D_ARRAY ? region[1] : region[2];
    layout->row_pitch = row_pitch ? row_pitch : layout->row;
    if (layout->row_pitch < layout->row || layout->rows > RME_CL_ABI_MAX / layout->row_pitch) return 0;
    size_t minimum_slice = layout->row_pitch * layout->rows;
    layout->slice_pitch = slice_pitch ? slice_pitch : minimum_slice;
    if (layout->slice_pitch < minimum_slice || layout->slices > RME_CL_ABI_MAX / layout->slice_pitch) return 0;
    layout->bytes = (layout->slices - 1) * layout->slice_pitch + (layout->rows - 1) * layout->row_pitch + layout->row;
    return 1;
}

static void descriptor_region(const cl_image_desc *desc, size_t region[3]) {
    region[0] = desc->image_width;
    region[1] = desc->image_height ? desc->image_height : 1;
    region[2] = desc->image_depth ? desc->image_depth : 1;
    if (desc->image_type == CL_MEM_OBJECT_IMAGE1D_ARRAY) region[1] = desc->image_array_size;
    if (desc->image_type == CL_MEM_OBJECT_IMAGE2D_ARRAY) region[2] = desc->image_array_size;
}

#endif
