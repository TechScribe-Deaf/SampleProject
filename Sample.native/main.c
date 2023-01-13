
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include "smaPass1.h"
#include "smaPass2.h"

const int WORKGROUP_SIZE = 256;

#define VK_CHECK_RESULT(f)                                                                \
    {                                                                                     \
        VkResult res = (f);                                                               \
        if (res != VK_SUCCESS)                                                            \
        {                                                                                 \
            printf("Fatal : VkResult is %d in %s at line %d\n", res, __FILE__, __LINE__); \
            assert(res == VK_SUCCESS);                                                    \
        }                                                                                 \
    }

typedef struct Candlestick
{
    float open;
    float high;
    float low;
    float close;
} Candlestick;

typedef struct Indicator
{
    float sma;
    float padding1; // Can be used for future Indicators inclusion like DI+/DI-/ADX and so forth
    float padding2;
    float padding3;
} Indicator;

typedef struct ComputeApplication
{
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkPipeline smaFirstPassPipeline;
    VkPipeline smaSecondPassPipeline;
    VkPipelineLayout pipelineLayout;
    VkShaderModule smaFirstPassShaderModule;
    VkShaderModule smaSecondPassShaderModule;
    VkShaderModule adxFirstPassShaderModule;
    VkCommandPool commandPool;
    VkCommandBuffer smaPass1CommandBuffer;
    VkCommandBuffer smaPass2CommandBuffer;
    VkCommandBuffer copyInputBufferToDeviceCommand;
    VkCommandBuffer copyFromDeviceOutputCommand;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    VkDescriptorSetLayout descriptorSetLayout;
    VkQueue queue;
    uint32_t queueFamilyIndex;

    // Sample data
    uint32_t* inputData;
    uint32_t inputDataElementsCount;
    uint32_t inputBufferSize;

    // Related to Input Buffer
    VkBuffer inputBuffer;
    VkDeviceMemory inputBufferMemory;

    VkBuffer deviceOnlyInputBuffer;
    VkDeviceMemory deviceOnlyInputBufferMemory;

    // Related to Output Buffer
    VkBuffer outputBuffer;
    VkDeviceMemory outputBufferMemory;

    VkBuffer deviceOnlyOutputBuffer;
    VkDeviceMemory deviceOnlyOutputBufferMemory;
} *ComputeApplication;

static void InitializeVulkanInstance(ComputeApplication this)
{
    VkApplicationInfo applicationInfo = (VkApplicationInfo){
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Compute Sample",
        .applicationVersion = 0,
        .pEngineName = "TradingAnalysisEngine",
        .engineVersion = 0,
        .apiVersion = VK_API_VERSION_1_1
    };
    VkInstanceCreateInfo createInfo = (VkInstanceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = 0,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = NULL
    };
    VK_CHECK_RESULT(vkCreateInstance(&createInfo, NULL, &this->instance));
}

static void SelectPhysicalDevice(ComputeApplication this)
{
    uint32_t deviceCount;
    VK_CHECK_RESULT(vkEnumeratePhysicalDevices(this->instance, &deviceCount, NULL));
    if (deviceCount == 0)
    {
        printf("could not find a device with vulkan support\n");
        return;
    }

    VkPhysicalDevice devices[deviceCount];
    VK_CHECK_RESULT(vkEnumeratePhysicalDevices(this->instance, &deviceCount, devices));
    this->physicalDevice = devices[0];
}

static uint32_t getComputeQueueFamilyIndex(ComputeApplication this)
{
    uint32_t queueFamilyCount;

    vkGetPhysicalDeviceQueueFamilyProperties(this->physicalDevice, &queueFamilyCount, NULL);

    VkQueueFamilyProperties queueFamilies[queueFamilyCount];
    vkGetPhysicalDeviceQueueFamilyProperties(this->physicalDevice, &queueFamilyCount, queueFamilies);

    uint32_t i = 0;
    for (; i < queueFamilyCount; ++i)
    {
        VkQueueFamilyProperties props = queueFamilies[i];

        if (props.queueCount > 0 && (props.queueFlags & VK_QUEUE_COMPUTE_BIT))
        {
            break;
        }
    }

    if (i == queueFamilyCount)
    {
        printf("could not find a queue family that supports operations\n");
        exit(1);
    }

    return i;
}

static void InitializeVulkanDevice(ComputeApplication this)
{
    this->queueFamilyIndex = getComputeQueueFamilyIndex(this);
    float queuePriorities = 1.0;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = this->queueFamilyIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriorities
    };
    VkPhysicalDeviceFeatures deviceFeatures = {0};
    VkDeviceCreateInfo deviceCreateInfo = (VkDeviceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .pQueueCreateInfos = &queueCreateInfo,
        .queueCreateInfoCount = 1,
        .pEnabledFeatures = &deviceFeatures
    };

    VK_CHECK_RESULT(vkCreateDevice(this->physicalDevice, &deviceCreateInfo, NULL, &this->device));
    vkGetDeviceQueue(this->device, this->queueFamilyIndex, 0, &this->queue);
}

static uint32_t RetrieveMemoryType(ComputeApplication this, uint32_t memoryTypeBits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties;

    vkGetPhysicalDeviceMemoryProperties(this->physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((memoryTypeBits & (1 << i)) &&
            ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties))
            return i;
    }
    printf("Memory type not found!\n");
    return -1;
}

static void InitializeBuffers(ComputeApplication this)
{
    VkBufferCreateInfo bufferCreateInfo = (VkBufferCreateInfo){
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = this->inputBufferSize,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK_RESULT(vkCreateBuffer(this->device, &bufferCreateInfo, NULL, &this->inputBuffer));

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(this->device, this->inputBuffer, &memoryRequirements);
    VkMemoryAllocateInfo allocateInfo = (VkMemoryAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .pNext = NULL,
        .memoryTypeIndex = RetrieveMemoryType(this, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };

    VK_CHECK_RESULT(vkAllocateMemory(this->device, &allocateInfo, NULL, &this->inputBufferMemory));
    VK_CHECK_RESULT(vkBindBufferMemory(this->device, this->inputBuffer, this->inputBufferMemory, 0));
    // Device Only portion
    VK_CHECK_RESULT(vkCreateBuffer(this->device, &bufferCreateInfo, NULL, &this->deviceOnlyInputBuffer));
    vkGetBufferMemoryRequirements(this->device, this->deviceOnlyInputBuffer, &memoryRequirements);
    allocateInfo = (VkMemoryAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .pNext = NULL,
        .memoryTypeIndex = RetrieveMemoryType(this, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    VK_CHECK_RESULT(vkAllocateMemory(this->device, &allocateInfo, NULL, &this->deviceOnlyInputBufferMemory));
    VK_CHECK_RESULT(vkBindBufferMemory(this->device, this->deviceOnlyInputBuffer, this->deviceOnlyInputBufferMemory, 0));

    // Output Buffer Related
    bufferCreateInfo = (VkBufferCreateInfo){
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(Indicator) * this->inputDataElementsCount,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK_RESULT(vkCreateBuffer(this->device, &bufferCreateInfo, NULL, &this->outputBuffer));

    vkGetBufferMemoryRequirements(this->device, this->outputBuffer, &memoryRequirements);
    allocateInfo = (VkMemoryAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .pNext = NULL,
        .memoryTypeIndex = RetrieveMemoryType(this, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };

    VK_CHECK_RESULT(vkAllocateMemory(this->device, &allocateInfo, NULL, &this->outputBufferMemory));
    VK_CHECK_RESULT(vkBindBufferMemory(this->device, this->outputBuffer, this->outputBufferMemory, 0));

    // Device Only Portion
    VK_CHECK_RESULT(vkCreateBuffer(this->device, &bufferCreateInfo, NULL, &this->deviceOnlyOutputBuffer));
    vkGetBufferMemoryRequirements(this->device, this->deviceOnlyOutputBuffer, &memoryRequirements);
    allocateInfo = (VkMemoryAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .pNext = NULL,
        .memoryTypeIndex = RetrieveMemoryType(this, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    VK_CHECK_RESULT(vkAllocateMemory(this->device, &allocateInfo, NULL, &this->deviceOnlyOutputBufferMemory));
    VK_CHECK_RESULT(vkBindBufferMemory(this->device, this->deviceOnlyOutputBuffer, this->deviceOnlyOutputBufferMemory, 0));
}

static void InitializeDescriptorSetLayout(ComputeApplication this)
{
    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding[2];
    descriptorSetLayoutBinding[0].binding = 0;
    descriptorSetLayoutBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBinding[0].descriptorCount = 1;
    descriptorSetLayoutBinding[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    descriptorSetLayoutBinding[1].binding = 1;
    descriptorSetLayoutBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorSetLayoutBinding[1].descriptorCount = 1;
    descriptorSetLayoutBinding[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = (VkDescriptorSetLayoutCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = descriptorSetLayoutBinding
    };
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(this->device, &descriptorSetLayoutCreateInfo, NULL, &this->descriptorSetLayout));
}

static void InitializeDescriptorSets(ComputeApplication this)
{
    VkDescriptorPoolSize descriptorPoolSize = (VkDescriptorPoolSize){
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 2
    };

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = (VkDescriptorPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &descriptorPoolSize
    };
    VK_CHECK_RESULT(vkCreateDescriptorPool(this->device, &descriptorPoolCreateInfo, NULL, &this->descriptorPool));

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = (VkDescriptorSetAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = this->descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &this->descriptorSetLayout
    };
    VK_CHECK_RESULT(vkAllocateDescriptorSets(this->device, &descriptorSetAllocateInfo, &this->descriptorSet));

    VkDescriptorBufferInfo descriptorBufferInfo[2];
    descriptorBufferInfo[0].buffer = this->deviceOnlyInputBuffer;
    descriptorBufferInfo[0].offset = 0;
    descriptorBufferInfo[0].range = this->inputBufferSize;
    descriptorBufferInfo[1].buffer = this->deviceOnlyOutputBuffer;
    descriptorBufferInfo[1].offset = 0;
    descriptorBufferInfo[1].range = sizeof(Indicator) * this->inputDataElementsCount;

    VkWriteDescriptorSet writeDescriptorSet = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = this->descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 2,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = descriptorBufferInfo
    };
    vkUpdateDescriptorSets(this->device, 1, &writeDescriptorSet, 0, NULL);
}

static uint32_t* readFile(const char *filename, uint32_t* outlength)
{

    FILE *fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        printf("Could not find or open file: %s\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    long filesizepadded = ((long)ceil(filesize / 4.0)) * 4;

    char* str = (char*)calloc(sizeof(char), filesizepadded);
    fread(str, filesize, sizeof(char), fp);
    fclose(fp);

    for (int i = filesize; i < filesizepadded; i++)
    {
        str[i] = 0;
    }

    *outlength = filesizepadded;
    return (uint32_t *)str;
}

static void InitializeComputePipelines(ComputeApplication this)
{
    uint32_t filelength = smaPass1_spv_len;
    uint32_t *code = (uint32_t*) smaPass1_spv;
    VkShaderModuleCreateInfo createInfo = (VkShaderModuleCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pCode = code,
        .codeSize = filelength
    };
    VK_CHECK_RESULT(vkCreateShaderModule(this->device, &createInfo, NULL, &this->smaFirstPassShaderModule));

    filelength = smaPass2_spv_len;
    code = (uint32_t*) smaPass2_spv;
    createInfo = (VkShaderModuleCreateInfo){
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pCode = code,
        .codeSize = filelength
    };
    VK_CHECK_RESULT(vkCreateShaderModule(this->device, &createInfo, NULL, &this->smaSecondPassShaderModule));

    VkPipelineShaderStageCreateInfo smaPass1ShaderStageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = this->smaFirstPassShaderModule,
        .pName = "main"
    };

    VkPipelineShaderStageCreateInfo smaPass2ShaderStageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = this->smaSecondPassShaderModule,
        .pName = "main"
    };

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &this->descriptorSetLayout
    };
    VK_CHECK_RESULT(vkCreatePipelineLayout(this->device, &pipelineLayoutCreateInfo, NULL, &this->pipelineLayout));

    VkComputePipelineCreateInfo smaPass1PipelineCreateInfo = {0};
    VkComputePipelineCreateInfo smaPass2PipelineCreateInfo = {0};

    smaPass1PipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    smaPass1PipelineCreateInfo.stage = smaPass1ShaderStageCreateInfo;
    smaPass1PipelineCreateInfo.layout = this->pipelineLayout;

    smaPass2PipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    smaPass2PipelineCreateInfo.stage = smaPass2ShaderStageCreateInfo;
    smaPass2PipelineCreateInfo.layout = this->pipelineLayout;

    VK_CHECK_RESULT(vkCreateComputePipelines(
        this->device, VK_NULL_HANDLE,
        1, &smaPass1PipelineCreateInfo,
        NULL, &this->smaFirstPassPipeline));

    VK_CHECK_RESULT(vkCreateComputePipelines(
        this->device, VK_NULL_HANDLE,
        1, &smaPass2PipelineCreateInfo,
        NULL, &this->smaSecondPassPipeline));
}

static void InitializeCommandBuffers(ComputeApplication this)
{
    VkCommandPoolCreateInfo commandPoolCreateInfo = (VkCommandPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = 0,
        .queueFamilyIndex = this->queueFamilyIndex
    };
    VK_CHECK_RESULT(vkCreateCommandPool(this->device, &commandPoolCreateInfo, NULL, &this->commandPool));
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = (VkCommandBufferAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = this->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VK_CHECK_RESULT(vkAllocateCommandBuffers(this->device, &commandBufferAllocateInfo, &this->smaPass1CommandBuffer));
    VK_CHECK_RESULT(vkAllocateCommandBuffers(this->device, &commandBufferAllocateInfo, &this->smaPass2CommandBuffer));
    VK_CHECK_RESULT(vkAllocateCommandBuffers(this->device, &commandBufferAllocateInfo, &this->copyInputBufferToDeviceCommand));
    VK_CHECK_RESULT(vkAllocateCommandBuffers(this->device, &commandBufferAllocateInfo, &this->copyFromDeviceOutputCommand));
    VkCommandBufferBeginInfo beginInfo = (VkCommandBufferBeginInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VkBufferCopy bufferCopy = (VkBufferCopy){
        .size = this->inputBufferSize,
        .dstOffset = 0,
        .srcOffset = 0
    };

    VK_CHECK_RESULT(vkBeginCommandBuffer(this->copyInputBufferToDeviceCommand, &beginInfo));
    vkCmdCopyBuffer(this->copyInputBufferToDeviceCommand, this->inputBuffer, this->deviceOnlyInputBuffer, 1, &bufferCopy);
    VK_CHECK_RESULT(vkEndCommandBuffer(this->copyInputBufferToDeviceCommand));

    VK_CHECK_RESULT(vkBeginCommandBuffer(this->smaPass1CommandBuffer, &beginInfo));
    vkCmdBindPipeline(this->smaPass1CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, this->smaFirstPassPipeline);
    vkCmdBindDescriptorSets(this->smaPass1CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, this->pipelineLayout, 0, 1, &this->descriptorSet, 0, NULL);
    vkCmdDispatch(this->smaPass1CommandBuffer, (uint32_t)ceil((double)this->inputBufferSize / (double)WORKGROUP_SIZE) + 1, 1, 1);
    VK_CHECK_RESULT(vkEndCommandBuffer(this->smaPass1CommandBuffer));

    VK_CHECK_RESULT(vkBeginCommandBuffer(this->smaPass2CommandBuffer, &beginInfo));
    vkCmdBindPipeline(this->smaPass2CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, this->smaSecondPassPipeline);
    vkCmdBindDescriptorSets(this->smaPass2CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, this->pipelineLayout, 0, 1, &this->descriptorSet, 0, NULL);
    vkCmdDispatch(this->smaPass2CommandBuffer, (uint32_t)ceil((double)this->inputBufferSize / (double)WORKGROUP_SIZE), 1, 1);
    VK_CHECK_RESULT(vkEndCommandBuffer(this->smaPass2CommandBuffer));

    bufferCopy = (VkBufferCopy){
        .size = this->inputDataElementsCount * sizeof(Indicator),
        .dstOffset = 0,
        .srcOffset = 0
    };

    VK_CHECK_RESULT(vkBeginCommandBuffer(this->copyFromDeviceOutputCommand, &beginInfo));
    vkCmdCopyBuffer(this->copyFromDeviceOutputCommand, this->deviceOnlyOutputBuffer, this->outputBuffer, 1, &bufferCopy);
    VK_CHECK_RESULT(vkEndCommandBuffer(this->copyFromDeviceOutputCommand));
}

static void ExecuteComputeShaders(ComputeApplication this)
{
    VkSubmitInfo inputBufferCopySubmitInfo = (VkSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &this->copyInputBufferToDeviceCommand
    };
    VkSubmitInfo outputBufferCopySubmitInfo = (VkSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &this->copyFromDeviceOutputCommand
    };
    VkSubmitInfo smaPass1SubmitInfo = (VkSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &this->smaPass1CommandBuffer
    };
    VkSubmitInfo smaPass2SubmitInfo = (VkSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &this->smaPass2CommandBuffer
    };
    VkFence fence;
    VkFenceCreateInfo fenceCreateInfo = (VkFenceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = 0
    };
    VK_CHECK_RESULT(vkCreateFence(this->device, &fenceCreateInfo, NULL, &fence));

    VK_CHECK_RESULT(vkQueueSubmit(this->queue, 1, &inputBufferCopySubmitInfo, fence));
    VK_CHECK_RESULT(vkWaitForFences(this->device, 1, &fence, VK_TRUE, 100000000000));
    VK_CHECK_RESULT(vkQueueSubmit(this->queue, 1, &smaPass1SubmitInfo, fence));
    VK_CHECK_RESULT(vkWaitForFences(this->device, 1, &fence, VK_TRUE, 100000000000));
    VK_CHECK_RESULT(vkQueueSubmit(this->queue, 1, &smaPass2SubmitInfo, fence));
    VK_CHECK_RESULT(vkWaitForFences(this->device, 1, &fence, VK_TRUE, 100000000000));
    VK_CHECK_RESULT(vkQueueSubmit(this->queue, 1, &outputBufferCopySubmitInfo, fence));
    VK_CHECK_RESULT(vkWaitForFences(this->device, 1, &fence, VK_TRUE, 100000000000));

    vkDestroyFence(this->device, fence, NULL);
}

static void CleanUpVulkan(ComputeApplication this)
{
    vkFreeMemory(this->device, this->inputBufferMemory, NULL);
    vkFreeMemory(this->device, this->outputBufferMemory, NULL);
    vkFreeMemory(this->device, this->deviceOnlyInputBufferMemory, NULL);
    vkFreeMemory(this->device, this->deviceOnlyOutputBufferMemory, NULL);
    vkDestroyBuffer(this->device, this->inputBuffer, NULL);
    vkDestroyBuffer(this->device, this->outputBuffer, NULL);
    vkDestroyBuffer(this->device, this->deviceOnlyInputBuffer, NULL);
    vkDestroyBuffer(this->device, this->deviceOnlyOutputBuffer, NULL);
    vkDestroyShaderModule(this->device, this->smaFirstPassShaderModule, NULL);
    vkDestroyShaderModule(this->device, this->smaSecondPassShaderModule, NULL);
    vkDestroyDescriptorPool(this->device, this->descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(this->device, this->descriptorSetLayout, NULL);
    vkDestroyPipelineLayout(this->device, this->pipelineLayout, NULL);
    vkDestroyPipeline(this->device, this->smaFirstPassPipeline, NULL);
    vkDestroyPipeline(this->device, this->smaSecondPassPipeline, NULL);
    vkDestroyCommandPool(this->device, this->commandPool, NULL);
    vkDestroyDevice(this->device, NULL);
    vkDestroyInstance(this->instance, NULL);
}

static void LoadSampleFile(ComputeApplication this)
{
    this->inputData = readFile("sample.dat", &this->inputBufferSize);
    this->inputDataElementsCount = this->inputBufferSize / sizeof(Candlestick);
}

static void CopySampleDataIntoInputBuffer(ComputeApplication this)
{
    void *mappedMemory = NULL;
    VK_CHECK_RESULT(vkMapMemory(this->device, this->inputBufferMemory, 0, VK_WHOLE_SIZE, 0, &mappedMemory));
    memcpy(mappedMemory, this->inputData, this->inputBufferSize);
    vkUnmapMemory(this->device, this->inputBufferMemory);
}

static void PrintAllResults(ComputeApplication this)
{
    Candlestick* mappedMemory = NULL;
    Indicator* outputMem = NULL;
    VK_CHECK_RESULT(vkMapMemory(this->device, this->inputBufferMemory, 0, VK_WHOLE_SIZE, 0, (void**)&mappedMemory));
    VK_CHECK_RESULT(vkMapMemory(this->device, this->outputBufferMemory, 0, VK_WHOLE_SIZE, 0, (void**)&outputMem));
    for (uint64_t i = 1; i < this->inputDataElementsCount; ++i)
    {
        printf("Open: %f, High: %f, Low: %f, Close: %f\n", mappedMemory[i].open, mappedMemory[i].high, mappedMemory[i].low, mappedMemory[i].close);
        printf("\tsma: %f\n", outputMem[i].sma);
    }
    vkUnmapMemory(this->device, this->inputBufferMemory);
    vkUnmapMemory(this->device, this->outputBufferMemory);
}

void run(ComputeApplication this)
{
    LoadSampleFile(this);
    InitializeVulkanInstance(this);
    SelectPhysicalDevice(this);
    InitializeVulkanDevice(this);
    InitializeBuffers(this);
    InitializeDescriptorSetLayout(this);
    InitializeDescriptorSets(this);
    InitializeComputePipelines(this);
    InitializeCommandBuffers(this);
    CopySampleDataIntoInputBuffer(this);
    ExecuteComputeShaders(this);
    PrintAllResults(this);
    CleanUpVulkan(this);
}

static ComputeApplication app;

int ComputeResult(Candlestick* kline, size_t kline_elements_count, Indicator* output)
{
    if (output == NULL || kline == NULL || kline_elements_count <= 0)
        return 1;
    app = (ComputeApplication) calloc(sizeof(struct ComputeApplication), 1);
    app->inputData = (uint32_t*) kline;
    app->inputBufferSize = kline_elements_count * sizeof(struct Candlestick);
    app->inputDataElementsCount = kline_elements_count;
    InitializeVulkanInstance(app);
    SelectPhysicalDevice(app);
    InitializeVulkanDevice(app);
    InitializeBuffers(app);
    InitializeDescriptorSetLayout(app);
    InitializeDescriptorSets(app);
    InitializeComputePipelines(app);
    InitializeCommandBuffers(app);
    void *mappedMemory = NULL;
    VK_CHECK_RESULT(vkMapMemory(app->device, app->inputBufferMemory, 0, VK_WHOLE_SIZE, 0, &mappedMemory));
    memcpy(mappedMemory, kline, sizeof(struct Candlestick) * kline_elements_count);
    vkUnmapMemory(app->device, app->inputBufferMemory);
    ExecuteComputeShaders(app);
    Indicator* outputMem = NULL;
    VK_CHECK_RESULT(vkMapMemory(app->device, app->outputBufferMemory, 0, VK_WHOLE_SIZE, 0, (void**)&outputMem));
    memcpy(output, outputMem, sizeof(Indicator) * kline_elements_count);
    vkUnmapMemory(app->device, app->outputBufferMemory);
    CleanUpVulkan(app);
    free(app);
    return 0;
}