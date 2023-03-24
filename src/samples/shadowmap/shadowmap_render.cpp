#include "shadowmap_render.h"

#include <geom/vk_mesh.h>
#include <vk_pipeline.h>
#include <vk_buffers.h>
#include <iostream>

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <vulkan/vulkan_core.h>

#define DASIZE 2048

/// RESOURCE ALLOCATION

void SimpleShadowmapRender::AllocateResources()
{
    mainViewDepth = m_context->createImage(etna::Image::CreateInfo{
        .extent     = vk::Extent3D{ m_width, m_height, 1 },
        .name       = "main_view_depth",
        .format     = vk::Format::eD32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled
        }
    );

    shadowMap = m_context->createImage(etna::Image::CreateInfo{
        .extent     = vk::Extent3D{ DASIZE, DASIZE, 1 },
        .name       = "shadow_map",
        .format     = vk::Format::eD16Unorm,
        .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled
        }
    );

    gNormalMap = m_context->createImage(etna::Image::CreateInfo{
        .extent     = vk::Extent3D{ m_width, m_height, 1 },
        .name       = "g_normal_map",
        .format     = vk::Format::eR32G32B32A32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
        }
    );

    gAlbedoMap = m_context->createImage(etna::Image::CreateInfo{
        .extent     = vk::Extent3D{ m_width, m_height, 1 },
        .name       = "albedo_map",
        .format     = vk::Format::eR32G32B32A32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
        }
    );
    
    defaultSampler = etna::Sampler(etna::Sampler::CreateInfo{ .name = "default_sampler" });
    constants      = m_context->createBuffer(etna::Buffer::CreateInfo{
        .size        = sizeof(UniformParams),
        .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
        .name        = "constants"
        }
    );

    m_uboMappedMem = constants.map();
}

void SimpleShadowmapRender::LoadScene(const char *path, bool transpose_inst_matrices)
{
    m_pScnMgr->LoadSceneXML(path, transpose_inst_matrices);

    // TODO: Make a separate stage
    loadShaders();
    PreparePipelines();

    auto loadedCam = m_pScnMgr->GetCamera(0);
    m_cam.fov      = loadedCam.fov;
    m_cam.pos      = float3(loadedCam.pos);
    m_cam.up       = float3(loadedCam.up);
    m_cam.lookAt   = float3(loadedCam.lookAt);
    m_cam.tdist    = loadedCam.farPlane;
}

void SimpleShadowmapRender::DeallocateResources()
{
    gNormalMap.reset();
    gAlbedoMap.reset();
    mainViewDepth.reset();// TODO: Make an etna method to reset all the resources
    shadowMap.reset();
    m_swapchain.Cleanup();
    vkDestroySurfaceKHR(GetVkInstance(), m_surface, nullptr);

    constants = etna::Buffer();
}


/// PIPELINES CREATION

void SimpleShadowmapRender::PreparePipelines()
{
    // create full screen quad for debug purposes
    //
    m_pFSQuadLightDepth = std::make_shared<vk_utils::FSQuad>();
    m_pFSQuadGNormal    = std::make_shared<vk_utils::FSQuad>();
    m_pFSQuadLightDepth->Create(m_context->getDevice(),
        VK_GRAPHICS_BASIC_ROOT "/resources/shaders/quad3_vert.vert.spv",
        VK_GRAPHICS_BASIC_ROOT "/resources/shaders/quad.frag.spv",
        vk_utils::RenderTargetInfo2D{
            .size          = VkExtent2D{ m_width, m_height },// this is debug full screen quad
            .format        = m_swapchain.GetFormat(),
            .loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD,// seems we need LOAD_OP_LOAD if we want to draw quad to part of screen
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        }
    );
    m_pFSQuadGNormal->Create(m_context->getDevice(),
        VK_GRAPHICS_BASIC_ROOT "/resources/shaders/quad3_vert.vert.spv",
        VK_GRAPHICS_BASIC_ROOT "/resources/shaders/quad.frag.spv",
        vk_utils::RenderTargetInfo2D{
            .size          = VkExtent2D{ m_width, m_height },// this is debug full screen quad
            .format        = m_swapchain.GetFormat(),
            .loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD,// seems we need LOAD_OP_LOAD if we want to draw quad to part of screen
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        }
    );
    SetupSimplePipeline();
}

void SimpleShadowmapRender::loadShaders()
{
    etna::create_program("simple_material",
        { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple_shadow.frag.spv", VK_GRAPHICS_BASIC_ROOT "/resources/shaders/quad.vert.spv"	}
    );
    etna::create_program("simple_shadow",
        { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple.vert.spv" }
    );
    etna::create_program("simple_deferred",
        { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple.vert.spv", VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple_deferred.frag.spv" }
    );
}

void SimpleShadowmapRender::SetupSimplePipeline()
{
    std::vector<std::pair<VkDescriptorType, uint32_t>> dtypes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 }
    };

    m_pBindings = std::make_shared<vk_utils::DescriptorMaker>(m_context->getDevice(), dtypes, 2);

    m_pBindings->BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
    m_pBindings->BindImage(0, shadowMap.getView({}), defaultSampler.get(), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    m_pBindings->BindEnd(&m_quadDSLightDepth, &m_quadDSLayoutLightDepth);

    m_pBindings->BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
    m_pBindings->BindImage(0, gNormalMap.getView({}), defaultSampler.get(), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    m_pBindings->BindEnd(&m_quadDSGNormal, &m_quadDSLayoutGNormal);

    etna::VertexShaderInputDescription sceneVertexInputDesc{
        .bindings = { etna::VertexShaderInputDescription::Binding{
            .byteStreamDescription = m_pScnMgr->GetVertexStreamDescription() } }
    };

    auto &pipelineManager  = etna::get_context().getPipelineManager();
    m_basicForwardPipeline = pipelineManager.createGraphicsPipeline("simple_material", {
            .inputAssemblyConfig = { .topology = vk::PrimitiveTopology::eTriangleStrip },	
            .rasterizationConfig  = { .cullMode = vk::CullModeFlagBits::eNone, .lineWidth = 1.0f },	
            .fragmentShaderOutput = {	
                .colorAttachmentFormats = {static_cast<vk::Format>(m_swapchain.GetFormat())},	
                .depthAttachmentFormat = vk::Format::eD32Sfloat	
            }	
        }
    );

    m_shadowPipeline       = pipelineManager.createGraphicsPipeline("simple_shadow", {
            .vertexShaderInput    = sceneVertexInputDesc,
            .fragmentShaderOutput = {
                .depthAttachmentFormat = vk::Format::eD16Unorm,
            }
        }
    );

    std::vector<vk::PipelineColorBlendAttachmentState> colorAttachmentStates;
    auto blendState = vk::PipelineColorBlendAttachmentState{
    .blendEnable    = false,
    .colorWriteMask = vk::ColorComponentFlagBits::eR
                        | vk::ColorComponentFlagBits::eG
                        | vk::ColorComponentFlagBits::eB
                        | vk::ColorComponentFlagBits::eA
    };
    for (size_t i = 0; i < 2; i++)
    {
    colorAttachmentStates.push_back(blendState);
    }

    m_deferredPipeline = pipelineManager.createGraphicsPipeline("simple_deferred", {
            .vertexShaderInput = sceneVertexInputDesc,
            .blendingConfig       = colorAttachmentStates,			
            .fragmentShaderOutput = {
                .colorAttachmentFormats = { vk::Format::eR32G32B32A32Sfloat, vk::Format::eR32G32B32A32Sfloat },
                .depthAttachmentFormat = vk::Format::eD32Sfloat
            }
        }
    );
}

void SimpleShadowmapRender::DestroyPipelines()
{	
    m_pFSQuadLightDepth = nullptr;// smartptr delete it's resources	
    m_pFSQuadGNormal    = nullptr;
}
//you stopped here you fuck

/// COMMAND BUFFER FILLING

void SimpleShadowmapRender::DrawSceneCmd(VkCommandBuffer a_cmdBuff, const float4x4 &a_wvp)
{
    VkShaderStageFlags stageFlags = (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDeviceSize zero_offset = 0u;
    VkBuffer vertexBuf       = m_pScnMgr->GetVertexBuffer();
    VkBuffer indexBuf        = m_pScnMgr->GetIndexBuffer();

    vkCmdBindVertexBuffers(a_cmdBuff, 0, 1, &vertexBuf, &zero_offset);
    vkCmdBindIndexBuffer(a_cmdBuff, indexBuf, 0, VK_INDEX_TYPE_UINT32);

    pushConst2M.projView = a_wvp;
    for (uint32_t i = 0; i < m_pScnMgr->InstancesNum(); ++i)
    {
        auto inst         = m_pScnMgr->GetInstanceInfo(i);
        pushConst2M.model = m_pScnMgr->GetInstanceMatrix(i);
        pushConst2M.id_albedo = i;
        vkCmdPushConstants(a_cmdBuff, m_basicForwardPipeline.getVkPipelineLayout(), stageFlags, 0, sizeof(pushConst2M), &pushConst2M);

        auto mesh_info = m_pScnMgr->GetMeshInfo(inst.mesh_id);
        vkCmdDrawIndexed(a_cmdBuff, mesh_info.m_indNum, 1, mesh_info.m_indexOffset, mesh_info.m_vertexOffset, 0);
    }
}

void SimpleShadowmapRender::BuildCommandBufferSimple(VkCommandBuffer a_cmdBuff, VkImage a_targetImage, VkImageView a_targetImageView)
{
    vkResetCommandBuffer(a_cmdBuff, 0);
    
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

    VK_CHECK_RESULT(vkBeginCommandBuffer(a_cmdBuff, &beginInfo));

    //// draw scene to shadowmap
    //
    {
        //etna::set_state(a_cmdBuff, shadowMap.get(), vk::PipelineStageFlagBits2::eEarlyFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth);
        etna::RenderTargetState renderTargets(a_cmdBuff, { DASIZE, DASIZE }, {}, shadowMap);
        {
            vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline.getVkPipeline());
            DrawSceneCmd(a_cmdBuff, m_lightMatrix);
        }
    }

    //// deferred - albedo + normals
    //
    {
        etna::RenderTargetState renderTargets(a_cmdBuff, { m_width, m_height }, { gNormalMap, gAlbedoMap},	mainViewDepth);
        {
            auto deferredInfo     = etna::get_shader_program("simple_deferred");
            auto set              = etna::create_descriptor_set(deferredInfo.getDescriptorLayoutId(0), a_cmdBuff, { etna::Binding{ 0, constants.genBinding() } });
            VkDescriptorSet vkSet = set.getVkSet();

            vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferredPipeline.getVkPipeline());
            vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferredPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, VK_NULL_HANDLE);
            DrawSceneCmd(a_cmdBuff, m_worldViewProj);
        }
    }

    //// draw final scene to screen
    //
    {
        etna::set_state(a_cmdBuff, shadowMap.get(), vk::PipelineStageFlagBits2::eEarlyFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eDepth);
        etna::set_state(a_cmdBuff, mainViewDepth.get(), vk::PipelineStageFlagBits2::eEarlyFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eDepth);
        etna::set_state(a_cmdBuff, gAlbedoMap.get(), vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
        etna::set_state(a_cmdBuff, gNormalMap.get(), vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
        
        {
            etna::RenderTargetState renderTargets(a_cmdBuff, { m_width, m_height }, { { a_targetImage, a_targetImageView } }, {});
            auto simpleMaterialInfo = etna::get_shader_program("simple_material");

            auto set = etna::create_descriptor_set(simpleMaterialInfo.getDescriptorLayoutId(0), a_cmdBuff, { 
                    etna::Binding{ 0, constants.genBinding() }, 
                    etna::Binding{ 1, shadowMap.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) },
                    etna::Binding{ 2, mainViewDepth.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) },  
                    etna::Binding{ 3, gAlbedoMap.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) }, 
                    etna::Binding{ 4, gNormalMap.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) }, 
                }
            );
            VkDescriptorSet vkSet = set.getVkSet();

            vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_basicForwardPipeline.getVkPipeline());
            vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_basicForwardPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, VK_NULL_HANDLE);

            pushConstDeferred.scaleAndOffset = LiteMath::float4(1.0f, 1.0f, 0.0f, 0.0f);
            vkCmdPushConstants(a_cmdBuff, m_basicForwardPipeline.getVkPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstDeferred), &pushConstDeferred);

            vkCmdDraw(a_cmdBuff, 4, 1, 0, 0);
        }
    }

    if (m_input.drawFSQuad)
    {
        float scaleAndOffset[4] = {0.25f, 0.25f, -0.75f, +0.75f}; 
        m_pFSQuadLightDepth->SetRenderTarget(a_targetImageView);
        m_pFSQuadLightDepth->DrawCmd(a_cmdBuff, m_quadDSLightDepth, scaleAndOffset);
        float scaleAndOffsetNorm[4] = {0.25f, 0.25f, -0.25f, 0.75f};
        m_pFSQuadGNormal->SetRenderTarget(a_targetImageView);
        m_pFSQuadGNormal->DrawCmd(a_cmdBuff, m_quadDSGNormal, scaleAndOffsetNorm);
    }

    etna::set_state(a_cmdBuff, a_targetImage, vk::PipelineStageFlagBits2::eBottomOfPipe, vk::AccessFlags2(), vk::ImageLayout::ePresentSrcKHR, vk::ImageAspectFlagBits::eColor);

    etna::finish_frame(a_cmdBuff);

    VK_CHECK_RESULT(vkEndCommandBuffer(a_cmdBuff));
}
