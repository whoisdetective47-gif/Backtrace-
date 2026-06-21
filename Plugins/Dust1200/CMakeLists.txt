juce_add_plugin(Dust1200
    VERSION                     1.0.0
    COMPANY_NAME                "Sound Detective"
    PLUGIN_MANUFACTURER_CODE    SDet
    PLUGIN_CODE                 D120
    BUNDLE_ID                   "com.sounddetective.dust1247"
    FORMATS                     VST3 AU
    PRODUCT_NAME                "Detective 47s Dust 12.47"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             "Fx"
    AU_MAIN_TYPE                "kAudioUnitType_Effect"
    COPY_PLUGIN_AFTER_BUILD     TRUE
)

juce_generate_juce_header(Dust1200)

# Embed logo PNG directly into the plugin binary
juce_add_binary_data(Dust1200BinaryData SOURCES
    Assets/logo_detective47_dust1200.png
)
target_link_libraries(Dust1200BinaryData PRIVATE juce::juce_core)

target_sources(Dust1200 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(Dust1200 PRIVATE
    Source
    ../../Common
)

target_compile_definitions(Dust1200 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Dust1200 PRIVATE
    Dust1200BinaryData
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Copy logo into VST3 bundle resources if it exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/logo_detective47_dust1200.png")
    add_custom_command(TARGET Dust1200_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:Dust1200_VST3>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/Assets/logo_detective47_dust1200.png"
            "$<TARGET_BUNDLE_DIR:Dust1200_VST3>/Contents/Resources/logo_detective47_dust1200.png"
        COMMENT "Copying logo into VST3 bundle"
    )
endif()
