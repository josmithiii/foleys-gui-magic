
# add modules to juce. You need to list it in `target_link_libraries` too
juce_add_modules (foleys_gui_magic ALIAS_NAMESPACE foleys)

# Note: Cannot suppress warnings for INTERFACE library foleys_gui_magic
# INTERFACE libraries don't support PRIVATE compile options
