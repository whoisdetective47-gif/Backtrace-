juce_add_plugin(Backtrace
    VERSION                     1.0.0
    COMPANY_NAME                "Sound Detective"
    PLUGIN_MANUFACTURER_CODE    SDet
    PLUGIN_CODE                 Bktr
    BUNDLE_ID                   "com.sounddetective.backtrace"
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Detective 47s Backtrace"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             "Fx|Delay|Reverb"
    AU_MAIN_TYPE                "kAudioUnitType_Effect"
    COPY_PLUGIN_AFTER_BUILD     TRUE
)

# ---------------------------------------------------------------------------
# Cross-DAW coverage
#   VST3       → Live, Cubase, Nuendo, Studio One, Reaper, FL, Bitwig, Waveform
#                (Windows / macOS / Linux)
#   AU         → Logic, GarageBand, Final Cut, MainStage (macOS)
#   Standalone → host-free testing of the capture path
#   AAX (Pro Tools)  → requires the Avid AAX SDK + PACE/iLok signing; add later
#                      with AAX_SDK_PATH and "AAX" appended to FORMATS once registered.
#   CLAP (Bitwig/Reaper) → optional, via clap-juce-extensions as a sibling target.
# ---------------------------------------------------------------------------

juce_generate_juce_header(Backtrace)

# Embed the Detective 47 / Dust 12.47 logo into the plugin binary
juce_add_binary_data(BacktraceBinaryData SOURCES
    Assets/logo_detective47_dust1200_BLUE.png
)
target_link_libraries(BacktraceBinaryData PRIVATE juce::juce_core)

target_sources(Backtrace PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(Backtrace PRIVATE
    Source
    ../../Common
)

target_compile_definitions(Backtrace PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# ---------------------------------------------------------------------------
# DSP safety stress test (console) — verifies no runaway/NaN at max settings
# ---------------------------------------------------------------------------
juce_add_console_app(BacktraceSafetyTest PRODUCT_NAME "BacktraceSafetyTest")
juce_generate_juce_header(BacktraceSafetyTest)
target_sources(BacktraceSafetyTest PRIVATE Tests/SafetyTest.cpp)
target_include_directories(BacktraceSafetyTest PRIVATE Source ../../Common)
target_compile_definitions(BacktraceSafetyTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(BacktraceSafetyTest PRIVATE
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_link_libraries(Backtrace PRIVATE
    BacktraceBinaryData
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# End-to-end render measurement test (console) — drives the real processor
# headless (no GUI) and measures ringout / level / filter / determinism.
# ---------------------------------------------------------------------------
juce_add_console_app(BacktraceRenderTest PRODUCT_NAME "BacktraceRenderTest")
juce_generate_juce_header(BacktraceRenderTest)
target_sources(BacktraceRenderTest PRIVATE Tests/RenderTest.cpp Source/PluginProcessor.cpp)
target_include_directories(BacktraceRenderTest PRIVATE Source ../../Common)
target_compile_definitions(BacktraceRenderTest PRIVATE
    BACKTRACE_NO_EDITOR=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=0
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(BacktraceRenderTest PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
