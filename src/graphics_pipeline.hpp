#ifndef GRAPHICS_PIPELINE_HPP_INCLUDED
#define GRAPHICS_PIPELINE_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <memory>

#include "vulkan_include.hpp"

#include "logical_device.hpp"

namespace vkBasalt
{
    // Added default argument of 0 so existing calls don't break
    VkPipelineLayout createGraphicsPipelineLayout(LogicalDevice* pLogicalDevice, const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts, uint32_t pushConstantSize = 0);

    VkPipeline createGraphicsPipeline(LogicalDevice*        pLogicalDevice,
                                      VkShaderModule        vertexModule,
                                      VkSpecializationInfo* vertexSpecializationInfo,
                                      const std::string&    vertexEntryPoint,
                                      VkShaderModule        fragmentModule,
                                      VkSpecializationInfo* fragmentSpecializationInfo,
                                      const std::string&    fragmentEntryPoint,
                                      VkExtent2D            extent,
                                      VkRenderPass          renderPass,
                                      VkPipelineLayout      pipelineLayout,
                                      bool                  flip = false);

} // namespace vkBasalt

#endif // GRAPHICS_PIPELINE_HPP_INCLUDED
