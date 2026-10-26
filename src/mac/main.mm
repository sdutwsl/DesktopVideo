// Muralop for macOS: libmpv draws into borderless windows at the desktop level
// (below the Finder's icons); a menu bar item controls it.

#include "DesktopSurface.h"
#include "core/Config.h"
#include "core/Menu.h"
#include "core/Wallpaper.h"

#import <Cocoa/Cocoa.h>
#import <ServiceManagement/ServiceManagement.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <unistd.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace muralop;

namespace {

NSString* ns(const std::string& text)
{
    return [NSString stringWithUTF8String:text.c_str()] ?: @"";
}

std::string configPath()
{
    NSURL* dir = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory
                                                      inDomain:NSUserDomainMask
                                             appropriateForURL:nil
                                                        create:NO
                                                         error:nil];
    if (!dir)
        return "config.ini";
    return [dir URLByAppendingPathComponent:@"Muralop/config.ini"].fileSystemRepresentation;
}

std::string legacyConfigPath()
{
    NSURL* dir = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory
                                                      inDomain:NSUserDomainMask
                                             appropriateForURL:nil
                                                        create:NO
                                                         error:nil];
    return dir ? [dir URLByAppendingPathComponent:@"VideoWallpaper/config.ini"].fileSystemRepresentation : "";
}

NSArray<UTType*>* videoTypes()
{
    // Matroska and WebM only count as movies if some installed app says so.
    NSMutableArray<UTType*>* types = [NSMutableArray arrayWithObject:UTTypeMovie];
    for (NSString* ext in @[@"mp4", @"mkv", @"webm", @"avi", @"mov", @"wmv", @"flv", @"m4v", @"ts"])
        if (UTType* type = [UTType typeWithFilenameExtension:ext])
            [types addObject:type];
    return types;
}

} // namespace

// Target of one menu item; the item keeps it alive through representedObject.
@interface MuralopAction : NSObject
- (instancetype)initWithHandler:(std::function<void()>)handler;
- (void)run:(id)sender;
@end

@implementation MuralopAction {
    std::function<void()> _handler;
}

- (instancetype)initWithHandler:(std::function<void()>)handler
{
    if ((self = [super init]))
        _handler = std::move(handler);
    return self;
}

- (void)run:(id)sender
{
    // A copy: the menu can be rebuilt, releasing this object, while the handler is
    // still inside a modal open panel.
    std::function<void()> handler = _handler;
    if (handler)
        handler();
}

@end

@interface MuralopAppDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate>
@end

@implementation MuralopAppDelegate {
    std::unique_ptr<Config> _config;
    std::unique_ptr<MacPlatform> _platform;
    std::unique_ptr<Wallpaper> _wallpaper;
    MenuActions _actions;
    NSStatusItem* _statusItem;
    bool _choosing;
    // Pause reasons, combined into the wallpaper's locked / suspended states.
    bool _screenLocked, _sessionInactive, _systemAsleep, _displaysAsleep;
}

- (void)applicationWillFinishLaunching:(NSNotification*)notification
{
    // Before application:openURLs:, which arrives first when the app is started by
    // opening a video with it.
    _config = std::make_unique<Config>(configPath(), legacyConfigPath());
    _config->load();
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    _platform = std::make_unique<MacPlatform>();
    _wallpaper = std::make_unique<Wallpaper>(*_platform, *_config);

    MuralopAppDelegate* me = self;
    _actions.quit = [] { [NSApp terminate:nil]; };
    _actions.chooseVideo = [me](const std::string& monitor) { [me chooseVideo:monitor]; };

    _statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    _statusItem.button.toolTip = @"Muralop";
    NSMenu* menu = [[NSMenu alloc] init];
    menu.autoenablesItems = NO;
    menu.delegate = self;   // rebuilt from the wallpaper's state each time it opens
    _statusItem.menu = menu;
    [self updateStatusIcon];

    [self installMainMenu];
    [self observeSystem];

    _wallpaper->refreshMonitors();
    if (_wallpaper->videoFor("").empty())
        dispatch_async(dispatch_get_main_queue(), ^{
            [me chooseVideo:""];
        });
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    _wallpaper.reset();   // stops the players, closes the windows
    _platform.reset();
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app
{
    return YES;
}

// Opened again from the Finder or Launchpad while running: show the menu.
- (BOOL)applicationShouldHandleReopen:(NSApplication*)app hasVisibleWindows:(BOOL)visible
{
    [_statusItem.button performClick:nil];
    return NO;
}

// A video dropped on the app icon or opened with "Open With" becomes the default.
- (void)application:(NSApplication*)app openURLs:(NSArray<NSURL*>*)urls
{
    NSURL* url = urls.firstObject;
    if (!url.isFileURL)
        return;
    const std::string path = url.fileSystemRepresentation;
    if (_wallpaper) {
        _wallpaper->setVideo(path);
    } else {
        _config->set("video", path);
        _config->save();
    }
}

// ---- menu bar item ---------------------------------------------------------------

- (void)updateStatusIcon
{
    const bool paused = _wallpaper && _wallpaper->userPaused();
    _statusItem.button.image = [NSImage imageWithSystemSymbolName:paused ? @"pause.rectangle" : @"play.rectangle"
                                         accessibilityDescription:@"Muralop"];
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    std::vector<MenuItem> items = buildAppMenu(*_wallpaper, _actions);
    if (NSBundle.mainBundle.bundleIdentifier) {
        // Login items need the app bundle; goes just above "退出".
        MenuItem login;
        login.label = "登录时启动";
        login.checkable = true;
        login.checked = SMAppService.mainAppService.status == SMAppServiceStatusEnabled;
        MuralopAppDelegate* me = self;
        login.activate = [me] { [me toggleLoginItem]; };
        items.insert(items.end() - 1, login);
    }
    [menu removeAllItems];
    [self fill:menu with:items];
}

- (void)fill:(NSMenu*)menu with:(const std::vector<MenuItem>&)items
{
    for (const MenuItem& item : items) {
        if (item.separator) {
            [menu addItem:NSMenuItem.separatorItem];
            continue;
        }
        NSMenuItem* entry = [[NSMenuItem alloc] initWithTitle:ns(item.label) action:nil keyEquivalent:@""];
        entry.enabled = item.enabled;
        if (item.checked)
            entry.state = NSControlStateValueOn;
        if (!item.children.empty()) {
            NSMenu* submenu = [[NSMenu alloc] initWithTitle:entry.title];
            submenu.autoenablesItems = NO;
            [self fill:submenu with:item.children];
            entry.submenu = submenu;
        } else if (item.activate) {
            MuralopAppDelegate* me = self;
            std::function<void()> activate = item.activate;
            MuralopAction* action = [[MuralopAction alloc] initWithHandler:[me, activate] {
                activate();
                [me updateStatusIcon];
            }];
            entry.target = action;
            entry.action = @selector(run:);
            entry.representedObject = action;   // target is a weak reference
        }
        [menu addItem:entry];
    }
}

- (void)chooseVideo:(const std::string&)monitor
{
    if (_choosing)
        return;
    _choosing = true;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedContentTypes = videoTypes();
    panel.message = ns(monitor.empty() ? "选择壁纸视频" : "选择 " + monitor + " 的壁纸视频");
    const std::string target = monitor;   // the menu that owns `monitor` may be rebuilt meanwhile
    [NSApp activateIgnoringOtherApps:YES];   // a menu bar app's panel would open behind other windows
    if ([panel runModal] == NSModalResponseOK && panel.URL)
        _wallpaper->setVideo(panel.URL.fileSystemRepresentation, target);
    _choosing = false;
}

- (void)toggleLoginItem
{
    SMAppService* service = SMAppService.mainAppService;
    NSError* error = nil;
    const bool enable = service.status != SMAppServiceStatusEnabled;
    const BOOL ok = enable ? [service registerAndReturnError:&error] : [service unregisterAndReturnError:&error];
    if (!ok)
        fprintf(stderr, "[login item] %s\n", error ? error.localizedDescription.UTF8String : "failed");
    if (service.status == SMAppServiceStatusRequiresApproval)
        [SMAppService openSystemSettingsLoginItems];
}

// Never visible (menu bar apps have no menu bar), but its key equivalents make
// ⌘X / ⌘C / ⌘V / ⌘A work in the open panel's text fields.
- (void)installMainMenu
{
    NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
    [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];

    NSMenu* main = [[NSMenu alloc] init];
    [main addItemWithTitle:@"" action:nil keyEquivalent:@""].submenu = [[NSMenu alloc] init];   // the app menu
    [main addItemWithTitle:@"Edit" action:nil keyEquivalent:@""].submenu = edit;
    NSApp.mainMenu = main;
}

// ---- system events -----------------------------------------------------------------

- (void)observeSystem
{
    MuralopAppDelegate* me = self;
    auto on = [](NSNotificationCenter* center, NSNotificationName name, void (^handler)(void)) {
        [center addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
            handler();
        }];
    };
    NSNotificationCenter* workspace = NSWorkspace.sharedWorkspace.notificationCenter;
    NSNotificationCenter* distributed = NSDistributedNotificationCenter.defaultCenter;

    on(distributed, @"com.apple.screenIsLocked", ^{ me->_screenLocked = true; [me applyPauseReasons]; });
    on(distributed, @"com.apple.screenIsUnlocked", ^{ me->_screenLocked = false; [me applyPauseReasons]; });
    // Fast user switching.
    on(workspace, NSWorkspaceSessionDidResignActiveNotification, ^{ me->_sessionInactive = true; [me applyPauseReasons]; });
    on(workspace, NSWorkspaceSessionDidBecomeActiveNotification, ^{ me->_sessionInactive = false; [me applyPauseReasons]; });
    on(workspace, NSWorkspaceWillSleepNotification, ^{ me->_systemAsleep = true; [me applyPauseReasons]; });
    on(workspace, NSWorkspaceDidWakeNotification, ^{ me->_systemAsleep = false; [me applyPauseReasons]; });
    on(workspace, NSWorkspaceScreensDidSleepNotification, ^{ me->_displaysAsleep = true; [me applyPauseReasons]; });
    on(workspace, NSWorkspaceScreensDidWakeNotification, ^{ me->_displaysAsleep = false; [me applyPauseReasons]; });

    on(NSNotificationCenter.defaultCenter, NSApplicationDidChangeScreenParametersNotification, ^{
        // Debounce: several of these arrive while the layout settles.
        [NSObject cancelPreviousPerformRequestsWithTarget:me selector:@selector(refreshMonitors) object:nil];
        [me performSelector:@selector(refreshMonitors) withObject:nil afterDelay:1.0 inModes:@[NSRunLoopCommonModes]];
    });
}

- (void)applyPauseReasons
{
    _wallpaper->setLocked(_screenLocked || _sessionInactive);
    _wallpaper->setSuspended(_systemAsleep || _displaysAsleep);
}

- (void)refreshMonitors
{
    _wallpaper->refreshMonitors();
}

@end

int main()
{
    @autoreleasepool {
        // Finder and `open` never start a second copy; running the binary directly can.
        if (NSString* bundleId = NSBundle.mainBundle.bundleIdentifier) {
            for (NSRunningApplication* other in [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleId]) {
                if (other.processIdentifier != getpid()) {
                    fputs("Muralop is already running; look for it in the menu bar\n", stderr);
                    return 0;
                }
            }
        }
        NSApplication* app = NSApplication.sharedApplication;
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];   // also without the bundle's LSUIElement
        MuralopAppDelegate* delegate = [[MuralopAppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
