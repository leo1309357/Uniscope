/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_PAL_INPUT_H
#define UNISCOPE_PAL_INPUT_H

namespace uniscope::pal {

// 指针状态，坐标是帧缓冲像素。桌面由鼠标填充，Android 同步主触点。
struct PointerState {
    double cursorX = 0.0;
    double cursorY = 0.0;
    bool cursorValid = false;
    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;
    double scrollX = 0.0;
    double scrollY = 0.0;
};

// 一个活动触点。down 表示当前按住；pressed/released 是单帧边沿。
struct TouchContact {
    int id = -1;
    float x = 0.0f;
    float y = 0.0f;
    bool down = false;
    bool pressed = false;
    bool released = false;
};

constexpr int kMaxTouchContacts = 10;

// 键码数值与 GLFW / 既有 RW_KEY_* 完全一致，保证 settings.cfg 兼容。
namespace key {
enum : int {
    Unknown = -1,
    Space = 32, Comma = 44, Period = 46, GraveAccent = 96,
    Num0 = 48, Num1 = 49, Num2 = 50, Num3 = 51, Num4 = 52,
    Num5 = 53, Num6 = 54, Num7 = 55, Num8 = 56, Num9 = 57,
    A = 65, C = 67, D = 68, E = 69, F = 70, I = 73,
    Q = 81, R = 82, S = 83, V = 86, W = 87, X = 88, Z = 90,
    Escape = 256, Enter = 257, Tab = 258, Backspace = 259,
    Right = 262, Left = 263, Down = 264, Up = 265,
    PageUp = 266, PageDown = 267, F3 = 291,
    LeftShift = 340, LeftControl = 341, LeftAlt = 342,
    RightShift = 344, RightControl = 345, RightAlt = 346
};
} // namespace key

} // namespace uniscope::pal

#endif
