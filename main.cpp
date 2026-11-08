#define VOLK_IMPLEMENTATION
#include <volk.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <array>
#include <string>
#include <iostream>
#include <filesystem>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <shader-slang/slang.h>
#include <shader-slang/slang-com-ptr.h>
#include <ktx.h>
#include <ktxvulkan.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

constexpr uint32_t maxFramesInFlight{ 2 };
uint32_t imageIndex{ 0 };
uint32_t frameIndex{ 0 };
VkInstance instance{ VK_NULL_HANDLE };
VkDevice device{ VK_NULL_HANDLE };
VkQueue queue{ VK_NULL_HANDLE };
VkSurfaceKHR surface{ VK_NULL_HANDLE };
bool updateSwapchain{ false };
VkSwapchainKHR swapchain{ VK_NULL_HANDLE };
VkCommandPool commandPool{ VK_NULL_HANDLE };
VkPipeline pipeline{ VK_NULL_HANDLE };
VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
VkImage depthImage;
VmaAllocator allocator{ VK_NULL_HANDLE };
VmaAllocation depthImageAllocation;
VkImageView depthImageView;
std::vector<VkImage> swapchainImages;
std::vector<VkImageView> swapchainImageViews;
std::array<VkCommandBuffer, maxFramesInFlight> commandBuffers;
std::array<VkFence, maxFramesInFlight> fences;
std::array<VkSemaphore, maxFramesInFlight> imageAcquiredSemaphores;
std::vector<VkSemaphore> renderCompleteSemaphores;
VmaAllocation vBufferAllocation{ VK_NULL_HANDLE };
VkBuffer vBuffer{ VK_NULL_HANDLE };
struct ShaderData {
	glm::mat4 projection;
	glm::mat4 view;
	glm::mat4 model[3];
	glm::vec4 lightPos{ 0.0f, -10.0f, 10.0f, 0.0f };
	uint32_t selected{ 1 };
} shaderData{};
struct ShaderDataBuffer {
	VmaAllocation allocation{ VK_NULL_HANDLE };
	VmaAllocationInfo allocationInfo{};
	VkBuffer buffer{ VK_NULL_HANDLE };
	VkDeviceAddress deviceAddress{};
};
std::array<ShaderDataBuffer, maxFramesInFlight> shaderDataBuffers;
struct Texture {
	VmaAllocation allocation{ VK_NULL_HANDLE };
	VkImage image{ VK_NULL_HANDLE };
	VkImageView view{ VK_NULL_HANDLE };
	VkSampler sampler{ VK_NULL_HANDLE };
};
std::array<Texture, 3> textures{};
VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
VkDescriptorSetLayout descriptorSetLayoutTex{ VK_NULL_HANDLE };
VkDescriptorSet descriptorSetTex{ VK_NULL_HANDLE };
Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
glm::vec3 camPos{ 0.0f, 0.0f, -6.0f };
glm::vec3 objectRotations[3]{};
glm::ivec2 windowSize{};
struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
};

int main() {
	std::cout << "Starting initializations" << std::endl;
	if (!std::filesystem::is_directory("assets")) {
		std::cerr << "Could not locate assets folder from current working directory\n";
		exit(-1);
	}
	if (SDL_Init(SDL_INIT_VIDEO) == 0) {
		std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
    return -1;
	} 
	if (!SDL_Vulkan_LoadLibrary(NULL)) {
		std::cerr << "Failed to load Vulkan library: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return -1;
	}
	if (volkInitialize() != VK_SUCCESS) {
		std::cerr << "Failed to initialize Volk " << std::endl;
		return -1;
	}
  const VkApplicationInfo appInfo{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "How to Vulkan",
      .apiVersion = VK_API_VERSION_1_3,
  };

  uint32_t instanceExtensionCount{ 0 };
  char const* const* instanceExtensions{ SDL_Vulkan_GetInstanceExtensions(&instanceExtensionCount) };
	if (!instanceExtensions || instanceExtensionCount == 0) {
	    std::cerr << "No Vulkan instance extensions available." << std::endl;
      SDL_Quit();
	    return -1;
	}

  const char* layers[] = {
  	"VK_LAYER_KHRONOS_validation",
  	"VK_LAYER_KHRONOS_profiles"
  };
  const VkInstanceCreateInfo instanceCreateInfo{ 
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pNext = nullptr,
	    .pApplicationInfo = &appInfo,
	    .enabledLayerCount = static_cast<uint32_t>(std::size(layers)),
	    .ppEnabledLayerNames= layers,
	    .enabledExtensionCount = instanceExtensionCount,
	    .ppEnabledExtensionNames = instanceExtensions,
	  };

	 vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
  if (vkCreateInstance(&instanceCreateInfo, nullptr, &instance)!= VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan instance: " << std::endl;
        SDL_Quit();
        return -1;
    }
  volkLoadInstance(instance);

  uint32_t deviceCount{ 0 };
  vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
  if (deviceCount == 0 || vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()) != VK_SUCCESS) {
  	std::cerr << "Failed to find physical device" << std::endl;
		return -1;
  }

	// Manually set device index here, crappy but is what it is for now
  uint32_t deviceIndex{ 0 };
	VkPhysicalDeviceProperties2 deviceProperties{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
	};
	vkGetPhysicalDeviceProperties2(devices[deviceIndex], &deviceProperties);
	std::cout << "Selected device: " << deviceProperties.properties.deviceName << std::endl;

	uint32_t queueFamilyCount{ 0 };
	vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, queueFamilies.data());
  uint32_t queueFamily{ 0 };
	for (size_t i = 0; i < queueFamilies.size(); i++) {
		if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			queueFamily = i;
			break;
		}
	}
	if (!SDL_Vulkan_GetPresentationSupport(instance, devices[deviceIndex], queueFamily)) {
		std::cerr << "Presentation support on graphics queue not found" << SDL_GetError() << std::endl;
		return -1;
	} 
	const float queueFamilyPriorities{ 1.0f };
	VkDeviceQueueCreateInfo queueCreateInfo{
	    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
	    .queueFamilyIndex = queueFamily,
	    .queueCount = 1,
	    .pQueuePriorities = &queueFamilyPriorities
	};

	VkPhysicalDeviceVulkan12Features enabledVk12Features{
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
	    .descriptorIndexing = true,
	    .shaderSampledImageArrayNonUniformIndexing = true,
	    .descriptorBindingVariableDescriptorCount = true,
	    .runtimeDescriptorArray = true,
	    .bufferDeviceAddress = true
	};
	VkPhysicalDeviceVulkan13Features enabledVk13Features{
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
	    .pNext = &enabledVk12Features,
	    .synchronization2 = true,
	    .dynamicRendering = true,
	};
	VkPhysicalDeviceFeatures enabledVk10Features{
	    .samplerAnisotropy = VK_TRUE
	};
	const std::vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceCreateInfo deviceCI{
	    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
	    .pNext = &enabledVk13Features,
	    .queueCreateInfoCount = 1,
	    .pQueueCreateInfos = &queueCreateInfo,
	    .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
	    .ppEnabledExtensionNames = deviceExtensions.data(),
	    .pEnabledFeatures = &enabledVk10Features
	};
	if (vkCreateDevice(devices[deviceIndex], &deviceCI, nullptr, &device) != VK_SUCCESS) {
		std::cerr << "Failed to create logical device" << std::endl;
		return -1;
	}
	vkGetDeviceQueue(device, queueFamily, 0, &queue);

	// Initializing VMA
	VmaVulkanFunctions vkFunctions{
	    .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
	    .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
	    .vkCreateImage = vkCreateImage
	};
	VmaAllocatorCreateInfo allocatorCreateInfo{
	    .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, 
	    .physicalDevice = devices[deviceIndex],
	    .device = device,
	    .pVulkanFunctions = &vkFunctions,
	    .instance = instance
	};
	if (vmaCreateAllocator(&allocatorCreateInfo, &allocator) != VK_SUCCESS) {
		std::cerr << "Failed to create memory allocator" << std::endl;
		return -1;
	}

	// Initializing Window and Surface
	SDL_Window* window = SDL_CreateWindow("Vulkan", 2560u, 1440u, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
		std::cerr << "Failed to create surface" << std::endl;
		return -1;
	}
	VkSurfaceCapabilitiesKHR surfaceCapabilities{};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface, &surfaceCapabilities) != VK_SUCCESS) {
		std::cerr << "Failed to get surface capabilities" << std::endl;
		return -1;
	}
	assert(window);
	if (!SDL_GetWindowSize(window, &windowSize.x, &windowSize.y)) {
		std::cerr << "Failed to get window size" << std::endl;
		return -1;
	}

	// Swapchain
  VkExtent2D swapchainExtent{ surfaceCapabilities.currentExtent };
	if (surfaceCapabilities.currentExtent.width == 0xFFFFFFFF) {
	    swapchainExtent = { .width = static_cast<uint32_t>(windowSize.x), .height = static_cast<uint32_t>(windowSize.y) };
	}

	const VkFormat imageFormat{ VK_FORMAT_B8G8R8A8_SRGB };
	VkSwapchainCreateInfoKHR swapchainCreateInfo{
	    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
	    .surface = surface,
	    .minImageCount = surfaceCapabilities.minImageCount,
	    .imageFormat = imageFormat,
	    .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
	    .imageExtent{.width = swapchainExtent.width, .height = swapchainExtent.height },
	    .imageArrayLayers = 1,
	    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	    .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
	    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
	    .presentMode = VK_PRESENT_MODE_FIFO_KHR
	};
	if (vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain) != VK_SUCCESS) {
		std::cerr << "Failed to create swapchain" << std::endl;
		return -1;
	}
	uint32_t imageCount{ 0 };
	if(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to get swapcahin image count" << std::endl;
		return -1;
	}
	swapchainImages.resize(imageCount);
	if(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()) != VK_SUCCESS) {
		std::cerr << "Failed to get swapcahin image handles" << std::endl;
		return -1;
	}
	swapchainImageViews.resize(imageCount);

	// Initialize Depth attachment
	std::vector<VkFormat> depthFormatList{
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D24_UNORM_S8_UINT
	};
	VkFormat depthFormat{ VK_FORMAT_UNDEFINED };
	for (VkFormat& format : depthFormatList) {
	    VkFormatProperties2 formatProperties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
	    vkGetPhysicalDeviceFormatProperties2(devices[deviceIndex], format, &formatProperties);
	    if (formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
	        depthFormat = format;
	        break;
	    }
	}
	VkImageCreateInfo depthImageCreateInfo{
	    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .imageType = VK_IMAGE_TYPE_2D,
	    .format = depthFormat,
	    .extent{.width = static_cast<uint32_t>(windowSize.x), .height = static_cast<uint32_t>(windowSize.y), .depth = 1 },
	    .mipLevels = 1,
	    .arrayLayers = 1,
	    .samples = VK_SAMPLE_COUNT_1_BIT,
	    .tiling = VK_IMAGE_TILING_OPTIMAL,
	    .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
	    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VmaAllocationCreateInfo allocCreateInfo{
	    .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
	    .usage = VMA_MEMORY_USAGE_AUTO
	};
	if (vmaCreateImage(allocator, &depthImageCreateInfo, &allocCreateInfo, &depthImage, &depthImageAllocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create images" << std::endl;
		return -1;
	}
	VkImageViewCreateInfo depthViewCreateInfo{ 
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = depthImage,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = depthFormat,
    .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
};
if (vkCreateImageView(device, &depthViewCreateInfo, nullptr, &depthImageView) != VK_SUCCESS) {
	std::cerr << "Failed to create depth image view" << std::endl;
	return -1;
}

	// Loading meshes
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, "assets/suzanne.obj")) {
		std::cerr << "Failed to load objects" << std::endl;
		return -1;
	}
	const VkDeviceSize indexCount{shapes[0].mesh.indices.size()};   
	std::vector<Vertex> vertices{};
	std::vector<uint16_t> indices{};
	// Load vertex and index data
	for (auto& index : shapes[0].mesh.indices) {
	    Vertex v{
	        .pos = { attrib.vertices[index.vertex_index * 3], -attrib.vertices[index.vertex_index * 3 + 1], attrib.vertices[index.vertex_index * 3 + 2] },
	        .normal = { attrib.normals[index.normal_index * 3], -attrib.normals[index.normal_index * 3 + 1], attrib.normals[index.normal_index * 3 + 2] },
	        .uv = { attrib.texcoords[index.texcoord_index * 2], 1.0 - attrib.texcoords[index.texcoord_index * 2 + 1] }
	    };
	    vertices.push_back(v);
	    indices.push_back(indices.size());
	}
	VkDeviceSize vBufSize{ sizeof(Vertex) * vertices.size()};
	VkDeviceSize iBufSize{ sizeof(uint16_t) * indices.size()};
	VkBufferCreateInfo bufferCreateInfo{
	    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .size = vBufSize + iBufSize,
	    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
	};
	VmaAllocationCreateInfo vBufferAllocCreateInfo{
    .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .usage = VMA_MEMORY_USAGE_AUTO
	};
	VmaAllocationInfo vBufferAllocInfo{};
	if  (vmaCreateBuffer(allocator, &bufferCreateInfo, &vBufferAllocCreateInfo, &vBuffer, &vBufferAllocation, &vBufferAllocInfo) != VK_SUCCESS){
		std::cerr << "Failed to create vertex buffer" << std::endl;
		return -1;
	}
	memcpy(vBufferAllocInfo.pMappedData, vertices.data(), vBufSize);
	memcpy(((char*)vBufferAllocInfo.pMappedData) + vBufSize, indices.data(), iBufSize);

	for (auto i = 0; i < maxFramesInFlight; i++) {
		VkBufferCreateInfo uBufferCreateInfo{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizeof(ShaderData),
			.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		};
		VmaAllocationCreateInfo uBufferAllocCreateInfo{
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
		};
		if (vmaCreateBuffer(allocator, &uBufferCreateInfo, &uBufferAllocCreateInfo, &shaderDataBuffers[i].buffer, &shaderDataBuffers[i].allocation, &shaderDataBuffers[i].allocationInfo) != VK_SUCCESS) {
			std::cerr << "Failed to create shader buffer" << std::endl;
			return -1;
		}
    VkBufferDeviceAddressInfo uBufferBdaInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = shaderDataBuffers[i].buffer
    };
    shaderDataBuffers[i].deviceAddress = vkGetBufferDeviceAddress(device, &uBufferBdaInfo);
	}

	// Initializing synch objects
	VkSemaphoreCreateInfo semaphoreCreateInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};
	VkFenceCreateInfo fenceCreateInfo{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};
	for (auto i = 0; i < maxFramesInFlight; i++) {
		if (vkCreateFence(device, &fenceCreateInfo, nullptr, &fences[i]) != VK_SUCCESS) {
			std::cerr << "Failed to create fence" << std::endl;
			return -1;		
		}
		if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &imageAcquiredSemaphores[i]) != VK_SUCCESS) {
			std::cerr << "Failed to create semaphore" << std::endl;
			return -1;		
		}
	}
	renderCompleteSemaphores.resize(swapchainImages.size());
	for (auto& semaphore : renderCompleteSemaphores) {
		if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphore) != VK_SUCCESS) {
			std::cerr << "Failed to create semaphore" << std::endl;
			return -1;		
		}
	}

	// Initialize command buffers
	VkCommandPoolCreateInfo commandPoolCreateInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = queueFamily
	};
	if (vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool) != VK_SUCCESS) {
		std::cerr << "Failed to create command pool" << std::endl;
		return -1;
	}
	VkCommandBufferAllocateInfo commandBufferAllocCreateInfo{
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	    .commandPool = commandPool,
	    .commandBufferCount = maxFramesInFlight
	};
	if (vkAllocateCommandBuffers(device, &commandBufferAllocCreateInfo, commandBuffers.data()) != VK_SUCCESS) {
		std::cerr << "Failed to create command buffer" << std::endl;
		return -1;
	}
	// Loading textures
	std::vector<VkDescriptorImageInfo> textureDescriptors{};
	for (auto i = 0; i < textures.size(); i++) {
    ktxTexture* ktxTexture{ nullptr };
    std::string filename = "assets/suzanne" + std::to_string(i) + ".ktx";
    if (ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture) != KTX_SUCCESS) {
	    std::cerr << "Failed to load KTX file: " << filename << std::endl;
			return -1;
    }
		VkImageCreateInfo texImgCreateInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = ktxTexture_GetVkFormat(ktxTexture),
			.extent = {.width = ktxTexture->baseWidth, .height = ktxTexture->baseHeight, .depth = 1 },
			.mipLevels = ktxTexture->numLevels,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
		};
		VmaAllocationCreateInfo texImageAllocCreateInfo{
			.usage = VMA_MEMORY_USAGE_AUTO
		};
		if (vmaCreateImage(allocator, &texImgCreateInfo, &texImageAllocCreateInfo, &textures[i].image, &textures[i].allocation, nullptr) != VK_SUCCESS) {
			std::cerr << "Failed to create texture image" << std::endl;
			return -1;
		}
		VkImageViewCreateInfo texViewCreateInfo{
	    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	    .image = textures[i].image,
	    .viewType = VK_IMAGE_VIEW_TYPE_2D,
	    .format = texImgCreateInfo.format,
	    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
		};
		if (vkCreateImageView(device, &texViewCreateInfo, nullptr, &textures[i].view) != VK_SUCCESS) {
			std::cerr << "Failed to create texture views" << std::endl;
			return -1;
		}
		VkBuffer imgSrcBuffer{};
		VmaAllocation imgSrcAllocation{};
		VkBufferCreateInfo imgSrcBufferCreateInfo{
		    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		    .size = (uint32_t)ktxTexture->dataSize,
		    .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		};
		VmaAllocationCreateInfo imgSrcAllocCreateInfo{
		    .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
		    .usage = VMA_MEMORY_USAGE_AUTO
		};
		VmaAllocationInfo imgSrcAllocInfo{};
		if (vmaCreateBuffer(allocator, &imgSrcBufferCreateInfo, &imgSrcAllocCreateInfo, &imgSrcBuffer, &imgSrcAllocation, &imgSrcAllocInfo) != VK_SUCCESS) {
			std::cerr << "Failed to create texture source buffer" << std::endl;
			return -1;
		}
		memcpy(imgSrcAllocInfo.pMappedData, ktxTexture->pData, ktxTexture->dataSize);
		VkFenceCreateInfo fenceOneTimeCreateInfo{
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
		};
		VkFence fenceOneTime{};
		if (vkCreateFence(device, &fenceOneTimeCreateInfo, nullptr, &fenceOneTime) != VK_SUCCESS) {
			std::cerr << "Failed to create fenceOneTime" << std::endl;
			return -1;
		}
		VkCommandBuffer cbOneTime{};
		VkCommandBufferAllocateInfo cbOneTimeAllocInfo{
		    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		    .commandPool = commandPool,
		    .commandBufferCount = 1
		};
		if (vkAllocateCommandBuffers(device, &cbOneTimeAllocInfo, &cbOneTime) != VK_SUCCESS) {
			std::cerr << "Failed to create single use command buffer" << std::endl;
			return -1;
		}
		VkCommandBufferBeginInfo cbOneTimeBufferInfo{
		    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};
		if (vkBeginCommandBuffer(cbOneTime, &cbOneTimeBufferInfo) != VK_SUCCESS) {
			std::cerr << "Failed to begin command buffer" << std::endl;
			return -1;
		}
		VkImageMemoryBarrier2 barrierTexImage{
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
	    .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
	    .srcAccessMask = VK_ACCESS_2_NONE,
	    .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
	    .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
	    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	    .image = textures[i].image,
	    .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
		};
		VkDependencyInfo barrierTexInfo{
	    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
	    .imageMemoryBarrierCount = 1,
	    .pImageMemoryBarriers = &barrierTexImage
		};
		vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
		std::vector<VkBufferImageCopy> copyRegions{};
		for (auto j = 0; j < ktxTexture->numLevels; j++) {
			ktx_size_t mipOffset{0};
	    KTX_error_code ret = ktxTexture_GetImageOffset(ktxTexture, j, 0, 0, &mipOffset);
	    copyRegions.push_back({
        .bufferOffset = mipOffset,
        .imageSubresource{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = (uint32_t)j, .layerCount = 1},
        .imageExtent{.width = ktxTexture->baseWidth >> j, .height = ktxTexture->baseHeight >> j, .depth = 1 },
	    });
		}
		vkCmdCopyBufferToImage(cbOneTime, imgSrcBuffer, textures[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(copyRegions.size()), copyRegions.data());
		VkImageMemoryBarrier2 barrierTexRead{
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
	    .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
	    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
	    .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
	    .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	    .newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
	    .image = textures[i].image,
	    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
		};
		barrierTexInfo.pImageMemoryBarriers = &barrierTexRead;
		vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
		if (vkEndCommandBuffer(cbOneTime) != VK_SUCCESS) {
			std::cerr << "Failed to end command buffer" << std::endl;
			return -1;
		}
		VkCommandBufferSubmitInfo cbOneTimeSubmitInfo{
		    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		    .commandBuffer = cbOneTime
		};
		VkSubmitInfo2 oneTimeSI{
		    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		    .commandBufferInfoCount = 1,
		    .pCommandBufferInfos = &cbOneTimeSubmitInfo
		};
		if (vkQueueSubmit2(queue, 1, &oneTimeSI, fenceOneTime) != VK_SUCCESS) {
			std::cerr << "Failed to submit command buffer to queue" << std::endl;
			return -1;
		}
		if (vkWaitForFences(device, 1, &fenceOneTime, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
			std::cerr << "Something went wrong while waiting for fenceOneTime" << std::endl;
			return -1;
		}
		VkSamplerCreateInfo samplerCreateInfo{
	    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
	    .magFilter = VK_FILTER_LINEAR,
	    .minFilter = VK_FILTER_LINEAR,
	    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
	    .anisotropyEnable = VK_TRUE,
	    .maxAnisotropy = 8.0f, // 8 is a widely supported value for max anisotropy
	    .maxLod = (float)ktxTexture->numLevels,
		};
		if (vkCreateSampler(device, &samplerCreateInfo, nullptr, &textures[i].sampler) != VK_SUCCESS) {
			std::cerr << "Failed to create sampler" << std::endl;
			return -1;
		}
		ktxTexture_Destroy(ktxTexture);
		textureDescriptors.push_back({
	    .sampler = textures[i].sampler,
	    .imageView = textures[i].view,
	    .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
		});
	}
	// Initializing descriptor
	VkDescriptorBindingFlags descVariableFlag{ VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT };
	VkDescriptorSetLayoutBindingFlagsCreateInfo descBindingFlags{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
    .bindingCount = 1,
    .pBindingFlags = &descVariableFlag
	};
	VkDescriptorSetLayoutBinding descLayoutBindingTex{
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = static_cast<uint32_t>(textures.size()),
    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
	};
	VkDescriptorSetLayoutCreateInfo descLayoutTexCreateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .pNext = &descBindingFlags,
    .bindingCount = 1,
    .pBindings = &descLayoutBindingTex
	};
	if (vkCreateDescriptorSetLayout(device, &descLayoutTexCreateInfo, nullptr, &descriptorSetLayoutTex) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layout" << std::endl;
		return -1;
	}
	VkDescriptorPoolSize poolSize{
    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = static_cast<uint32_t>(textures.size())
	};
	VkDescriptorPoolCreateInfo descPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1,
    .poolSizeCount = 1,
    .pPoolSizes = &poolSize
	};
	if (vkCreateDescriptorPool(device, &descPoolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool" << std::endl;
		return -1;
	}
	uint32_t variableDescCount{ static_cast<uint32_t>(textures.size()) };
	VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescCountAI{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT,
    .descriptorSetCount = 1,
    .pDescriptorCounts = &variableDescCount
	};
	VkDescriptorSetAllocateInfo texDescSetAlloc{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .pNext = &variableDescCountAI,
    .descriptorPool = descriptorPool,
    .descriptorSetCount = 1,
    .pSetLayouts = &descriptorSetLayoutTex
	};
	if (vkAllocateDescriptorSets(device, &texDescSetAlloc, &descriptorSetTex) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor sets" << std::endl;
		return -1;
	}
	VkWriteDescriptorSet writeDescSet{
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = descriptorSetTex,
    .dstBinding = 0,
    .descriptorCount = static_cast<uint32_t>(textureDescriptors.size()),
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
    .pImageInfo = textureDescriptors.data()
	};
	vkUpdateDescriptorSets(device, 1, &writeDescSet, 0, nullptr);
	// Loading shaders
	slang::createGlobalSession(slangGlobalSession.writeRef());
	auto slangTargets{ std::to_array<slang::TargetDesc>({ {
    .format{SLANG_SPIRV},
    .profile{slangGlobalSession->findProfile("spirv_1_4")}
	} })};
	auto slangOptions{ std::to_array<slang::CompilerOptionEntry>({ {
    slang::CompilerOptionName::EmitSpirvDirectly,
    {slang::CompilerOptionValueKind::Int, 1}
	} })};
	slang::SessionDesc slangSessionDesc{
    .targets{slangTargets.data()},
    .targetCount{SlangInt(slangTargets.size())},
    .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
    .compilerOptionEntries{slangOptions.data()},
    .compilerOptionEntryCount{uint32_t(slangOptions.size())}
	};
	Slang::ComPtr<slang::ISession> slangSession;
	slangGlobalSession->createSession(slangSessionDesc, slangSession.writeRef());
	Slang::ComPtr<slang::IModule> slangModule{
    slangSession->loadModuleFromSource("triangle", "assets/shader.slang", nullptr, nullptr)
	};
	Slang::ComPtr<ISlangBlob> spirv;
	slangModule->getTargetCode(0, spirv.writeRef());
	VkShaderModuleCreateInfo shaderModuleCreateInfo{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = spirv->getBufferSize(),
    .pCode = (uint32_t*)spirv->getBufferPointer()
	};
	VkShaderModule shaderModule{};
	if (vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule)!= VK_SUCCESS) {
		std::cerr << "Failed to create shader module" << std::endl;
		return -1;
	}
	// Pipeline creation
	VkPushConstantRange pushConstantRange{
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    .size = sizeof(VkDeviceAddress)
	};
	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = &descriptorSetLayoutTex,
    .pushConstantRangeCount = 1,
    .pPushConstantRanges = &pushConstantRange
	};
	if (vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layout" << std::endl;
		return -1;
	}
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = shaderModule, .pName = "main"},
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = shaderModule, .pName = "main" }
	};
	VkVertexInputBindingDescription vertexBinding{
     .binding = 0,
     .stride = sizeof(Vertex),
     .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
	};
	std::vector<VkVertexInputAttributeDescription> vertexAttributes{
    { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT },
    { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
    { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
	};
	VkPipelineVertexInputStateCreateInfo vertexInputState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &vertexBinding,
    .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
    .pVertexAttributeDescriptions = vertexAttributes.data(),
	};
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};
	VkPipelineViewportStateCreateInfo viewportState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .scissorCount = 1
	};
	std::vector<VkDynamicState> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = 2,
    .pDynamicStates = dynamicStates.data()
	};
	VkPipelineDepthStencilStateCreateInfo depthStencilState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .depthTestEnable = VK_TRUE,
    .depthWriteEnable = VK_TRUE,
    .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
	};
	VkPipelineRenderingCreateInfo renderingCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &imageFormat,
    .depthAttachmentFormat = depthFormat
	};
	VkPipelineColorBlendAttachmentState blendAttachment{
    .colorWriteMask = 0xF
	};
	VkPipelineColorBlendStateCreateInfo colorBlendState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &blendAttachment
	};
	VkPipelineRasterizationStateCreateInfo rasterizationState{
     .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
     .lineWidth = 1.0f
	};
	VkPipelineMultisampleStateCreateInfo multisampleState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};
	VkGraphicsPipelineCreateInfo pipelineCreateInfo{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .pNext = &renderingCreateInfo,
    .stageCount = 2,
    .pStages = shaderStages.data(),
    .pVertexInputState = &vertexInputState,
    .pInputAssemblyState = &inputAssemblyState,
    .pViewportState = &viewportState,
    .pRasterizationState = &rasterizationState,
    .pMultisampleState = &multisampleState,
    .pDepthStencilState = &depthStencilState,
    .pColorBlendState = &colorBlendState,
    .pDynamicState = &dynamicState,
    .layout = pipelineLayout
	};
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
		std::cerr << "Failed to create graphics pipeline" << std::endl;
		return -1;
	}

	// Main loop!
	uint64_t lastTime{ SDL_GetTicks() };
	bool quit{ false };
	while (!quit) {
    // Wait on fence
    if (vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX) != VK_SUCCESS) {
			std::cerr << "Something went wrong while waiting for fences" << std::endl;
			return -1;
    }
		if (vkResetFences(device, 1, &fences[frameIndex]) != VK_SUCCESS) {
			std::cerr << "Something went wrong while resetting fences" << std::endl;
			return -1;
		}
    // Acquire next image
    if (vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAcquiredSemaphores[frameIndex], VK_NULL_HANDLE, &imageIndex) == VK_ERROR_OUT_OF_DATE_KHR) {
      updateSwapchain = true;
			// std::cerr << "Write swapchain recreation code! It was needed." << std::endl;
			// return -1;
    }
    // Update shader data
    shaderData.projection = glm::perspective(glm::radians(45.0f), (float)windowSize.x / (float)windowSize.y, 0.1f, 32.0f);
		shaderData.view = glm::translate(glm::mat4(1.0f), camPos);
		for (auto i = 0; i < 3; i++) {
	    auto instancePos = glm::vec3((float)(i - 1) * 3.0f, 0.0f, 0.0f);
	    shaderData.model[i] = glm::translate(glm::mat4(1.0f), instancePos) * glm::mat4_cast(glm::quat(objectRotations[i]));
		}
		memcpy(shaderDataBuffers[frameIndex].allocationInfo.pMappedData, &shaderData, sizeof(ShaderData));
    // Record command buffer
    auto cb = commandBuffers[frameIndex];
		if (vkResetCommandBuffer(cb, 0) != VK_SUCCESS) {
			std::cerr << "Failed to reset command buffer" << std::endl;
			return -1;
		}
		VkCommandBufferBeginInfo cbBI {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};
		if (vkBeginCommandBuffer(cb, &cbBI) != VK_SUCCESS) {
			std::cerr << "Failed to begin command buffer" << std::endl;
			return -1;
		}
		std::array<VkImageMemoryBarrier2, 2> outputBarriers{
    VkImageMemoryBarrier2{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0,
      .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .image = swapchainImages[imageIndex],
      .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
    },
    VkImageMemoryBarrier2{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
      .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
      .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .image = depthImage,
      .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
    }
      };
		VkDependencyInfo barrierDependencyInfo{
	    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
	    .imageMemoryBarrierCount = 2,
	    .pImageMemoryBarriers = outputBarriers.data()
		};
		vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);
		VkRenderingAttachmentInfo colorAttachmentInfo{
	    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	    .imageView = swapchainImageViews[imageIndex],
	    .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
	    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	    .clearValue{.color{ 0.0f, 0.0f, 0.2f, 1.0f }}
		};
		VkRenderingAttachmentInfo depthAttachmentInfo{
	    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	    .imageView = depthImageView,
	    .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
	    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	    .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
	    .clearValue = {.depthStencil = {1.0f,  0}}
		};
		VkRenderingInfo renderingInfo{
	    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
	    .renderArea{.extent{.width = static_cast<uint32_t>(windowSize.x), .height = static_cast<uint32_t>(windowSize.y) }},
	    .layerCount = 1,
	    .colorAttachmentCount = 1,
	    .pColorAttachments = &colorAttachmentInfo,
	    .pDepthAttachment = &depthAttachmentInfo
		};
		vkCmdBeginRendering(cb, &renderingInfo);
		VkViewport vp{
	    .width = static_cast<float>(windowSize.x),
	    .height = static_cast<float>(windowSize.y),
	    .minDepth = 0.0f,
	    .maxDepth = 1.0f
		};
		vkCmdSetViewport(cb, 0, 1, &vp);
		VkRect2D scissor{ .extent{ .width = static_cast<uint32_t>(windowSize.x), .height = static_cast<uint32_t>(windowSize.y) } };
		vkCmdSetScissor(cb, 0, 1, &scissor);
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		VkDeviceSize vOffset{ 0 };
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSetTex, 0, nullptr);
		vkCmdBindVertexBuffers(cb, 0, 1, &vBuffer, &vOffset);
		vkCmdBindIndexBuffer(cb, vBuffer, vBufSize, VK_INDEX_TYPE_UINT16);
		vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkDeviceAddress), &shaderDataBuffers[frameIndex].deviceAddress);
    vkCmdDrawIndexed(cb, indexCount, 3, 0, 0, 0);
    VkImageMemoryBarrier2 barrierPresent{
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
	    .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	    .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	    .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	    .dstAccessMask = 0,
	    .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
	    .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	    .image = swapchainImages[imageIndex],
	    .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
		};
		VkDependencyInfo barrierPresentDependencyInfo{
	    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
	    .imageMemoryBarrierCount = 1,
	    .pImageMemoryBarriers = &barrierPresent
		};
		vkCmdPipelineBarrier2(cb, &barrierPresentDependencyInfo);
		vkEndCommandBuffer(cb);
    // Submit command buffer
    VkSemaphoreSubmitInfo waitSemaphoreInfo{
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
	    .semaphore = imageAcquiredSemaphores[frameIndex],
	    .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		};
		VkCommandBufferSubmitInfo commandBufferSubmitInfo{
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
	    .commandBuffer = cb
		};
		VkSemaphoreSubmitInfo signalSemaphoreInfo{
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
	    .semaphore = renderCompleteSemaphores[imageIndex],
	    .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		};
		VkSubmitInfo2 submitInfo{
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
	    .waitSemaphoreInfoCount = 1,
	    .pWaitSemaphoreInfos = &waitSemaphoreInfo,
	    .commandBufferInfoCount = 1,
	    .pCommandBufferInfos = &commandBufferSubmitInfo,
	    .signalSemaphoreInfoCount = 1,
	    .pSignalSemaphoreInfos = &signalSemaphoreInfo,
		};
		if (vkQueueSubmit2(queue, 1, &submitInfo, fences[frameIndex]) != VK_SUCCESS) {
			std::cerr << "Failed to submit to queue" << std::endl;
			return -1;
		}
		frameIndex = (frameIndex + 1) % maxFramesInFlight;
    // Present image
    VkPresentInfoKHR presentInfo{
	    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
	    .waitSemaphoreCount = 1,
	    .pWaitSemaphores = &renderCompleteSemaphores[imageIndex],
	    .swapchainCount = 1,
	    .pSwapchains = &swapchain,
	    .pImageIndices = &imageIndex
		};
		if (vkQueuePresentKHR(queue, &presentInfo) == VK_ERROR_OUT_OF_DATE_KHR) {
      updateSwapchain = true;
			// std::cerr << "Write swapchain recreation code! It was needed." << std::endl;
			// return -1;
		}
    // Poll events
    float elapsedTime{ (SDL_GetTicks() - lastTime) / 1000.0f };
		lastTime = SDL_GetTicks();
		for (SDL_Event event; SDL_PollEvent(&event);) {

	    // Exit loop if the application is about to close
	    if (event.type == SDL_EVENT_QUIT) {
	      quit = true;
	      break;
	    }

	    // Rotate the selected object with mouse drag
	    if (event.type == SDL_EVENT_MOUSE_MOTION) {
	      if (event.button.button == SDL_BUTTON_LEFT) {
	        objectRotations[shaderData.selected].x -= (float)event.motion.yrel * elapsedTime;
	        objectRotations[shaderData.selected].y += (float)event.motion.xrel * elapsedTime;
	      }
	    }

	    // Zooming with the mouse wheel
	    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
	      camPos.z += (float)event.wheel.y * elapsedTime * 10.0f;
	    }

	    // Select active model instance
	    if (event.type == SDL_EVENT_KEY_DOWN) {
	      if (event.key.key == SDLK_PLUS || event.key.key == SDLK_KP_PLUS) {
	        shaderData.selected = (shaderData.selected < 2) ? shaderData.selected + 1 : 0;
	      }
	      if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
	        shaderData.selected = (shaderData.selected > 0) ? shaderData.selected - 1 : 2;
	      }
	    }

	    // Window resize
	    if (event.type == SDL_EVENT_WINDOW_RESIZED) {
	      updateSwapchain = true;
	    }
		}
		if (updateSwapchain) {
	    updateSwapchain = false;
	    if (vkDeviceWaitIdle(device) != VK_SUCCESS) {
				std::cerr << "Something went wrong while waiting for device." << std::endl;
				return -1;
	    }
	    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface, &surfaceCapabilities) != VK_SUCCESS) {
				std::cerr << "Could not retrieve physical device capabilities." << std::endl;
				return -1;
	    }
	    swapchainCreateInfo.oldSwapchain = swapchain;
	    swapchainCreateInfo.imageExtent = { .width = static_cast<uint32_t>(windowSize.x), .height = static_cast<uint32_t>(windowSize.y) };
	    if (vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain) != VK_SUCCESS) {
				std::cerr << "Failed to create swapchain." << std::endl;
				return -1;
	    }
	    for (auto i = 0; i < imageCount; i++) {
	        vkDestroyImageView(device, swapchainImageViews[i], nullptr);
	    }
	    if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr) != VK_SUCCESS) {
				std::cerr << "Failed to get swapchain images." << std::endl;
				return -1;
	    }
	    swapchainImages.resize(imageCount);
	    if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()) != VK_SUCCESS) {
				std::cerr << "Failed to get swapchain images." << std::endl;
				return -1;
	    }
	    swapchainImageViews.resize(imageCount);
	    for (auto i = 0; i < imageCount; i++) {
	        VkImageViewCreateInfo viewCI{
	            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	            .image = swapchainImages[i],
	            .viewType = VK_IMAGE_VIEW_TYPE_2D,
	            .format = imageFormat,
	            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
	        };
	        if (vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
						std::cerr << "Failed to create image view." << std::endl;
						return -1;
	        }
	    }
	    for (auto& semaphore : renderCompleteSemaphores) {
	        vkDestroySemaphore(device, semaphore, nullptr);
	    }
	    renderCompleteSemaphores.resize(imageCount);
	    for (auto& semaphore : renderCompleteSemaphores) {
	        if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphore)  != VK_SUCCESS) {
						std::cerr << "Failed to create semaphore." << std::endl;
						return -1;
	        }
	    }   
	    vkDestroySwapchainKHR(device, swapchainCreateInfo.oldSwapchain, nullptr);
	    vmaDestroyImage(allocator, depthImage, depthImageAllocation);
	    vkDestroyImageView(device, depthImageView, nullptr);
	    depthImageCreateInfo.extent = { .width = static_cast<uint32_t>(windowSize.x), .height = static_cast<uint32_t>(windowSize.y), .depth = 1 };
	    VmaAllocationCreateInfo allocCI{
	        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
	        .usage = VMA_MEMORY_USAGE_AUTO
	    };
	    if (vmaCreateImage(allocator, &depthImageCreateInfo, &allocCI, &depthImage, &depthImageAllocation, nullptr) != VK_SUCCESS) {
						std::cerr << "Failed to create image." << std::endl;
						return -1;
	    }
	    VkImageViewCreateInfo viewCreateInfo{
	        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	        .image = depthImage,
	        .viewType = VK_IMAGE_VIEW_TYPE_2D,
	        .format = depthFormat,
	        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
	    };
	    if (vkCreateImageView(device, &viewCreateInfo, nullptr, &depthImageView) != VK_SUCCESS) {
				std::cerr << "Failed to create image view." << std::endl;
				return -1;
	    }
		}
	}

  // Clean up
  	for (auto i = 0; i < maxFramesInFlight; i++) {
		vkDestroyFence(device, fences[i], nullptr);
		vkDestroySemaphore(device, imageAcquiredSemaphores[i], nullptr);
		vmaDestroyBuffer(allocator, shaderDataBuffers[i].buffer, shaderDataBuffers[i].allocation);
	}
	for (auto i = 0; i < renderCompleteSemaphores.size(); i++) {
		vkDestroySemaphore(device, renderCompleteSemaphores[i], nullptr);
	}
	vmaDestroyImage(allocator, depthImage, depthImageAllocation);
	vkDestroyImageView(device, depthImageView, nullptr);
	for (auto i = 0; i < swapchainImageViews.size(); i++) {
		vkDestroyImageView(device, swapchainImageViews[i], nullptr);
	}
	vmaDestroyBuffer(allocator, vBuffer, vBufferAllocation);
	for (auto i = 0; i < textures.size(); i++) {
		vkDestroyImageView(device, textures[i].view, nullptr);
		vkDestroySampler(device, textures[i].sampler, nullptr);
		vmaDestroyImage(allocator, textures[i].image, textures[i].allocation);
	}
	vkDestroyDescriptorSetLayout(device, descriptorSetLayoutTex, nullptr);
	vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroySwapchainKHR(device, swapchain, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyCommandPool(device, commandPool, nullptr);
	vkDestroyShaderModule(device, shaderModule, nullptr);
	vmaDestroyAllocator(allocator);
	SDL_DestroyWindow(window);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	SDL_Quit();
	vkDestroyDevice(device, nullptr);
	vkDestroyInstance(instance, nullptr);
  return 0;
}
