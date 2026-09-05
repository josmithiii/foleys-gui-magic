/*
 ==============================================================================
    Copyright (c) 2019-2023 Foleys Finest Audio - Daniel Walz
    All rights reserved.

    **BSD 3-Clause License**

    Redistribution and use in source and binary forms, with or without modification,
    are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

 ==============================================================================

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
    OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
    OF THE POSSIBILITY OF SUCH DAMAGE.
 ==============================================================================
 */

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "foleys_Decorator.h"
#include "../General/foleys_SettableProperties.h"

namespace foleys
{

class MagicGUIBuilder;
class MagicGUIState;

enum class LayoutType;

/**
 The GuiItem class will draw borders and descriptions around widgets, if defined.
 It also owns the Component and the Attachment, in case the Component is connected
 to an AudioProcessorValueTreeState.
 */
class GuiItem   : public juce::Component,
                  public juce::Value::Listener,
                  private juce::ValueTree::Listener,
                  public juce::DragAndDropTarget
{
public:
    GuiItem (MagicGUIBuilder& builder, juce::ValueTree node);
    ~GuiItem() override;

    bool didUpdateInternal = false;

    /**
     Allows accessing the Component inside that GuiItem. Don't keep this pointer!
     */
    virtual juce::Component* getWrappedComponent() = 0;

    /**
     In update() the ValueTree properties should be used to set all properties to the component.
     You can use the magicBuilder to resolve properties from CSS.
     The Colours will be handled by default.
     */
    virtual void update() = 0;

    /**
     Set colours in the wrapped Component to the value from the stylesheet and palette.
     */
    virtual void updateColours();

    /**
     Override this to return each settable option the designer should be able to configure on your component.
     */
    virtual std::vector<SettableProperty> getSettableProperties() const { return {}; }

    /**
     For each factory you can register a translation table, which will forward the colours from the
     Stylesheet to the Components.
     */
    void setColourTranslation (std::vector<std::pair<juce::String, int>> mapping);

    /**
     Return the names of configurable colours
     */
    juce::StringArray getColourNames() const;

    /**
     Returns the parameterID that is controlled from this component.
     To allow multiple return values depending of the position where the drop arrived
     there is the drop position supplied.
     */
    virtual juce::String getControlledParameterID (juce::Point<int>) { return {}; }

    /**
     Look up a value through the DOM and CSS
     */
    juce::var getProperty (const juce::Identifier& property);

    MagicGUIState& getMagicState();

    /**
     Lookup a Component through the tree. It will return the first with that id regardless if there is another one.
     We discourage using that function, because that Component can be deleted and recreated at any time without notice.
     */
    virtual GuiItem* findGuiItemWithId (const juce::String& name);

    /**
     Reread properties from the config ValueTree
     */
    void updateInternal();

    void paint (juce::Graphics& g) final;
    void resized() override;

    virtual bool isContainer() const { return false; }

    virtual void createSubComponents() {}

    /**
     This will trigger a recalculation of the children layout regardless of resized
     */
    virtual void updateLayout();

    /**
     Returns the layout type this item is managed by.
     */
    LayoutType getParentsLayoutType() const;

    /**
     Parse the values and set it to the FlexBox::Item for layouting.
     */
    void configureFlexBoxItem (const juce::ValueTree& node);

    void configurePosition (const juce::ValueTree& node);

    /**
     Calculates the position according to the parent area
     */
    juce::Rectangle<int> resolvePosition (juce::Rectangle<int> parent);

    /**
     Returns the bounds of the wrapped Component. This is the GuiItems bounds
     reduced by margin, padding and the caption, if one was set.
     */
    juce::Rectangle<int> getClientBounds() const;

    juce::String getTabCaption (const juce::String& defaultName) const;
    juce::Colour getTabColour() const;

    juce::FlexItem& getFlexItem() { return flexItem; }

    void itemDragEnter (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragExit (const juce::DragAndDropTarget::SourceDetails& details) override;

    void paintOverChildren (juce::Graphics& g) override;

    /**
     Seeks recursively for a GuiItem
     */
    virtual GuiItem* findGuiItem (const juce::ValueTree& node);

#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE

    /**
     This method sets the GUI in edit mode, that allows to drag the components around.
     */
    virtual void setEditMode (bool shouldEdit);

    void setDraggable (bool selected);

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

#endif

    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails &dragSourceDetails) override;
    void itemDropped (const juce::DragAndDropTarget::SourceDetails &dragSourceDetails) override;

    MagicGUIBuilder& magicBuilder;

    // BEGIN JOS: public because a CONTAINER calls these on its children, i.e.
    // through a GuiItem* - protected would not reach through the base pointer
    // even though Container is itself a GuiItem.
    /**
     Re-assert what the `visibility=` binding wants.

     MUST be called by whoever parents this item, because the order is against
     us: createGuiItem() configures the item (which is where the binding is read
     and can hide it) and only THEN does the caller add it to its parent - and
     juce::Component::addAndMakeVisible ends with setVisible (true), which would
     silently undo the hide.  The panel would then come up expanded and only
     collapse the first time its enable was toggled.  Containers call
     addChildComponent() plus this instead.
     */
    void applyVisibilityBinding() { setVisible (visibilityWanted); }

    bool isVisibilityWanted() const { return visibilityWanted; }
    // END JOS

    // BEGIN JOS CHANGE: a widget bound to a parameter THIS processor does not
    // have hides itself instead of drawing a dead control.
    /**
     Does `paramID` name no parameter on this processor?  If so, say it ONCE on
     stderr (naming the layout item and the parameter), hide this item so the
     parent FlexBox reflows and the row leaves no hole, and return true.

     Returns false - and does nothing at all - when the parameter exists, or
     when paramID is empty.

     WHY.  A shared layout fragment is a UNION: the day AutoHarp wears
     `shared:PerformControls` it wears every row in it, including rows only a
     guitar declares.  Before this, `MagicProcessorState::createAttachment` hit
     `jassertfalse` (so every DEBUG build stopped on every layout build) and in
     Release returned a null attachment, leaving a DRAWN but inert control
     parked at its range minimum.  JOS's ruling, 2026-09-03: "PGM sliders that
     are bound to nonexistent parameters just do nothing" - do nothing,
     properly, which means not being there.

     PARAMETER bindings only.  A missing `value=`/`property=` is a different
     animal: pgmf INVENTS and seeds the property, which is a working per-GUI
     value and not an error.
     */
    bool hideIfParameterMissing (const juce::String& paramID);
    // END JOS CHANGE

protected:

    juce::ValueTree configNode;

    Decorator       decorator;

    juce::FlexItem  flexItem { juce::FlexItem (*this).withFlex (1.0f) };

    std::vector<std::pair<juce::String, int>> colourTranslation;

private:

    class BorderDragger : public juce::ResizableBorderComponent
    {
    public:
        BorderDragger (juce::Component* component, juce::ComponentBoundsConstrainer* constrainer = nullptr) : juce::ResizableBorderComponent (component, constrainer) {}
        std::function<void()> onDragStart, onDragging, onDragEnd;

        void mouseDown (const juce::MouseEvent& event) override
        {
            if (onDragStart) onDragStart();
            juce::ResizableBorderComponent::mouseDown (event);
        }

        void mouseDrag (const juce::MouseEvent& event) override
        {
            juce::ResizableBorderComponent::mouseDrag (event);
            if (onDragging) onDragging();
        }

        void mouseUp (const juce::MouseEvent& event) override
        {
            juce::ResizableBorderComponent::mouseUp (event);
            if (onDragEnd) onDragEnd();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BorderDragger)
    };
    std::unique_ptr<BorderDragger>          borderDragger;
    std::unique_ptr<juce::ComponentDragger> componentDragger;

protected:
    // JOS: protected, not private - Container::valueChanged overrides it and
    // must chain here, or a <View> bound to a `visibility=` PROPERTY never hides.
    void valueChanged (juce::Value& source) override;
private:

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;

    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;

    void valueTreeParentChanged (juce::ValueTree&) override;

    /**
     This will get the necessary information from the stylesheet, using inheritance
     of nodes if needed, to set specific properties for the wrapped component.
     */
    void configureComponent();

    // BEGIN JOS: `visibility=` names either a magicState PROPERTY (upstream) or,
    // since 2026-08-26, a PARAMETER id.  The parameter form is what makes a
    // panel collapse when the effect it belongs to is switched off, without any
    // C++ glue mirroring the parameter into a property (which is what
    // jos::TubePreamp had to do by hand for its four "Tube:show*" flags).  A
    // ParameterAttachment marshals the change to the message thread for us.
    juce::Value     visibility { true };
    std::unique_ptr<juce::ParameterAttachment> visibilityAttachment;
    // JOS 2026-09-05: true once configureVisibility() bound `visibility=` to a
    // parameter or a property.  Container::hasVisibleBoundDescendant() asks it.
    bool            visibilityBound = false;
public:
    bool isVisibilityBound() const noexcept { return visibilityBound; }
protected:

    /** Read `visibility=` and bind it.  Called from updateInternal, NOT from
        configureComponent, which returns early for every Container. */
    void configureVisibility();

    /**
     Show or hide this item AND make the parent re-run its layout, so that a
     hidden item gives its space back to its siblings instead of leaving a hole.
     Container::updateLayout skips invisible children when it fills the FlexBox.
     */
    void setVisibleAndRelayout (bool shouldBeVisible);

    /**
     Re-assert what the `visibility=` binding wants.

     MUST be called by whoever parents this item, because the order is against
     us: createGuiItem() configures the item (which is where the binding is read
     and can hide it) and only THEN does the caller add it to its parent - and
     juce::Component::addAndMakeVisible ends with setVisible (true), which would
     silently undo the hide.  The panel would then come up expanded and only
     collapse the first time its enable was toggled.  Containers call
     addChildComponent() plus this instead.
     */
    bool            visibilityWanted = true;   // what that binding last asked for
    // END JOS

    // BEGIN JOS CHANGE: the paramID hideIfParameterMissing() has already
    // complained about, so the `***` line is once per ITEM and not once per
    // update() - a Container leaving Tabbed layout, an edit-mode rebuild or a
    // stylesheet change all re-run update() on every item in the window.
    juce::String    missingParameterReported;
    // END JOS CHANGE

    juce::String    highlight;

    struct Position
    {
        bool   absolute = true;
        double value = 0.0;
    };
    Position posX, posY, posWidth, posHeight;

    void configurePosition (const juce::var& v, Position& p, double d);
    void savePosition ();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuiItem)
};

} // namespace foleys
