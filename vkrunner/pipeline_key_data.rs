// Automatically generated by make-pipeline-key-data.py

const N_BOOL_PROPERTIES: usize = 10;
const N_INT_PROPERTIES: usize = 28;
const N_FLOAT_PROPERTIES: usize = 6;

const TOPOLOGY_PROP_NUM: usize = 0;
const PATCH_CONTROL_POINTS_PROP_NUM: usize = 1;

static PROPERTIES: [Property; 44] = [
    Property {
        prop_type: PropertyType::Int,
        num: 11,
        name: "alphaBlendOp",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 25,
        name: "back.compareMask",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 24,
        name: "back.compareOp",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 23,
        name: "back.depthFailOp",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 21,
        name: "back.failOp",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 22,
        name: "back.passOp",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 27,
        name: "back.reference",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 26,
        name: "back.writeMask",
    },
    Property {
        prop_type: PropertyType::Bool,
        num: 5,
        name: "blendEnable",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 8,
        name: "colorBlendOp",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 12,
        name: "colorWriteMask",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 3,
        name: "cullMode",
    },
    Property {
        prop_type: PropertyType::Float,
        num: 1,
        name: "depthBiasClamp",
    },
    Property {
        prop_type: PropertyType::Float,
        num: 0,
        name: "depthBiasConstantFactor",
    },
    Property {
        prop_type: PropertyType::Bool,
        num: 3,
        name: "depthBiasEnable",
    },
    Property {
        prop_type: PropertyType::Float,
        num: 2,
        name: "depthBiasSlopeFactor",
    },
    Property {
        prop_type: PropertyType::Bool,
        num: 8,
        name: "depthBoundsTestEnable",
    },
    Property {
        prop_type: PropertyType::Bool,
        num: 1,
        name: "depthClampEnable",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 13,
        name: "depthCompareOp",
    },
    Property {
        prop_type: PropertyType::Bool,
        num: 6,
        name: "depthTestEnable",
    },
    Property {
        prop_type: PropertyType::Bool,
        num: 7,
        name: "depthWriteEnable",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 10,
        name: "dstAlphaBlendFactor",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 7,
        name: "dstColorBlendFactor",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 18,
        name: "front.compareMask",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 17,
        name: "front.compareOp",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 16,
        name: "front.depthFailOp",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 14,
        name: "front.failOp",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 15,
        name: "front.passOp",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 20,
        name: "front.reference",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 19,
        name: "front.writeMask",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 4,
        name: "frontFace",
    },
    Property {
        prop_type: PropertyType::Float,
        num: 3,
        name: "lineWidth",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 5,
        name: "logicOp",
    },
    Property {
        prop_type: PropertyType::Bool,
        num: 4,
        name: "logicOpEnable",
    },
    Property {
        prop_type: PropertyType::Float,
        num: 5,
        name: "maxDepthBounds",
    },
    Property {
        prop_type: PropertyType::Float,
        num: 4,
        name: "minDepthBounds",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 1,
        name: "patchControlPoints",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 2,
        name: "polygonMode",
    },
    Property {
        prop_type: PropertyType::Bool,
        num: 0,
        name: "primitiveRestartEnable",
    },
    Property {
        prop_type: PropertyType::Bool,
        num: 2,
        name: "rasterizerDiscardEnable",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 9,
        name: "srcAlphaBlendFactor",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 6,
        name: "srcColorBlendFactor",
    },
    Property {
        prop_type: PropertyType::Bool,
        num: 9,
        name: "stencilTestEnable",
    },
    Property {
        prop_type: PropertyType::Int,
        num: 0,
        name: "topology",
    },
];

fn copy_properties_to_create_info(
    key: &Key,
    s: &mut vk::VkGraphicsPipelineCreateInfo
) {
    {
        let s = unsafe { std::mem::transmute::<_, &mut vk::VkPipelineInputAssemblyStateCreateInfo>(s.pInputAssemblyState) };
        s.topology = key.int_properties[0] as vk::VkPrimitiveTopology;
        s.primitiveRestartEnable = key.bool_properties[0] as vk::VkBool32;
    }
    {
        let s = unsafe { std::mem::transmute::<_, &mut vk::VkPipelineTessellationStateCreateInfo>(s.pTessellationState) };
        s.patchControlPoints = key.int_properties[1] as u32;
    }
    {
        let s = unsafe { std::mem::transmute::<_, &mut vk::VkPipelineRasterizationStateCreateInfo>(s.pRasterizationState) };
        s.depthClampEnable = key.bool_properties[1] as vk::VkBool32;
        s.rasterizerDiscardEnable = key.bool_properties[2] as vk::VkBool32;
        s.polygonMode = key.int_properties[2] as vk::VkPolygonMode;
        s.cullMode = key.int_properties[3] as vk::VkCullModeFlags;
        s.frontFace = key.int_properties[4] as vk::VkFrontFace;
        s.depthBiasEnable = key.bool_properties[3] as vk::VkBool32;
        s.depthBiasConstantFactor = key.float_properties[0] as f32;
        s.depthBiasClamp = key.float_properties[1] as f32;
        s.depthBiasSlopeFactor = key.float_properties[2] as f32;
        s.lineWidth = key.float_properties[3] as f32;
    }
    {
        let s = unsafe { std::mem::transmute::<_, &mut vk::VkPipelineColorBlendStateCreateInfo>(s.pColorBlendState) };
        s.logicOpEnable = key.bool_properties[4] as vk::VkBool32;
        s.logicOp = key.int_properties[5] as vk::VkLogicOp;
        {
            let s = unsafe { std::mem::transmute::<_, &mut vk::VkPipelineColorBlendAttachmentState>(s.pAttachments) };
            s.blendEnable = key.bool_properties[5] as vk::VkBool32;
            s.srcColorBlendFactor = key.int_properties[6] as vk::VkBlendFactor;
            s.dstColorBlendFactor = key.int_properties[7] as vk::VkBlendFactor;
            s.colorBlendOp = key.int_properties[8] as vk::VkBlendOp;
            s.srcAlphaBlendFactor = key.int_properties[9] as vk::VkBlendFactor;
            s.dstAlphaBlendFactor = key.int_properties[10] as vk::VkBlendFactor;
            s.alphaBlendOp = key.int_properties[11] as vk::VkBlendOp;
            s.colorWriteMask = key.int_properties[12] as vk::VkColorComponentFlags;
        }
    }
    {
        let s = unsafe { std::mem::transmute::<_, &mut vk::VkPipelineDepthStencilStateCreateInfo>(s.pDepthStencilState) };
        s.depthTestEnable = key.bool_properties[6] as vk::VkBool32;
        s.depthWriteEnable = key.bool_properties[7] as vk::VkBool32;
        s.depthCompareOp = key.int_properties[13] as vk::VkCompareOp;
        s.depthBoundsTestEnable = key.bool_properties[8] as vk::VkBool32;
        s.stencilTestEnable = key.bool_properties[9] as vk::VkBool32;
        s.front.failOp = key.int_properties[14] as vk::VkStencilOp;
        s.front.passOp = key.int_properties[15] as vk::VkStencilOp;
        s.front.depthFailOp = key.int_properties[16] as vk::VkStencilOp;
        s.front.compareOp = key.int_properties[17] as vk::VkCompareOp;
        s.front.compareMask = key.int_properties[18] as u32;
        s.front.writeMask = key.int_properties[19] as u32;
        s.front.reference = key.int_properties[20] as u32;
        s.back.failOp = key.int_properties[21] as vk::VkStencilOp;
        s.back.passOp = key.int_properties[22] as vk::VkStencilOp;
        s.back.depthFailOp = key.int_properties[23] as vk::VkStencilOp;
        s.back.compareOp = key.int_properties[24] as vk::VkCompareOp;
        s.back.compareMask = key.int_properties[25] as u32;
        s.back.writeMask = key.int_properties[26] as u32;
        s.back.reference = key.int_properties[27] as u32;
        s.minDepthBounds = key.float_properties[4] as f32;
        s.maxDepthBounds = key.float_properties[5] as f32;
    }
}

impl Default for Key {
    fn default() -> Key {
        Key {
            pipeline_type: Type::Graphics,
            source: Source::Rectangle,
            entrypoints: Default::default(),

            bool_properties: [
                false, // primitiveRestartEnable
                false, // depthClampEnable
                false, // rasterizerDiscardEnable
                false, // depthBiasEnable
                false, // logicOpEnable
                false, // blendEnable
                false, // depthTestEnable
                false, // depthWriteEnable
                false, // depthBoundsTestEnable
                false, // stencilTestEnable
            ],
            int_properties: [
                vk::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP as i32, // topology
                0 as i32, // patchControlPoints
                vk::VK_POLYGON_MODE_FILL as i32, // polygonMode
                vk::VK_CULL_MODE_NONE as i32, // cullMode
                vk::VK_FRONT_FACE_COUNTER_CLOCKWISE as i32, // frontFace
                vk::VK_LOGIC_OP_SET as i32, // logicOp
                vk::VK_BLEND_FACTOR_SRC_ALPHA as i32, // srcColorBlendFactor
                vk::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA as i32, // dstColorBlendFactor
                vk::VK_BLEND_OP_ADD as i32, // colorBlendOp
                vk::VK_BLEND_FACTOR_SRC_ALPHA as i32, // srcAlphaBlendFactor
                vk::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA as i32, // dstAlphaBlendFactor
                vk::VK_BLEND_OP_ADD as i32, // alphaBlendOp
                (vk::VK_COLOR_COMPONENT_R_BIT | vk::VK_COLOR_COMPONENT_G_BIT | vk::VK_COLOR_COMPONENT_B_BIT | vk::VK_COLOR_COMPONENT_A_BIT) as i32, // colorWriteMask
                vk::VK_COMPARE_OP_LESS as i32, // depthCompareOp
                vk::VK_STENCIL_OP_KEEP as i32, // front.failOp
                vk::VK_STENCIL_OP_KEEP as i32, // front.passOp
                vk::VK_STENCIL_OP_KEEP as i32, // front.depthFailOp
                vk::VK_COMPARE_OP_ALWAYS as i32, // front.compareOp
                u32::MAX as i32, // front.compareMask
                u32::MAX as i32, // front.writeMask
                0 as i32, // front.reference
                vk::VK_STENCIL_OP_KEEP as i32, // back.failOp
                vk::VK_STENCIL_OP_KEEP as i32, // back.passOp
                vk::VK_STENCIL_OP_KEEP as i32, // back.depthFailOp
                vk::VK_COMPARE_OP_ALWAYS as i32, // back.compareOp
                u32::MAX as i32, // back.compareMask
                u32::MAX as i32, // back.writeMask
                0 as i32, // back.reference
            ],
            float_properties: [
                0.0, // depthBiasConstantFactor
                0.0, // depthBiasClamp
                0.0, // depthBiasSlopeFactor
                1.0, // lineWidth
                0.0, // minDepthBounds
                0.0, // maxDepthBounds
            ],
        }
    }
}
