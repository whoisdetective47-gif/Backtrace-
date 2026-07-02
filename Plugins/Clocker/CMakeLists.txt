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
