// SPDX-License-Identifier: GPL-2.0-or-later
// The buffer cache's overlapping arenas (video_core/buffer_cache): one allocation bound into two
// sparse buffers that overlap by an arena page, a reserve slot bound at a non-zero memory offset,
// and a chunk released and bound again with new memory, all read back through both buffers.
// Needs a GPU with sparse buffer residency; no game, no window.
// Build (MSYS2 CLANG64): clang -O1 tests/test_arena_overlap.c -lvulkan-1 -o out/arena-overlap-test
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        VkResult r_ = (x);                                                                         \
        if (r_ != VK_SUCCESS) {                                                                    \
            printf("FAIL %s:%d %s = %d\n", __FILE__, __LINE__, #x, r_);                            \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static VkDevice device;
static VkQueue queue;
static uint32_t family;
static VkPhysicalDeviceMemoryProperties memory_properties;
static VkCommandPool pool;
static int failures;

static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags wanted) {
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((bits >> i & 1) && (memory_properties.memoryTypes[i].propertyFlags & wanted) == wanted) {
            return i;
        }
    }
    printf("FAIL no memory type\n");
    exit(1);
}

static VkBuffer sparse_buffer(VkDeviceSize size) {
    const VkBufferCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .flags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT | VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    VkBuffer buffer;
    CHECK(vkCreateBuffer(device, &ci, NULL, &buffer));
    return buffer;
}

static void bind(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkDeviceMemory memory,
                 VkDeviceSize memory_offset) {
    const VkSparseMemoryBind b = {offset, size, memory, memory_offset, 0};
    const VkSparseBufferMemoryBindInfo bi = {buffer, 1, &b};
    const VkBindSparseInfo info = {.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO,
                                   .bufferBindCount = 1,
                                   .pBufferBinds = &bi};
    CHECK(vkQueueBindSparse(queue, 1, &info, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));
}

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint32_t* data;
} Host;

// Fills 4 KiB at `fill_offset` of `writer` with `value`, then copies 4 KiB at `read_offset` of
// `reader` to the host and checks every word.
static void write_read(const char* label, VkBuffer writer, VkDeviceSize fill_offset,
                       VkBuffer reader, VkDeviceSize read_offset, uint32_t value, Host* host) {
    const VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                            .commandPool = pool,
                                            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                            .commandBufferCount = 1};
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(device, &ai, &cmd));
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(cmd, &begin));
    vkCmdFillBuffer(cmd, writer, fill_offset, 4096, value);
    // A global barrier, as the renderer uses (Runtime::FlushBarriers).
    const VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                     .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1,
                         &barrier, 0, NULL, 0, NULL);
    const VkBufferCopy copy = {read_offset, 0, 4096};
    vkCmdCopyBuffer(cmd, reader, host->buffer, 1, &copy);
    CHECK(vkEndCommandBuffer(cmd));
    const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                 .commandBufferCount = 1,
                                 .pCommandBuffers = &cmd};
    memset(host->data, 0, 4096);
    CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    for (int i = 0; i < 1024; ++i) {
        if (host->data[i] != value) {
            printf("FAIL %s: word %d is %#x, expected %#x\n", label, i, host->data[i], value);
            ++failures;
            return;
        }
    }
    printf("ok   %s\n", label);
}

int main(void) {
    const VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                   .apiVersion = VK_API_VERSION_1_3};
    const VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                      .pApplicationInfo = &app};
    VkInstance instance;
    CHECK(vkCreateInstance(&ici, NULL, &instance));
    uint32_t count = 8;
    VkPhysicalDevice gpus[8];
    CHECK(vkEnumeratePhysicalDevices(instance, &count, gpus));
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceVulkan13Properties props13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES};
    VkPhysicalDeviceProperties2 props2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                          .pNext = &props13};
    for (uint32_t i = 0; i < count && !gpu; ++i) {
        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(gpus[i], &features);
        if (features.sparseBinding && features.sparseResidencyBuffer) {
            gpu = gpus[i];
        }
    }
    if (!gpu) {
        printf("SKIP no GPU with sparse buffer residency\n");
        return 0;
    }
    vkGetPhysicalDeviceProperties2(gpu, &props2);
    props = props2.properties;
    vkGetPhysicalDeviceMemoryProperties(gpu, &memory_properties);
    VkQueueFamilyProperties families[16];
    count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families);
    family = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        const VkQueueFlags want = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_SPARSE_BINDING_BIT;
        if ((families[i].queueFlags & want) == want) {
            family = i;
            break;
        }
    }
    if (family == UINT32_MAX) {
        printf("SKIP no graphics queue with sparse binding\n");
        return 0;
    }
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                         .queueFamilyIndex = family,
                                         .queueCount = 1,
                                         .pQueuePriorities = &priority};
    const VkPhysicalDeviceFeatures enabled = {.sparseBinding = VK_TRUE,
                                              .sparseResidencyBuffer = VK_TRUE};
    const VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                    .queueCreateInfoCount = 1,
                                    .pQueueCreateInfos = &qci,
                                    .pEnabledFeatures = &enabled};
    CHECK(vkCreateDevice(gpu, &dci, NULL, &device));
    vkGetDeviceQueue(device, family, 0, &queue);
    const VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                         .queueFamilyIndex = family};
    CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));

    // Arena pages as the buffer cache sizes them: 4 GiB unless the driver's buffer size limit is
    // lower, so that an arena of two pages fits (AMD on Windows: 2 GiB limit, 1 GiB pages).
    uint64_t page = 1ull << 32;
    while (page > (1ull << 30) && 2 * page > props13.maxBufferSize) {
        page >>= 1;
    }
    printf("GPU %s, buffer limit %llu MiB, arena page %llu MiB\n", props.deviceName,
           (unsigned long long)(props13.maxBufferSize >> 20), (unsigned long long)(page >> 20));
    // Arena of page 0 spans pages 0-1, arena of page 1 spans pages 1-2.
    VkBuffer first = sparse_buffer(2 * page), second = sparse_buffer(2 * page);
    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(device, first, &reqs);
    const uint32_t vram = memory_type(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    const VkDeviceSize chunk = reqs.alignment > (2u << 20) ? reqs.alignment : (2u << 20);

    Host host;
    const VkBufferCreateInfo hci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                    .size = 4096,
                                    .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    CHECK(vkCreateBuffer(device, &hci, NULL, &host.buffer));
    VkMemoryRequirements hreqs;
    vkGetBufferMemoryRequirements(device, host.buffer, &hreqs);
    const VkMemoryAllocateInfo hai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = hreqs.size,
        .memoryTypeIndex = memory_type(hreqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    CHECK(vkAllocateMemory(device, &hai, NULL, &host.memory));
    CHECK(vkBindBufferMemory(device, host.buffer, host.memory, 0));
    CHECK(vkMapMemory(device, host.memory, 0, 4096, 0, (void**)&host.data));

    // A chunk of its own at the start of page 1 (+ 4 MiB), bound in both arenas.
    const VkDeviceSize at = 4u << 20;
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                               .allocationSize = chunk,
                               .memoryTypeIndex = vram};
    VkDeviceMemory own;
    CHECK(vkAllocateMemory(device, &ai, NULL, &own));
    bind(first, page + at, chunk, own, 0);
    bind(second, at, chunk, own, 0);
    write_read("chunk written through the first arena, read through the second", first,
               page + at, second, at, 0x11111111u, &host);
    write_read("chunk written through the second arena, read through the first", second, at + 8192,
               first, page + at + 8192, 0x22222222u, &host);

    // A reserve slot: a chunk at a non-zero offset of one larger allocation.
    VkDeviceMemory reserve;
    ai.allocationSize = 4 * chunk;
    CHECK(vkAllocateMemory(device, &ai, NULL, &reserve));
    bind(first, page + at + chunk, chunk, reserve, 2 * chunk);
    bind(second, at + chunk, chunk, reserve, 2 * chunk);
    write_read("reserve slot, first arena to second", first, page + at + chunk, second, at + chunk,
               0x33333333u, &host);

    // Released from both arenas (the collector), then made resident again with other memory.
    bind(first, page + at, chunk, VK_NULL_HANDLE, 0);
    bind(second, at, chunk, VK_NULL_HANDLE, 0);
    bind(first, page + at, chunk, reserve, 0);
    bind(second, at, chunk, reserve, 0);
    write_read("chunk bound again after a release, second arena to first", second, at, first,
               page + at, 0x44444444u, &host);
    write_read("other reserve slot untouched by the rebind", second, at + chunk + 4096, second,
               at + chunk, 0x33333333u, &host);

    // Page 0 is in the arena of page 0 only (no earlier arena).
    bind(first, 0, chunk, own, 0);
    write_read("page 0 in the first arena", first, 0, first, 0, 0x55555555u, &host);

    vkDeviceWaitIdle(device);
    printf(failures ? "FAILED (%d)\n" : "all passed\n", failures);
    return failures != 0;
}
