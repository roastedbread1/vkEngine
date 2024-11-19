#pragma once 
#include <vk_types.h>


namespace vkutil {

	struct PipelineBuilder 
	{
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
		VkPipelineInputAssemblyStateCreateInfo inputAssembly;
		VkPipelineRasterizationStateCreateInfo rasterizer;
		VkPipelineColorBlendAttachmentState colorBlendAttachment;
		VkPipelineMultisampleStateCreateInfo multisampling;
		VkPipelineLayout pipelineLayout;
		VkPipelineDepthStencilStateCreateInfo depthStencil;
		VkPipelineRenderingCreateInfo renderInfo;
		VkFormat colorAttachmentFormat;

		
		
	};

	bool load_shader_module(const char* filepath, VkDevice device, VkShaderModule* outShaderModule);


	VkPipeline build_pipeline(VkDevice device, PipelineBuilder* Builder);

	void clear(PipelineBuilder* builder);
	void set_shaders(PipelineBuilder* builder,VkShaderModule vertexShader, VkShaderModule fragmentShader);
	void set_input_topology(PipelineBuilder* builder, VkPrimitiveTopology topology);
	void set_polygon_mode(PipelineBuilder* builder, VkPolygonMode mode);
	void set_cull_mode(PipelineBuilder* builder, VkCullModeFlags cullMode, VkFrontFace frontFace);
	void set_multisampling_none(PipelineBuilder* builder);
	void disable_blending(PipelineBuilder* builder);
	void set_color_attachment_format(PipelineBuilder* builder,VkFormat format);
	void set_depth_format(PipelineBuilder* builder, VkFormat format);
	void disable_depthtest(PipelineBuilder* builder);
};