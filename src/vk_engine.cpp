#include <vk_engine.h>
#include <SDL.h>
#include <vk_images.h>
#include <iostream>
#include <vk_pipelines.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

///TODO : https://vkguide.dev/docs/new_chapter_2/vulkan_shader_code/
namespace ENGINE {
	/*VulkanEngine* loadedEngine = nullptr;*/
	vkEngine* loadedEngine = nullptr;
	constexpr bool useValidationLayer = true;

	void init(vkEngine* engine)
	{
		assert(!loadedEngine);

		loadedEngine = engine;
		engine->isInitialized = false;
		engine->frameNumber = 0;
		engine->stopRendering = false;
		engine->windowExtent = { 1700, 900 };

		SDL_Init(SDL_INIT_VIDEO);
		SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
		engine->window = SDL_CreateWindow("Vulkan Engine v2", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			engine->windowExtent.width, engine->windowExtent.height, windowFlags);

		init_vulkan(engine, useValidationLayer);
		init_swapchain(engine);
		init_commands(engine);
		init_sync_structures(engine);
		init_descriptors(engine);
		init_pipeline(engine);
		init_imgui(engine);

		engine->isInitialized = true;
	}
	void draw(vkEngine* engine)
	{
		///wait for gpu to draw the last frame, 1 second timeout

		VK_CHECK(vkWaitForFences(engine->device, 1, &get_current_frame(engine).renderFence, true, 1000000000));

		get_current_frame(engine).deletionQueue.flush();

		VK_CHECK(vkResetFences(engine->device, 1, &get_current_frame(engine).renderFence));

		///req image index from swapchain

		uint32 swapchainImageIndex;
		VK_CHECK(vkAcquireNextImageKHR(engine->device, engine->swapchain, 1000000000, get_current_frame(engine).swapchainSemaphore, VK_NULL_HANDLE,
			&swapchainImageIndex));


		VkCommandBuffer cmd = get_current_frame(engine).mainCommandBufer;

		VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		///start recording

		engine->drawExtent.width = engine->drawImage.imageExtent.width;
		engine->drawExtent.height = engine->drawImage.imageExtent.height;

		VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
		///transision main draw image into general layout so it can be written into
		vkutil::transition_image(cmd, engine->drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		draw_background(engine, cmd);

		vkutil::transition_image(cmd, engine->drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		draw_geometry(engine, cmd);

		///transition draw image and the swapchain into their correct transfer layout
		vkutil::transition_image(cmd, engine->drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		vkutil::transition_image(cmd, engine->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		///excute a copy from the draw image into the swapchain
		vkutil::copy_image_to_image(cmd, engine->drawImage.image, engine->swapchainImages[swapchainImageIndex],
			engine->drawExtent, engine->swapchainExtent);



		///set swapchain image layout to Present
		vkutil::transition_image(cmd, engine->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		draw_imgui(cmd, engine->swapchainImageViews[swapchainImageIndex], engine->swapchainExtent);

		///set swapchain image to present
		vkutil::transition_image(cmd, engine->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		///finalize command buffer, no longer able to add commands
		VK_CHECK(vkEndCommandBuffer(cmd));
		///prepare submission
		///wait present semaphore
		///signal render semaphore
		VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);

		VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
			get_current_frame(engine).swapchainSemaphore);
		VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
			get_current_frame(engine).renderSemaphore);

		VkSubmitInfo2 submit = vkinit::submit_info(&cmdInfo, &signalInfo, &waitInfo);

		VK_CHECK(vkQueueSubmit2(engine->graphicsQueue, 1, &submit, get_current_frame(engine).renderFence));


		///present image

		VkPresentInfoKHR presentInfo =
		{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.pNext = nullptr,

			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &get_current_frame(engine).renderSemaphore,

			.swapchainCount = 1,
			.pSwapchains = &engine->swapchain,
			.pImageIndices = &swapchainImageIndex,

		};

		VK_CHECK(vkQueuePresentKHR(engine->graphicsQueue, &presentInfo));

		engine->frameNumber++;
	}


	void cleanup(vkEngine* engine)
	{
		if (engine->isInitialized)
		{
			vkDeviceWaitIdle(engine->device);

			for (int i = 0; i < FRAME_OVERLAP; i++)
			{
				vkDestroyCommandPool(engine->device, engine->frames[i].commandPool, nullptr);

				///destroy sync object

				vkDestroyFence(engine->device, engine->frames[i].renderFence, nullptr);
				vkDestroySemaphore(engine->device, engine->frames[i].renderSemaphore, nullptr);
				vkDestroySemaphore(engine->device, engine->frames[i].swapchainSemaphore, nullptr);

				engine->frames[i].deletionQueue.flush();

			}

			engine->mainDeletionQueue.flush();

			destroy_swapchain(engine->device, engine->swapchain, engine->swapchainImageViews);
			vkDestroySurfaceKHR(engine->instance, engine->surface, nullptr);

			vkb::destroy_debug_utils_messenger(engine->instance, engine->debugMessenger);
			vkDestroyDevice(engine->device, nullptr);
			vkDestroyInstance(engine->instance, nullptr);

			SDL_DestroyWindow(engine->window);
		}
		loadedEngine = nullptr;
	}

	void run(vkEngine* engine)
	{
		SDL_Event e;
		bool32 bQuit = false;

		/// main loop
		while (!bQuit) {
			/// Handle events on queue
			while (SDL_PollEvent(&e) != 0) {
				/// close the window when user alt-f4s or clicks the X button
				if (e.type == SDL_QUIT)
					bQuit = true;

				if (e.type == SDL_WINDOWEVENT) {
					if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
						engine->stopRendering = true;
					}
					if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
						engine->stopRendering = false;
					}
				}
				ImGui_ImplSDL2_ProcessEvent(&e);
			}

			/// do not draw if we are minimized
			if (engine->stopRendering) {
				/// throttle the speed to avoid the endless spinning
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplSDL2_NewFrame();
			ImGui::NewFrame();

			
			if (ImGui::Begin("background")) {

				ComputeEffect& selected = engine->backgroundEffects[engine->currentBackgroundEffect];

				ImGui::Text("Selected effect: ", selected.name);

				ImGui::SliderInt("Effect Index", &engine->currentBackgroundEffect, 0, engine->backgroundEffects.size() - 1);

				ImGui::InputFloat4("data1", (float*)&selected.data.data1);
				ImGui::InputFloat4("data2", (float*)&selected.data.data2);
				ImGui::InputFloat4("data3", (float*)&selected.data.data3);
				ImGui::InputFloat4("data4", (float*)&selected.data.data4);

				ImGui::End();
			}
			ImGui::Render();


			draw(engine);
		}
	}

	void create_instance_and_messenger(vkb::Instance* instance, VkDebugUtilsMessengerEXT* debugMessenger, bool useValidationLayer) {

		vkb::InstanceBuilder builder;
		auto inst_ret = builder.set_app_name("VkEnginev2")
			.request_validation_layers(useValidationLayer)
			.use_default_debug_messenger()
			.require_api_version(1, 3, 0)
			.build();

		if (inst_ret) {
			*instance = inst_ret.value();
			*debugMessenger = instance->debug_messenger;
		}
		else {
			throw std::runtime_error("Failed to create Vulkan instance with debug messenger.");
		}
	}
	void create_surface(SDL_Window* window, vkb::Instance* instance, VkSurfaceKHR* surface) {
		if (!SDL_Vulkan_CreateSurface(window, instance->instance, surface)) {
			throw std::runtime_error("Failed to create Vulkan surface.");
		}
	}

	void create_device_and_grab_queue(vkb::Instance* instance, VkSurfaceKHR surface, VkDevice* device, VkPhysicalDevice* chosenGPU,
		VkQueue* graphicsQueue, uint32* graphicsQueueFamily) {
		vkb::PhysicalDeviceSelector selector{ *instance };

		vkb::PhysicalDevice physicalDevice = selector
			.set_surface(surface)
			.select()
			.value();

		// Enable VK_KHR_synchronization2
		VkPhysicalDeviceSynchronization2Features synchronization2Features{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
			.pNext = nullptr,
			.synchronization2 = VK_TRUE,
		};

		VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
			.pNext = nullptr,
			.bufferDeviceAddress = VK_TRUE,
		};

		VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
			.pNext = nullptr,
			.dynamicRendering = VK_TRUE,
		};

		// Create the device builder
		vkb::DeviceBuilder deviceBuilder{ physicalDevice };


		// Add synchronization2Features to the pNext chain
		deviceBuilder.add_pNext(&synchronization2Features);
		deviceBuilder.add_pNext(&bufferDeviceAddressFeatures);
		deviceBuilder.add_pNext(&dynamicRenderingFeatures);

		vkb::Device vkbDevice = deviceBuilder.build().value();

		*device = vkbDevice.device;
		*chosenGPU = physicalDevice.physical_device;

		*graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
		*graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
	}


	void init_vulkan(vkEngine* engine, bool useValidationLayer) {
		vkb::Instance vkb_instance;
		create_instance_and_messenger(&vkb_instance, &engine->debugMessenger, useValidationLayer);
		engine->instance = vkb_instance.instance;
		create_surface(engine->window, &vkb_instance, &engine->surface);
		create_device_and_grab_queue(&vkb_instance, engine->surface, &engine->device, &engine->chosenGPU, &engine->graphicsQueue,
			&engine->graphicsQueueFamily);

		VmaAllocatorCreateInfo  allocInfo =
		{
			.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
			.physicalDevice = engine->chosenGPU,
			.device = engine->device,
			.instance = engine->instance,
		};

		vmaCreateAllocator(&allocInfo, &engine->allocator);

		engine->mainDeletionQueue.push_function([&]() {
			vmaDestroyAllocator(engine->allocator);
			});

	}

	void init_swapchain(vkEngine* engine)
	{
		create_swapchain(engine);
		///image to match window size
		VkExtent3D drawImageExtent =
		{
			engine->windowExtent.width,
			engine->windowExtent.height,
			1,
		};

		///32 bit float draw format
		engine->drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		engine->drawImage.imageExtent = drawImageExtent;

		VkImageUsageFlags drawImageUsages =
		{
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
			| VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
		};

		VkImageCreateInfo rimg_info = vkinit::image_create_info(engine->drawImage.imageFormat, drawImageUsages, drawImageExtent);


		///allocate draw image from GPU local memory
		VmaAllocationCreateInfo rimg_allocinfo =
		{
			.usage = VMA_MEMORY_USAGE_GPU_ONLY,
			.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
		};

		///allocate and create the image
		vmaCreateImage(engine->allocator, &rimg_info, &rimg_allocinfo, &engine->drawImage.image, &engine->drawImage.allocation, nullptr);


		///build an image-view to use for rendering
		VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(engine->drawImage.imageFormat, engine->drawImage.image,
			VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(engine->device, &rview_info, nullptr, &engine->drawImage.imageView));

		///add to deletion queue
		engine->mainDeletionQueue.push_function([=]() {
			vkDestroyImageView(engine->device, engine->drawImage.imageView, nullptr);
			vmaDestroyImage(engine->allocator, engine->drawImage.image, engine->drawImage.allocation);
			});
	}


	void draw_background(vkEngine* engine, VkCommandBuffer cmd)
	{
		ComputeEffect& effect = engine->backgroundEffects[engine->currentBackgroundEffect];
		///bind gradient compute pipeline
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

		/// bind the descriptor set containing the draw image for the compute pipeline
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, engine->gradientPipelineLayout, 0, 1, &engine->drawImageDescriptors, 0, nullptr);



		vkCmdPushConstants(cmd, engine->gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

		// execute the compute pipeline dispatch.  16x16 workgroup size so divide by it
		vkCmdDispatch(cmd, std::ceil(engine->drawExtent.width / 16.0), std::ceil(engine->drawExtent.height / 16.0), 1);
	}

	void draw_geometry(vkEngine* engine, VkCommandBuffer cmd)
	{
		///begin renderpass connected to the draw image
		VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(engine->drawImage.imageView, nullptr, 
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		VkRenderingInfo renderInfo = vkinit::rendering_info(engine->drawExtent, &colorAttachment, nullptr);
		vkCmdBeginRendering(cmd, &renderInfo);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, engine->trianglePipeline);

		///dynamic viewport and scissors

		VkViewport viewport =
		{
			.x = 0,
			.y = 0,
			.width = (float)engine->drawExtent.width,
			.height= (float)engine->drawExtent.height,
			.minDepth = 0.f,
			.maxDepth = 1.f,
		};

		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.offset.x = 0;
		scissor.offset.y = 0;
		scissor.extent.width = engine->drawExtent.width;
		scissor.extent.height = engine->drawExtent.height;

		vkCmdSetScissor(cmd, 0, 1, &scissor);

		///launch a draw command to draw 3 vertices

		vkCmdDraw(cmd, 3, 1, 0, 0);
		vkCmdEndRendering(cmd);
	}
	void create_swapchain(vkEngine* engine)
	{
		vkb::SwapchainBuilder swapchainBuilder{ engine->chosenGPU, engine->device, engine->surface };

		engine->swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

		vkb::Swapchain vkbSwapchain = swapchainBuilder
			.set_desired_format(VkSurfaceFormatKHR{ .format = engine->swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
			.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
			.set_desired_extent(engine->windowExtent.width, engine->windowExtent.height)
			.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
			.build()
			.value();

		engine->swapchainExtent = vkbSwapchain.extent;
		engine->swapchain = vkbSwapchain.swapchain;
		engine->swapchainImages = vkbSwapchain.get_images().value();
		engine->swapchainImageViews = vkbSwapchain.get_image_views().value();
	}

	void destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain, std::vector<VkImageView>& swapchainImageViews)
	{
		vkDestroySwapchainKHR(device, swapchain, nullptr);
		for (int i = 0; i < swapchainImageViews.size(); i++)
		{
			vkDestroyImageView(device, swapchainImageViews[i], nullptr);
		}
	}

	void init_commands(vkEngine* engine)
	{

		VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(engine->graphicsQueueFamily, 
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
		VK_CHECK(vkCreateCommandPool(engine->device, &commandPoolInfo, nullptr, &engine->frames[i].commandPool));

		///allocate command buffer
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(engine->frames[i].commandPool, 1);
		VK_CHECK(vkAllocateCommandBuffers(engine->device, &cmdAllocInfo, &engine->frames[i].mainCommandBufer));
		}

		///imm_cmd

		VK_CHECK(vkCreateCommandPool(engine->device, &commandPoolInfo, nullptr, &engine->immCommandPool));

		// allocate the command buffer for immediate submits
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(engine->immCommandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(engine->device, &cmdAllocInfo, &engine->immCommandBuffer));

		engine->mainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(engine->device, engine->immCommandPool, nullptr);
			});

	}
	void init_sync_structures(vkEngine* engine)
	{
		VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
		VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
			VK_CHECK(vkCreateFence(engine->device, &fenceCreateInfo, nullptr, &engine->frames[i].renderFence));

			VK_CHECK(vkCreateSemaphore(engine->device, &semaphoreCreateInfo, nullptr, &engine->frames[i].swapchainSemaphore));
			VK_CHECK(vkCreateSemaphore(engine->device, &semaphoreCreateInfo, nullptr, &engine->frames[i].renderSemaphore));

		}

		VK_CHECK(vkCreateFence(engine->device, &fenceCreateInfo, nullptr, &engine->immFence));
		engine->mainDeletionQueue.push_function([=]() {
			vkDestroyFence(engine->device, engine->immFence, nullptr);
			});
	}

	void init_descriptors(vkEngine* engine)
	{
		//create descriptor pool that holds 10 sets with 1 image each
		std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
		{
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
		};

		engine->globalDescriptorAllocator.init_pool(engine->device, 10, sizes);

		//descriptor set layout for compute draw
		{
			DescriptorLayoutBuilder builder;
			builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); //matching the pool
			engine->drawImageDescriptorLayout = builder.build(engine->device, VK_SHADER_STAGE_COMPUTE_BIT);
		}

		engine->drawImageDescriptors = engine->globalDescriptorAllocator.allocate(engine->device, engine->drawImageDescriptorLayout);

		VkDescriptorImageInfo imgInfo =
		{
			.imageView = engine->drawImage.imageView,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};

		VkWriteDescriptorSet drawImageWrite =
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = engine->drawImageDescriptors,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &imgInfo,
		};

		vkUpdateDescriptorSets(engine->device, 1, &drawImageWrite, 0, nullptr);

		engine->mainDeletionQueue.push_function([&]() {
			engine->globalDescriptorAllocator.destroy_pool(engine->device);
			vkDestroyDescriptorSetLayout(engine->device, engine->drawImageDescriptorLayout, nullptr);
			});

	}

	void init_pipeline(vkEngine* engine)
	{
		VkPipelineLayoutCreateInfo computeLayout = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.setLayoutCount = 1,
			.pSetLayouts = &engine->drawImageDescriptorLayout,
		};

		VkPushConstantRange pushConstants = {
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			.offset =0,
			.size = sizeof(ComputePushConstants),
		};
		
		computeLayout.pPushConstantRanges = &pushConstants;
		computeLayout.pushConstantRangeCount = 1;

		VK_CHECK(vkCreatePipelineLayout(engine->device, &computeLayout, nullptr, &engine->gradientPipelineLayout));


		VkShaderModule gradientShader;
		if (!vkutil::load_shader_module("D:/codes/more codes/c++/vulkan/vkengine/shaders/gradient_color.comp.spv", engine->device, &gradientShader))
		{
			fmt::print("error when building the compute shader\n");
		}

		VkShaderModule skyShader;
		if (!vkutil::load_shader_module("D:/codes/more codes/c++/vulkan/vkengine/shaders/sky.comp.spv", engine->device, &skyShader))
		{
			fmt::print("Error when building the compute shader\n");
		}

		VkPipelineShaderStageCreateInfo stageinfo =
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = nullptr,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = gradientShader,
			.pName = "main",
		};

		VkComputePipelineCreateInfo computePipelineCreateInfo =
		{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.pNext = nullptr,
			.stage = stageinfo,
			.layout = engine->gradientPipelineLayout,
		};

		ComputeEffect gradient =
		{
			.name = "gradient",
			.layout = engine->gradientPipelineLayout,
			.data = {},
		};

		//default colors
		gradient.data.data1 = glm::vec4(1, 0, 0, 1);
		gradient.data.data2 = glm::vec4(0, 0, 1, 1);

		VK_CHECK(vkCreateComputePipelines(engine->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

		///load the sky shader
		computePipelineCreateInfo.stage.module = skyShader;

		ComputeEffect sky =
		{
			.name = "sky",
			.layout = engine->gradientPipelineLayout,
			.data = {},
		};
		
		sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

		VK_CHECK(vkCreateComputePipelines(engine->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));


		///add effects to an array
		engine->backgroundEffects.emplace_back(gradient);
		engine->backgroundEffects.emplace_back(sky);


		vkDestroyShaderModule(engine->device, gradientShader, nullptr);
		vkDestroyShaderModule(engine->device, skyShader, nullptr);

		engine->mainDeletionQueue.push_function([=]() {
			vkDestroyPipelineLayout(engine->device, engine->gradientPipelineLayout, nullptr);
			vkDestroyPipeline(engine->device, sky.pipeline, nullptr);
			vkDestroyPipeline(engine->device, gradient.pipeline, nullptr);

			});



		//init_background_pipelines(engine);
		init_triangle_pipeline(engine);
	}
	void init_background_pipelines(vkEngine* engine)
	{
		VkPipelineLayoutCreateInfo computeLayout =
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.pNext = nullptr,
			.setLayoutCount = 1,
			.pSetLayouts = &engine->drawImageDescriptorLayout,
		};

		VK_CHECK(vkCreatePipelineLayout(engine->device, &computeLayout, nullptr, &engine->gradientPipelineLayout));

		VkShaderModule computeDrawShader;

		if (!vkutil::load_shader_module("D:/codes/more codes/c++/vulkan/vkengine/shaders/gradient_color.comp.spv", engine->device, &computeDrawShader))
		{
			fmt::print("error when building the colored mesh shader\n");
		}

		VkPipelineShaderStageCreateInfo stageInfo =
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = nullptr,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = computeDrawShader,
			.pName = "main",
		};

		VkComputePipelineCreateInfo computePipelineCreateInfo =
		{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.pNext = nullptr,
			.stage = stageInfo,
			.layout = engine->gradientPipelineLayout,
		};

		VK_CHECK(vkCreateComputePipelines(engine->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &engine->gradientPipeline));

		vkDestroyShaderModule(engine->device, computeDrawShader, nullptr);

		engine->mainDeletionQueue.push_function([&]() {
			vkDestroyPipelineLayout(engine->device, engine->gradientPipelineLayout, nullptr);
			vkDestroyPipeline(engine->device, engine->gradientPipeline, nullptr);
			});
	}

	void init_imgui(vkEngine* engine)
	{
		///descriptor pool for IMGUI
		VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

		VkDescriptorPoolCreateInfo pool_info =
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
			.maxSets = 1000,
			.poolSizeCount = (uint32_t)std::size(pool_sizes),
			.pPoolSizes = pool_sizes
		};

		VkDescriptorPool imguiPool;
		VK_CHECK(vkCreateDescriptorPool(engine->device, &pool_info, nullptr, &imguiPool));

		///init imgui library

		ImGui::CreateContext();

		ImGui_ImplSDL2_InitForVulkan(engine->window);

		ImGui_ImplVulkan_InitInfo init_info =
		{
			.Instance = engine->instance,
			.PhysicalDevice = engine->chosenGPU,
			.Device = engine->device,
			.Queue = engine->graphicsQueue,
			.DescriptorPool = imguiPool,
			.MinImageCount = 3,
			.ImageCount = 3,
			.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
			.UseDynamicRendering = true,
			///dynamic rendering param
			.PipelineRenderingCreateInfo = 
			{ 
				.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, 
				.colorAttachmentCount = 1, 
				.pColorAttachmentFormats = &engine->swapchainImageFormat,
			},
			
		};

		ImGui_ImplVulkan_Init(&init_info);

		ImGui_ImplVulkan_CreateFontsTexture();

		engine->mainDeletionQueue.push_function([=]() {
			ImGui_ImplVulkan_Shutdown();
			vkDestroyDescriptorPool(engine->device, imguiPool, nullptr);
			});
	}

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function, vkEngine* engine)
	{
		VK_CHECK(vkResetFences(engine->device, 1, &engine->immFence));

		VK_CHECK(vkResetCommandBuffer(engine->immCommandBuffer, 0));

		VkCommandBuffer cmd = engine->immCommandBuffer;

		VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

		function(cmd);

		VK_CHECK(vkEndCommandBuffer(cmd));

		VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);

		VkSubmitInfo2 submit = vkinit::submit_info(&cmdInfo, nullptr, nullptr);

		///submit command buffer to queue and execute it
		///render fence will block other operations until graphics command is finished
		VK_CHECK(vkQueueSubmit2(engine->graphicsQueue, 1, &submit, engine->immFence));

		VK_CHECK(vkWaitForFences(engine->device, 1, &engine->immFence, true, 9999999999));

	}

	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView, VkExtent2D swapchainExtent)
	{
		VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		VkRenderingInfo renderInfo = vkinit::rendering_info(swapchainExtent, &colorAttachment, nullptr);

		vkCmdBeginRendering(cmd, &renderInfo);

		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

		vkCmdEndRendering(cmd);
	}

	void init_triangle_pipeline(vkEngine* engine)
	{
		VkShaderModule triangleFragShader;

		if (!vkutil::load_shader_module("D:/codes/more codes/c++/vulkan/vkengine/shaders/colored_triangle.frag.spv", engine->device, &triangleFragShader))
		{
			fmt::print("Error when building the triangle fragment shader module\n");
		}
		else
		{
			fmt::print("Triangle fragment shader successfully loaded");
		}

		VkShaderModule triangleVertexShader;

		if (!vkutil::load_shader_module("D:/codes/more codes/c++/vulkan/vkengine/shaders/colored_triangle.vert.spv", engine->device, &triangleVertexShader))
		{
			fmt::print("Error when building the triangle vertex shader module\n");
		}
		else
		{
			fmt::print("Triangle fragment shader successfully loaded");
		}

		///build the pipeline layout
		VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
		VK_CHECK(vkCreatePipelineLayout(engine->device, &pipeline_layout_info, nullptr, &engine->trianglePipelineLayout));

		///create the actual pipeline

		vkutil::PipelineBuilder pipelinebuilder = {};
		vkutil::clear(&pipelinebuilder);
		pipelinebuilder.pipelineLayout = engine->trianglePipelineLayout;
		vkutil::set_shaders(&pipelinebuilder, triangleVertexShader, triangleFragShader);
		vkutil::set_input_topology(&pipelinebuilder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		vkutil::set_polygon_mode(&pipelinebuilder, VK_POLYGON_MODE_FILL);
		vkutil::set_cull_mode(&pipelinebuilder, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
		vkutil::set_multisampling_none(&pipelinebuilder);
		vkutil::disable_blending(&pipelinebuilder);
		vkutil::disable_depthtest(&pipelinebuilder);

		///connect image format from draw image
		vkutil::set_color_attachment_format(&pipelinebuilder, engine->drawImage.imageFormat);
		vkutil::set_depth_format(&pipelinebuilder, VK_FORMAT_UNDEFINED);

		///actually building the pipeline this time
		engine->trianglePipeline = vkutil::build_pipeline(engine->device, &pipelinebuilder);

		///clean struct
		vkDestroyShaderModule(engine->device, triangleFragShader, nullptr);
		vkDestroyShaderModule(engine->device, triangleVertexShader, nullptr);


		engine->mainDeletionQueue.push_function([&]() {
			vkDestroyPipelineLayout(engine->device, engine->trianglePipelineLayout, nullptr);
			vkDestroyPipeline(engine->device, engine->trianglePipeline, nullptr);
			});
	}

	FrameData& get_current_frame(vkEngine* engine) {
		return engine->frames[engine->frameNumber % FRAME_OVERLAP];
	}
}