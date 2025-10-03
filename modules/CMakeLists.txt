
# add modules to juce. You need to list it in `target_link_libraries` too
juce_add_modules (foleys_gui_magic ALIAS_NAMESPACE foleys)

# Suppress warnings from third-party foleys_gui_magic module
if(TARGET foleys_gui_magic)
    target_compile_options(foleys_gui_magic PRIVATE
        -w  # Suppress all warnings for this target
    )
endif()
