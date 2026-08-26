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

#include "foleys_GuiItem.h"
#include <iostream>

namespace foleys
{

GuiItem::GuiItem (MagicGUIBuilder& builder, juce::ValueTree node)
  : magicBuilder (builder),
    configNode (node)
{
    setOpaque (false);
    setInterceptsMouseClicks (false, true);

    visibility.addListener (this);
    configNode.addListener (this);
    magicBuilder.getStylesheet().addListener (this);
}

GuiItem::~GuiItem()
{
    magicBuilder.getStylesheet().removeListener (this);
}

void GuiItem::setColourTranslation (std::vector<std::pair<juce::String, int>> mapping)
{
    colourTranslation = mapping;
}

juce::StringArray GuiItem::getColourNames() const
{
    juce::StringArray names;

    for (const auto& pair : colourTranslation)
        names.addIfNotAlreadyThere (pair.first);

    return names;
}

juce::var GuiItem::getProperty (const juce::Identifier& property)
{
    return magicBuilder.getStyleProperty (property, configNode);
}

MagicGUIState& GuiItem::getMagicState()
{
    return magicBuilder.getMagicState();
}

GuiItem* GuiItem::findGuiItemWithId (const juce::String& name)
{
    if (configNode.getProperty (IDs::id, juce::String()).toString() == name)
        return this;

    return nullptr;
}

void GuiItem::updateInternal()
{
  // BEGIN JOS: (from Nick):
#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE
  if (!magicBuilder.isEditModeOn())
#endif
  {
    if (didUpdateInternal) {
      return;
    }
    didUpdateInternal = true;
  }
  // END JOS: (from Nick):

    auto& stylesheet = magicBuilder.getStylesheet();

    // BEGIN JOS: apply lookAndFeel only where it is EXPLICITLY defined (on the
    // node, its id style, a class or a type); everything else inherits through
    // the juce component hierarchy (setLookAndFeel (nullptr) is a no-op when
    // nothing was set). The root falls back to "FoleysFinest" when nothing is
    // defined anywhere, preserving PGM's stock look. Previously
    // getPropertyDefaultValue() faked "FoleysFinest" for every node, so EVERY
    // item called setLookAndFeel on itself and a lookAndFeel set once on the
    // root could never cascade: the children always overrode it.
    {
        juce::ValueTree lnfDefinedIn;
        stylesheet.getStyleProperty (IDs::lookAndFeel, configNode, true, &lnfDefinedIn);
        const bool isRootNode = configNode.getParent().getType() == IDs::magic;

        juce::LookAndFeel* newLookAndFeel = nullptr;
        if (lnfDefinedIn.isValid())
            newLookAndFeel = stylesheet.getLookAndFeel (configNode);

        if (newLookAndFeel == nullptr && isRootNode)
            newLookAndFeel = stylesheet.getLookAndFeelByName ("FoleysFinest");

        setLookAndFeel (newLookAndFeel);   // nullptr => inherit from parent
    }
    // END JOS.

    decorator.configure (magicBuilder, configNode);
    configureComponent();
    configureVisibility();   // JOS: NOT inside configureComponent - see there
    configureFlexBoxItem (configNode);
    configurePosition (configNode);

    updateColours();

    update();

#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE
    setEditMode (magicBuilder.isEditModeOn());
#endif

    repaint();
}

void GuiItem::updateColours()
{
    decorator.updateColours (magicBuilder, configNode);

    auto* component = getWrappedComponent();
    if (component == nullptr)
    {
        repaint();
        return;
    }

    for (auto& pair : colourTranslation)
    {
        auto colour = magicBuilder.getStyleProperty (pair.first, configNode).toString();
        if (colour.isNotEmpty())
            component->setColour (pair.second, magicBuilder.getStylesheet().getColour (colour));
    }

    component->repaint();
    repaint();
}

// BEGIN JOS: the `visibility=` binding, lifted OUT of configureComponent.
//
// configureComponent() returns immediately when getWrappedComponent() is null,
// and Container::getWrappedComponent() IS null - so upstream's binding was only
// ever read for leaf widgets.  `visibility=` on a <View> did nothing at all,
// silently, which is why TubePreampPanel.xml puts it on four <Slider>s rather
// than on the group boxes it actually wants to hide.  A View is the whole point
// of collapsing a PANEL, so this runs for every item, wrapped or not.
//
// `visibility=` resolves as a PARAMETER first and only then as a magicState
// property.  Upstream only knew properties, so a panel that wanted to collapse
// when its effect was switched off needed C++ mirroring the parameter into a
// property (jos::TubePreamp still does that for its four "Tube:show*" flags,
// which are a CHOICE, not a bool parameter).  Binding straight to the bool
// keeps the whole thing in the layout XML.
//
// The unset-property case matters too: juce::Value::referTo calls its listeners
// immediately, and an unwritten property reads void, i.e. FALSE - so before
// this, `visibility="Show:foo"` hid the item forever on a first run, until
// something wrote the property.  An unseeded show/hide flag now means SHOWN,
// which is the only safe default: the failure mode of the other choice is an
// invisible control with no way to get it back.
void GuiItem::configureVisibility()
{
    auto visibilityNode = magicBuilder.getStyleProperty (IDs::visibility, configNode);
    visibilityAttachment.reset();

    if (visibilityNode.isVoid())
        return;

    const auto visibilityName = visibilityNode.toString();

    if (auto* parameter = magicBuilder.getMagicState().getParameter (visibilityName))
    {
        visibilityAttachment = std::make_unique<juce::ParameterAttachment>
            (*parameter,
             [this] (float value) { setVisibleAndRelayout (value >= 0.5f); },
             nullptr);
        visibilityAttachment->sendInitialUpdate();
        return;
    }

    auto value = magicBuilder.getMagicState().getPropertyAsValue (visibilityName);
    if (value.getValue().isVoid())
    {
        // FAIL LOUD: a `visibility=` that names neither a parameter nor a
        // property anything writes is almost always a typo, and its failure is
        // otherwise SILENT - we would invent the property, seed it true, and
        // the component would simply never hide.
        std::cerr << "*** visibility=\"" << visibilityName << "\" on <"
                  << configNode.getType().toString()
                  << "> names no parameter and no existing property; "
                  << "defaulting to SHOWN\n";
        value.setValue (true);
    }

    visibility.referTo (value);
}
// END JOS

void GuiItem::configureComponent()
{
    auto* component = getWrappedComponent();
    if (component == nullptr)
        return;

    component->setComponentID (configNode.getProperty (IDs::id, juce::String()).toString());

    if (auto* tooltipClient = dynamic_cast<juce::SettableTooltipClient*>(component))
    {
        auto tooltip = magicBuilder.getStyleProperty (IDs::tooltip, configNode).toString();
        if (tooltip.isNotEmpty())
            tooltipClient->setTooltip (tooltip);
        else if (const auto& provider = magicBuilder.getTooltipProvider())
        {
            // JOS: no explicit tooltip attribute in the layout -- fall back to the
            // app-installed provider, keyed by the controlled parameter's ID, or
            // by an explicit help-id for a control that has no parameter at all
            // (a TextButton wired to an onClick trigger).  Without the second key
            // every action button in the GUI was undocumentable.
            auto paramID = getControlledParameterID ({});
            if (paramID.isEmpty())
                paramID = magicBuilder.getStyleProperty (IDs::helpID, configNode).toString();

            if (paramID.isNotEmpty())
            {
                auto provided = provider (paramID);
                if (provided.isNotEmpty())
                    tooltipClient->setTooltip (provided);
            }
        }
    }

    component->setAccessible (magicBuilder.getStyleProperty (IDs::accessibility, configNode));
    component->setTitle (magicBuilder.getStyleProperty (IDs::accessibilityTitle, configNode));
    component->setDescription (magicBuilder.getStyleProperty (IDs::accessibilityDescription, configNode).toString());
    component->setHelpText (magicBuilder.getStyleProperty (IDs::accessibilityHelpText, configNode).toString());
    component->setExplicitFocusOrder (magicBuilder.getStyleProperty (IDs::accessibilityFocusOrder, configNode));

}

void GuiItem::configureFlexBoxItem (const juce::ValueTree& node)
{
    flexItem = juce::FlexItem (*this).withFlex (1.0f);

    const auto minWidth = magicBuilder.getStyleProperty (IDs::minWidth, node);
    if (! minWidth.isVoid())
        flexItem.minWidth = minWidth;

    const auto maxWidth = magicBuilder.getStyleProperty (IDs::maxWidth, node);
    if (! maxWidth.isVoid())
        flexItem.maxWidth = maxWidth;

    const auto minHeight = magicBuilder.getStyleProperty (IDs::minHeight, node);
    if (! minHeight.isVoid())
        flexItem.minHeight = minHeight;

    const auto maxHeight = magicBuilder.getStyleProperty (IDs::maxHeight, node);
    if (! maxHeight.isVoid())
        flexItem.maxHeight = maxHeight;

    const auto width = magicBuilder.getStyleProperty (IDs::width, node);
    if (! width.isVoid())
        flexItem.width = width;

    const auto height = magicBuilder.getStyleProperty (IDs::height, node);
    if (! height.isVoid())
        flexItem.height = height;

    auto grow = magicBuilder.getStyleProperty (IDs::flexGrow, node);
    if (! grow.isVoid())
        flexItem.flexGrow = grow;

    const auto flexShrink = magicBuilder.getStyleProperty (IDs::flexShrink, node);
    if (! flexShrink.isVoid())
        flexItem.flexShrink = flexShrink;

    const auto flexOrder = magicBuilder.getStyleProperty (IDs::flexOrder, node);
    if (! flexOrder.isVoid())
        flexItem.order = flexOrder;

    const auto alignSelf = magicBuilder.getStyleProperty (IDs::flexAlignSelf, node).toString();
    if (alignSelf == IDs::flexStart)
        flexItem.alignSelf = juce::FlexItem::AlignSelf::flexStart;
    else if (alignSelf == IDs::flexEnd)
        flexItem.alignSelf = juce::FlexItem::AlignSelf::flexEnd;
    else if (alignSelf == IDs::flexCenter)
        flexItem.alignSelf = juce::FlexItem::AlignSelf::center;
    else if (alignSelf == IDs::flexAuto)
        flexItem.alignSelf = juce::FlexItem::AlignSelf::autoAlign;
    else
        flexItem.alignSelf = juce::FlexItem::AlignSelf::stretch;
}

void GuiItem::configurePosition (const juce::ValueTree& node)
{
    configurePosition (magicBuilder.getStyleProperty (IDs::posX, node), posX, 0.0);
    configurePosition (magicBuilder.getStyleProperty (IDs::posY, node), posY, 0.0);
    configurePosition (magicBuilder.getStyleProperty (IDs::posWidth, node), posWidth, 100.0);
    configurePosition (magicBuilder.getStyleProperty (IDs::posHeight, node), posHeight, 100.0);
}

void GuiItem::configurePosition (const juce::var& v, Position& p, double d)
{
    if (v.isVoid())
    {
        p.absolute = false;
        p.value = d;
    }
    else
    {
        auto const s = v.toString();
        p.absolute = ! s.endsWith ("%");
        p.value = s.getDoubleValue();
    }
}

juce::Rectangle<int> GuiItem::resolvePosition (juce::Rectangle<int> parent)
{
    return juce::Rectangle<int>
    (
        parent.getX() + juce::roundToInt (posX.absolute ? posX.value : posX.value * parent.getWidth() * 0.01),
        parent.getY() + juce::roundToInt (posY.absolute ? posY.value : posY.value * parent.getHeight() * 0.01),
        juce::roundToInt (posWidth.absolute ? posWidth.value : posWidth.value * parent.getWidth() * 0.01),
        juce::roundToInt (posHeight.absolute ? posHeight.value : posHeight.value * parent.getHeight() * 0.01)
    );
}

void GuiItem::paint (juce::Graphics& g)
{
    decorator.drawDecorator (g, getLocalBounds());
}

juce::Rectangle<int> GuiItem::getClientBounds() const
{
    return decorator.getClientBounds (getLocalBounds()).client;
}

void GuiItem::resized()
{
#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE
    if (borderDragger)
        borderDragger->setBounds (getLocalBounds());
#endif

    if (auto* component = getWrappedComponent())
    {
        auto b = getClientBounds();
        component->setVisible (b.getWidth() > 2 && b.getHeight() > 2);
//        if (visibility.getValue())
            component->setBounds (b);
//        else {
//            setBounds (juce::Rectangle<int>(0,0,0,0));
//            updateInternal();
//        }
    }
}

void GuiItem::updateLayout()
{
    resized();
}

LayoutType GuiItem::getParentsLayoutType() const
{
    if (auto* container = dynamic_cast<Container*>(getParentComponent()))
        return container->getLayoutMode();

    return LayoutType::Contents;
}

juce::String GuiItem::getTabCaption (const juce::String& defaultName) const
{
    return decorator.getTabCaption (defaultName);
}

juce::Colour GuiItem::getTabColour() const
{
    return decorator.getTabColour();
}

// BEGIN JOS: hiding an item now COLLAPSES it.  Upstream (and the commented-out
// experiment that used to sit in valueChanged below) only called setVisible,
// which leaves the item's FlexItem in the parent's FlexBox - so the panel
// vanished but its gap stayed, which is the "KNOWN COSMETIC LIMIT" recorded in
// TubePreampPanel.xml.  Container::updateLayout now skips invisible children,
// and this asks the parent to re-run it so the siblings take the space back.
void GuiItem::setVisibleAndRelayout (bool shouldBeVisible)
{
    // Recorded even when nothing changes right now: this runs from
    // ParameterAttachment::sendInitialUpdate, BEFORE the item is parented, and
    // whoever parents it re-asserts it via applyVisibilityBinding().
    visibilityWanted = shouldBeVisible;

    if (isVisible() == shouldBeVisible)
        return;

    setVisible (shouldBeVisible);

    // The nearest Container is the one whose FlexBox this item sits in; a
    // relayout there reflows the siblings.  Going through the whole builder
    // would work too, but it re-reads the stylesheet for every item in the
    // window on every toggle.
    if (auto* container = dynamic_cast<Container*>(getParentComponent()))
        container->updateLayout();
    else
        magicBuilder.updateLayout();
}
// END JOS

void GuiItem::valueChanged (juce::Value& source)
{
  if (source == visibility) {
    setVisibleAndRelayout (visibility.getValue());
  }
}

void GuiItem::valueTreePropertyChanged (juce::ValueTree& treeThatChanged, const juce::Identifier&)
{
    if (treeThatChanged == configNode)
    {
        // BEGIN JOS: a genuine property edit must not be swallowed by the
        // didUpdateInternal once-only guard above (outside edit mode it made
        // every property-panel edit a silent no-op until the next full
        // rebuild -- e.g. lookAndFeel set on the root View never propagated
        // unless the Edit toggle happened to be ON). Clear the guard on the
        // edited item (and on the parent that re-runs it, whose
        // Container::update() drives the children); the SIBLINGS' guards stay
        // set, keeping the construction-time double-update dedup intact.
        didUpdateInternal = false;
        if (auto* parent = findParentComponentOfClass<GuiItem>())
        {
            parent->didUpdateInternal = false;
            parent->updateInternal();
        }
        else
            updateInternal();
        // END JOS.

        return;
    }

    auto& stylesheet = magicBuilder.getStylesheet();
    if (stylesheet.isClassNode (treeThatChanged))
    {
        auto name = treeThatChanged.getType().toString();
        auto classes = configNode.getProperty (IDs::styleClass, juce::String()).toString();
        if (classes.contains (name))
        {
            didUpdateInternal = false;   // JOS: same reason as above
            updateInternal();
        }
    }
}

void GuiItem::valueTreeChildAdded (juce::ValueTree& treeThatChanged, juce::ValueTree&)
{
    if (treeThatChanged == configNode)
        createSubComponents();
}

void GuiItem::valueTreeChildRemoved (juce::ValueTree& treeThatChanged, juce::ValueTree&, int)
{
    if (treeThatChanged == configNode)
        createSubComponents();
}

void GuiItem::valueTreeChildOrderChanged (juce::ValueTree& treeThatChanged, int, int)
{
    if (treeThatChanged == configNode)
        createSubComponents();
}

void GuiItem::valueTreeParentChanged (juce::ValueTree& treeThatChanged)
{
    if (treeThatChanged == configNode)
    {
        if (auto* parent = dynamic_cast<GuiItem*>(getParentComponent()))
            parent->updateInternal();
        else
            updateInternal();
    }
}

void GuiItem::itemDragEnter (const juce::DragAndDropTarget::SourceDetails& details)
{
    if (details.description.toString().startsWith (IDs::dragCC))
    {
        auto paramID = getControlledParameterID (details.localPosition);
        if (paramID.isNotEmpty())
            if (auto* parameter = magicBuilder.getMagicState().getParameter (paramID))
                highlight = parameter->getName (64);

        repaint();
    }
}

void GuiItem::itemDragExit (const juce::DragAndDropTarget::SourceDetails& details)
{
    juce::ignoreUnused (details);
    highlight.clear();
    repaint();
}

GuiItem* GuiItem::findGuiItem (const juce::ValueTree& node)
{
    if (node == configNode)
        return this;

    return nullptr;
}

void GuiItem::paintOverChildren (juce::Graphics& g)
{
#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE
    if (magicBuilder.isEditModeOn() && magicBuilder.getSelectedNode() == configNode)
    {
        g.setColour (juce::Colours::orange.withAlpha (0.5f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    }
#endif

    if (highlight.isNotEmpty())
    {
        g.setColour (juce::Colours::red);
        g.drawFittedText (highlight, getLocalBounds(), juce::Justification::centred, 3);
    }
}

#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE

void GuiItem::setEditMode (bool shouldEdit)
{
    setInterceptsMouseClicks (shouldEdit, true);

    if (auto* component = getWrappedComponent())
        component->setInterceptsMouseClicks (!shouldEdit, !shouldEdit);
}

void GuiItem::setDraggable (bool selected)
{
    if (selected &&
        getParentsLayoutType() == LayoutType::Contents &&
        configNode != magicBuilder.getGuiRootNode())
    {
        toFront (false);
        borderDragger = std::make_unique<BorderDragger>(this, nullptr);
        componentDragger = std::make_unique<juce::ComponentDragger>();

        borderDragger->onDragStart = [&]
        {
            magicBuilder.getUndoManager().beginNewTransaction ("Drag component position");
        };
        borderDragger->onDragging = [&]
        {
            savePosition();
        };
        borderDragger->onDragEnd = [&]
        {
            savePosition();
        };

        borderDragger->setBounds (getLocalBounds());
        addAndMakeVisible (*borderDragger);
    }
    else
    {
        borderDragger.reset();
        componentDragger.reset();
    }
}

void GuiItem::savePosition ()
{
    auto* container = findParentComponentOfClass<Container>();

    if (container == nullptr)
        return;

    auto parent = container->getClientBounds();

    auto px = posX.absolute ? juce::String (getX() - parent.getX()) : juce::String (100.0 * (getX() - parent.getX()) / parent.getWidth()) + "%";
    auto py = posY.absolute ? juce::String (getY() - parent.getY()) : juce::String (100.0 * (getY() - parent.getY()) / parent.getHeight()) + "%";
    auto pw = posWidth.absolute ? juce::String (getWidth()) : juce::String (100.0 * getWidth() / parent.getWidth()) + "%";
    auto ph = posHeight.absolute ? juce::String (getHeight()) : juce::String (100.0 * getHeight() / parent.getHeight()) + "%";

    auto* undo = &magicBuilder.getUndoManager();
    configNode.setProperty (IDs::posX, px, undo);
    configNode.setProperty (IDs::posY, py, undo);
    configNode.setProperty (IDs::posWidth, pw, undo);
    configNode.setProperty (IDs::posHeight, ph, undo);
}

void GuiItem::mouseDown (const juce::MouseEvent& event)
{
    if (componentDragger)
    {
        magicBuilder.getUndoManager().beginNewTransaction ("Drag component position");
        componentDragger->startDraggingComponent (this, event);
    }
}

void GuiItem::mouseDrag (const juce::MouseEvent& event)
{
    if (componentDragger)
    {
        componentDragger->dragComponent (this, event, nullptr);
        savePosition();
    }
    else if (event.mouseWasDraggedSinceMouseDown())
    {
        auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);
        container->startDragging (IDs::dragSelected, this);
    }
}

void GuiItem::mouseUp (const juce::MouseEvent& event)
{
    if (! event.mouseWasDraggedSinceMouseDown())
        magicBuilder.setSelectedNode (configNode);
}

#endif

bool GuiItem::isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails &)
{
    return true;
}

void GuiItem::itemDropped (const juce::DragAndDropTarget::SourceDetails &dragSourceDetails)
{
    highlight.clear();

    auto dragString = dragSourceDetails.description.toString();
    if (dragString.startsWith (IDs::dragCC))
    {
        auto number = dragString.substring (IDs::dragCC.length()).getIntValue();
        auto parameterID = getControlledParameterID (dragSourceDetails.localPosition);
        if (number > 0 && parameterID.isNotEmpty())
            if (auto* procState = dynamic_cast<MagicProcessorState*>(&magicBuilder.getMagicState()))
                procState->mapMidiController (number, parameterID);

        repaint();
        return;
    }

#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE
    if (dragSourceDetails.description == IDs::dragSelected)
    {
        auto dragged = magicBuilder.getSelectedNode();
        if (dragged.isValid() == false)
            return;

        magicBuilder.draggedItemOnto (dragged, configNode);
        return;
    }

    auto node = juce::ValueTree::fromXml (dragSourceDetails.description.toString());
    if (node.isValid())
        magicBuilder.draggedItemOnto (node, configNode);
#endif
}


}
