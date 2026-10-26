#include "DesktopSurface.h"
#include "core/MpvPlayer.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <dlfcn.h>
#include <mpv/render_gl.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>

// Draws the player's current frame whenever Core Animation asks for it. Not
// asynchronous: the surface calls setNeedsDisplay when mpv has a new frame, and the
// drawing then happens on the main thread, in the same OpenGL context the render
// context was created in.
@interface VWVideoLayer : CAOpenGLLayer
- (instancetype)initWithContext:(CGLContextObj)context pixelFormat:(CGLPixelFormatObj)pixelFormat label:(const std::string&)label;
@property (nonatomic) mpv_render_context* render;   // owned by the surface
@end

@implementation VWVideoLayer {
    CGLContextObj _context;
    CGLPixelFormatObj _pixelFormat;
    std::string _label;
    unsigned _framesDrawn;   // VW_DEBUG statistics
}

- (instancetype)initWithContext:(CGLContextObj)context pixelFormat:(CGLPixelFormatObj)pixelFormat label:(const std::string&)label
{
    if ((self = [super init])) {
        _context = CGLRetainContext(context);
        _pixelFormat = CGLRetainPixelFormat(pixelFormat);
        _label = label;
        self.asynchronous = NO;
        self.opaque = YES;
        self.needsDisplayOnBoundsChange = YES;
    }
    return self;
}

- (void)dealloc
{
    CGLReleasePixelFormat(_pixelFormat);
    CGLReleaseContext(_context);
}

- (CGLPixelFormatObj)copyCGLPixelFormatForDisplayMask:(uint32_t)mask
{
    return CGLRetainPixelFormat(_pixelFormat);
}

- (CGLContextObj)copyCGLContextForPixelFormat:(CGLPixelFormatObj)pixelFormat
{
    return CGLRetainContext(_context);
}

- (void)drawInCGLContext:(CGLContextObj)context
             pixelFormat:(CGLPixelFormatObj)pixelFormat
            forLayerTime:(CFTimeInterval)layerTime
             displayTime:(const CVTimeStamp*)displayTime
{
    // Core Animation has bound its own framebuffer and set the viewport to its pixel size.
    GLint fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);

    if (_render) {
        mpv_opengl_fbo target{int(fbo), viewport[2], viewport[3], 0};
        int flipY = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &target},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(_render, params);
        if (getenv("VW_DEBUG") && (++_framesDrawn == 1 || _framesDrawn % 600 == 0))
            fprintf(stderr, "[%s] frames drawn: %u (%dx%d)\n", _label.c_str(), _framesDrawn, viewport[2], viewport[3]);
    } else {
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    [super drawInCGLContext:context pixelFormat:pixelFormat forLayerTime:layerTime displayTime:displayTime];
}

@end

namespace vw {
namespace {

std::string utf8(NSString* text)
{
    const char* s = text.UTF8String;
    return s ? s : "";
}

// Screens with their monitor descriptions. The id is the display's name ("DELL U2720Q",
// "Built-in Retina Display"); two monitors of the same model get their serial number
// (or display id) appended so each keeps its own video.
std::vector<std::pair<NSScreen*, MonitorInfo>> enumerateScreens()
{
    NSArray<NSScreen*>* screens = NSScreen.screens;
    std::map<std::string, int> names;
    for (NSScreen* screen in screens)
        ++names[utf8(screen.localizedName)];

    std::vector<std::pair<NSScreen*, MonitorInfo>> out;
    for (NSScreen* screen in screens) {
        MonitorInfo m;
        m.id = utf8(screen.localizedName);
        if (names[m.id] > 1) {
            const CGDirectDisplayID display = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
            const uint32_t serial = CGDisplaySerialNumber(display);
            m.id += " #" + std::to_string(serial ? serial : display);
        }
        const NSRect frame = screen.frame;
        m.x = int(frame.origin.x);
        m.y = int(frame.origin.y);
        m.width = int(frame.size.width);
        m.height = int(frame.size.height);
        m.primary = screen == screens.firstObject;   // the one with the menu bar
        out.emplace_back(screen, m);
    }
    return out;
}

CGLPixelFormatObj choosePixelFormat()
{
    // A hardware renderer if there is one, Apple's software renderer otherwise.
    for (bool accelerated : {true, false}) {
        std::vector<CGLPixelFormatAttribute> attrs = {
            kCGLPFAOpenGLProfile, CGLPixelFormatAttribute(kCGLOGLPVersion_3_2_Core),
            kCGLPFADoubleBuffer,
            kCGLPFAAllowOfflineRenderers,   // do not force the discrete GPU on dual-GPU Macs
            kCGLPFASupportsAutomaticGraphicsSwitching,
        };
        if (accelerated)
            attrs.push_back(kCGLPFAAccelerated);
        attrs.push_back(CGLPixelFormatAttribute(0));
        CGLPixelFormatObj pixelFormat = nullptr;
        GLint count = 0;
        if (CGLChoosePixelFormat(attrs.data(), &pixelFormat, &count) == kCGLNoError && pixelFormat)
            return pixelFormat;
    }
    return nullptr;
}

void* getProcAddress(void*, const char* name)
{
    static void* gl = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
    return gl ? dlsym(gl, name) : nullptr;
}

class DesktopSurface;

// Hands mpv's wakeups (from its own threads) to the main thread, with at most one
// pending at a time. The queued blocks keep it alive; `owner` is only touched on the
// main thread and cleared when the surface goes away.
struct Waker : std::enable_shared_from_this<Waker> {
    std::atomic<bool> pending{false};
    DesktopSurface* owner = nullptr;
    void post();
};

class DesktopSurface : public Surface {
public:
    DesktopSurface(NSScreen* screen, const MonitorInfo& monitor);
    ~DesktopSurface() override;

    const MonitorInfo& monitor() const override { return monitor_; }
    void attach(MpvPlayer& player) override;
    void detach() override;

    void wake();

private:
    MonitorInfo monitor_;
    NSWindow* window_ = nil;
    VWVideoLayer* layer_ = nil;
    id backingObserver_ = nil;
    CGLContextObj context_ = nullptr;
    std::shared_ptr<Waker> waker_ = std::make_shared<Waker>();
    MpvPlayer* player_ = nullptr;
    mpv_render_context* render_ = nullptr;
};

void Waker::post()
{
    if (pending.exchange(true))
        return;
    std::shared_ptr<Waker> keep = shared_from_this();
    dispatch_async(dispatch_get_main_queue(), ^{
        keep->pending = false;
        if (keep->owner)
            keep->owner->wake();
    });
}

DesktopSurface::DesktopSurface(NSScreen* screen, const MonitorInfo& monitor) : monitor_(monitor)
{
    waker_->owner = this;

    CGLPixelFormatObj pixelFormat = choosePixelFormat();
    if (!pixelFormat || CGLCreateContext(pixelFormat, nullptr, &context_) != kCGLNoError) {
        fprintf(stderr, "[%s] cannot create an OpenGL context\n", monitor_.id.c_str());
        context_ = nullptr;
    }

    const NSRect frame = screen.frame;
    window_ = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskBorderless
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    window_.releasedWhenClosed = NO;
    window_.level = CGWindowLevelForKey(kCGDesktopWindowLevelKey);
    // On every Space, left alone by Mission Control and window cycling.
    window_.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
                                 NSWindowCollectionBehaviorIgnoresCycle | NSWindowCollectionBehaviorFullScreenNone;
    window_.ignoresMouseEvents = YES;   // clicks go through to the Finder's desktop
    window_.canHide = NO;               // "Hide Others" in another app must not take the wallpaper away
    window_.excludedFromWindowsMenu = YES;
    window_.hasShadow = NO;
    window_.opaque = YES;
    window_.backgroundColor = NSColor.blackColor;
    window_.animationBehavior = NSWindowAnimationBehaviorNone;

    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)];
    if (context_) {
        layer_ = [[VWVideoLayer alloc] initWithContext:context_ pixelFormat:pixelFormat label:monitor_.id];
        layer_.contentsScale = screen.backingScaleFactor;   // draw at the panel's pixel size
        view.layer = layer_;   // a layer-hosting view: the layer is set before wantsLayer
        view.wantsLayer = YES;
    }
    if (pixelFormat)
        CGLReleasePixelFormat(pixelFormat);
    window_.contentView = view;
    [window_ setFrame:frame display:NO];
    [window_ orderFront:nil];

    // The display switched between Retina and non-Retina modes without changing size.
    if (layer_) {
        VWVideoLayer* layer = layer_;
        NSWindow* __weak window = window_;
        backingObserver_ = [NSNotificationCenter.defaultCenter
            addObserverForName:NSWindowDidChangeBackingPropertiesNotification
                        object:window_
                         queue:nil
                    usingBlock:^(NSNotification*) {
                        layer.contentsScale = window.backingScaleFactor;
                        [layer setNeedsDisplay];
                    }];
    }
}

DesktopSurface::~DesktopSurface()
{
    detach();
    waker_->owner = nullptr;
    if (backingObserver_)
        [NSNotificationCenter.defaultCenter removeObserver:backingObserver_];
    [window_ orderOut:nil];
    [window_ close];
    if (context_)
        CGLReleaseContext(context_);
}

void DesktopSurface::attach(MpvPlayer& player)
{
    player_ = &player;
    player.setOption("vo", "libmpv");
    std::shared_ptr<Waker> waker = waker_;
    player.setWakeup([waker] { waker->post(); });
    if (!player.initialize() || !context_)
        return;

    // The render context has to exist before a file is loaded, or mpv disables video.
    CGLSetCurrentContext(context_);
    if (getenv("VW_DEBUG"))
        fprintf(stderr, "[%s] OpenGL renderer: %s\n", monitor_.id.c_str(),
                reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    mpv_opengl_init_params gl{};
    gl.get_proc_address = getProcAddress;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    const int err = mpv_render_context_create(&render_, player.handle(), params);
    if (err < 0) {
        fprintf(stderr, "[%s] mpv render context: %s\n", monitor_.id.c_str(), mpv_error_string(err));
        render_ = nullptr;
        return;
    }
    mpv_render_context_set_update_callback(render_, [](void* waker) { static_cast<Waker*>(waker)->post(); }, waker_.get());
    layer_.render = render_;
}

void DesktopSurface::detach()
{
    if (player_)
        player_->setWakeup(nullptr);
    if (render_) {
        layer_.render = nullptr;
        CGLSetCurrentContext(context_);
        mpv_render_context_free(render_);   // must happen before the player's core goes away
        render_ = nullptr;
    }
    player_ = nullptr;
}

void DesktopSurface::wake()
{
    if (player_)
        player_->processEvents();
    if (render_ && (mpv_render_context_update(render_) & MPV_RENDER_UPDATE_FRAME))
        [layer_ setNeedsDisplay];
}

} // namespace

std::vector<MonitorInfo> MacPlatform::monitors()
{
    std::vector<MonitorInfo> out;
    for (const auto& screen : enumerateScreens())
        out.push_back(screen.second);
    return out;
}

std::unique_ptr<Surface> MacPlatform::createSurface(const MonitorInfo& monitor)
{
    for (const auto& [screen, info] : enumerateScreens())
        if (info.id == monitor.id)
            return std::make_unique<DesktopSurface>(screen, monitor);
    return nullptr;
}

} // namespace vw
