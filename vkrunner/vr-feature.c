/* Automatically generated by make-features.py */

#include "config.h"
#include <stddef.h>

#include "vr-feature.h"
#include "vr-vk.h"

static const struct vr_feature_offset
offsets_KHR_16BIT_STORAGE[] = {
        {
                .name = "storageBuffer16BitAccess",
                .offset = offsetof(VkPhysicalDevice16BitStorageFeaturesKHR, storageBuffer16BitAccess)
        },
        {
                .name = "uniformAndStorageBuffer16BitAccess",
                .offset = offsetof(VkPhysicalDevice16BitStorageFeaturesKHR, uniformAndStorageBuffer16BitAccess)
        },
        {
                .name = "storagePushConstant16",
                .offset = offsetof(VkPhysicalDevice16BitStorageFeaturesKHR, storagePushConstant16)
        },
        {
                .name = "storageInputOutput16",
                .offset = offsetof(VkPhysicalDevice16BitStorageFeaturesKHR, storageInputOutput16)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_KHR_8BIT_STORAGE[] = {
        {
                .name = "storageBuffer8BitAccess",
                .offset = offsetof(VkPhysicalDevice8BitStorageFeaturesKHR, storageBuffer8BitAccess)
        },
        {
                .name = "uniformAndStorageBuffer8BitAccess",
                .offset = offsetof(VkPhysicalDevice8BitStorageFeaturesKHR, uniformAndStorageBuffer8BitAccess)
        },
        {
                .name = "storagePushConstant8",
                .offset = offsetof(VkPhysicalDevice8BitStorageFeaturesKHR, storagePushConstant8)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_EXT_ASTC_DECODE_MODE[] = {
        {
                .name = "decodeModeSharedExponent",
                .offset = offsetof(VkPhysicalDeviceASTCDecodeFeaturesEXT, decodeModeSharedExponent)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_EXT_BLEND_OPERATION_ADVANCED[] = {
        {
                .name = "advancedBlendCoherentOperations",
                .offset = offsetof(VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT, advancedBlendCoherentOperations)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_EXT_BUFFER_DEVICE_ADDRESS[] = {
        {
                .name = "bufferDeviceAddress",
                .offset = offsetof(VkPhysicalDeviceBufferAddressFeaturesEXT, bufferDeviceAddress)
        },
        {
                .name = "bufferDeviceAddressCaptureReplay",
                .offset = offsetof(VkPhysicalDeviceBufferAddressFeaturesEXT, bufferDeviceAddressCaptureReplay)
        },
        {
                .name = "bufferDeviceAddressMultiDevice",
                .offset = offsetof(VkPhysicalDeviceBufferAddressFeaturesEXT, bufferDeviceAddressMultiDevice)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_NV_COMPUTE_SHADER_DERIVATIVES[] = {
        {
                .name = "computeDerivativeGroupQuads",
                .offset = offsetof(VkPhysicalDeviceComputeShaderDerivativesFeaturesNV, computeDerivativeGroupQuads)
        },
        {
                .name = "computeDerivativeGroupLinear",
                .offset = offsetof(VkPhysicalDeviceComputeShaderDerivativesFeaturesNV, computeDerivativeGroupLinear)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_EXT_CONDITIONAL_RENDERING[] = {
        {
                .name = "conditionalRendering",
                .offset = offsetof(VkPhysicalDeviceConditionalRenderingFeaturesEXT, conditionalRendering)
        },
        {
                .name = "inheritedConditionalRendering",
                .offset = offsetof(VkPhysicalDeviceConditionalRenderingFeaturesEXT, inheritedConditionalRendering)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_NV_CORNER_SAMPLED_IMAGE[] = {
        {
                .name = "cornerSampledImage",
                .offset = offsetof(VkPhysicalDeviceCornerSampledImageFeaturesNV, cornerSampledImage)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_EXT_DESCRIPTOR_INDEXING[] = {
        {
                .name = "shaderInputAttachmentArrayDynamicIndexing",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, shaderInputAttachmentArrayDynamicIndexing)
        },
        {
                .name = "shaderUniformTexelBufferArrayDynamicIndexing",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, shaderUniformTexelBufferArrayDynamicIndexing)
        },
        {
                .name = "shaderStorageTexelBufferArrayDynamicIndexing",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, shaderStorageTexelBufferArrayDynamicIndexing)
        },
        {
                .name = "shaderUniformBufferArrayNonUniformIndexing",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, shaderUniformBufferArrayNonUniformIndexing)
        },
        {
                .name = "shaderSampledImageArrayNonUniformIndexing",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, shaderSampledImageArrayNonUniformIndexing)
        },
        {
                .name = "shaderStorageBufferArrayNonUniformIndexing",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, shaderStorageBufferArrayNonUniformIndexing)
        },
        {
                .name = "shaderStorageImageArrayNonUniformIndexing",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, shaderStorageImageArrayNonUniformIndexing)
        },
        {
                .name = "shaderInputAttachmentArrayNonUniformIndexing",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, shaderInputAttachmentArrayNonUniformIndexing)
        },
        {
                .name = "shaderUniformTexelBufferArrayNonUniformIndexing",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, shaderUniformTexelBufferArrayNonUniformIndexing)
        },
        {
                .name = "shaderStorageTexelBufferArrayNonUniformIndexing",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, shaderStorageTexelBufferArrayNonUniformIndexing)
        },
        {
                .name = "descriptorBindingUniformBufferUpdateAfterBind",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, descriptorBindingUniformBufferUpdateAfterBind)
        },
        {
                .name = "descriptorBindingSampledImageUpdateAfterBind",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, descriptorBindingSampledImageUpdateAfterBind)
        },
        {
                .name = "descriptorBindingStorageImageUpdateAfterBind",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, descriptorBindingStorageImageUpdateAfterBind)
        },
        {
                .name = "descriptorBindingStorageBufferUpdateAfterBind",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, descriptorBindingStorageBufferUpdateAfterBind)
        },
        {
                .name = "descriptorBindingUniformTexelBufferUpdateAfterBind",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, descriptorBindingUniformTexelBufferUpdateAfterBind)
        },
        {
                .name = "descriptorBindingStorageTexelBufferUpdateAfterBind",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, descriptorBindingStorageTexelBufferUpdateAfterBind)
        },
        {
                .name = "descriptorBindingUpdateUnusedWhilePending",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, descriptorBindingUpdateUnusedWhilePending)
        },
        {
                .name = "descriptorBindingPartiallyBound",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, descriptorBindingPartiallyBound)
        },
        {
                .name = "descriptorBindingVariableDescriptorCount",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, descriptorBindingVariableDescriptorCount)
        },
        {
                .name = "runtimeDescriptorArray",
                .offset = offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, runtimeDescriptorArray)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_NV_SCISSOR_EXCLUSIVE[] = {
        {
                .name = "exclusiveScissor",
                .offset = offsetof(VkPhysicalDeviceExclusiveScissorFeaturesNV, exclusiveScissor)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_KHR_SHADER_FLOAT16_INT8[] = {
        {
                .name = "shaderFloat16",
                .offset = offsetof(VkPhysicalDeviceFloat16Int8FeaturesKHR, shaderFloat16)
        },
        {
                .name = "shaderInt8",
                .offset = offsetof(VkPhysicalDeviceFloat16Int8FeaturesKHR, shaderInt8)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_EXT_FRAGMENT_DENSITY_MAP[] = {
        {
                .name = "fragmentDensityMap",
                .offset = offsetof(VkPhysicalDeviceFragmentDensityMapFeaturesEXT, fragmentDensityMap)
        },
        {
                .name = "fragmentDensityMapDynamic",
                .offset = offsetof(VkPhysicalDeviceFragmentDensityMapFeaturesEXT, fragmentDensityMapDynamic)
        },
        {
                .name = "fragmentDensityMapNonSubsampledImages",
                .offset = offsetof(VkPhysicalDeviceFragmentDensityMapFeaturesEXT, fragmentDensityMapNonSubsampledImages)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_NV_FRAGMENT_SHADER_BARYCENTRIC[] = {
        {
                .name = "fragmentShaderBarycentric",
                .offset = offsetof(VkPhysicalDeviceFragmentShaderBarycentricFeaturesNV, fragmentShaderBarycentric)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_EXT_INLINE_UNIFORM_BLOCK[] = {
        {
                .name = "inlineUniformBlock",
                .offset = offsetof(VkPhysicalDeviceInlineUniformBlockFeaturesEXT, inlineUniformBlock)
        },
        {
                .name = "descriptorBindingInlineUniformBlockUpdateAfterBind",
                .offset = offsetof(VkPhysicalDeviceInlineUniformBlockFeaturesEXT, descriptorBindingInlineUniformBlockUpdateAfterBind)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_EXT_MEMORY_PRIORITY[] = {
        {
                .name = "memoryPriority",
                .offset = offsetof(VkPhysicalDeviceMemoryPriorityFeaturesEXT, memoryPriority)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_NV_MESH_SHADER[] = {
        {
                .name = "taskShader",
                .offset = offsetof(VkPhysicalDeviceMeshShaderFeaturesNV, taskShader)
        },
        {
                .name = "meshShader",
                .offset = offsetof(VkPhysicalDeviceMeshShaderFeaturesNV, meshShader)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_KHR_MULTIVIEW[] = {
        {
                .name = "multiview",
                .offset = offsetof(VkPhysicalDeviceMultiviewFeaturesKHR, multiview)
        },
        {
                .name = "multiviewGeometryShader",
                .offset = offsetof(VkPhysicalDeviceMultiviewFeaturesKHR, multiviewGeometryShader)
        },
        {
                .name = "multiviewTessellationShader",
                .offset = offsetof(VkPhysicalDeviceMultiviewFeaturesKHR, multiviewTessellationShader)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_NV_REPRESENTATIVE_FRAGMENT_TEST[] = {
        {
                .name = "representativeFragmentTest",
                .offset = offsetof(VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV, representativeFragmentTest)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_KHR_SAMPLER_YCBCR_CONVERSION[] = {
        {
                .name = "samplerYcbcrConversion",
                .offset = offsetof(VkPhysicalDeviceSamplerYcbcrConversionFeaturesKHR, samplerYcbcrConversion)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_EXT_SCALAR_BLOCK_LAYOUT[] = {
        {
                .name = "scalarBlockLayout",
                .offset = offsetof(VkPhysicalDeviceScalarBlockLayoutFeaturesEXT, scalarBlockLayout)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_KHR_SHADER_ATOMIC_INT64[] = {
        {
                .name = "shaderBufferInt64Atomics",
                .offset = offsetof(VkPhysicalDeviceShaderAtomicInt64FeaturesKHR, shaderBufferInt64Atomics)
        },
        {
                .name = "shaderSharedInt64Atomics",
                .offset = offsetof(VkPhysicalDeviceShaderAtomicInt64FeaturesKHR, shaderSharedInt64Atomics)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_NV_SHADER_IMAGE_FOOTPRINT[] = {
        {
                .name = "imageFootprint",
                .offset = offsetof(VkPhysicalDeviceShaderImageFootprintFeaturesNV, imageFootprint)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_NV_SHADING_RATE_IMAGE[] = {
        {
                .name = "shadingRateImage",
                .offset = offsetof(VkPhysicalDeviceShadingRateImageFeaturesNV, shadingRateImage)
        },
        {
                .name = "shadingRateCoarseSampleOrder",
                .offset = offsetof(VkPhysicalDeviceShadingRateImageFeaturesNV, shadingRateCoarseSampleOrder)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_EXT_TRANSFORM_FEEDBACK[] = {
        {
                .name = "transformFeedback",
                .offset = offsetof(VkPhysicalDeviceTransformFeedbackFeaturesEXT, transformFeedback)
        },
        {
                .name = "geometryStreams",
                .offset = offsetof(VkPhysicalDeviceTransformFeedbackFeaturesEXT, geometryStreams)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_KHR_VARIABLE_POINTERS[] = {
        {
                .name = "variablePointersStorageBuffer",
                .offset = offsetof(VkPhysicalDeviceVariablePointerFeaturesKHR, variablePointersStorageBuffer)
        },
        {
                .name = "variablePointers",
                .offset = offsetof(VkPhysicalDeviceVariablePointerFeaturesKHR, variablePointers)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_EXT_VERTEX_ATTRIBUTE_DIVISOR[] = {
        {
                .name = "vertexAttributeInstanceRateDivisor",
                .offset = offsetof(VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT, vertexAttributeInstanceRateDivisor)
        },
        {
                .name = "vertexAttributeInstanceRateZeroDivisor",
                .offset = offsetof(VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT, vertexAttributeInstanceRateZeroDivisor)
        },
        { .name = NULL }
};

static const struct vr_feature_offset
offsets_KHR_VULKAN_MEMORY_MODEL[] = {
        {
                .name = "vulkanMemoryModel",
                .offset = offsetof(VkPhysicalDeviceVulkanMemoryModelFeaturesKHR, vulkanMemoryModel)
        },
        {
                .name = "vulkanMemoryModelDeviceScope",
                .offset = offsetof(VkPhysicalDeviceVulkanMemoryModelFeaturesKHR, vulkanMemoryModelDeviceScope)
        },
        { .name = NULL }
};

const struct vr_feature_offset
vr_feature_base_offsets[] = {
        {
                .name = "robustBufferAccess",
                .offset = offsetof(VkPhysicalDeviceFeatures, robustBufferAccess)
        },
        {
                .name = "fullDrawIndexUint32",
                .offset = offsetof(VkPhysicalDeviceFeatures, fullDrawIndexUint32)
        },
        {
                .name = "imageCubeArray",
                .offset = offsetof(VkPhysicalDeviceFeatures, imageCubeArray)
        },
        {
                .name = "independentBlend",
                .offset = offsetof(VkPhysicalDeviceFeatures, independentBlend)
        },
        {
                .name = "geometryShader",
                .offset = offsetof(VkPhysicalDeviceFeatures, geometryShader)
        },
        {
                .name = "tessellationShader",
                .offset = offsetof(VkPhysicalDeviceFeatures, tessellationShader)
        },
        {
                .name = "sampleRateShading",
                .offset = offsetof(VkPhysicalDeviceFeatures, sampleRateShading)
        },
        {
                .name = "dualSrcBlend",
                .offset = offsetof(VkPhysicalDeviceFeatures, dualSrcBlend)
        },
        {
                .name = "logicOp",
                .offset = offsetof(VkPhysicalDeviceFeatures, logicOp)
        },
        {
                .name = "multiDrawIndirect",
                .offset = offsetof(VkPhysicalDeviceFeatures, multiDrawIndirect)
        },
        {
                .name = "drawIndirectFirstInstance",
                .offset = offsetof(VkPhysicalDeviceFeatures, drawIndirectFirstInstance)
        },
        {
                .name = "depthClamp",
                .offset = offsetof(VkPhysicalDeviceFeatures, depthClamp)
        },
        {
                .name = "depthBiasClamp",
                .offset = offsetof(VkPhysicalDeviceFeatures, depthBiasClamp)
        },
        {
                .name = "fillModeNonSolid",
                .offset = offsetof(VkPhysicalDeviceFeatures, fillModeNonSolid)
        },
        {
                .name = "depthBounds",
                .offset = offsetof(VkPhysicalDeviceFeatures, depthBounds)
        },
        {
                .name = "wideLines",
                .offset = offsetof(VkPhysicalDeviceFeatures, wideLines)
        },
        {
                .name = "largePoints",
                .offset = offsetof(VkPhysicalDeviceFeatures, largePoints)
        },
        {
                .name = "alphaToOne",
                .offset = offsetof(VkPhysicalDeviceFeatures, alphaToOne)
        },
        {
                .name = "multiViewport",
                .offset = offsetof(VkPhysicalDeviceFeatures, multiViewport)
        },
        {
                .name = "samplerAnisotropy",
                .offset = offsetof(VkPhysicalDeviceFeatures, samplerAnisotropy)
        },
        {
                .name = "textureCompressionETC2",
                .offset = offsetof(VkPhysicalDeviceFeatures, textureCompressionETC2)
        },
        {
                .name = "textureCompressionASTC_LDR",
                .offset = offsetof(VkPhysicalDeviceFeatures, textureCompressionASTC_LDR)
        },
        {
                .name = "textureCompressionBC",
                .offset = offsetof(VkPhysicalDeviceFeatures, textureCompressionBC)
        },
        {
                .name = "occlusionQueryPrecise",
                .offset = offsetof(VkPhysicalDeviceFeatures, occlusionQueryPrecise)
        },
        {
                .name = "pipelineStatisticsQuery",
                .offset = offsetof(VkPhysicalDeviceFeatures, pipelineStatisticsQuery)
        },
        {
                .name = "vertexPipelineStoresAndAtomics",
                .offset = offsetof(VkPhysicalDeviceFeatures, vertexPipelineStoresAndAtomics)
        },
        {
                .name = "fragmentStoresAndAtomics",
                .offset = offsetof(VkPhysicalDeviceFeatures, fragmentStoresAndAtomics)
        },
        {
                .name = "shaderTessellationAndGeometryPointSize",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderTessellationAndGeometryPointSize)
        },
        {
                .name = "shaderImageGatherExtended",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderImageGatherExtended)
        },
        {
                .name = "shaderStorageImageExtendedFormats",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderStorageImageExtendedFormats)
        },
        {
                .name = "shaderStorageImageMultisample",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderStorageImageMultisample)
        },
        {
                .name = "shaderStorageImageReadWithoutFormat",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderStorageImageReadWithoutFormat)
        },
        {
                .name = "shaderStorageImageWriteWithoutFormat",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderStorageImageWriteWithoutFormat)
        },
        {
                .name = "shaderUniformBufferArrayDynamicIndexing",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderUniformBufferArrayDynamicIndexing)
        },
        {
                .name = "shaderSampledImageArrayDynamicIndexing",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderSampledImageArrayDynamicIndexing)
        },
        {
                .name = "shaderStorageBufferArrayDynamicIndexing",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderStorageBufferArrayDynamicIndexing)
        },
        {
                .name = "shaderStorageImageArrayDynamicIndexing",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderStorageImageArrayDynamicIndexing)
        },
        {
                .name = "shaderClipDistance",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderClipDistance)
        },
        {
                .name = "shaderCullDistance",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderCullDistance)
        },
        {
                .name = "shaderFloat64",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderFloat64)
        },
        {
                .name = "shaderInt64",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderInt64)
        },
        {
                .name = "shaderInt16",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderInt16)
        },
        {
                .name = "shaderResourceResidency",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderResourceResidency)
        },
        {
                .name = "shaderResourceMinLod",
                .offset = offsetof(VkPhysicalDeviceFeatures, shaderResourceMinLod)
        },
        {
                .name = "sparseBinding",
                .offset = offsetof(VkPhysicalDeviceFeatures, sparseBinding)
        },
        {
                .name = "sparseResidencyBuffer",
                .offset = offsetof(VkPhysicalDeviceFeatures, sparseResidencyBuffer)
        },
        {
                .name = "sparseResidencyImage2D",
                .offset = offsetof(VkPhysicalDeviceFeatures, sparseResidencyImage2D)
        },
        {
                .name = "sparseResidencyImage3D",
                .offset = offsetof(VkPhysicalDeviceFeatures, sparseResidencyImage3D)
        },
        {
                .name = "sparseResidency2Samples",
                .offset = offsetof(VkPhysicalDeviceFeatures, sparseResidency2Samples)
        },
        {
                .name = "sparseResidency4Samples",
                .offset = offsetof(VkPhysicalDeviceFeatures, sparseResidency4Samples)
        },
        {
                .name = "sparseResidency8Samples",
                .offset = offsetof(VkPhysicalDeviceFeatures, sparseResidency8Samples)
        },
        {
                .name = "sparseResidency16Samples",
                .offset = offsetof(VkPhysicalDeviceFeatures, sparseResidency16Samples)
        },
        {
                .name = "sparseResidencyAliased",
                .offset = offsetof(VkPhysicalDeviceFeatures, sparseResidencyAliased)
        },
        {
                .name = "variableMultisampleRate",
                .offset = offsetof(VkPhysicalDeviceFeatures, variableMultisampleRate)
        },
        {
                .name = "inheritedQueries",
                .offset = offsetof(VkPhysicalDeviceFeatures, inheritedQueries)
        },
        { .name = NULL }
};

const struct vr_feature_extension
vr_feature_extensions[] = {
        {
                .name = VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDevice16BitStorageFeaturesKHR),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR,
                .offsets = offsets_KHR_16BIT_STORAGE
        },
        {
                .name = VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDevice8BitStorageFeaturesKHR),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR,
                .offsets = offsets_KHR_8BIT_STORAGE
        },
        {
                .name = VK_EXT_ASTC_DECODE_MODE_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceASTCDecodeFeaturesEXT),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ASTC_DECODE_FEATURES_EXT,
                .offsets = offsets_EXT_ASTC_DECODE_MODE
        },
        {
                .name = VK_EXT_BLEND_OPERATION_ADVANCED_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT,
                .offsets = offsets_EXT_BLEND_OPERATION_ADVANCED
        },
        {
                .name = VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceBufferAddressFeaturesEXT),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_ADDRESS_FEATURES_EXT,
                .offsets = offsets_EXT_BUFFER_DEVICE_ADDRESS
        },
        {
                .name = VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceComputeShaderDerivativesFeaturesNV),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV,
                .offsets = offsets_NV_COMPUTE_SHADER_DERIVATIVES
        },
        {
                .name = VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceConditionalRenderingFeaturesEXT),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT,
                .offsets = offsets_EXT_CONDITIONAL_RENDERING
        },
        {
                .name = VK_NV_CORNER_SAMPLED_IMAGE_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceCornerSampledImageFeaturesNV),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CORNER_SAMPLED_IMAGE_FEATURES_NV,
                .offsets = offsets_NV_CORNER_SAMPLED_IMAGE
        },
        {
                .name = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT,
                .offsets = offsets_EXT_DESCRIPTOR_INDEXING
        },
        {
                .name = VK_NV_SCISSOR_EXCLUSIVE_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceExclusiveScissorFeaturesNV),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXCLUSIVE_SCISSOR_FEATURES_NV,
                .offsets = offsets_NV_SCISSOR_EXCLUSIVE
        },
        {
                .name = VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceFloat16Int8FeaturesKHR),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT16_INT8_FEATURES_KHR,
                .offsets = offsets_KHR_SHADER_FLOAT16_INT8
        },
        {
                .name = VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceFragmentDensityMapFeaturesEXT),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT,
                .offsets = offsets_EXT_FRAGMENT_DENSITY_MAP
        },
        {
                .name = VK_NV_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceFragmentShaderBarycentricFeaturesNV),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_NV,
                .offsets = offsets_NV_FRAGMENT_SHADER_BARYCENTRIC
        },
        {
                .name = VK_EXT_INLINE_UNIFORM_BLOCK_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceInlineUniformBlockFeaturesEXT),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES_EXT,
                .offsets = offsets_EXT_INLINE_UNIFORM_BLOCK
        },
        {
                .name = VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceMemoryPriorityFeaturesEXT),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT,
                .offsets = offsets_EXT_MEMORY_PRIORITY
        },
        {
                .name = VK_NV_MESH_SHADER_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceMeshShaderFeaturesNV),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV,
                .offsets = offsets_NV_MESH_SHADER
        },
        {
                .name = VK_KHR_MULTIVIEW_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceMultiviewFeaturesKHR),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR,
                .offsets = offsets_KHR_MULTIVIEW
        },
        {
                .name = VK_NV_REPRESENTATIVE_FRAGMENT_TEST_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_REPRESENTATIVE_FRAGMENT_TEST_FEATURES_NV,
                .offsets = offsets_NV_REPRESENTATIVE_FRAGMENT_TEST
        },
        {
                .name = VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceSamplerYcbcrConversionFeaturesKHR),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES_KHR,
                .offsets = offsets_KHR_SAMPLER_YCBCR_CONVERSION
        },
        {
                .name = VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceScalarBlockLayoutFeaturesEXT),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT,
                .offsets = offsets_EXT_SCALAR_BLOCK_LAYOUT
        },
        {
                .name = VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceShaderAtomicInt64FeaturesKHR),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES_KHR,
                .offsets = offsets_KHR_SHADER_ATOMIC_INT64
        },
        {
                .name = VK_NV_SHADER_IMAGE_FOOTPRINT_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceShaderImageFootprintFeaturesNV),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_FOOTPRINT_FEATURES_NV,
                .offsets = offsets_NV_SHADER_IMAGE_FOOTPRINT
        },
        {
                .name = VK_NV_SHADING_RATE_IMAGE_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceShadingRateImageFeaturesNV),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_FEATURES_NV,
                .offsets = offsets_NV_SHADING_RATE_IMAGE
        },
        {
                .name = VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceTransformFeedbackFeaturesEXT),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT,
                .offsets = offsets_EXT_TRANSFORM_FEEDBACK
        },
        {
                .name = VK_KHR_VARIABLE_POINTERS_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceVariablePointerFeaturesKHR),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTER_FEATURES_KHR,
                .offsets = offsets_KHR_VARIABLE_POINTERS
        },
        {
                .name = VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT,
                .offsets = offsets_EXT_VERTEX_ATTRIBUTE_DIVISOR
        },
        {
                .name = VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME,
                .struct_size = sizeof(VkPhysicalDeviceVulkanMemoryModelFeaturesKHR),
                .struct_type = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES_KHR,
                .offsets = offsets_KHR_VULKAN_MEMORY_MODEL
        },
        {
                .name = NULL,
                .struct_size = sizeof(VkPhysicalDeviceFeatures),
                .struct_type = 0,
                .offsets = vr_feature_base_offsets
        },
        { .struct_size = 0 }
};

