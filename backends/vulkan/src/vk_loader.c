#include "vk_minimal.h"

#include <dlfcn.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

bool jvk_load_fns(void *loader, jvk_fns_t *out_fns) {
    static const char *const names[] = {
        "vkEnumerateInstanceVersion",
        "vkCreateInstance",
        "vkDestroyInstance",
        "vkEnumeratePhysicalDevices",
        "vkGetPhysicalDeviceProperties",
        "vkGetPhysicalDeviceQueueFamilyProperties",
        "vkGetPhysicalDeviceMemoryProperties",
        "vkCreateDevice",
        "vkDestroyDevice",
        "vkGetDeviceQueue",
        "vkCreateBuffer",
        "vkDestroyBuffer",
        "vkGetBufferMemoryRequirements",
        "vkAllocateMemory",
        "vkFreeMemory",
        "vkBindBufferMemory",
        "vkMapMemory",
        "vkUnmapMemory",
        "vkFlushMappedMemoryRanges",
        "vkInvalidateMappedMemoryRanges",
        "vkCreateShaderModule",
        "vkDestroyShaderModule",
        "vkCreateDescriptorSetLayout",
        "vkDestroyDescriptorSetLayout",
        "vkCreatePipelineLayout",
        "vkDestroyPipelineLayout",
        "vkCreateDescriptorPool",
        "vkDestroyDescriptorPool",
        "vkAllocateDescriptorSets",
        "vkUpdateDescriptorSets",
        "vkCreateComputePipelines",
        "vkDestroyPipeline",
        "vkCreateCommandPool",
        "vkDestroyCommandPool",
        "vkAllocateCommandBuffers",
        "vkFreeCommandBuffers",
        "vkBeginCommandBuffer",
        "vkEndCommandBuffer",
        "vkCmdBindPipeline",
        "vkCmdBindDescriptorSets",
        "vkCmdPushConstants",
        "vkCmdDispatch",
        "vkCmdPipelineBarrier",
        "vkQueueSubmit",
        "vkCreateFence",
        "vkDestroyFence",
        "vkWaitForFences",
        "vkDeviceWaitIdle",
    };
    /* Must match the declaration order of jvk_fns_t field by field. */
    _Static_assert(sizeof(names) / sizeof(*names) == 48,
        "loader table must cover every jvk_fns_t entry");
    if (!loader || !out_fns) {
        return false;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        void *symbol = dlsym(loader, names[i]);
        if (!symbol) {
            memset(out_fns, 0, sizeof(*out_fns));
            return false;
        }
        /* Copy the bits without aliasing a function pointer as data. */
        memcpy((char *)out_fns + i * sizeof(symbol), &symbol, sizeof(symbol));
    }
    return true;
}
