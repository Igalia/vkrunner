/* This file will be included multiple times with different values for
 * the macros.
 */
VR_PIPELINE_STRUCT_BEGIN(pInputAssemblyState)
VR_PIPELINE_PROP(INT,
                 VkPipelineInputAssemblyStateCreateInfo,
                 topology)
VR_PIPELINE_PROP(BOOL,
                 VkPipelineInputAssemblyStateCreateInfo,
                 primitiveRestartEnable)
VR_PIPELINE_STRUCT_END()

VR_PIPELINE_STRUCT_BEGIN(pTessellationState)
VR_PIPELINE_PROP(INT,
                 VkPipelineTessellationStateCreateInfo,
                 patchControlPoints)
VR_PIPELINE_STRUCT_END()

VR_PIPELINE_STRUCT_BEGIN(pRasterizationState)
VR_PIPELINE_PROP(BOOL,
                 VkPipelineRasterizationStateCreateInfo,
                 depthClampEnable)
VR_PIPELINE_PROP(BOOL,
                 VkPipelineRasterizationStateCreateInfo,
                 rasterizerDiscardEnable)
VR_PIPELINE_PROP(INT,
                 VkPipelineRasterizationStateCreateInfo,
                 polygonMode)
VR_PIPELINE_PROP(INT,
                 VkPipelineRasterizationStateCreateInfo,
                 cullMode)
VR_PIPELINE_PROP(INT,
                 VkPipelineRasterizationStateCreateInfo,
                 frontFace)
VR_PIPELINE_PROP(INT,
                 VkPipelineRasterizationStateCreateInfo,
                 depthBiasEnable)
VR_PIPELINE_PROP(FLOAT,
                 VkPipelineRasterizationStateCreateInfo,
                 depthBiasConstantFactor)
VR_PIPELINE_PROP(FLOAT,
                 VkPipelineRasterizationStateCreateInfo,
                 depthBiasClamp)
VR_PIPELINE_PROP(FLOAT,
                 VkPipelineRasterizationStateCreateInfo,
                 depthBiasSlopeFactor)
VR_PIPELINE_PROP(FLOAT,
                 VkPipelineRasterizationStateCreateInfo,
                 lineWidth)
VR_PIPELINE_STRUCT_END()

VR_PIPELINE_STRUCT_BEGIN(pColorBlendState)
VR_PIPELINE_PROP(BOOL, VkPipelineColorBlendStateCreateInfo, logicOpEnable)
VR_PIPELINE_PROP(INT, VkPipelineColorBlendStateCreateInfo, logicOp)
VR_PIPELINE_STRUCT_END()

VR_PIPELINE_STRUCT_BEGIN2(pColorBlendState,
                          VkPipelineColorBlendStateCreateInfo,
                          pAttachments)
VR_PIPELINE_PROP(INT, VkPipelineColorBlendAttachmentState, blendEnable)
VR_PIPELINE_PROP(INT, VkPipelineColorBlendAttachmentState, srcColorBlendFactor)
VR_PIPELINE_PROP(INT, VkPipelineColorBlendAttachmentState, dstColorBlendFactor)
VR_PIPELINE_PROP(INT, VkPipelineColorBlendAttachmentState, colorBlendOp)
VR_PIPELINE_PROP(INT, VkPipelineColorBlendAttachmentState, srcAlphaBlendFactor)
VR_PIPELINE_PROP(INT, VkPipelineColorBlendAttachmentState, dstAlphaBlendFactor)
VR_PIPELINE_PROP(INT, VkPipelineColorBlendAttachmentState, alphaBlendOp)
VR_PIPELINE_PROP(INT, VkPipelineColorBlendAttachmentState, colorWriteMask)
VR_PIPELINE_STRUCT_END()

VR_PIPELINE_STRUCT_BEGIN(pDepthStencilState)
VR_PIPELINE_PROP(BOOL, VkPipelineDepthStencilStateCreateInfo, depthTestEnable)
VR_PIPELINE_PROP(BOOL, VkPipelineDepthStencilStateCreateInfo, depthWriteEnable)
VR_PIPELINE_PROP(INT, VkPipelineDepthStencilStateCreateInfo, depthCompareOp)
VR_PIPELINE_PROP(BOOL,
                 VkPipelineDepthStencilStateCreateInfo,
                 depthBoundsTestEnable)
VR_PIPELINE_PROP(BOOL, VkPipelineDepthStencilStateCreateInfo, stencilTestEnable)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      front.failOp,
                      front_failOp)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      front.passOp,
                      front_passOp)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      front.depthFailOp,
                      front_depthFailOp)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      front.compareOp,
                      front_compareOp)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      front.compareMask,
                      front_compareMask)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      front.writeMask,
                      front_writeMask)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      front.reference,
                      front_reference)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      back.failOp,
                      back_failOp)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      back.passOp,
                      back_passOp)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      back.depthFailOp,
                      back_depthFailOp)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      back.compareOp,
                      back_compareOp)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      back.compareMask,
                      back_compareMask)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      back.writeMask,
                      back_writeMask)
VR_PIPELINE_PROP_NAME(INT,
                      VkPipelineDepthStencilStateCreateInfo,
                      back.reference,
                      back_reference)
VR_PIPELINE_PROP(FLOAT, VkPipelineDepthStencilStateCreateInfo, minDepthBounds)
VR_PIPELINE_PROP(FLOAT, VkPipelineDepthStencilStateCreateInfo, maxDepthBounds)
VR_PIPELINE_STRUCT_END()
