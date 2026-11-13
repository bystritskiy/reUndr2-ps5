/* reUndr 2 PS5, MIT. The port menu (Touchpad + Options), the performance
 * overlay and the 60-second benchmark, drawn with Dear ImGui over every
 * presented image (nv2a_vk.c calls nv2a_host_overlay). While the menu is open
 * the renderer holds the title (nv2a_host_paused) and shows the last frame;
 * after a change the title draws a few frames to show it. */
#include <vulkan/vulkan.h>
#include <algorithm>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern "C" {
#include "nv2a_vk.h"
#include "pad_ps5.h"
#include "port_options.h"
void nfsu2_audio_mute(int on);
}

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

__asm__(".section .rodata\n"
        ".balign 16\n"
        ".global nfsu2_port_font\n"
        "nfsu2_port_font:\n"
        ".incbin \"" NFSU2_PORT_FONT "\"\n"
        ".global nfsu2_port_font_end\n"
        "nfsu2_port_font_end:\n"
        ".global nfsu2_whats_new\n"
        "nfsu2_whats_new:\n"
        ".incbin \"" NFSU2_WHATS_NEW "\"\n"
        ".byte 0\n"
        ".previous\n");
extern "C" const unsigned char nfsu2_port_font[], nfsu2_port_font_end[];
extern "C" const char nfsu2_whats_new[];

namespace {

/* libScePad buttons. */
constexpr uint32_t kUp = 0x10, kDown = 0x40, kLeft = 0x80, kRight = 0x20;
constexpr uint32_t kOptions = 0x08, kTouchpad = 0x100000, kCross = 0x4000, kCircle = 0x2000;
constexpr uint32_t kL1 = 0x400, kR1 = 0x800;

constexpr const char *kLogDir = "/data/nfsu2/PPSA99027/";
constexpr double kBenchSeconds = 60.0;
constexpr int kHistory = 240;

double now_s()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}

/* ── Rows ─────────────────────────────────────────────────────────── */

enum Tab { kGraphics, kGameplay, kAudio, kPerformance, kWhatsNew, kTabCount };
const char *const kTabNames[kTabCount] = { "Graphics", "Gameplay", "Audio", "Performance", "What's New" };

struct Row {
    const char *label;
    int option;                     /* NFSU2_OPT_*, or -1 for the benchmark */
    const char *const *values;      /* nullptr: the option gives its own text */
    Tab tab;
};

const char *const kScale[] = { "1x", "1.5x", "2x", "2.5x", "3x", "4.5x" };
const char *const kOffOn[] = { "Off", "On" };
const char *const kAa[] = { "Off", "FXAA" };
const char *const kAniso[] = { "Off", "2x", "4x", "8x", "16x" };
const char *const kLod[] = { "Original", "2x", "4x", "8x" };
const char *const kGrid[] = { "Native", "Original" };
const char *const kFps[] = { "60 fps", "30 fps" };
const char *const kBlur[] = { "Off", "Low", "Original" };
const char *const kVib[] = { "Off", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" };
const char *const kTest[] = { "Off", "Front Left", "Front Right", "Centre", "Subwoofer", "Surround Left",
                              "Surround Right", "Back Left", "Back Right", "All in Turn" };

const Row kRows[] = {
    { "Resolution Scale", NFSU2_OPT_SCALE, kScale, kGraphics },
    { "Anti-Aliasing", NFSU2_OPT_FXAA, kAa, kGraphics },
    { "Anisotropic Filtering", NFSU2_OPT_ANISO, kAniso, kGraphics },
    { "Square Pixels", NFSU2_OPT_SQUARE, kOffOn, kGraphics },
    { "Car Reflections", NFSU2_OPT_REFLECTIONS, kOffOn, kGraphics },
    { "Motion Blur", NFSU2_OPT_BLUR, kBlur, kGraphics },
    { "Detail Distance", NFSU2_OPT_LOD, kLod, kGraphics },
    { "Geometry Precision", NFSU2_OPT_GRID3D, kGrid, kGraphics },
    { "Vibration", NFSU2_OPT_VIBRATION, kVib, kGameplay },
    { "Text Language", NFSU2_OPT_TEXT_LANG, nullptr, kGameplay },
    { "Voice Language", NFSU2_OPT_VOICE_LANG, nullptr, kGameplay },
    { "Multichannel Sound", NFSU2_OPT_SPEAKERS, kOffOn, kAudio },
    { "Speaker Test", NFSU2_OPT_SPEAKER_TEST, kTest, kAudio },
    { "Frame Rate", NFSU2_OPT_FPS, kFps, kPerformance },
    { "Performance Overlay", NFSU2_OPT_OVERLAY, kOffOn, kPerformance },
    { "Pause While Open", NFSU2_OPT_PAUSE, kOffOn, kPerformance },
    { "Benchmark", -1, nullptr, kPerformance },
};
constexpr int kRowCount = (int)(sizeof kRows / sizeof kRows[0]);

bool row_shown(const Row &r) { return r.option < 0 || nfsu2_opt_shown(r.option); }

/* The rows of a tab this build shows, into out; their number. */
int tab_rows(int tab, int *out)
{
    int n = 0;
    for (int i = 0; i < kRowCount; i++)
        if (kRows[i].tab == tab && row_shown(kRows[i]))
            out[n++] = i;
    return n;
}

const char *value_text(const Row &r, int v, char *buf, unsigned cap)
{
    return r.values ? r.values[v] : nfsu2_opt_lang_text(r.option, v, buf, cap);
}

/* ── State (the executor thread only) ─────────────────────────────── */

volatile int s_open;
int s_tab;
float s_news_scroll, s_news_max;    /* What's New, in pixels */

/* While a value is being changed only its row stays, so the change shows
 * on the whole picture; moving to another row or tab brings the menu back. */
bool s_peek;
float s_peek_t;                     /* 0 menu .. 1 row only, eased */
ImVec2 s_cur_min, s_cur_max;        /* the selected row, as last drawn */
const char *s_cur_label;
char s_cur_value[96];

/* The hint at start (until the menu has been opened once): shown when the
 * title draws a real screen for a moment, i.e. the intro movies are over. */
double s_busy_since, s_hint_from, s_hint_until;
bool s_hint_done;
constexpr double kHintSeconds = 6.0;
int s_row;                          /* within the tab's shown rows */
uint32_t s_prev;                    /* last buttons, for edges */
double s_repeat_at;
bool s_changed;
constexpr int kStepFrames = 3;
int s_step;                         /* frames the title may still draw while open */

float s_frame_ms[kHistory];
int s_frame_next, s_frame_count;
double s_last_frame;

bool s_bench;                       /* recording */
double s_bench_start;
float *s_bench_ms;
int s_bench_n, s_bench_cap;
struct { bool shown; double until; float avg_fps, low_fps, worst_ms; int frames; } s_result;

/* ── Vulkan / ImGui ───────────────────────────────────────────────── */

bool s_ready, s_failed;
VkDevice s_dev;
VkRenderPass s_pass;
VkFramebuffer s_fb[8];
uint32_t s_fb_gen = ~0u, s_fb_count;
float s_ui = 1.0f;                  /* output height / 1080 */

void destroy_framebuffers()
{
    for (uint32_t i = 0; i < s_fb_count; i++)
        if (s_fb[i]) vkDestroyFramebuffer(s_dev, s_fb[i], nullptr);
    memset(s_fb, 0, sizeof s_fb);
    s_fb_count = 0;
}

bool make_framebuffers(const Nv2aVkOverlay *o)
{
    destroy_framebuffers();
    s_fb_count = std::min<uint32_t>(o->count, 8);
    for (uint32_t i = 0; i < s_fb_count; i++) {
        VkFramebufferCreateInfo fi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fi.renderPass = s_pass;
        fi.attachmentCount = 1;
        fi.pAttachments = &o->views[i];
        fi.width = o->extent.width;
        fi.height = o->extent.height;
        fi.layers = 1;
        if (vkCreateFramebuffer(s_dev, &fi, nullptr, &s_fb[i]) != VK_SUCCESS)
            return false;
    }
    s_fb_gen = o->gen;
    s_ui = o->extent.height / 1080.0f;
    return true;
}

void style()
{
    ImGuiStyle &st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 10.0f;
    st.WindowBorderSize = 0.0f;
    st.WindowPadding = ImVec2(28, 24);
    st.ItemSpacing = ImVec2(12, 6);
    st.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.90f);
    st.Colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.94f, 1.0f);
    st.Colors[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.57f, 0.62f, 1.0f);
    st.Colors[ImGuiCol_PlotLines] = ImVec4(0.55f, 0.75f, 1.0f, 1.0f);
    st.ScaleAllSizes(s_ui);
}

bool init(const Nv2aVkOverlay *o)
{
    s_dev = o->dev;
    VkAttachmentDescription at = {};
    at.format = o->format;
    at.samples = VK_SAMPLE_COUNT_1_BIT;
    at.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    at.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    at.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rp = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rp.attachmentCount = 1;
    rp.pAttachments = &at;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    if (vkCreateRenderPass(s_dev, &rp, nullptr, &s_pass) != VK_SUCCESS)
        return false;
    if (!make_framebuffers(o))
        return false;

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    style();
    ImFontConfig fc;
    fc.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void *)nfsu2_port_font, (int)(nfsu2_port_font_end - nfsu2_port_font),
                                   30.0f * s_ui, &fc);

    ImGui_ImplVulkan_InitInfo ii = {};
    ii.ApiVersion = VK_API_VERSION_1_1;
    ii.Instance = o->inst;
    ii.PhysicalDevice = o->pd;
    ii.Device = o->dev;
    ii.QueueFamily = o->qfam;
    ii.Queue = o->queue;
    ii.DescriptorPoolSize = 16;
    ii.MinImageCount = 2;
    ii.ImageCount = std::max<uint32_t>(o->count, 2);
    ii.PipelineInfoMain.RenderPass = s_pass;
    ii.PipelineInfoMain.Subpass = 0;
    if (!ImGui_ImplVulkan_Init(&ii))
        return false;
    fprintf(stderr, "[port] menu ready, %ux%u\n", o->extent.width, o->extent.height);
    return true;
}

/* ── Frame times and the benchmark ────────────────────────────────── */

void note_frame(double t)
{
    if (s_last_frame > 0.0) {
        float ms = (float)((t - s_last_frame) * 1000.0);
        s_frame_ms[s_frame_next] = std::min(ms, 250.0f);
        s_frame_next = (s_frame_next + 1) % kHistory;
        s_frame_count = std::min(s_frame_count + 1, kHistory);
        if (s_bench && s_bench_n < s_bench_cap)
            s_bench_ms[s_bench_n++] = ms;
    }
    s_last_frame = t;
}

void bench_start(double t)
{
    if (!s_bench_ms) {
        s_bench_cap = (int)(kBenchSeconds * 200);
        s_bench_ms = (float *)malloc(sizeof(float) * (size_t)s_bench_cap);
        if (!s_bench_ms) return;
    }
    s_bench = true;
    s_bench_n = 0;
    s_bench_start = t;
    s_result.shown = false;
    fprintf(stderr, "[bench] started, %.0f s\n", kBenchSeconds);
}

void bench_finish(double t)
{
    s_bench = false;
    int n = s_bench_n;
    if (n < 2)
        return;
    double total = 0.0;
    for (int i = 0; i < n; i++) total += s_bench_ms[i];
    std::sort(s_bench_ms, s_bench_ms + n);
    /* 1% low: the frame rate of the slowest 1% of frames, averaged. */
    int k = std::max(1, n / 100);
    double slow = 0.0;
    for (int i = n - k; i < n; i++) slow += s_bench_ms[i];
    s_result.frames = n;
    s_result.avg_fps = (float)(n * 1000.0 / total);
    s_result.low_fps = (float)(k * 1000.0 / slow);
    s_result.worst_ms = s_bench_ms[n - 1];
    s_result.shown = true;
    s_result.until = t + 20.0;

    char opts[192];
    snprintf(opts, sizeof opts, "scale %s, aa %s, aniso %s, square %s, reflections %s, blur %s, lod %s, geometry %s, %s",
             kScale[nfsu2_opt_get(NFSU2_OPT_SCALE)], kAa[nfsu2_opt_get(NFSU2_OPT_FXAA)],
             kAniso[nfsu2_opt_get(NFSU2_OPT_ANISO)], kOffOn[nfsu2_opt_get(NFSU2_OPT_SQUARE)],
             kOffOn[nfsu2_opt_get(NFSU2_OPT_REFLECTIONS)], kBlur[nfsu2_opt_get(NFSU2_OPT_BLUR)],
             kLod[nfsu2_opt_get(NFSU2_OPT_LOD)],
             kGrid[nfsu2_opt_get(NFSU2_OPT_GRID3D)], kFps[nfsu2_opt_get(NFSU2_OPT_FPS)]);
    char line[320];
    snprintf(line, sizeof line, "%.1f fps average, %.1f fps 1%% low, worst %.1f ms, %d frames in %.0f s (%s)",
             s_result.avg_fps, s_result.low_fps, s_result.worst_ms, n, total / 1000.0, opts);
    fprintf(stderr, "[bench] %s\n", line);
    char path[96];
    snprintf(path, sizeof path, "%sbenchmark.txt", kLogDir);
    if (FILE *f = fopen(path, "a")) {
        time_t wall = time(nullptr);
        char when[32];
        strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&wall));
        fprintf(f, "%s  %s\n", when, line);
        fclose(f);
    }
}

/* ── Input ────────────────────────────────────────────────────────── */

bool holding() { return nfsu2_opt_get(NFSU2_OPT_PAUSE) != 0; }

void set_open(bool open)
{
    bool held = s_open && holding();
    s_open = open;
    s_peek = false;
    s_peek_t = 0.0f;
    if (open) {
        s_hint_until = 0.0;
        if (!nfsu2_port_menu_seen) {
            nfsu2_port_menu_seen = 1;
            s_changed = true;
        }
    }
    nfsu2_ps5_pad_hold(open);
    nfsu2_audio_mute(open && holding());
    if (!open) {
        if (nfsu2_opt_get(NFSU2_OPT_SPEAKER_TEST))
            nfsu2_opt_set(NFSU2_OPT_SPEAKER_TEST, 0);
        if (s_changed) nfsu2_opt_save();
        s_changed = false;
        if (held) s_last_frame = 0.0;   /* the held time is not a frame */
    }
}

void change(const Row &r, int dir, double t)
{
    if (r.option < 0) {
        if (dir == 0) {             /* Cross on Benchmark */
            set_open(false);
            bench_start(t);
        }
        return;
    }
    int n = nfsu2_opt_steps(r.option);
    int v = nfsu2_opt_get(r.option);
    if (dir == 0) v = (v + 1) % n;
    else v = std::clamp(v + dir, 0, n - 1);
    if (v != nfsu2_opt_get(r.option)) {
        nfsu2_opt_set(r.option, v);
        s_changed = true;
        s_peek = r.option != NFSU2_OPT_PAUSE;
        /* The held frame was drawn with the old setting; a few new ones
         * show the real result (a lower scale cannot be undone in place). */
        if (r.option == NFSU2_OPT_PAUSE) {
            nfsu2_audio_mute(v);
            if (!v) s_last_frame = 0.0;
        } else if (r.tab == kGraphics) {
            s_step = kStepFrames;
        }
    }
}

void input(double t)
{
    uint32_t b = nfsu2_ps5_pad_buttons();
    uint32_t pressed = b & ~s_prev;
    bool combo = (b & (kOptions | kTouchpad)) == (kOptions | kTouchpad);
    bool was_combo = (s_prev & (kOptions | kTouchpad)) == (kOptions | kTouchpad);
    s_prev = b;
    if (combo && !was_combo) {
        set_open(!s_open);
        return;
    }
    if (!s_open)
        return;
    if (pressed & kCircle) {
        set_open(false);
        return;
    }
    /* Held D-pad repeats after 0.4 s, every 0.09 s. */
    uint32_t dirs = b & (kUp | kDown | kLeft | kRight);
    uint32_t act = pressed & dirs;
    if (!act && dirs && t >= s_repeat_at) {
        act = dirs;
        s_repeat_at = t + 0.09;
    } else if (act) {
        s_repeat_at = t + 0.4;
    }
    if ((act & (kUp | kDown)) || (pressed & (kL1 | kR1)))
        s_peek = false;
    if (pressed & (kL1 | kR1)) {
        s_tab = (s_tab + (pressed & kR1 ? 1 : kTabCount - 1)) % kTabCount;
        s_row = 0;
        s_news_scroll = 0.0f;
    }
    if (s_tab == kWhatsNew) {
        const float step = 90.0f * s_ui;
        if (act & kUp) s_news_scroll = std::max(0.0f, s_news_scroll - step);
        if (act & kDown) s_news_scroll = std::min(s_news_max, s_news_scroll + step);
        return;
    }
    int rows[kRowCount];
    int n = tab_rows(s_tab, rows);
    if (!n)
        return;
    s_row = std::min(s_row, n - 1);
    if (act & kUp) s_row = (s_row + n - 1) % n;
    if (act & kDown) s_row = (s_row + 1) % n;
    const Row &r = kRows[rows[s_row]];
    if (act & kLeft) change(r, -1, t);
    if (act & kRight) change(r, +1, t);
    if (pressed & kCross) change(r, 0, t);
}

/* ── Drawing ──────────────────────────────────────────────────────── */

/* A controller key, outlined, centred on a text line of height h at y. */
float key_width(const char *name)
{
    return ImGui::GetFont()->CalcTextSizeA(20.0f * s_ui, FLT_MAX, 0.0f, name).x + 16.0f * s_ui;
}

float draw_key(ImDrawList *dl, float x, float y, float h, const char *name)
{
    const float size = 20.0f * s_ui, pad = 8.0f * s_ui;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, name);
    float w = ts.x + pad * 2.0f, top = y + (h - ts.y - pad) * 0.5f;
    ImU32 col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    dl->AddRect(ImVec2(x, top), ImVec2(x + w, top + ts.y + pad), col, 5.0f * s_ui, 1.5f * s_ui);
    dl->AddText(ImGui::GetFont(), size, ImVec2(x + pad, top + pad * 0.5f), col, name);
    return w;
}

/* whats_new.txt: a version on a line of its own, then its "- " points. */
void draw_news(float height)
{
    ImGui::BeginChild("##news", ImVec2(0.0f, height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
    const char *p = nfsu2_whats_new;
    bool first = true;
    while (*p) {
        const char *end = strchr(p, '\n');
        if (!end) end = p + strlen(p);
        if (end > p && end[-1] == '\r') end--;
        if (end - p >= 2 && p[0] == '-' && p[1] == ' ') {
            ImGui::Bullet();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(p + 2, end);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        } else if (end > p) {
            if (!first) ImGui::Dummy(ImVec2(1.0f, 14.0f * s_ui));
            ImGui::TextUnformatted(p, end);
            first = false;
        }
        p = *end == '\r' ? end + 2 : *end ? end + 1 : end;
    }
    s_news_max = ImGui::GetScrollMaxY();
    s_news_scroll = std::min(s_news_scroll, s_news_max);
    ImGui::SetScrollY(s_news_scroll);
    ImGui::EndChild();
}

void hint_watch(double t)
{
    if (s_hint_done || nfsu2_port_menu_seen) {
        s_hint_done = true;
        return;
    }
    if (nv2a_vk_frame_draws < 40) {
        s_busy_since = 0.0;
        return;
    }
    if (s_busy_since == 0.0)
        s_busy_since = t;
    if (t - s_busy_since >= 1.5) {
        s_hint_done = true;
        s_hint_from = t;
        s_hint_until = t + kHintSeconds;
    }
}

void draw_hint(double t)
{
    float a = (float)std::min({ 1.0, (t - s_hint_from) / 0.3, (s_hint_until - t) / 0.5 });
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 24.0f * s_ui), ImGuiCond_Always,
                            ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(a, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * s_ui, 16.0f * s_ui));
    ImGui::Begin("##hint", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetTextLineHeight(), gap = 10.0f * s_ui;
    const char *label = "Port settings";
    ImVec2 p = ImGui::GetCursorScreenPos();
    float x = p.x;
    x += draw_key(dl, x, p.y, h, "Touchpad") + gap;
    dl->AddText(ImVec2(x, p.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), "+");
    x += ImGui::CalcTextSize("+").x + gap;
    x += draw_key(dl, x, p.y, h, "Options") + gap * 2.0f;
    dl->AddText(ImVec2(x, p.y), ImGui::GetColorU32(ImGuiCol_Text), label);
    x += ImGui::CalcTextSize(label).x;
    ImGui::Dummy(ImVec2(x - p.x, h));
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void draw_menu_window()
{
    ImGuiIO &io = ImGui::GetIO();
    const float w = 880.0f * s_ui;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("##port", nullptr, flags)) {
        ImGui::End();
        return;
    }
    ImGui::PushFont(nullptr, 38.0f * s_ui);
    ImGui::TextUnformatted("reUndr 2");
    ImGui::PopFont();
    ImGui::SameLine();
    const char *hint = "Touchpad + Options";
    ImGui::SetCursorPosX(w - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(hint).x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * s_ui);
    ImGui::TextDisabled("%s", hint);
    ImGui::Spacing();

    /* Tabs: the L1 / R1 keys at the ends, the tabs spread between them,
     * the open one underlined. */
    {
        ImDrawList *tl = ImGui::GetWindowDrawList();
        const float h = ImGui::GetTextLineHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        p.y += 8.0f * s_ui;
        const float right = p.x + ImGui::GetContentRegionAvail().x;
        float lw = draw_key(tl, p.x, p.y, h, "L1");
        float rw = key_width("R1");
        draw_key(tl, right - rw, p.y, h, "R1");
        const float x0 = p.x + lw + 16.0f * s_ui, x1 = right - rw - 16.0f * s_ui;
        tl->AddLine(ImVec2(p.x, p.y + h + 8.0f * s_ui), ImVec2(right, p.y + h + 8.0f * s_ui),
                    IM_COL32(255, 255, 255, 30), 1.0f * s_ui);
        float names = 0.0f;
        for (int k = 0; k < kTabCount; k++)
            names += ImGui::CalcTextSize(kTabNames[k]).x;
        const float gap = (x1 - x0 - names) / (float)kTabCount;
        float x = x0 + gap * 0.5f;
        for (int k = 0; k < kTabCount; k++) {
            ImVec2 ts = ImGui::CalcTextSize(kTabNames[k]);
            bool on = k == s_tab;
            tl->AddText(ImVec2(x, p.y), ImGui::GetColorU32(on ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                        kTabNames[k]);
            if (on)
                tl->AddRectFilled(ImVec2(x, p.y + h + 6.0f * s_ui), ImVec2(x + ts.x, p.y + h + 9.0f * s_ui),
                                  ImGui::GetColorU32(ImGuiCol_Text), 1.5f * s_ui);
            x += ts.x + gap;
        }
        ImGui::Dummy(ImVec2(right - p.x, h + 8.0f * s_ui + 12.0f * s_ui));
    }
    ImGui::Spacing();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float row_h = ImGui::GetTextLineHeight() + 18.0f * s_ui;
    int rows[kRowCount];
    int most = 0;
    for (int k = 0; k < kTabCount; k++)
        most = std::max(most, tab_rows(k, rows));
    const float area_h = most * (row_h + ImGui::GetStyle().ItemSpacing.y);
    if (s_tab == kWhatsNew) {
        /* Under the notes, where to write: issues and pull requests. */
        const float foot = ImGui::GetTextLineHeightWithSpacing() * 2.0f + 12.0f * s_ui;
        draw_news(area_h - foot);
        ImGui::Dummy(ImVec2(1.0f, 4.0f * s_ui));
        ImDrawList *fl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        fl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
                    IM_COL32(255, 255, 255, 30), 1.0f * s_ui);
        ImGui::Dummy(ImVec2(1.0f, 6.0f * s_ui));
        ImGui::TextDisabled("Problems or ideas:");
        ImGui::SameLine();
        ImGui::TextUnformatted("github.com/bystritskiy/reUndr2-ps5/issues");
        ImGui::TextDisabled("Pull requests are welcome.");
        ImGui::End();
        return;
    }
    int n = tab_rows(s_tab, rows);
    for (int i = 0; i < n; i++) {
        const Row &r = kRows[rows[i]];
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 q(p.x + ImGui::GetContentRegionAvail().x, p.y + row_h);
        if (i == s_row)
            dl->AddRectFilled(ImVec2(p.x - 10.0f * s_ui, p.y), ImVec2(q.x + 10.0f * s_ui, q.y),
                              IM_COL32(255, 255, 255, 34), 6.0f * s_ui);
        float ty = p.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(p.x, ty), ImGui::GetColorU32(ImGuiCol_Text), r.label);
        char value[96], text[80];
        if (r.option >= 0) {
            int v = nfsu2_opt_get(r.option);
            snprintf(value, sizeof value, i == s_row ? "<  %s  >" : "%s", value_text(r, v, text, sizeof text));
        } else {
            snprintf(value, sizeof value, "%.0f s", kBenchSeconds);
        }
        float vw = ImGui::CalcTextSize(value).x;
        if (i == s_row) {
            s_cur_min = ImVec2(p.x - 10.0f * s_ui, p.y);
            s_cur_max = ImVec2(q.x + 10.0f * s_ui, q.y);
            s_cur_label = r.label;
            snprintf(s_cur_value, sizeof s_cur_value, "%s", value);
        }
        dl->AddText(ImVec2(q.x - vw, ty),
                    ImGui::GetColorU32(i == s_row ? ImGuiCol_Text : ImGuiCol_TextDisabled), value);
        ImGui::Dummy(ImVec2(q.x - p.x, row_h));
    }
    /* Every tab as tall as the longest, so the window keeps its place. */
    if (most > n)
        ImGui::Dummy(ImVec2(1.0f, (most - n) * (row_h + ImGui::GetStyle().ItemSpacing.y)));
    ImGui::End();
}

void draw_menu()
{
    ImGuiIO &io = ImGui::GetIO();
    float target = s_peek ? 1.0f : 0.0f;
    s_peek_t += (target - s_peek_t) * std::min(1.0f, io.DeltaTime * 14.0f);
    if (fabsf(target - s_peek_t) < 0.01f)
        s_peek_t = target;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f - s_peek_t);
    draw_menu_window();
    ImGui::PopStyleVar();
    if (s_peek_t <= 0.0f || !s_cur_label)
        return;
    /* The selected row on its own, where it was. */
    ImDrawList *fg = ImGui::GetForegroundDrawList();
    const float a = s_peek_t, pad = 14.0f * s_ui;
    ImVec2 lo(s_cur_min.x - pad, s_cur_min.y - pad * 0.5f), hi(s_cur_max.x + pad, s_cur_max.y + pad * 0.5f);
    fg->AddRectFilled(lo, hi, IM_COL32(15, 15, 20, (int)(230 * a)), 10.0f * s_ui);
    fg->AddRectFilled(s_cur_min, s_cur_max, IM_COL32(255, 255, 255, (int)(34 * a)), 6.0f * s_ui);
    float h = ImGui::GetTextLineHeight();
    float ty = s_cur_min.y + (s_cur_max.y - s_cur_min.y - h) * 0.5f;
    ImU32 text = IM_COL32(235, 235, 240, (int)(255 * a));
    fg->AddText(ImVec2(s_cur_min.x + 10.0f * s_ui, ty), text, s_cur_label);
    float vw = ImGui::CalcTextSize(s_cur_value).x;
    fg->AddText(ImVec2(s_cur_max.x - 10.0f * s_ui - vw, ty), text, s_cur_value);
}

void corner_window(const char *id, bool right)
{
    ImGuiIO &io = ImGui::GetIO();
    float m = 40.0f * s_ui;
    ImGui::SetNextWindowPos(ImVec2(right ? io.DisplaySize.x - m : m, m), ImGuiCond_Always,
                            ImVec2(right ? 1.0f : 0.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.62f);
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
}

void draw_overlay()
{
    float sum = 0.0f, worst = 0.0f;
    int n = 0;
    for (int i = 1; i <= s_frame_count && sum < 1000.0f; i++) {
        float ms = s_frame_ms[(s_frame_next - i + kHistory) % kHistory];
        sum += ms;
        worst = std::max(worst, ms);
        n++;
    }
    float avg = n ? sum / n : 0.0f;
    corner_window("##perf", false);
    ImGui::PushFont(nullptr, 24.0f * s_ui);
    ImGui::Text("%.0f fps", avg > 0.0f ? 1000.0f / avg : 0.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("%.1f ms  worst %.1f", avg, worst);
    float ordered[kHistory];
    for (int i = 0; i < s_frame_count; i++)
        ordered[i] = s_frame_ms[(s_frame_next - s_frame_count + i + kHistory) % kHistory];
    ImGui::PlotLines("##frames", ordered, s_frame_count, 0, nullptr, 0.0f, 50.0f,
                     ImVec2(320.0f * s_ui, 54.0f * s_ui));
    ImGui::TextDisabled("%u draws per frame", nv2a_vk_frame_draws);
    ImGui::PopFont();
    ImGui::End();
}

void draw_bench(double t)
{
    corner_window("##bench", true);
    ImGui::PushFont(nullptr, 24.0f * s_ui);
    if (s_bench) {
        ImGui::Text("Benchmark  %.0f s", std::max(0.0, kBenchSeconds - (t - s_bench_start)));
    } else {
        ImGui::Text("%.1f fps", s_result.avg_fps);
        ImGui::TextDisabled("1%% low %.1f fps", s_result.low_fps);
        ImGui::TextDisabled("worst %.1f ms", s_result.worst_ms);
        ImGui::TextDisabled("%d frames", s_result.frames);
    }
    ImGui::PopFont();
    ImGui::End();
}

void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to)
{
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
}

} // namespace

extern "C" int nv2a_host_paused(void)
{
    if (!s_open || !holding())
        return 0;
    if (s_step > 0) {               /* let the title draw with the new setting */
        s_step--;
        return 0;
    }
    usleep(12000);                  /* the held frame needs no more than ~60 Hz */
    return s_open;
}

extern "C" void nv2a_host_overlay(const Nv2aVkOverlay *o)
{
    double t = now_s();
    if (!s_open || !holding())
        note_frame(t);
    input(t);
    hint_watch(t);
    bool hint = t < s_hint_until;
    if (s_bench && t - s_bench_start >= kBenchSeconds)
        bench_finish(t);
    if (s_result.shown && t > s_result.until)
        s_result.shown = false;
    bool overlay = nfsu2_opt_get(NFSU2_OPT_OVERLAY) != 0;
    if (!s_open && !overlay && !s_bench && !s_result.shown && !hint)
        return;
    if (s_failed)
        return;
    if (!s_ready) {
        s_ready = init(o);
        if (!s_ready) {
            s_failed = true;
            fprintf(stderr, "[port] menu could not start\n");
            return;
        }
    }
    if (o->gen != s_fb_gen && !make_framebuffers(o))
        return;
    if (o->index >= s_fb_count)
        return;

    static double last;
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)o->extent.width, (float)o->extent.height);
    io.DeltaTime = last > 0.0 && t > last ? (float)(t - last) : 1.0f / 60.0f;
    last = t;
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (overlay) draw_overlay();
    if (s_bench || s_result.shown) draw_bench(t);
    if (hint && !s_open) draw_hint(t);
    if (s_open) draw_menu();
    ImGui::Render();

    VkImage img = o->images[o->index];
    barrier(o->cb, img, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderPassBeginInfo rb = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rb.renderPass = s_pass;
    rb.framebuffer = s_fb[o->index];
    rb.renderArea.extent = o->extent;
    vkCmdBeginRenderPass(o->cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), o->cb);
    vkCmdEndRenderPass(o->cb);
    barrier(o->cb, img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
