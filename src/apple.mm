// copied from https://github.com/qimiko/click-on-steps/blob/main/src/macos.mm, with permission

#include <Geode/platform/cplatform.h>
#define CommentType CommentTypeDummy
#ifdef GEODE_IS_IOS
#import <UIKit/UIKit.h>
#else
#import <Cocoa/Cocoa.h>
#endif
#include <objc/runtime.h>
#undef CommentType

#include <Geode/Geode.hpp>

#include <cstdint>
#include <time.h>

#include "includes.hpp"

TimestampType getCurrentTimestamp() {
	// convert ns to μs
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1'000;
}

@interface EAGLView : GEODE_MACOS(NSOpenGLView) GEODE_IOS(UIView)
@end

struct TimestampSetter {
    TimestampSetter(TimestampType t) {
        pendingInputTimestamp = t;
    }
    ~TimestampSetter() {
        pendingInputTimestamp = 0;
    }
};

#define SET_TIMESTAMP(t) TimestampSetter setter_(t)

#ifdef GEODE_IS_MACOS
static IMP keyDownExecOIMP;
void keyDownExec(EAGLView* self, SEL sel, NSEvent* event) {
	auto timestamp = static_cast<std::uint64_t>([event timestamp] * 1000000.0);
	SET_TIMESTAMP(timestamp);

	reinterpret_cast<decltype(&keyDownExec)>(keyDownExecOIMP)(self, sel, event);
}

static IMP keyUpExecOIMP;
void keyUpExec(EAGLView* self, SEL sel, NSEvent* event) {
	auto timestamp = static_cast<std::uint64_t>([event timestamp] * 1000000.0);
	SET_TIMESTAMP(timestamp);

	reinterpret_cast<decltype(&keyUpExec)>(keyUpExecOIMP)(self, sel, event);
}

static IMP mouseDownExecOIMP;
void mouseDownExec(EAGLView* self, SEL sel, NSEvent* event) {
	auto timestamp = static_cast<std::uint64_t>([event timestamp] * 1000000.0);
	SET_TIMESTAMP(timestamp);

	reinterpret_cast<decltype(&mouseDownExec)>(mouseDownExecOIMP)(self, sel, event);
}

static IMP mouseDraggedExecOIMP;
void mouseDraggedExec(EAGLView* self, SEL sel, NSEvent* event) {
	auto timestamp = static_cast<std::uint64_t>([event timestamp] * 1000000.0);
	SET_TIMESTAMP(timestamp);

	reinterpret_cast<decltype(&mouseDraggedExec)>(mouseDraggedExecOIMP)(self, sel, event);
}

static IMP mouseUpExecOIMP;
void mouseUpExec(EAGLView* self, SEL sel, NSEvent* event) {
	auto timestamp = static_cast<std::uint64_t>([event timestamp] * 1000000.0);
	SET_TIMESTAMP(timestamp);

	reinterpret_cast<decltype(&mouseUpExec)>(mouseUpExecOIMP)(self, sel, event);
}
#endif

#ifdef GEODE_IS_IOS
static IMP touchesBeganOIMP;
void touchesBegan(EAGLView* self, SEL sel, NSSet* touches, UIEvent* event) {
	auto timestamp = static_cast<std::uint64_t>([event timestamp] * 1000000.0);
	SET_TIMESTAMP(timestamp);

	reinterpret_cast<decltype(&touchesBegan)>(touchesBeganOIMP)(self, sel, touches, event);
}

static IMP touchesMovedOIMP;
void touchesMoved(EAGLView* self, SEL sel, NSSet* touches, UIEvent* event) {
	auto timestamp = static_cast<std::uint64_t>([event timestamp] * 1000000.0);
	SET_TIMESTAMP(timestamp);

	reinterpret_cast<decltype(&touchesMoved)>(touchesMovedOIMP)(self, sel, touches, event);
}

static IMP touchesEndedOIMP;
void touchesEnded(EAGLView* self, SEL sel, NSSet* touches, UIEvent* event) {
	auto timestamp = static_cast<std::uint64_t>([event timestamp] * 1000000.0);
	SET_TIMESTAMP(timestamp);

	reinterpret_cast<decltype(&touchesEnded)>(touchesEndedOIMP)(self, sel, touches, event);
}

static IMP touchesCancelledOIMP;
void touchesCancelled(EAGLView* self, SEL sel, NSSet* touches, UIEvent* event) {
	auto timestamp = static_cast<std::uint64_t>([event timestamp] * 1000000.0);
	SET_TIMESTAMP(timestamp);

	reinterpret_cast<decltype(&touchesCancelled)>(touchesCancelledOIMP)(self, sel, touches, event);
}
#endif

$execute {
	auto eaglView = objc_getClass("EAGLView");

#ifdef GEODE_IS_MACOS
	auto keyDownExecMethod = class_getInstanceMethod(eaglView, @selector(keyDownExec:));
	keyDownExecOIMP = method_getImplementation(keyDownExecMethod);
	method_setImplementation(keyDownExecMethod, (IMP)&keyDownExec);

	auto keyUpExecMethod = class_getInstanceMethod(eaglView, @selector(keyUpExec:));
	keyUpExecOIMP = method_getImplementation(keyUpExecMethod);
	method_setImplementation(keyUpExecMethod, (IMP)&keyUpExec);

	auto mouseDownExecMethod = class_getInstanceMethod(eaglView, @selector(mouseDownExec:));
	mouseDownExecOIMP = method_getImplementation(mouseDownExecMethod);
	method_setImplementation(mouseDownExecMethod, (IMP)&mouseDownExec);

	auto mouseDraggedExecMethod = class_getInstanceMethod(eaglView, @selector(mouseDraggedExec:));
	mouseDraggedExecOIMP = method_getImplementation(mouseDraggedExecMethod);
	method_setImplementation(mouseDraggedExecMethod, (IMP)&mouseDraggedExec);

	auto mouseUpExecMethod = class_getInstanceMethod(eaglView, @selector(mouseUpExec:));
	mouseUpExecOIMP = method_getImplementation(mouseUpExecMethod);
	method_setImplementation(mouseUpExecMethod, (IMP)&mouseUpExec);
#endif

#ifdef GEODE_IS_IOS
	auto touchesBeganMethod = class_getInstanceMethod(eaglView, @selector(touchesBegan:withEvent:));
	touchesBeganOIMP = method_getImplementation(touchesBeganMethod);
	method_setImplementation(touchesBeganMethod, (IMP)&touchesBegan);

	auto touchesMovedMethod = class_getInstanceMethod(eaglView, @selector(touchesMoved:withEvent:));
	touchesMovedOIMP = method_getImplementation(touchesMovedMethod);
	method_setImplementation(touchesMovedMethod, (IMP)&touchesMoved);

	auto touchesEndedMethod = class_getInstanceMethod(eaglView, @selector(touchesEnded:withEvent:));
	touchesEndedOIMP = method_getImplementation(touchesEndedMethod);
	method_setImplementation(touchesEndedMethod, (IMP)&touchesEnded);

	auto touchesCancelledMethod = class_getInstanceMethod(eaglView, @selector(touchesCancelled:withEvent:));
	touchesCancelledOIMP = method_getImplementation(touchesCancelledMethod);
	method_setImplementation(touchesCancelledMethod, (IMP)&touchesCancelled);
#endif
}