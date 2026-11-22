//
//  streaming_view.cpp
//  Moonlight
//
//  Created by Даниил Виноградов on 27.05.2021.
//

#ifdef __SWITCH__
#include <borealis/platforms/switch/switch_input.hpp>
#endif

#include "streaming_view.hpp"
#include "diagnostics_log.hpp"
#include "stream_fault.hpp"

#include <algorithm>
#ifdef __SWITCH2__
#include "streaming/video/Vulkan/Switch2VulkanContext.hpp"
#endif
#include "AVFrameHolder.hpp"
#include "InputManager.hpp"
#include "click_gesture_recognizer.hpp"
#include "helper.hpp"
#include "ingame_overlay_view.hpp"
#include "streaming_input_overlay.hpp"
#include "two_finger_scroll_recognizer.hpp"
#include <Limelight.h>
#include <chrono>
#include <nanovg.h>

#if defined(__SDL3__)
#include <SDL3/SDL.h>
#elif defined(__SDL2__)
#include <SDL2/SDL.h>
#endif

using namespace brls;

#ifdef PLATFORM_TVOS
extern void updatePreferredDisplayMode(bool streamActive);
#endif

void setBottomBarStatus(const char *value) {
#if defined(__SDL2__) || defined(__SDL3__)
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, value);
#endif
}

void overrideButtonsIfNeeded(bool value)
{
#ifdef PLATFORM_SWITCH
    auto* input = static_cast<SwitchInputManager*>(brls::Application::getPlatform()->getInputManager());
    input->setHalfJoyConMappingEnabled(value);
    input->setScreenshotButtonOverrideMode(ButtonOverrideMode::NONE);
    input->setHomeButtonOverrideMode(ButtonOverrideMode::NONE);
#ifdef __SWITCH2__
    input->setChatButtonOverrideMode(ButtonOverrideMode::NONE);
#endif
    if (!value)
        return;

    switch (Settings::instance().get_overlay_system_button())
    {
        case ButtonOverrideType::NONE:
            break;
        case ButtonOverrideType::HOME:
            input->setHomeButtonOverrideMode(ButtonOverrideMode::CUSTOM_EVENT);
            break;
        case ButtonOverrideType::SCREENSHOT:
            input->setScreenshotButtonOverrideMode(ButtonOverrideMode::CUSTOM_EVENT);
            break;
        case ButtonOverrideType::CHAT:
#ifdef __SWITCH2__
            input->setChatButtonOverrideMode(ButtonOverrideMode::CUSTOM_EVENT);
#endif
            break;
    }

    switch (Settings::instance().get_guide_system_button())
    {
        case ButtonOverrideType::NONE:
            break;
        case ButtonOverrideType::HOME:
            input->setHomeButtonOverrideMode(ButtonOverrideMode::GUIDE_BUTTON);
            break;
        case ButtonOverrideType::SCREENSHOT:
            input->setScreenshotButtonOverrideMode(ButtonOverrideMode::GUIDE_BUTTON);
            break;
        case ButtonOverrideType::CHAT:
#ifdef __SWITCH2__
            input->setChatButtonOverrideMode(ButtonOverrideMode::GUIDE_BUTTON);
#endif
            break;
    }
#endif
}

StreamingView::StreamingView(const Host& host, const AppInfo& app) : host(host), app(app) {
    Application::getPlatform()->disableScreenDimming(true);

    setFocusable(true);
    setHideHighlight(true);
    loader = new LoadingOverlay(this);

    keyboardHolder = new Box(Axis::COLUMN);
    keyboardHolder->detach();
    keyboardHolder->setJustifyContent(JustifyContent::FLEX_END);
    keyboardHolder->setAlignItems(AlignItems::STRETCH);
    addView(keyboardHolder);

    session = new MoonlightSession(host.preferred_address(), app.app_id);

#ifdef PLATFORM_TVOS
        updatePreferredDisplayMode(true);
#endif

    ASYNC_RETAIN
    GameStreamClient::instance().connect(
        host, [ASYNC_TOKEN](GSResult<SERVER_DATA> result) {
            ASYNC_RELEASE
            if (!result.isSuccess()) {
                showError(result.error(), [this]() { terminate(false); });
                return;
            }

            session->set_address(
                GameStreamClient::instance().active_address(this->host));

            ASYNC_RETAIN
            session->start([ASYNC_TOKEN](GSResult<bool> result) {
                ASYNC_RELEASE

                loader->setHidden(true);
                if (!result.isSuccess()) {
                    showError(result.error(), [this]() { terminate(false); });
                }
            }, result.value().isSunshine());
        });

    // Subscribed once here, not in onFocusGained, which runs again every time
    // an overlay closes.
    windowFocusSubscription =
        Application::getWindowFocusChangedEvent()->subscribe(
            [this](bool focused) { this->onWindowFocusChanged(focused); });

    MoonlightInputManager::instance().reloadButtonMappingLayout();

    static bool lMouseKeyGate = false;
    static bool lMouseKeyUsed = false;
    addGestureRecognizer(new FingersGestureRecognizer([](){
                             return Settings::instance().get_keyboard_fingers();
                         }, [this] { addKeyboard(); }));

    addGestureRecognizer(
        new ClickGestureRecognizer(1, [](TapGestureStatus status) {
            if (Settings::instance().touchscreen_mouse_mode()) return;

            if (status.state == brls::GestureState::END) {
                Logger::debug("Left mouse click");
                MoonlightInputManager::leftMouseClick();
                lMouseKeyGate = true;
                delay(200, [] { lMouseKeyGate = false; });
            }
        }));

    addGestureRecognizer(
        new ClickGestureRecognizer(2, [](TapGestureStatus status) {
            if (Settings::instance().touchscreen_mouse_mode()) return;

            if (status.state == brls::GestureState::END) {
                Logger::debug("Right mouse click");
                MoonlightInputManager::rightMouseClick();
            }
        }));

    addGestureRecognizer(new PanGestureRecognizer(
        [this](PanGestureStatus status, Sound* sound) {
            static bool overlayTriggered = false;

            // Close keyboard by swiping outside of it
            if (status.state == brls::GestureState::START) {
                removeKeyboard();
                overlayTriggered = false;
            }

            // Open overlay by swipe from left screen corner
            bool hasControllers = Application::getPlatform()->getInputManager()->getControllersConnectedCount() > 0;
            if (!hasControllers && !overlayTriggered && status.state == brls::GestureState::STAY && status.startPosition.x < 100 && status.position.x > 200) {
                overlayTriggered = true;
                auto overlay = new IngameOverlay(this);
                Application::pushActivity(new Activity(overlay));
            }

            if (Settings::instance().touchscreen_mouse_mode()) return;

            if (status.state == brls::GestureState::UNSURE && lMouseKeyGate) {
                lMouseKeyGate = false;
                lMouseKeyUsed = true;
            } else if (status.state == brls::GestureState::START) {
                if (lMouseKeyUsed) {
//                    Logger::debug("Pressed key at {}", status.state);
                    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS,
                                           BUTTON_MOUSE_LEFT);
                }
            } else if (status.state == brls::GestureState::STAY) {
                brls::RawMouseState mouseState;
                Application::getPlatform()->getInputManager()->updateMouseStates(&mouseState);
                // Dirty hack to not update pan if mouse left button is pressed, because pan gesture recognizer will append its speed with raw mouse value
                // Need to improve gesture recognizers to determine the input source and ignore it for mouse
                if (!mouseState.leftButton) {
                    MoonlightInputManager::instance().updateTouchScreenPanDelta(
                            status);
                }
            } else if (lMouseKeyUsed) {
//                Logger::debug("Release key at {}", status.state);
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,
                                       BUTTON_MOUSE_LEFT);
                lMouseKeyUsed = false;
            }
        },
        PanAxis::ANY));

    scrollTouchRecognizer = new TwoFingerScrollGestureRecognizer(
            [this](TwoFingerScrollState state) {
                if (Settings::instance().touchscreen_mouse_mode()) return;

                if (state.state == brls::GestureState::START)
                    this->touchScrollCounter = 0;

                int threshold = int(state.delta.y / 25);
                if (threshold != this->touchScrollCounter) {
                    Logger::debug("Scroll on: {}",
                                  threshold - this->touchScrollCounter);
                    int invert = Settings::instance().swap_mouse_scroll() ? -1 : 1;
                    const int scrollCount = threshold - this->touchScrollCounter;
                    MoonlightInputManager::instance().sendMouseScroll({0, static_cast<float>(scrollCount * invert)});
                    this->touchScrollCounter = threshold;
                }
            });
    addGestureRecognizer(scrollTouchRecognizer);

    keysSubscription =
        Application::getPlatform()
            ->getInputManager()
            ->getKeyboardKeyStateChanged()
            ->subscribe([this, host, app](brls::KeyState state) {
                if (state.key == BRLS_KBD_KEY_ESCAPE) {
                    static std::chrono::high_resolution_clock::time_point
                        clock_counter;
                    static bool buttonState = false;
                    static bool used = false;

                    auto duration =
                        std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::high_resolution_clock::now() -
                            clock_counter);

                    if (!buttonState && state.pressed) {
                        buttonState = true;
                        clock_counter =
                            std::chrono::high_resolution_clock::now();
                    } else if (buttonState && !state.pressed) {
                        buttonState = false;
                        used = false;
                    } else if (buttonState && duration.count() >= 2 && !used) {
                        used = true;

                        auto overlay = new IngameOverlay(this);
                        Application::pushActivity(new Activity(overlay));
                    }
                }
            });
}

void StreamingView::onFocusGained() {
#ifdef __SWITCH2__
    if (auto* context = Switch2VulkanContext::current())
        context->setFrameDriven(true);
#endif
    Box::onFocusGained();

    MoonlightInputManager::instance().setInputEnabled(true);

    if (!blocked) {
        blocked = true;
        Application::blockInputs(true);
    }

    tempInputLock = true;
    ASYNC_RETAIN
    delay(300, [ASYNC_TOKEN]() {
        ASYNC_RELEASE
        this->tempInputLock = false;
    });

    Application::getPlatform()->getInputManager()->setPointerLock(true);

    overrideButtonsIfNeeded(true);
    setBottomBarStatus("1");

    scrollTouchRecognizer->forceReset();
}

void StreamingView::onFocusLost() {
#ifdef __SWITCH2__
    if (auto* context = Switch2VulkanContext::current())
        context->setFrameDriven(false);
#endif
    Box::onFocusLost();

    MoonlightInputManager::instance().setInputEnabled(false);
    MoonlightInputManager::instance().dropInput();

    if (blocked) {
        blocked = false;
        Application::unblockInputs();
    }

    removeKeyboard();
    Application::getPlatform()->getInputManager()->setPointerLock(false);

    overrideButtonsIfNeeded(false);
    setBottomBarStatus("2");

    if (bottombarDelayTask != -1)
        cancelDelay(bottombarDelayTask);
}

void StreamingView::onWindowFocusChanged(bool focused) {
    if (focused || terminated)
        return;

    // Losing focus on Switch means the console is going to sleep or the HOME
    // menu has taken over. Moonlight on Android ends the session at the
    // equivalent point (Game.onStop calls stopConnection then finish). End
    // the stream and return to the host list rather than carrying decoder
    // and GPU state across a suspend.
    //
    // Nothing destructive happens here. This runs inside Event::fire, and
    // terminate() would dismiss this view and unsubscribe from inside that
    // iteration. Record the intent; draw() acts on it.
    Logger::info("StreamingView: focus lost, will end the stream");
    MoonlightInputManager::instance().setInputEnabled(false);
    MoonlightInputManager::instance().dropInput();
    pendingSuspendTerminate = true;
}

void StreamingView::draw(NVGcontext* vg, float x, float y, float width,
                         float height, Style style, FrameContext* ctx) {
    if (pendingSuspendTerminate) {
        // Focus was lost. Safe to tear down here: this is the main loop, not
        // an event callback. When the loss was a console sleep this runs on
        // the first frame after waking, which is also the first moment the
        // graphics service is back.
        pendingSuspendTerminate = false;
        terminate(false);
        return;
    }

    if (session->is_terminated()) {
        terminate(false);
        return;
    }

    session->draw(vg, (int) width, (int) height);

    if (!tempInputLock && session->is_active())
        handleInput();
    handleOverlayCombo();
    handleMouseInputCombo();

    const auto now = std::chrono::steady_clock::now();
    if (!streamContextLogged && session->is_active()) {
        DiagnosticsLog::write(describeStreamContext());
        streamContextLogged = true;
    }
    if (session->is_active() &&
        (loadSampledAt.time_since_epoch().count() == 0 ||
         now - loadSampledAt >= std::chrono::milliseconds(250))) {
        cachedLoad = SwitchLoad::instance().sample();
        loadSampledAt = now;
    }

    const bool poor = session->connection_status_is_poor();
    if (poor) {
        faultText = describeStreamFault(*session->session_stats(), cachedLoad,
                                         Settings::instance().fps(),
                                         Settings::instance().bitrate());
        faultHideAt = now + std::chrono::seconds(10);
        if (faultLoggedAt.time_since_epoch().count() == 0 ||
            now - faultLoggedAt >= std::chrono::seconds(1)) {
            DiagnosticsLog::write(faultText);
            faultLoggedAt = now;
        }
    } else if (faultWasPoor) {
        DiagnosticsLog::write(
            "Bad connection cleared. The message stays on screen for 10 seconds.");
    }
    faultWasPoor = poor;

    if (now < faultHideAt && !faultText.empty()) {
        nvgFontFaceId(vg, Application::getFont(FONT_REGULAR));
        nvgFontSize(vg, 18);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFontBlur(vg, 0);
        float bounds[4] = {};
        nvgTextBoxBounds(vg, 24, 0, width - 48, faultText.c_str(), nullptr, bounds);
        const float textHeight = std::max(0.0f, bounds[3] - bounds[1]);
        const float top = std::max(16.0f, height - textHeight - 28.0f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 12, top - 8, width - 24, textHeight + 20, 8);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 190));
        nvgFill(vg);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgTextBox(vg, 24, top, width - 48, faultText.c_str(), nullptr);
    }

    if (session->use_hdr() != m_use_hdr) {
        m_use_hdr = session->use_hdr();

#ifdef PLATFORM_TVOS
        updatePreferredDisplayMode(true);
#endif
    }

    if (draw_stats) {
        // The stats only change a few times a second; formatting them and
        // laying the text out twice (blurred shadow + text) every frame cost
        // a noticeable part of the frame. Rebuild the text every 250 ms and
        // draw it once over a plain backdrop.
        nvgFontFaceId(vg, Application::getFont(FONT_REGULAR));
        nvgFontSize(vg, 20);
        nvgFontBlur(vg, 0);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);

        const auto statsNow = std::chrono::steady_clock::now();
        if (statsText.empty() ||
            statsNow - statsTextTime >= std::chrono::milliseconds(250)) {
            statsText = buildStatsText();
            statsTextTime = statsNow;
            nvgTextBoxBounds(vg, 20, 30, width, statsText.c_str(), nullptr,
                             statsBounds);
        }

        nvgBeginPath(vg);
        nvgRect(vg, statsBounds[0] - 6, statsBounds[1] - 4,
                statsBounds[2] - statsBounds[0] + 12,
                statsBounds[3] - statsBounds[1] + 8);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 170));
        nvgFill(vg);

        nvgFillColor(vg, nvgRGBA(0, 255, 0, 255));
        nvgTextBox(vg, 20, 30, width, statsText.c_str(), nullptr);
    }

    Box::draw(vg, x, y, width, height, style, ctx);
}

std::string StreamingView::buildStatsText() {
    auto stats = session->session_stats();

    auto statistics = fmt::format(
                "Estimated host PC frame rate: {:.{}f} FPS\n"
                    "Incoming frame rate from network: {:.{}f} FPS\n"
                    "Decoding frame rate: {:.{}f} FPS\n"
                    "Rendering frame rate: {:.{}f} FPS\n",
                stats->video_decode_stats.current_host_fps, 2,
                stats->video_decode_stats.current_received_fps, 2,
                stats->video_decode_stats.current_decoded_fps, 2,
                stats->video_render_stats.rendered_fps, 2);

    statistics += fmt::format("Frames dropped by your network connection: {}\n"
                              "Average receive time: {:.{}f} | {:.{}f} ms\n"
                              "Average decode time: {:.{}f} | {:.{}f} ms\n"
                              "Average decoder delay: {:.{}f} | {:.{}f} ms\n"
                              "Average rendering time: {:.{}f} ms\n",
                              stats->video_decode_stats.network_dropped_frames,
                              stats->video_decode_stats.current_receive_time, 2,
                              stats->video_decode_stats.session_receive_time, 2,
                              stats->video_decode_stats.current_decoding_time, 2,
                              stats->video_decode_stats.session_decoding_time, 2,
                              stats->video_decode_stats.current_decoder_delay, 2,
                              stats->video_decode_stats.session_decoder_delay, 2,
                              stats->video_render_stats.rendering_time, 2);

    if (stats->video_render_stats.gpu_timed_frames > 0) {
        statistics += fmt::format("Average GPU render time: {:.{}f} ms\n",
                                  stats->video_render_stats.gpu_rendering_time, 2);
    }

    if (stats->video_render_stats.post_processed_frames > 0) {
        statistics += fmt::format(
            "Average post-processing pass time: {:.{}f} ms (D:{:.{}f} | U:{:.{}f} | S:{:.{}f})\n",
            /* "Post-processed frames: {} / {}\n", */
            stats->video_render_stats.post_processing_time, 2,
            stats->video_render_stats.dithering_time, 2,
            stats->video_render_stats.upscaling_time, 2,
            stats->video_render_stats.sharpening_time, 2
            /*stats->video_render_stats.post_processed_frames, */
            /*stats->video_render_stats.rendered_frames*/);
    }

    statistics += fmt::format("Frames queue underflows | skipped: {} | {}\n"
                              "Queue empty | startup holds: {} | {}\n"
                              "Queue overflow | paced skips: {} | {}\n"
                              "Scheduled frame holds: {}\n"
                              "Frames presented by local clock: {}\n"
                              "Playout resyncs | estimated source: {} | {:.2f} FPS\n"
                              "Max pushes between draws: {}\n"
                              "Frames queue depth | target | capacity: {} | {} | {}",
                              AVFrameHolder::instance().getFakeFrameStat(),
                              AVFrameHolder::instance().getFrameDropStat(),
                              AVFrameHolder::instance().getFrameQueueEmptyStat(),
                              AVFrameHolder::instance().getFrameQueueRebufferHoldStat(),
                              AVFrameHolder::instance().getFrameQueueOverflowDropStat(),
                              AVFrameHolder::instance().getFrameQueuePacingSkipStat(),
                              AVFrameHolder::instance().getFrameQueueScheduledHoldStat(),
                              AVFrameHolder::instance().getFrameQueueLocalClockPacedFrameStat(),
                              AVFrameHolder::instance().getFrameQueuePlayoutResyncStat(),
                              AVFrameHolder::instance().getFrameQueueEstimatedSourceFps(),
                              AVFrameHolder::instance().getFrameQueueMaxPushBurstStat(),
                              AVFrameHolder::instance().getFrameQueueSize(),
                              AVFrameHolder::instance().getFrameQueueTargetDepth(),
                              AVFrameHolder::instance().getFrameQueueCapacity());
    return statistics;
}

void StreamingView::addKeyboard() {
    if (keyboard)
        return;

    keyboard = new KeyboardView(false);
    keyboardHolder->addView(keyboard);
}

void StreamingView::removeKeyboard() {
    if (!keyboard)
        return;

    keyboard->removeFromSuperView();
    keyboard = nullptr;
    Application::giveFocus(this);
}

void StreamingView::terminate(bool terminateApp) {
    if (terminated)
        return;
    terminated = true;

    session->stop(terminateApp);

    int controllersCount = Application::getPlatform()->getInputManager()->getControllersConnectedCount();
    for (int i = 0; i < controllersCount; i++)
        Application::getPlatform()->getInputManager()->sendRumble(i, 0, 0);

    bool hasOverlays =
        Application::getActivitiesStack().back() != this->getParentActivity();
    this->dismiss([this, hasOverlays] {
        if (hasOverlays)
            this->dismiss();
    });
}

void StreamingView::handleInput() {
    if (!this->focused) {
        MoonlightInputManager::instance().dropInput();
        return;
    }

    if (keyboard) {
        static KeyboardState oldKeyboardState;
        KeyboardState keyboardState = keyboard->getKeyboardState();

        for (int i = 0; i < _VK_KEY_MAX; i++) {
            if (keyboardState.keys[i] != oldKeyboardState.keys[i]) {
                oldKeyboardState.keys[i] = keyboardState.keys[i];
                LiSendKeyboardEvent(
                    keyboard->getKeyCode((KeyboardKeys)i),
                    keyboardState.keys[i] ? KEY_ACTION_DOWN : KEY_ACTION_UP, 0);
            }
        }

        // Drop input if keyboard overlay presented
//        MoonlightInputManager::instance().dropInput();
    }
//    else {
    MoonlightInputManager::instance().handleInput(keyboard != nullptr);
//    }

    if (!Application::currentTouchState.empty()) {
        setBottomBarStatus("2");

        if (bottombarDelayTask != -1)
            cancelDelay(bottombarDelayTask);

        ASYNC_RETAIN
        bottombarDelayTask = delay(3000, [ASYNC_TOKEN]() {
            ASYNC_RELEASE
            setBottomBarStatus("1");
            bottombarDelayTask = -1;
        });
    }
}

void StreamingView::handleOverlayCombo() {
    if (!this->focused)
        return;

    KeyComboOptions options = Settings::instance().overlay_options();

    static ControllerState controller;
    Application::getPlatform()->getInputManager()->updateUnifiedControllerState(
        &controller);

    static std::chrono::high_resolution_clock::time_point clock_counter;
    static bool buttonState = false;
    static bool used = false;

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::high_resolution_clock::now() - clock_counter);

    bool buttonsPressed = true;
    for (auto button : options.buttons) {
        buttonsPressed &= controller.buttons[button];
    }

    if (!buttonState && buttonsPressed) {
        buttonState = true;
        clock_counter = std::chrono::high_resolution_clock::now();
    } else if (buttonState && !buttonsPressed) {
        buttonState = false;
        used = false;
    } else if (buttonState && duration.count() >= options.holdTime && !used) {
        used = true;

        auto overlay = new IngameOverlay(this);
        Application::pushActivity(new Activity(overlay));
    }

#ifdef PLATFORM_SWITCH
    static bool oldSystemButtonOverlayPressed = false;
    bool systemButtonOverlayPressed = false;
    if (Settings::instance().get_overlay_system_button() == ButtonOverrideType::HOME)
        systemButtonOverlayPressed |= ((SwitchInputManager*) Application::getPlatform()->getInputManager())->isHomeButtonPressed();

    if (Settings::instance().get_overlay_system_button() == ButtonOverrideType::SCREENSHOT)
        systemButtonOverlayPressed |= ((SwitchInputManager*) Application::getPlatform()->getInputManager())->isScreenshotButtonPressed();
#ifdef __SWITCH2__
    if (Settings::instance().get_overlay_system_button() == ButtonOverrideType::CHAT)
        systemButtonOverlayPressed |= ((SwitchInputManager*) Application::getPlatform()->getInputManager())->isChatButtonPressed();
#endif

    if (oldSystemButtonOverlayPressed != systemButtonOverlayPressed) {
        oldSystemButtonOverlayPressed = systemButtonOverlayPressed;
        if (systemButtonOverlayPressed) {
            auto overlay = new IngameOverlay(this);
            Application::pushActivity(new Activity(overlay));
        }
    }
#endif
}

void StreamingView::handleMouseInputCombo() {
    if (!this->focused)
        return;

    KeyComboOptions options = Settings::instance().mouse_input_options();
    if (options.buttons.empty())
        return;

    static ControllerState controller;
    Application::getPlatform()->getInputManager()->updateUnifiedControllerState(
        &controller);

    static std::chrono::high_resolution_clock::time_point clock_counter;
    static bool buttonState = false;
    static bool used = false;

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::high_resolution_clock::now() - clock_counter);

    bool buttonsPressed = true;
    for (auto button : options.buttons) {
        buttonsPressed &= controller.buttons[button];
    }

    if (!buttonState && buttonsPressed) {
        buttonState = true;
        clock_counter = std::chrono::high_resolution_clock::now();
    } else if (buttonState && !buttonsPressed) {
        buttonState = false;
        used = false;
    } else if (buttonState && duration.count() >= options.holdTime && !used) {
        used = true;

        auto overlay = new StreamingInputOverlay(this);
        Application::pushActivity(new Activity(overlay));
    }
}

void StreamingView::onLayout() {
    Box::onLayout();
    if (loader)
        loader->layout();

    if (keyboardHolder) {
        keyboardHolder->setWidth(getWidth());
        keyboardHolder->setHeight(getHeight());
    }
}

StreamingView::~StreamingView() {
#ifdef __SWITCH2__
    if (auto* context = Switch2VulkanContext::current())
        context->setFrameDriven(false);
#endif
#ifdef PLATFORM_TVOS
    updatePreferredDisplayMode(false);
#endif

    Application::getPlatform()->disableScreenDimming(false);
    Application::getWindowFocusChangedEvent()->unsubscribe(
        windowFocusSubscription);
    Application::getPlatform()
        ->getInputManager()
        ->getKeyboardKeyStateChanged()
        ->unsubscribe(keysSubscription);
    session->stop(false);
    delete session;
}
