#pragma once

//defining variable macros and helper

#include <SDL_vulkan.h>
#include <vk_initializers.h>
#include <chrono>
#include <thread>
#include <vk_types.h>
#include <VkBootstrap.h>
#include <vk_descriptors.h>
namespace ENGINE {
    //not optimal, should change it to an array storing vk handles for every object made
    struct DeletionQueue
    {
        std::deque<std::function<void()>> deletors;

        void push_function(std::function<void()>&& function)
        {
            deletors.push_back(function);
        }

        void flush()
        {
            for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
            {
                (*it)();
            }
            deletors.clear();
        }

    };

    struct ComputePushConstants
    {
        glm::vec4 data1;
        glm::vec4 data2;
        glm::vec4 data3;
        glm::vec4 data4;
    };

    struct ComputeEffect
    {
        const char* name;
        VkPipeline pipeline;
        VkPipelineLayout layout;

        ComputePushConstants data;
    };


    struct FrameData {
        VkCommandPool commandPool;
        VkCommandBuffer mainCommandBufer;
        VkSemaphore swapchainSemaphore;
        VkSemaphore renderSemaphore;
        VkFence renderFence;

        DeletionQueue deletionQueue;
    };

    constexpr uint64 FRAME_OVERLAP = 2;
    struct vkEngine {
        bool isInitialized;             // 1 byte
        bool stopRendering = false;     // 1 byte
        int frameNumber;                // 4 bytes
        int currentBackgroundEffect{ 0 };
        uint32 graphicsQueueFamily;     // 4 bytes
        VkFormat swapchainImageFormat;  // 4 bytes
        VkExtent2D drawExtent;          // 4 bytes
        VkExtent2D windowExtent;        // 8 bytes
        VkExtent2D swapchainExtent;     // 8 bytes
        SDL_Window* window;             // 8 bytes
        VkInstance instance;            // 8 bytes
        VkDebugUtilsMessengerEXT debugMessenger;  // 8 bytes
        VkPhysicalDevice chosenGPU;     // 8 bytes
        VkDevice device;                // 8 bytes
        VkSurfaceKHR surface;           // 8 bytes
        VkSwapchainKHR swapchain;       // 8 bytes
        VkQueue graphicsQueue;          // 8 bytes
        VmaAllocator allocator;         // 8 bytes
        VkPipeline gradientPipeline;    // 8 bytes
        VkPipelineLayout trianglePipelineLayout;
        VkPipeline trianglePipeline;
        VkFence immFence;
        VkCommandBuffer immCommandBuffer;
        VkCommandPool immCommandPool;
        VkPipelineLayout gradientPipelineLayout;        // 8 bytes
        DescriptorAllocator globalDescriptorAllocator; //8 bytes
        VkDescriptorSet drawImageDescriptors;           //8 bytes
        VkDescriptorSetLayout drawImageDescriptorLayout; //8 bytes
        std::vector<VkImage> swapchainImages;      // 32 bytes
        std::vector<VkImageView> swapchainImageViews;  // 32 bytes
        std::vector<ComputeEffect> backgroundEffects;
        AllocatedImage drawImage;        //40 bytes
        DeletionQueue mainDeletionQueue; //40 bytes
        FrameData frames[FRAME_OVERLAP]; //80 bytes

    };


  
   void init(vkEngine* engine);
   void draw(vkEngine* engine);
   void cleanup(vkEngine* engine);
   void  run(vkEngine* engine);
   FrameData& get_current_frame(vkEngine* engine);


   void init_vulkan(vkEngine* engine, bool useValidationLayer);
   void init_swapchain(vkEngine* engine);
   void init_commands(vkEngine* engine);
   void init_sync_structures(vkEngine* engine);
   void init_descriptors(vkEngine* engine);
   void init_pipeline(vkEngine* engine);
   void init_background_pipelines(vkEngine* engine);
   void init_imgui(vkEngine* engine);
   
   void init_triangle_pipeline(vkEngine* engine);
   
   void draw_background(vkEngine* engine, VkCommandBuffer cmd);
   void draw_geometry(vkEngine* engine, VkCommandBuffer cmd);
   void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView, VkExtent2D swapchainExtent);

   void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function, vkEngine* engine);

   void create_instance_and_messenger(vkb::Instance* instance, VkDebugUtilsMessengerEXT* debugMessenger, bool useValidationLayer);
   void create_surface(SDL_Window* window, vkb::Instance* instance, VkSurfaceKHR* surface);
   void create_swapchain(vkEngine* engine);
   void create_device_and_grab_queue(vkb::Instance* instance, VkSurfaceKHR surface, VkDevice* device, VkPhysicalDevice* chosenGPU,
       VkQueue* graphicsQueue, uint32* graphicsQueueFamily);
   

   void destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain, std::vector<VkImageView>& swapchainImageViews);


}