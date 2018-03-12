/*
 * vkrunner
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include "vr-feature-offsets.h"
#include "vr-vk.h"
#include "vr-util.h"

#define VR_FEATURE(n)                                                   \
        { VR_STRINGIFY(n), offsetof(VkPhysicalDeviceFeatures, n) },

const struct vr_feature_offset
vr_feature_offsets[] = {
        VR_FEATURE(robustBufferAccess)
        VR_FEATURE(fullDrawIndexUint32)
        VR_FEATURE(imageCubeArray)
        VR_FEATURE(independentBlend)
        VR_FEATURE(geometryShader)
        VR_FEATURE(tessellationShader)
        VR_FEATURE(sampleRateShading)
        VR_FEATURE(dualSrcBlend)
        VR_FEATURE(logicOp)
        VR_FEATURE(multiDrawIndirect)
        VR_FEATURE(drawIndirectFirstInstance)
        VR_FEATURE(depthClamp)
        VR_FEATURE(depthBiasClamp)
        VR_FEATURE(fillModeNonSolid)
        VR_FEATURE(depthBounds)
        VR_FEATURE(wideLines)
        VR_FEATURE(largePoints)
        VR_FEATURE(alphaToOne)
        VR_FEATURE(multiViewport)
        VR_FEATURE(samplerAnisotropy)
        VR_FEATURE(textureCompressionETC2)
        VR_FEATURE(textureCompressionASTC_LDR)
        VR_FEATURE(textureCompressionBC)
        VR_FEATURE(occlusionQueryPrecise)
        VR_FEATURE(pipelineStatisticsQuery)
        VR_FEATURE(vertexPipelineStoresAndAtomics)
        VR_FEATURE(fragmentStoresAndAtomics)
        VR_FEATURE(shaderTessellationAndGeometryPointSize)
        VR_FEATURE(shaderImageGatherExtended)
        VR_FEATURE(shaderStorageImageExtendedFormats)
        VR_FEATURE(shaderStorageImageMultisample)
        VR_FEATURE(shaderStorageImageReadWithoutFormat)
        VR_FEATURE(shaderStorageImageWriteWithoutFormat)
        VR_FEATURE(shaderUniformBufferArrayDynamicIndexing)
        VR_FEATURE(shaderSampledImageArrayDynamicIndexing)
        VR_FEATURE(shaderStorageBufferArrayDynamicIndexing)
        VR_FEATURE(shaderStorageImageArrayDynamicIndexing)
        VR_FEATURE(shaderClipDistance)
        VR_FEATURE(shaderCullDistance)
        VR_FEATURE(shaderFloat64)
        VR_FEATURE(shaderInt64)
        VR_FEATURE(shaderInt16)
        VR_FEATURE(shaderResourceResidency)
        VR_FEATURE(shaderResourceMinLod)
        VR_FEATURE(sparseBinding)
        VR_FEATURE(sparseResidencyBuffer)
        VR_FEATURE(sparseResidencyImage2D)
        VR_FEATURE(sparseResidencyImage3D)
        VR_FEATURE(sparseResidency2Samples)
        VR_FEATURE(sparseResidency4Samples)
        VR_FEATURE(sparseResidency8Samples)
        VR_FEATURE(sparseResidency16Samples)
        VR_FEATURE(sparseResidencyAliased)
        VR_FEATURE(variableMultisampleRate)
        VR_FEATURE(inheritedQueries)
        { NULL, 0 }
};
