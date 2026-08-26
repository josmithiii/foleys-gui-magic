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

#include "foleys_Container.h"

namespace foleys
{

Container::Container (MagicGUIBuilder& builder, juce::ValueTree node)
  : GuiItem (builder, node)
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&containerBox, false);
    currentTab.addListener (this);
}

Container::~Container()
{
    currentTab.removeListener (this);
}

void Container::update()
{
    configureFlexBox (configNode);

    auto focusType = magicBuilder.getStyleProperty (IDs::focusContainerType, configNode).toString();
    if (focusType == IDs::focusContainer)
        setFocusContainerType (FocusContainerType::focusContainer);
    else if (focusType == IDs::focusKeyContainer)
        setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    else
        setFocusContainerType (FocusContainerType::none);

    for (auto& child : *this)
        child->updateInternal();

    setTitle (magicBuilder.getStyleProperty (IDs::accessibilityTitle, configNode).toString());

    const auto display = magicBuilder.getStyleProperty (IDs::display, configNode).toString();
    if (display == IDs::contents)
        setLayoutMode (LayoutType::Contents);
    else if (display == IDs::tabbed)
        setLayoutMode (LayoutType::Tabbed);
    else
        setLayoutMode (LayoutType::FlexBox);

    auto tabHeightProperty = magicBuilder.getStyleProperty (IDs::tabHeight, configNode).toString();
    tabbarHeight = tabHeightProperty.isNotEmpty() ? tabHeightProperty.getIntValue() : 30;

    const auto tabProperty = magicBuilder.getStyleProperty (IDs::selectedTab, configNode).toString();
    if (tabProperty.isNotEmpty())
        currentTab.referTo(getMagicState().getPropertyAsValue(tabProperty));

    auto repaintHz = magicBuilder.getStyleProperty (IDs::repaintHz, configNode).toString();
    if (repaintHz.isNotEmpty())
    {
        refreshRateHz = repaintHz.getIntValue();
        updateContinuousRedraw();
    }

    auto scroll = magicBuilder.getStyleProperty (IDs::scrollMode, configNode).toString();
    if (scroll.isNotEmpty())
    {
        if (scroll == IDs::noScroll)
            scrollMode = ScrollMode::NoScroll;
        else if (scroll == IDs::scrollHorizontal)
            scrollMode = ScrollMode::ScrollHorizontal;
        else if (scroll == IDs::scrollVertical)
            scrollMode = ScrollMode::ScrollVertical;
        else if (scroll == IDs::scrollBoth)
            scrollMode = ScrollMode::ScrollBoth;

        updateLayout();
    }
}

void Container::addChildItem (std::unique_ptr<GuiItem> child)
{
    // BEGIN JOS: addAndMakeVisible would end in setVisible(true) and undo a hide
    // the `visibility=` binding already asked for while the item was being
    // configured (createGuiItem configures BEFORE the caller parents it).
    containerBox.addChildComponent (child.get());
    child->applyVisibilityBinding();
    // END JOS
    children.push_back (std::move (child));
}

void Container::createSubComponents()
{
    children.clear();

    for (auto childNode : configNode)
    {
        auto childItem = magicBuilder.createGuiItem (childNode);
        if (childItem)
        {
            // BEGIN JOS: see addChildItem - add hidden, then honour the binding.
            containerBox.addChildComponent (childItem.get());
            childItem->applyVisibilityBinding();
            // END JOS
            childItem->createSubComponents();

            children.push_back (std::move (childItem));
        }
    }

    updateLayout();
    updateContinuousRedraw();
}

GuiItem* Container::findGuiItemWithId (const juce::String& name)
{
    if (configNode.getProperty (IDs::id, juce::String()).toString() == name)
        return this;

    for (auto& item : children)
        if (auto* matching = item->findGuiItemWithId (name))
            return matching;

    return nullptr;
}

GuiItem* Container::findGuiItem (const juce::ValueTree& node)
{
    if (node == configNode)
        return this;

    for (auto& child : children)
        if (auto* item = child->findGuiItem (node))
            return item;

    return nullptr;
}

void Container::setLayoutMode (LayoutType layoutToUse)
{
    layout = layoutToUse;
    if (layout == LayoutType::Tabbed)
    {
        updateTabbedButtons();
    }
    else
    {
        tabbedButtons.reset();
        for (auto& child : children)
            // BEGIN JOS: was setVisible(true), which un-collapsed a child whose
            // `visibility=` binding wanted it hidden, every time a container
            // left Tabbed layout.
            child->applyVisibilityBinding();
            // END JOS
    }

    updateLayout();
}

LayoutType Container::getLayoutMode() const
{
    return layout;
}

void Container::resized()
{
    updateLayout();
}

void Container::updateLayout()
{
    if (children.empty())
        return;

    viewport.setBackgroundColour (decorator.getBackgroundColour());

    if (layout != LayoutType::Tabbed)
        tabbedButtons.reset();

    viewport.setBounds (getClientBounds());
    viewport.setScrollBarsShown (scrollMode == ScrollMode::ScrollVertical || scrollMode == ScrollMode::ScrollBoth,
                                 scrollMode == ScrollMode::ScrollHorizontal || scrollMode == ScrollMode::ScrollBoth);
    auto clientBounds = viewport.getLocalBounds();

    if (layout == LayoutType::FlexBox)
    {
        // BEGIN JOS: an INVISIBLE child gets no FlexItem, so it gives its space
        // back to its siblings instead of leaving a hole.  Upstream added one
        // for every child regardless, which is why TubePreampPanel.xml had to
        // record a hidden knob's gap as a "KNOWN COSMETIC LIMIT".  What flips
        // the visibility is `visibility="<parameter-or-property>"` on the item;
        // GuiItem::setVisibleAndRelayout calls back in here when it changes.
        //
        // Only the FlexBox layout collapses.  A Tabbed container decides its
        // children's visibility itself (updateSelectedTab, below), and an
        // absolute-positioned Contents container has no flow to reclaim, so
        // `visibility=` on a tab PAGE is still just a hide.
        flexBox.items.clear();
        for (auto& child : children)
            if (child->isVisible())
                flexBox.items.add (child->getFlexItem());

        auto overall = clientBounds;
        flexBox.performLayout (overall);

        if (scrollMode != ScrollMode::NoScroll)
        {
            // check sizes (a collapsed child keeps stale bounds - skip it, or
            // the scrolled extent would still reserve the space it gave up)
            for (auto& child : children)
                if (child->isVisible())
                    overall = overall.getUnion (child->getBounds());

            containerBox.setBounds (overall);

            if (scrollMode == ScrollMode::ScrollHorizontal && viewport.isHorizontalScrollBarShown())
                overall.removeFromBottom (viewport.getScrollBarThickness());
            else if (scrollMode == ScrollMode::ScrollVertical && viewport.isVerticalScrollBarShown())
                overall.removeFromRight (viewport.getScrollBarThickness());

            flexBox.performLayout (overall);
        }

        containerBox.setBounds (overall);
    }
    else if (layout == LayoutType::Tabbed)
    {
        if (tabbedButtons) {
            containerBox.setBounds(clientBounds);
            updateTabbedButtons();
            tabbedButtons->setBounds(clientBounds.removeFromTop (tabbarHeight));
        }

        for (auto& child : children)
            child->setBounds (clientBounds);
    }
    else // layout == Layout::Contents
    {
        containerBox.setBounds (clientBounds);

        for (auto& child : children)
            child->setBounds (child->resolvePosition (clientBounds));
    }

    for (auto& child : children)
        child->updateLayout();
}

void Container::updateColours()
{
    decorator.updateColours (magicBuilder, configNode);

    for (auto& child : children)
        child->updateColours();

    repaint();
}

void Container::updateContinuousRedraw()
{
    stopTimer();
    plotComponents.clear();
    audioPlotComponents.clear();

    for (auto& child : children)
    {
        if (auto* p = dynamic_cast<MagicPlotComponent*>(child->getWrappedComponent()))
            plotComponents.push_back (p);
        if (auto* p = dynamic_cast<MagicAudioPlotComponent*>(child->getWrappedComponent()))
            audioPlotComponents.push_back (p);
    }

    if (! plotComponents.empty() || ! audioPlotComponents.empty())
        startTimerHz (refreshRateHz);
}

void Container::updateTabbedButtons()
{
    tabbedButtons = std::make_unique<juce::TabbedButtonBar>(juce::TabbedButtonBar::TabsAtTop);
    containerBox.addAndMakeVisible (*tabbedButtons);

    for (auto& child : children)
    {
        tabbedButtons->addTab (child->getTabCaption ("Tab " + juce::String (tabbedButtons->getNumTabs())),
                               child->getTabColour(), -1);
    }

    tabbedButtons->addChangeListener (this);
    tabbedButtons->setCurrentTabIndex (currentTab.getValue(), false);
    updateSelectedTab();
}

void Container::configureFlexBox (const juce::ValueTree& node)
{
    flexBox = juce::FlexBox();

    const auto direction = magicBuilder.getStyleProperty (IDs::flexDirection, node).toString();
    if (direction == IDs::flexDirRow)
        flexBox.flexDirection = juce::FlexBox::Direction::row;
    else if (direction == IDs::flexDirRowReverse)
        flexBox.flexDirection = juce::FlexBox::Direction::rowReverse;
    else if (direction == IDs::flexDirColumn)
        flexBox.flexDirection = juce::FlexBox::Direction::column;
    else if (direction == IDs::flexDirColumnReverse)
        flexBox.flexDirection = juce::FlexBox::Direction::columnReverse;

    const auto wrap = magicBuilder.getStyleProperty (IDs::flexWrap, node).toString();
    if (wrap == IDs::flexWrapNormal)
        flexBox.flexWrap = juce::FlexBox::Wrap::wrap;
    else if (wrap == IDs::flexWrapReverse)
        flexBox.flexWrap = juce::FlexBox::Wrap::wrapReverse;
    else
        flexBox.flexWrap = juce::FlexBox::Wrap::noWrap;

    const auto alignContent = magicBuilder.getStyleProperty (IDs::flexAlignContent, node).toString();
    if (alignContent == IDs::flexStart)
        flexBox.alignContent = juce::FlexBox::AlignContent::flexStart;
    else if (alignContent == IDs::flexEnd)
        flexBox.alignContent = juce::FlexBox::AlignContent::flexEnd;
    else if (alignContent == IDs::flexCenter)
        flexBox.alignContent = juce::FlexBox::AlignContent::center;
    else if (alignContent == IDs::flexSpaceAround)
        flexBox.alignContent = juce::FlexBox::AlignContent::spaceAround;
    else if (alignContent == IDs::flexSpaceBetween)
        flexBox.alignContent = juce::FlexBox::AlignContent::spaceBetween;
    else
        flexBox.alignContent = juce::FlexBox::AlignContent::stretch;

    const auto alignItems = magicBuilder.getStyleProperty (IDs::flexAlignItems, node).toString();
    if (alignItems == IDs::flexStart)
        flexBox.alignItems = juce::FlexBox::AlignItems::flexStart;
    else if (alignItems == IDs::flexEnd)
        flexBox.alignItems = juce::FlexBox::AlignItems::flexEnd;
    else if (alignItems == IDs::flexCenter)
        flexBox.alignItems = juce::FlexBox::AlignItems::center;
    else
        flexBox.alignItems = juce::FlexBox::AlignItems::stretch;

    const auto justify = magicBuilder.getStyleProperty (IDs::flexJustifyContent, node).toString();
    if (justify == IDs::flexEnd)
        flexBox.justifyContent = juce::FlexBox::JustifyContent::flexEnd;
    else if (justify == IDs::flexCenter)
        flexBox.justifyContent = juce::FlexBox::JustifyContent::center;
    else if (justify == IDs::flexSpaceAround)
        flexBox.justifyContent = juce::FlexBox::JustifyContent::spaceAround;
    else if (justify == IDs::flexSpaceBetween)
        flexBox.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
    else
        flexBox.justifyContent = juce::FlexBox::JustifyContent::flexStart;
}

void Container::timerCallback()
{
    auto needsRepaint = false;

    for (auto p : plotComponents)
        if (p) needsRepaint |= p->needsUpdate();

    for (auto p : audioPlotComponents)
        if (p) needsRepaint |= p->needsUpdate();

    if (needsRepaint)
        containerBox.repaint();

}

void Container::changeListenerCallback (juce::ChangeBroadcaster*)
{
    currentTab = tabbedButtons ? tabbedButtons->getCurrentTabIndex() : 0;
    updateSelectedTab();
}

void Container::valueChanged (juce::Value& source)
{
    if (source == currentTab)
      updateSelectedTab();

    // BEGIN JOS: chain to the base, which owns the `visibility=` property
    // binding.  Without this a <View> bound to a PROPERTY never hid - the
    // override swallowed its own visibility notifications.
    GuiItem::valueChanged (source);
    // END JOS
}

void Container::updateSelectedTab()
{
    int index = 0;
    for (auto& child : children)
        child->setVisible (currentTab == index++);
}

std::vector<std::unique_ptr<GuiItem>>::iterator Container::begin()
{
    return children.begin();
}

std::vector<std::unique_ptr<GuiItem>>::iterator Container::end()
{
    return children.end();
}

#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE
void Container::setEditMode (bool shouldEdit)
{
    for (auto& child : children)
        child->setEditMode (shouldEdit);

    GuiItem::setEditMode (shouldEdit);
}
#endif

//==============================================================================

Container::Scroller::Scroller (Container& ownerToUse)
: owner (ownerToUse) {}

void Container::Scroller::paint (juce::Graphics& g)
{
    auto b = owner.getClientBounds();
    owner.decorator.drawDecorator (g, {-b.getX(), -b.getY(), owner.getWidth(), owner.getHeight()});
}

void Container::Scroller::setBackgroundColour (juce::Colour colour)
{
    backgroundColour = colour;
    setOpaque (backgroundColour.isOpaque());
}

} // namespace foleys
