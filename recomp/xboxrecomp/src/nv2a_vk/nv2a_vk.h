/* nv2a_vk.h -- Vulkan renderer for the pushbuffer executor (see nv2a_vk.c). */
#ifndef NV2A_VK_H
#define NV2A_VK_H

/* Register the Vulkan back end with the executor (nv2a_backend.h). */
void nv2a_vk_install(void);

#ifdef VK_VERSION_1_0
/* Host overlay (PS5): called at every present with the swapchain image in
 * PRESENT_SRC_KHR layout, recording into cb; it must leave it in that
 * layout. gen changes whenever the swapchain is rebuilt. */
typedef struct {
    VkInstance inst;
    VkPhysicalDevice pd;
    VkDevice dev;
    VkQueue queue;
    uint32_t qfam;
    VkFormat format;
    VkExtent2D extent;
    uint32_t count, index, gen;
    const VkImage *images;
    const VkImageView *views;
    VkCommandBuffer cb;
} Nv2aVkOverlay;
#endif

/* Renderer state the host's menu reads and sets (all live). */
extern volatile int nv2a_vk_grid3d;     /* 1: 3D on the title's 640x480 grid */
/* Last frame: draws issued, executor time spent building it (ns). */
extern volatile unsigned nv2a_vk_frame_draws;

#endif
