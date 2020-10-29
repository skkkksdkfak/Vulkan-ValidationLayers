#include "cast_utils.h"
#include "layer_validation_tests.h"
#include <iostream>

TEST_F(VkLayerTest, InvalidMixingProtectedResources) {
    TEST_DESCRIPTION("Test where there is mixing of protectedMemory backed resource in command buffers");
    std::cout << "InvalidMixingProtectedResources" << std::endl;

    SetTargetApiVersion(VK_API_VERSION_1_1);

    if (InstanceExtensionSupported(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
        m_instance_extension_names.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    } else {
        printf("%s Did not find required instance extension %s; skipped.\n", kSkipPrefix,
               VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        return;
    }
    ASSERT_NO_FATAL_FAILURE(InitFramework(m_errorMonitor));

    if (IsPlatform(kShieldTV) || IsPlatform(kShieldTVb)) {
        printf("%s CreateImageView calls crash ShieldTV, skipped for this platform.\n", kSkipPrefix);
        return;
    }

    PFN_vkGetPhysicalDeviceFeatures2KHR vkGetPhysicalDeviceFeatures2KHR =
        (PFN_vkGetPhysicalDeviceFeatures2KHR)vk::GetInstanceProcAddr(instance(), "vkGetPhysicalDeviceFeatures2KHR");
    ASSERT_TRUE(vkGetPhysicalDeviceFeatures2KHR != nullptr);

    auto protected_memory_features = lvl_init_struct<VkPhysicalDeviceProtectedMemoryFeatures>();
    auto features2 = lvl_init_struct<VkPhysicalDeviceFeatures2KHR>(&protected_memory_features);
    vkGetPhysicalDeviceFeatures2KHR(gpu(), &features2);

    if (protected_memory_features.protectedMemory == VK_FALSE) {
        printf("%s protectedMemory feature not supported, skipped.\n", kSkipPrefix);
        return;
    };

    // Turns m_commandBuffer into a unprotected command buffer
    ASSERT_NO_FATAL_FAILURE(InitState(nullptr, &features2));

    VkCommandPoolObj protectedCommandPool(m_device, m_device->graphics_queue_node_index_, VK_COMMAND_POOL_CREATE_PROTECTED_BIT);
    VkCommandBufferObj protectedCommandBuffer(m_device, &protectedCommandPool);

    if (DeviceValidationVersion() < VK_API_VERSION_1_1) {
        printf("%s Tests requires Vulkan 1.1+, skipping test\n", kSkipPrefix);
        return;
    }

    // Create actual protected and unprotected buffers
    VkBuffer buffer_protected = VK_NULL_HANDLE;
    VkBuffer buffer_unprotected = VK_NULL_HANDLE;
    VkBufferCreateInfo buffer_create_info = {};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.pNext = nullptr;
    buffer_create_info.size = 1 << 20;  // 1 MB
    buffer_create_info.usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    buffer_create_info.flags = VK_BUFFER_CREATE_PROTECTED_BIT;
    vk::CreateBuffer(device(), &buffer_create_info, nullptr, &buffer_protected);
    buffer_create_info.flags = 0;
    vk::CreateBuffer(device(), &buffer_create_info, nullptr, &buffer_unprotected);

    // Create actual protected and unprotected images
    VkImageObj image_protected(m_device);
    VkImageObj image_unprotected(m_device);
    VkImageObj image_protected_descriptor(m_device);
    VkImageObj image_unprotected_descriptor(m_device);
    VkImageView image_views[2];
    VkImageView image_views_descriptor[2];
    VkImageCreateInfo image_create_info = {};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.pNext = nullptr;
    image_create_info.extent = {64, 64, 1};
    image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.arrayLayers = 1;
    image_create_info.mipLevels = 1;

    image_create_info.flags = VK_IMAGE_CREATE_PROTECTED_BIT;
    image_protected.init_no_mem(*m_device, image_create_info);
    image_protected.SetLayout(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_views[0] = image_protected.targetView(VK_FORMAT_R8G8B8A8_UNORM);

    image_protected_descriptor.init_no_mem(*m_device, image_create_info);
    image_protected_descriptor.SetLayout(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_views_descriptor[0] = image_protected_descriptor.targetView(VK_FORMAT_R8G8B8A8_UNORM);

    image_create_info.flags = 0;
    image_unprotected.init_no_mem(*m_device, image_create_info);
    image_unprotected.SetLayout(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_views[1] = image_unprotected.targetView(VK_FORMAT_R8G8B8A8_UNORM);

    image_unprotected_descriptor.init_no_mem(*m_device, image_create_info);
    image_unprotected_descriptor.SetLayout(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_views_descriptor[1] = image_unprotected_descriptor.targetView(VK_FORMAT_R8G8B8A8_UNORM);

    // Create protected and unproteced memory
    VkDeviceMemory memory_protected = VK_NULL_HANDLE;
    VkDeviceMemory memory_unprotected = VK_NULL_HANDLE;

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = nullptr;
    alloc_info.allocationSize = 0;

    // set allocationSize to buffer as it will be larger than the image, but query image to avoid BP warning
    VkMemoryRequirements mem_reqs_protected;
    vk::GetImageMemoryRequirements(device(), image_protected.handle(), &mem_reqs_protected);
    vk::GetBufferMemoryRequirements(device(), buffer_protected, &mem_reqs_protected);
    VkMemoryRequirements mem_reqs_unprotected;
    vk::GetImageMemoryRequirements(device(), image_unprotected.handle(), &mem_reqs_unprotected);
    vk::GetBufferMemoryRequirements(device(), buffer_unprotected, &mem_reqs_unprotected);

    // Get memory index for a protected and unprotected memory
    VkPhysicalDeviceMemoryProperties phys_mem_props;
    vk::GetPhysicalDeviceMemoryProperties(gpu(), &phys_mem_props);
    uint32_t memory_type_protected = phys_mem_props.memoryTypeCount + 1;
    uint32_t memory_type_unprotected = phys_mem_props.memoryTypeCount + 1;
    for (uint32_t i = 0; i < phys_mem_props.memoryTypeCount; i++) {
        if ((mem_reqs_unprotected.memoryTypeBits & (1 << i)) &&
            ((phys_mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memory_type_unprotected = i;
        }
        // Check just protected bit is in type at all
        if ((mem_reqs_protected.memoryTypeBits & (1 << i)) &&
            ((phys_mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_PROTECTED_BIT) != 0)) {
            memory_type_protected = i;
        }
    }
    if ((memory_type_protected >= phys_mem_props.memoryTypeCount) || (memory_type_unprotected >= phys_mem_props.memoryTypeCount)) {
        printf("%s No valid memory type index could be found; skipped.\n", kSkipPrefix);
        vk::DestroyBuffer(device(), buffer_protected, nullptr);
        vk::DestroyBuffer(device(), buffer_unprotected, nullptr);
        return;
    }

    alloc_info.memoryTypeIndex = memory_type_protected;
    alloc_info.allocationSize = mem_reqs_protected.size;
    vk::AllocateMemory(device(), &alloc_info, NULL, &memory_protected);

    alloc_info.allocationSize = mem_reqs_unprotected.size;
    alloc_info.memoryTypeIndex = memory_type_unprotected;
    vk::AllocateMemory(device(), &alloc_info, NULL, &memory_unprotected);

    vk::BindBufferMemory(device(), buffer_protected, memory_protected, 0);
    vk::BindBufferMemory(device(), buffer_unprotected, memory_unprotected, 0);
    vk::BindImageMemory(device(), image_protected.handle(), memory_protected, 0);
    vk::BindImageMemory(device(), image_unprotected.handle(), memory_unprotected, 0);

    // A renderpass and framebuffer that contains a protected and unprotected image view
    VkAttachmentDescription attachments[2] = {
        {0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    };
    VkAttachmentReference references[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                           {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkSubpassDescription subpass = {0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, 2, references, nullptr, nullptr, 0, nullptr};
    VkSubpassDependency dependency = {0,
                                      0,
                                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                      VK_ACCESS_SHADER_WRITE_BIT,
                                      VK_ACCESS_SHADER_WRITE_BIT,
                                      VK_DEPENDENCY_BY_REGION_BIT};
    VkRenderPassCreateInfo render_pass_create_info = {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, 2, attachments, 1, &subpass, 1, &dependency};
    VkRenderPass render_pass;
    ASSERT_VK_SUCCESS(vk::CreateRenderPass(device(), &render_pass_create_info, nullptr, &render_pass));
    VkFramebufferCreateInfo framebuffer_create_info = {
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, render_pass, 2, image_views, 8, 8, 1};
    VkFramebuffer framebuffer;
    ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &framebuffer_create_info, nullptr, &framebuffer));

    // Various structs used for commands
    VkImageSubresourceLayers image_subresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkImageBlit blit_region = {};
    blit_region.srcSubresource = image_subresource;
    blit_region.dstSubresource = image_subresource;
    blit_region.srcOffsets[0] = {0, 0, 0};
    blit_region.srcOffsets[1] = {8, 8, 1};
    blit_region.dstOffsets[0] = {0, 8, 0};
    blit_region.dstOffsets[1] = {8, 8, 1};
    VkClearColorValue clear_color = {{0, 0, 0, 0}};
    VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkBufferCopy buffer_copy = {0, 0, 64};
    VkBufferImageCopy buffer_image_copy = {};
    buffer_image_copy.bufferRowLength = 0;
    buffer_image_copy.bufferImageHeight = 0;
    buffer_image_copy.imageSubresource = image_subresource;
    buffer_image_copy.imageOffset = {0, 0, 0};
    buffer_image_copy.imageExtent = {1, 1, 1};
    buffer_image_copy.bufferOffset = 0;
    VkImageCopy image_copy = {};
    image_copy.srcSubresource = image_subresource;
    image_copy.srcOffset = {0, 0, 0};
    image_copy.dstSubresource = image_subresource;
    image_copy.dstOffset = {0, 0, 0};
    image_copy.extent = {1, 1, 1};
    uint32_t update_data[4] = {0, 0, 0, 0};
    VkRect2D render_area = {{0, 0}, {8, 8}};
    VkRenderPassBeginInfo render_pass_begin = {
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr, render_pass, framebuffer, render_area, 0, nullptr};
    VkClearAttachment clear_attachments[2] = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, {m_clear_color}},
                                              {VK_IMAGE_ASPECT_COLOR_BIT, 1, {m_clear_color}}};
    VkClearRect clear_rect[2] = {{render_area, 0, 1}, {render_area, 0, 1}};

    const char fsSource[] =
        "#version 450\n"
        "layout(set=0, binding=0) uniform foo { int x; int y; } bar;\n"
        "layout(set=0, binding=1, rgba8) uniform image2D si1;\n"
        "layout(location=0) out vec4 x;\n"
        "void main(){\n"
        "   x = vec4(bar.y);\n"
        "   imageStore(si1, ivec2(0), vec4(0));\n"
        "}\n";
    VkShaderObj fs(m_device, fsSource, VK_SHADER_STAGE_FRAGMENT_BIT, this);

    CreatePipelineHelper g_pipe(*this);
    g_pipe.InitInfo();
    g_pipe.gp_ci_.renderPass = render_pass;
    g_pipe.shader_stages_ = {g_pipe.vs_->GetStageCreateInfo(), fs.GetStageCreateInfo()};
    g_pipe.dsl_bindings_ = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    g_pipe.InitState();
    ASSERT_VK_SUCCESS(g_pipe.CreateGraphicsPipeline());

    VkSampler sampler;
    VkSamplerCreateInfo sampler_ci = SafeSaneSamplerCreateInfo();
    VkResult err = vk::CreateSampler(m_device->device(), &sampler_ci, nullptr, &sampler);
    ASSERT_VK_SUCCESS(err);

    // Use protected resources in unprotected command buffer
    g_pipe.descriptor_set_->WriteDescriptorBufferInfo(0, buffer_protected, 1024);
    g_pipe.descriptor_set_->WriteDescriptorImageInfo(1, image_views_descriptor[0], sampler, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    g_pipe.descriptor_set_->UpdateDescriptorSets();

    m_commandBuffer->begin();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdBlitImage-commandBuffer-01834");
    vk::CmdBlitImage(m_commandBuffer->handle(), image_protected.handle(), VK_IMAGE_LAYOUT_GENERAL, image_unprotected.handle(),
                     VK_IMAGE_LAYOUT_GENERAL, 1, &blit_region, VK_FILTER_NEAREST);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdBlitImage-commandBuffer-01835");
    vk::CmdBlitImage(m_commandBuffer->handle(), image_unprotected.handle(), VK_IMAGE_LAYOUT_GENERAL, image_protected.handle(),
                     VK_IMAGE_LAYOUT_GENERAL, 1, &blit_region, VK_FILTER_NEAREST);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdClearColorImage-commandBuffer-01805");
    vk::CmdClearColorImage(m_commandBuffer->handle(), image_protected.handle(), VK_IMAGE_LAYOUT_GENERAL, &clear_color, 1,
                           &subresource_range);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyBuffer-commandBuffer-01822");
    vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_protected, buffer_unprotected, 1, &buffer_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyBuffer-commandBuffer-01823");
    vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_unprotected, buffer_protected, 1, &buffer_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyBufferToImage-commandBuffer-01828");
    vk::CmdCopyBufferToImage(m_commandBuffer->handle(), buffer_protected, image_unprotected.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                             &buffer_image_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyBufferToImage-commandBuffer-01829");
    vk::CmdCopyBufferToImage(m_commandBuffer->handle(), buffer_unprotected, image_protected.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                             &buffer_image_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyImage-commandBuffer-01825");
    vk::CmdCopyImage(m_commandBuffer->handle(), image_protected.handle(), VK_IMAGE_LAYOUT_GENERAL, image_unprotected.handle(),
                     VK_IMAGE_LAYOUT_GENERAL, 1, &image_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyImage-commandBuffer-01826");
    vk::CmdCopyImage(m_commandBuffer->handle(), image_unprotected.handle(), VK_IMAGE_LAYOUT_GENERAL, image_protected.handle(),
                     VK_IMAGE_LAYOUT_GENERAL, 1, &image_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyImageToBuffer-commandBuffer-01831");
    vk::CmdCopyImageToBuffer(m_commandBuffer->handle(), image_protected.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_unprotected, 1,
                             &buffer_image_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyImageToBuffer-commandBuffer-01832");
    vk::CmdCopyImageToBuffer(m_commandBuffer->handle(), image_unprotected.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_protected, 1,
                             &buffer_image_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdFillBuffer-commandBuffer-01811");
    vk::CmdFillBuffer(m_commandBuffer->handle(), buffer_protected, 0, 4, 0);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdUpdateBuffer-commandBuffer-01813");
    vk::CmdUpdateBuffer(m_commandBuffer->handle(), buffer_protected, 0, 4, (void *)update_data);
    m_errorMonitor->VerifyFound();

    vk::CmdBeginRenderPass(m_commandBuffer->handle(), &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdClearAttachments-commandBuffer-02504");
    vk::CmdClearAttachments(m_commandBuffer->handle(), 2, clear_attachments, 2, clear_rect);
    m_errorMonitor->VerifyFound();
    return;
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &g_pipe.descriptor_set_->set_, 0, nullptr);

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdDraw-commandBuffer-02712");
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);
    m_errorMonitor->VerifyFound();

    vk::CmdEndRenderPass(m_commandBuffer->handle());
    m_commandBuffer->end();

    // Use unprotected resources in protected command buffer
    g_pipe.descriptor_set_->WriteDescriptorBufferInfo(0, buffer_unprotected, 1024);
    g_pipe.descriptor_set_->WriteDescriptorImageInfo(1, image_views_descriptor[1], sampler);
    g_pipe.descriptor_set_->UpdateDescriptorSets();

    protectedCommandBuffer.begin();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdBlitImage-commandBuffer-01836");
    vk::CmdBlitImage(protectedCommandBuffer.handle(), image_protected.handle(), VK_IMAGE_LAYOUT_GENERAL, image_unprotected.handle(),
                     VK_IMAGE_LAYOUT_GENERAL, 1, &blit_region, VK_FILTER_NEAREST);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdClearColorImage-commandBuffer-01806");
    vk::CmdClearColorImage(protectedCommandBuffer.handle(), image_unprotected.handle(), VK_IMAGE_LAYOUT_GENERAL, &clear_color, 1,
                           &subresource_range);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyBuffer-commandBuffer-01824");
    vk::CmdCopyBuffer(protectedCommandBuffer.handle(), buffer_protected, buffer_unprotected, 1, &buffer_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyBufferToImage-commandBuffer-01830");
    vk::CmdCopyBufferToImage(protectedCommandBuffer.handle(), buffer_protected, image_unprotected.handle(), VK_IMAGE_LAYOUT_GENERAL,
                             1, &buffer_image_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyImage-commandBuffer-01827");
    vk::CmdCopyImage(protectedCommandBuffer.handle(), image_protected.handle(), VK_IMAGE_LAYOUT_GENERAL, image_unprotected.handle(),
                     VK_IMAGE_LAYOUT_GENERAL, 1, &image_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdCopyImageToBuffer-commandBuffer-01833");
    vk::CmdCopyImageToBuffer(protectedCommandBuffer.handle(), image_protected.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_unprotected,
                             1, &buffer_image_copy);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdFillBuffer-commandBuffer-01812");
    vk::CmdFillBuffer(protectedCommandBuffer.handle(), buffer_unprotected, 0, 4, 0);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdUpdateBuffer-commandBuffer-01814");
    vk::CmdUpdateBuffer(protectedCommandBuffer.handle(), buffer_unprotected, 0, 4, (void *)update_data);
    m_errorMonitor->VerifyFound();

    vk::CmdBeginRenderPass(protectedCommandBuffer.handle(), &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdClearAttachments-commandBuffer-02505");
    vk::CmdClearAttachments(protectedCommandBuffer.handle(), 2, clear_attachments, 2, clear_rect);
    m_errorMonitor->VerifyFound();

    vk::CmdBindPipeline(protectedCommandBuffer.handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(protectedCommandBuffer.handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0,
                              1, &g_pipe.descriptor_set_->set_, 0, nullptr);

    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdDraw-commandBuffer-02707");
    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-vkCmdDraw-commandBuffer-02707");
    vk::CmdDraw(protectedCommandBuffer.handle(), 1, 0, 0, 0);
    m_errorMonitor->VerifyFound();

    vk::CmdEndRenderPass(protectedCommandBuffer.handle());
    protectedCommandBuffer.end();

    // Try submitting together to test only 1 error occurs for the corresponding command buffer
    VkCommandBuffer comman_buffers[2] = {m_commandBuffer->handle(), protectedCommandBuffer.handle()};

    VkProtectedSubmitInfo protected_submit_info = {};
    protected_submit_info.sType = VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO;
    protected_submit_info.pNext = nullptr;

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = &protected_submit_info;
    submit_info.commandBufferCount = 2;
    submit_info.pCommandBuffers = comman_buffers;

    protected_submit_info.protectedSubmit = VK_TRUE;
    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-VkSubmitInfo-pNext-04148");
    vk::QueueSubmit(m_device->m_queue, 1, &submit_info, VK_NULL_HANDLE);
    m_errorMonitor->VerifyFound();

    protected_submit_info.protectedSubmit = VK_FALSE;
    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "VUID-VkSubmitInfo-pNext-04120");
    vk::QueueSubmit(m_device->m_queue, 1, &submit_info, VK_NULL_HANDLE);
    m_errorMonitor->VerifyFound();

    vk::DestroyBuffer(device(), buffer_protected, nullptr);
    vk::DestroyBuffer(device(), buffer_unprotected, nullptr);
    vk::FreeMemory(device(), memory_protected, nullptr);
    vk::FreeMemory(device(), memory_unprotected, nullptr);
    vk::DestroyFramebuffer(device(), framebuffer, nullptr);
    vk::DestroyRenderPass(device(), render_pass, nullptr);
}