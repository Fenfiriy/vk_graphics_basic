#include "shadowmap_render.h"

#include <geom/vk_mesh.h>
#include <vk_pipeline.h>
#include <vk_buffers.h>
#include <iostream>

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <vulkan/vulkan_core.h>


/// RESOURCE ALLOCATION

void SimpleShadowmapRender::AllocateResources()
{
  mainViewDepth = m_context->createImage(etna::Image::CreateInfo
  {
    .extent = vk::Extent3D{m_width, m_height, 1},
    .name = "main_view_depth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment
  });

  sqVarianceShadowMap = m_context->createImage(etna::Image::CreateInfo{
    .extent     = vk::Extent3D{ 2048, 2048, 1 },
    .name       = "temp_variance_shadow_map",
    .format     = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage });

  varianceShadowMap = m_context->createImage(etna::Image::CreateInfo{
    .extent     = vk::Extent3D{ 2048, 2048, 1 },
    .name       = "variance_shadow_map",
    .format     = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage });

  shadowMap = m_context->createImage(etna::Image::CreateInfo
  {
    .extent = vk::Extent3D{2048, 2048, 1},
    .name = "shadow_map",
    .format = vk::Format::eD16Unorm,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled
  });

  defaultSampler = etna::Sampler(etna::Sampler::CreateInfo{.name = "default_sampler"});
  constants = m_context->createBuffer(etna::Buffer::CreateInfo
  {
    .size = sizeof(UniformParams),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
    .name = "constants"
  });

  m_uboMappedMem = constants.map();
}

void SimpleShadowmapRender::LoadScene(const char* path, bool transpose_inst_matrices)
{
  m_pScnMgr->LoadSceneXML(path, transpose_inst_matrices);

  // TODO: Make a separate stage
  loadShaders();
  PreparePipelines();

  auto loadedCam = m_pScnMgr->GetCamera(0);
  m_cam.fov = loadedCam.fov;
  m_cam.pos = float3(loadedCam.pos);
  m_cam.up  = float3(loadedCam.up);
  m_cam.lookAt = float3(loadedCam.lookAt);
  m_cam.tdist  = loadedCam.farPlane;
}

void SimpleShadowmapRender::DeallocateResources()
{
  mainViewDepth.reset(); // TODO: Make an etna method to reset all the resources
  shadowMap.reset();
  varianceShadowMap.reset();
  sqVarianceShadowMap.reset();
  m_swapchain.Cleanup();
  vkDestroySurfaceKHR(GetVkInstance(), m_surface, nullptr);  

  constants = etna::Buffer();
}





/// PIPELINES CREATION

void SimpleShadowmapRender::PreparePipelines()
{
  // create full screen quad for debug purposes
  // 
  m_pQuad = std::make_unique<QuadRenderer>(QuadRenderer::CreateInfo{ 
      .format = static_cast<vk::Format>(m_swapchain.GetFormat()),
      .rect = { 0, 0, 512, 512 }, 
    });
  SetupSimplePipeline();
}

void SimpleShadowmapRender::loadShaders()
{
  etna::create_program("simple_material",
    {VK_GRAPHICS_BASIC_ROOT"/resources/shaders/simple_shadow.frag.spv", VK_GRAPHICS_BASIC_ROOT"/resources/shaders/simple.vert.spv"});
  etna::create_program("simple_shadow", { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple.vert.spv" });
  etna::create_program("vsm_frag", { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/vsm_frag.frag.spv", VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple.vert.spv" });
  etna::create_program("vsm_compute_blur", { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/vsm_compute_blur.comp.spv" });
}

void SimpleShadowmapRender::SetupSimplePipeline()
{
  etna::VertexShaderInputDescription sceneVertexInputDesc
    {
      .bindings = {etna::VertexShaderInputDescription::Binding
        {
          .byteStreamDescription = m_pScnMgr->GetVertexStreamDescription()
        }}
    };

  auto& pipelineManager = etna::get_context().getPipelineManager();
  m_basicForwardPipeline = pipelineManager.createGraphicsPipeline("simple_material",
    {
      .vertexShaderInput = sceneVertexInputDesc,
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {static_cast<vk::Format>(m_swapchain.GetFormat())},
          .depthAttachmentFormat = vk::Format::eD32Sfloat
        }
    });
  m_shadowPipeline = pipelineManager.createGraphicsPipeline("simple_shadow",
    {
      .vertexShaderInput = sceneVertexInputDesc,
      .fragmentShaderOutput =
        {
          .depthAttachmentFormat = vk::Format::eD16Unorm
        }
    });
  m_vsmGraphPipeline     = pipelineManager.createGraphicsPipeline("vsm_frag",
        { .vertexShaderInput    = sceneVertexInputDesc,
          .fragmentShaderOutput = {
            .colorAttachmentFormats = { vk::Format::eR32G32Sfloat },
            .depthAttachmentFormat  = vk::Format::eD16Unorm } });
  m_vsmCompPipeline      = pipelineManager.createComputePipeline("vsm_compute_blur", {});
}


/// COMMAND BUFFER FILLING

void SimpleShadowmapRender::DrawSceneCmd(VkCommandBuffer a_cmdBuff, const float4x4& a_wvp, VkPipelineLayout a_pipelineLayout)
{
  VkShaderStageFlags stageFlags = (VK_SHADER_STAGE_VERTEX_BIT);

  VkDeviceSize zero_offset = 0u;
  VkBuffer vertexBuf = m_pScnMgr->GetVertexBuffer();
  VkBuffer indexBuf  = m_pScnMgr->GetIndexBuffer();
  
  vkCmdBindVertexBuffers(a_cmdBuff, 0, 1, &vertexBuf, &zero_offset);
  vkCmdBindIndexBuffer(a_cmdBuff, indexBuf, 0, VK_INDEX_TYPE_UINT32);

  pushConst2M.projView = a_wvp;
  for (uint32_t i = 0; i < m_pScnMgr->InstancesNum(); ++i)
  {
    auto inst         = m_pScnMgr->GetInstanceInfo(i);
    pushConst2M.model = m_pScnMgr->GetInstanceMatrix(i);
    vkCmdPushConstants(a_cmdBuff, a_pipelineLayout,
      stageFlags, 0, sizeof(pushConst2M), &pushConst2M);

    auto mesh_info = m_pScnMgr->GetMeshInfo(inst.mesh_id);
    vkCmdDrawIndexed(a_cmdBuff, mesh_info.m_indNum, 1, mesh_info.m_indexOffset, mesh_info.m_vertexOffset, 0);
  }
}

void SimpleShadowmapRender::BuildCommandBufferSimple(VkCommandBuffer a_cmdBuff, VkImage a_targetImage, VkImageView a_targetImageView)
{
  vkResetCommandBuffer(a_cmdBuff, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

  VK_CHECK_RESULT(vkBeginCommandBuffer(a_cmdBuff, &beginInfo));

  //// draw scene to shadowmap
  //
  if (m_uniforms.vsmEnabled)
  {
    etna::set_state(a_cmdBuff, sqVarianceShadowMap.get(), vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);
    etna::RenderTargetState renderTargets(a_cmdBuff, { 0, 0, 2048, 2048 }, { { sqVarianceShadowMap.get(), sqVarianceShadowMap.getView({}) } }, shadowMap);
    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vsmGraphPipeline.getVkPipeline());
    DrawSceneCmd(a_cmdBuff, m_lightMatrix, m_shadowPipeline.getVkPipelineLayout());
  }
  else
  {
    etna::RenderTargetState renderTargets(a_cmdBuff, {0, 0, 2048, 2048}, {}, shadowMap);

    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline.getVkPipeline());
    DrawSceneCmd(a_cmdBuff, m_lightMatrix, m_shadowPipeline.getVkPipelineLayout());
  }

  if (m_uniforms.vsmEnabled)
  {
    etna::set_state(a_cmdBuff, sqVarianceShadowMap.get(), vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead, vk::ImageLayout::eGeneral, vk::ImageAspectFlagBits::eColor);
    etna::set_state(a_cmdBuff, varianceShadowMap.get(), vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite, vk::ImageLayout::eGeneral, vk::ImageAspectFlagBits::eColor);
    auto blur_info = etna::get_shader_program("vsm_compute_blur");

    auto set              = etna::create_descriptor_set(blur_info.getDescriptorLayoutId(0), a_cmdBuff, {
                                                                                            etna::Binding{ 0, sqVarianceShadowMap.genBinding(defaultSampler.get(), vk::ImageLayout::eGeneral) },
                                                                                            etna::Binding{ 1, varianceShadowMap.genBinding(defaultSampler.get(), vk::ImageLayout::eGeneral) },
                                                                                          });
    VkDescriptorSet vkSet = set.getVkSet();
    etna::flush_barriers(a_cmdBuff);

    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, m_vsmCompPipeline.getVkPipeline());
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, m_vsmCompPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, VK_NULL_HANDLE);
    vkCmdDispatch(a_cmdBuff, 2048 / 32 + 1, 2048 / 32 + 1, 1);

    etna::set_state(a_cmdBuff, varianceShadowMap.get(), vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);
  }

  //// draw final scene to screen
  //
  {
    auto simpleMaterialInfo = etna::get_shader_program("simple_material");

    auto set = etna::create_descriptor_set(simpleMaterialInfo.getDescriptorLayoutId(0), a_cmdBuff,
    {
      etna::Binding {0, constants.genBinding()},
      etna::Binding{ 1, shadowMap.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) },
      etna::Binding{ 2, varianceShadowMap.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) }
    });

    VkDescriptorSet vkSet = set.getVkSet();

    etna::RenderTargetState renderTargets(a_cmdBuff, {0, 0, m_width, m_height}, {{a_targetImage, a_targetImageView}}, mainViewDepth);

    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_basicForwardPipeline.getVkPipeline());
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS,
      m_basicForwardPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, VK_NULL_HANDLE);

    DrawSceneCmd(a_cmdBuff, m_worldViewProj, m_basicForwardPipeline.getVkPipelineLayout());
  }

  if(m_input.drawFSQuad)
    m_pQuad->RecordCommands(a_cmdBuff, a_targetImage, a_targetImageView, shadowMap, defaultSampler);

  etna::set_state(a_cmdBuff, a_targetImage, vk::PipelineStageFlagBits2::eBottomOfPipe,
    vk::AccessFlags2(), vk::ImageLayout::ePresentSrcKHR,
    vk::ImageAspectFlagBits::eColor);

  etna::finish_frame(a_cmdBuff);

  VK_CHECK_RESULT(vkEndCommandBuffer(a_cmdBuff));
}
