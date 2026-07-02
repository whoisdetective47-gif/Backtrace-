juce_add_plugin(Clocker
    VERSION                     1.0.0
    COMPANY_NAME                "Sound Detective"
    PLUGIN_MANUFACTURER_CODE    SDet
    PLUGIN_CODE                 Clkr
    BUNDLE_ID                   "com.sounddetective.clocker"
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Detective 47s Clocker"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             "Fx|Tools"
    AU_MAIN_TYPE                "kAudioUnitType_Effect"
    COPY_PLUGIN_AFTER_BUILD     TRUE
)

# Clocker is a time tracker, not an audio effect: processBlock is a straight
# passthrough and every feature runs on the message thread with timestamp math.
# EDITOR_WANTS_KEYBOARD_FOCUS is TRUE because the UI is mostly text fields.

juce_generate_juce_header(Clocker)

juce_add_binary_data(ClockerBinaryData SOURCES
    Assets/logo_detective47_dust1200_BLUE.png
)
target_link_libraries(ClockerBinaryData PRIVATE juce::juce_core)

target_sources(Clocker PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(Clocker PRIVATE
    Source
    ../../Common
)

target_compile_definitions(Clocker PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Clocker PRIVATE
    ClockerBinaryData
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Headless logic test (console) — timer engine, billing math, rounding,
# exports, and the save/restore auto-close path. No GUI, no audio.
# ---------------------------------------------------------------------------
juce_add_console_app(ClockerLogicTest PRODUCT_NAME "ClockerLogicTest")
juce_generate_juce_header(ClockerLogicTest)
target_sources(ClockerLogicTest PRIVATE Tests/LogicTest.cpp Source/PluginProcessor.cpp)
target_include_directories(ClockerLogicTest PRIVATE Source ../../Common)
target_compile_definitions(ClockerLogicTest PRIVATE
    CLOCKER_NO_EDITOR=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(ClockerLogicTest PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# GUI probe (console) — builds the real editor and diagnoses text-input issues
# (hit-testing, focus, simulated keypresses) without needing a DAW.
# ---------------------------------------------------------------------------
juce_add_console_app(ClockerGuiProbe PRODUCT_NAME "ClockerGuiProbe")
juce_generate_juce_header(ClockerGuiProbe)
target_sources(ClockerGuiProbe PRIVATE Tests/GuiProbe.cpp
    Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(ClockerGuiProbe PRIVATE Source ../../Common)
target_compile_definitions(ClockerGuiProbe PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(ClockerGuiProbe PRIVATE
    ClockerBinaryData
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
