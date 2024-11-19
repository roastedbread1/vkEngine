#include <vk_initializers.h>
#include <vk_pipelines.h>
#include <fstream>

namespace vkutil {

	bool load_shader_module(const char* filepath, VkDevice device, VkShaderModule* outShaderModule)
	{
		std::ifstream file(filepath, std::ios::ate | std::ios::binary);


		if (!file.is_open())
		{
			return false;
		}

		///lookup the size by the end cursor
		size_t fileSize = (size_t)file.tellg();

		///spirv expects the buffer to be a uint32
		///allocate vector big enough for the file

		std::vector<uint32> buffer(fileSize / sizeof(uint32));

		///return cursor at the start
		file.seekg(0);

		///load file into the buffer
		file.read((char*)buffer.data(), fileSize);

		file.close();

		///create shader module
		VkShaderModuleCreateInfo createInfo =
		{
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.pNext = nullptr,
			///codeSize must be in bytes, .size() returns the number of element in the buffer
			///so multiply by the size of uint32 to get the size of the entire buffer in bytes
			.codeSize = buffer.size() * sizeof(uint32),
			.pCode = buffer.data(),
		};


		VkShaderModule shaderModule;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
		{
			return false;
		}

		*outShaderModule = shaderModule;
		return true;

	}

	void clear(PipelineBuilder* builder)
	{
		builder->inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		builder->rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		builder->colorBlendAttachment = {};
		builder->multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		builder->pipelineLayout = {};
		builder->depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		builder->renderInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		builder->shaderStages.clear();

	}

	VkPipeline build_pipeline(VkDevice device, PipelineBuilder* builder)
	{
		///create viewport state
		VkPipelineViewportStateCreateInfo viewportState =
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.pNext = nullptr,
			.viewportCount = 1,
			.scissorCount = 1,
		};

		///dummy color blending
		VkPipelineColorBlendStateCreateInfo colorBlending =
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.pNext = nullptr,
			.logicOpEnable = VK_FALSE,
			.logicOp = VK_LOGIC_OP_COPY,
			.attachmentCount = 1,
			.pAttachments = &builder->colorBlendAttachment,
		};

		///clear vertexinputstatecreateinfo 
		VkPipelineVertexInputStateCreateInfo vertexInputInfo =
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		};

		///build graphics pipeline
		VkGraphicsPipelineCreateInfo pipelineInfo =
		{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.pNext = &builder->renderInfo,
			.stageCount = (uint32)builder->shaderStages.size(),
			.pStages = builder->shaderStages.data(),
			.pVertexInputState = &vertexInputInfo,
			.pInputAssemblyState = &builder->inputAssembly,
			.pViewportState = &viewportState,
			.pRasterizationState = &builder->rasterizer,
			.pMultisampleState = &builder->multisampling,
			.pDepthStencilState = &builder->depthStencil,
			.pColorBlendState = &colorBlending,
			.layout = builder->pipelineLayout,
		};
		///dynamic state enums
		VkDynamicState state[] = { VK_DYNAMIC_STATE_VIEWPORT , VK_DYNAMIC_STATE_SCISSOR };

		VkPipelineDynamicStateCreateInfo dynamicInfo =
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 2,
			.pDynamicStates = &state[0],

		};
		pipelineInfo.pDynamicState = &dynamicInfo;

		VkPipeline newPipeline;
		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
		{
			fmt::println("failed to create pipeline");
			return VK_NULL_HANDLE;
		}
		else
		{
			return newPipeline;
		}

	}

	void  set_shaders(PipelineBuilder* builder, VkShaderModule vertexShader, VkShaderModule fragmentShader)
	{
		builder->shaderStages.clear();

		builder->shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));
		builder->shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
	}

	void set_input_topology(PipelineBuilder* builder, VkPrimitiveTopology topology)
	{
		builder->inputAssembly.topology = topology;
		builder->inputAssembly.primitiveRestartEnable = VK_FALSE;
	}

	void set_polygon_mode(PipelineBuilder* builder, VkPolygonMode mode)
	{
		builder->rasterizer.polygonMode = mode;
		builder->rasterizer.lineWidth = 1.0f;
	}

	void set_cull_mode(PipelineBuilder* builder, VkCullModeFlags cullMode, VkFrontFace frontFace)
	{
		builder->rasterizer.cullMode = cullMode;
		builder->rasterizer.frontFace = frontFace;
	}

	void set_multisampling_none(PipelineBuilder* builder)
	{
		builder->multisampling.sampleShadingEnable = VK_FALSE;
		///no multisample (1 sample per pixel)
		builder->multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		builder->multisampling.minSampleShading = 1.0f;
		builder->multisampling.pSampleMask = nullptr;
		/// no alpha
		builder->multisampling.alphaToCoverageEnable = VK_FALSE;
		builder->multisampling.alphaToOneEnable = VK_FALSE;

	}

	void disable_blending(PipelineBuilder* builder)
	{
		///default write mask
		builder->colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		builder->colorBlendAttachment.blendEnable = VK_FALSE;
	}

	void set_color_attachment_format(PipelineBuilder* builder, VkFormat format)
	{
		builder->colorAttachmentFormat = format;
		///connect the format to the renderinfo struct
		builder->renderInfo.colorAttachmentCount = 1;
		builder->renderInfo.pColorAttachmentFormats = &builder->colorAttachmentFormat;
	}



	void set_depth_format(PipelineBuilder* builder, VkFormat format)
	{
		builder->renderInfo.depthAttachmentFormat = format;
	}

	void disable_depthtest(PipelineBuilder* builder)
	{
		builder->depthStencil.depthTestEnable = VK_FALSE;
		builder->depthStencil.depthWriteEnable = VK_FALSE;
		builder->depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
		builder->depthStencil.depthBoundsTestEnable = VK_FALSE;
		builder->depthStencil.stencilTestEnable = VK_FALSE;
		builder->depthStencil.front = {};
		builder->depthStencil.back = {};
		builder->depthStencil.minDepthBounds = 0.f;
		builder->depthStencil.maxDepthBounds = 1.f;
	}
}
