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

#include "foleys_MagicGUIBuilder.h"
#include "../Layout/foleys_RootItem.h"
#include "../Layout/foleys_Container.h"
#include "../Helpers/foleys_DefaultGuiTrees.h"
#include "../LookAndFeels/foleys_JuceLookAndFeels.h"
#include "../LookAndFeels/foleys_LookAndFeel.h"
#include "../LookAndFeels/foleys_Skeuomorphic.h"

#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE
#include "../Editor/foleys_ToolBox.h"
#endif

namespace foleys
{


MagicGUIBuilder::MagicGUIBuilder (MagicGUIState& state)
  : magicState (state)
{
    updateStylesheet();
    getConfigTree().addListener (this);
}

MagicGUIBuilder::~MagicGUIBuilder()
{
    // BEGIN JOS (from Nick, shared/JUCE 20eaba3a85): THE ITEM TREES GO FIRST,
    // before any member is destroyed.  Every GuiItem calls
    // stylesheet.removeListener (this) from ~GuiItem, and members die in the
    // reverse of their declaration order.  Here `root` and `parkedViews` (the
    // view cache) are declared AFTER `stylesheet`, so they already die first -
    // but only by that accident of position.  Nick's copy declares `root`
    // above `stylesheet`, and there the stylesheet (with the ListenerList in
    // its ValueTree) was freed before the items asked to leave it: a
    // heap-use-after-free on every editor teardown, found by his sanitizer
    // lane.  Resetting here states the order instead of leaving it to
    // declaration position.
    root.reset();
    parkedViews.clear();
    // END JOS

    getConfigTree().removeListener (this);
}

Stylesheet& MagicGUIBuilder::getStylesheet()
{
    return stylesheet;
}

juce::ValueTree& MagicGUIBuilder::getConfigTree()
{
    return magicState.getGuiTree();
}

juce::ValueTree MagicGUIBuilder::getGuiRootNode()
{
    return getConfigTree().getOrCreateChildWithName (IDs::view, &undo);
}

std::unique_ptr<GuiItem> MagicGUIBuilder::createGuiItem (const juce::ValueTree& node)
{
    if (node.getType() == IDs::view)
    {
        auto item = (node == getGuiRootNode()) ? std::make_unique<RootItem>(*this, node)
                                               : std::make_unique<Container>(*this, node);
        item->updateInternal();
        item->createSubComponents();
        return item;
    }

    auto factory = factories.find (node.getType());
    if (factory != factories.end())
    {
        auto item = factory->second (*this, node);
        item->updateInternal();
        return item;
    }

    DBG ("No factory for: " << node.getType().toString());
    return {};
}

void MagicGUIBuilder::updateStylesheet()
{
    auto stylesNode = getConfigTree().getOrCreateChildWithName (IDs::styles, &undo);
    if (stylesNode.getNumChildren() == 0)
        stylesNode.appendChild (DefaultGuiTrees::createDefaultStylesheet(), &undo);

    auto selectedName = stylesNode.getProperty (IDs::selected, {}).toString();
    if (selectedName.isNotEmpty())
    {
        auto style = stylesNode.getChildWithProperty (IDs::name, selectedName);
        stylesheet.setStyle (style);
    }
    else
    {
        stylesheet.setStyle (stylesNode.getChild (0));
    }

    stylesheet.updateStyleClasses();
    stylesheet.updateValidRanges();
}

void MagicGUIBuilder::clearGUI()
{
    auto guiNode = getConfigTree().getOrCreateChildWithName (IDs::view, &undo);
    guiNode.removeAllChildren (&undo);
    guiNode.removeAllProperties (&undo);

    updateComponents();
}

void MagicGUIBuilder::showOverlayDialog (std::unique_ptr<juce::Component> dialog)
{
    if (parent == nullptr)
        return;

    overlayDialog = std::move (dialog);
    parent->addAndMakeVisible (overlayDialog.get());

    updateLayout();
}

void MagicGUIBuilder::closeOverlayDialog()
{
    overlayDialog.reset();
}

void MagicGUIBuilder::createGUI (juce::Component& parentToUse)
{
    parent = &parentToUse;

    updateComponents();

#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE
    if (magicToolBox.get() != nullptr)
        magicToolBox->stateWasReloaded();
#endif
}

void MagicGUIBuilder::updateComponents()
{
    if (parent == nullptr)
        return;

    updateStylesheet();

    // BEGIN JOS: THE VIEW CACHE (2026-09-05).  See setViewCacheEnabled() in the
    // header for what this is and why the key is the node's IDENTITY.
    //
    // With the cache off this is exactly the line it always was -- one
    // statement, the old root destroyed by the assignment after the new one is
    // built -- so a build that does not ask for the cache is bit-for-bit the
    // old behaviour.
    const auto incomingRootNode = getGuiRootNode();

    if (viewCacheEnabled)
    {
        viewWasCached = false;

        if (incomingRootNode.isValid() && incomingRootNode == builtRootNode)
        {
            // THE SAME NODE, ASKED AGAIN.  clearGUI() empties the <View> in
            // place and then calls us; an explicit updateComponents() after a
            // tree edit means the same thing.  The contents changed under the
            // components that were built from them, so rebuild -- and drop any
            // parked copy of this node, which is stale for the same reason.
            dropParkedView (incomingRootNode);
            root.reset();
        }
        else
        {
            // PARK the outgoing view.  removeChildComponent, not setVisible:
            // a root with no parent has no peer, so isShowing() is false all
            // the way down and every self-polling GuiItem stands down.
            if (root != nullptr && builtRootNode.isValid())
            {
                parent->removeChildComponent (root.get());
                parkedViews.emplace_back (builtRootNode, std::move (root));
            }

            root = takeParkedView (incomingRootNode);   // nullptr = never built
            viewWasCached = (root != nullptr);
        }

        if (root == nullptr)
            root = createGuiItem (incomingRootNode);
    }
    else
    {
        root = createGuiItem (incomingRootNode);
    }

    builtRootNode = incomingRootNode;
    // END JOS

    parent->addAndMakeVisible (root.get());

    root->setBounds (parent->getLocalBounds());

#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE
    if (root.get() != nullptr)
        root->setEditMode (editMode);
#endif
}

// BEGIN JOS: the view cache's own small methods.
void MagicGUIBuilder::setViewCacheEnabled (bool shouldCacheViews)
{
    if (viewCacheEnabled == shouldCacheViews)
        return;

    viewCacheEnabled = shouldCacheViews;

    // Turning it OFF must not leave components parked forever: they hold
    // parameter attachments and would keep answering long after anybody could
    // see them.
    if (! viewCacheEnabled)
        clearViewCache();
}

bool MagicGUIBuilder::isViewCacheEnabled() const { return viewCacheEnabled; }

int MagicGUIBuilder::getNumCachedViews() const { return (int) parkedViews.size(); }

bool MagicGUIBuilder::lastUpdateWasCached() const { return viewWasCached; }

void MagicGUIBuilder::clearViewCache()
{
    parkedViews.clear();
}

std::unique_ptr<GuiItem> MagicGUIBuilder::takeParkedView (const juce::ValueTree& node)
{
    if (! node.isValid())
        return {};

    for (auto it = parkedViews.begin(); it != parkedViews.end(); ++it)
    {
        if (it->first == node)
        {
            auto item = std::move (it->second);
            parkedViews.erase (it);
            return item;
        }
    }

    return {};
}

void MagicGUIBuilder::dropParkedView (const juce::ValueTree& node)
{
    if (! node.isValid())
        return;

    for (auto it = parkedViews.begin(); it != parkedViews.end(); ++it)
    {
        if (it->first == node)
        {
            parkedViews.erase (it);
            return;
        }
    }
}
// END JOS

void MagicGUIBuilder::updateLayout()
{
    if (parent == nullptr)
        return;

    if (root.get() != nullptr)
    {
        if (! stylesheet.setMediaSize (parent->getWidth(), parent->getHeight()))
        {
            stylesheet.updateValidRanges();
            root->updateInternal();
        }

        if (root->getBounds() == parent->getLocalBounds())
            root->updateLayout();
        else
            root->setBounds (parent->getLocalBounds());
    }

    if (overlayDialog)
    {
        if (overlayDialog->getBounds() == parent->getLocalBounds())
            overlayDialog->resized();
        else
            overlayDialog->setBounds (parent->getLocalBounds());
    }

    parent->repaint();
}

void MagicGUIBuilder::updateColours()
{
    if (root)
        root->updateColours();
}

GuiItem* MagicGUIBuilder::findGuiItemWithId (const juce::String& name)
{
    if (root)
        return root->findGuiItemWithId (name);

    return nullptr;
}

GuiItem* MagicGUIBuilder::findGuiItem (const juce::ValueTree& node)
{
    if (node.isValid() && root)
        return root->findGuiItem (node);

    return nullptr;
}

void MagicGUIBuilder::registerFactory (juce::Identifier type, std::unique_ptr<GuiItem>(*factory)(MagicGUIBuilder& builder, const juce::ValueTree&))
{
    if (factories.find (type) != factories.cend())
    {
        // You tried to add two factories with the same type name!
        // That cannot work, the second factory will be ignored.
        jassertfalse;
        return;
    }

    factories [type] = factory;
}

juce::StringArray MagicGUIBuilder::getFactoryNames() const
{
    juce::StringArray names { IDs::view.toString() };

    names.ensureStorageAllocated (int (factories.size()));
    for (const auto& f : factories)
        names.add (f.first.toString());

    return names;
}

// JOS: fallback tooltips by parameter ID (see header)
void MagicGUIBuilder::setTooltipProvider (TooltipProvider provider)
{
    tooltipProvider = std::move (provider);
}

const MagicGUIBuilder::TooltipProvider& MagicGUIBuilder::getTooltipProvider() const
{
    return tooltipProvider;
}

void MagicGUIBuilder::registerLookAndFeel (juce::String name, std::unique_ptr<juce::LookAndFeel> lookAndFeel)
{
    stylesheet.registerLookAndFeel (name, std::move (lookAndFeel));
}

void MagicGUIBuilder::registerJUCELookAndFeels()
{
    stylesheet.registerLookAndFeel ("LookAndFeel_V1", std::make_unique<juce::LookAndFeel_V1>());
    stylesheet.registerLookAndFeel ("LookAndFeel_V2", std::make_unique<JuceLookAndFeel_V2>());
    stylesheet.registerLookAndFeel ("LookAndFeel_V3", std::make_unique<JuceLookAndFeel_V3>());
    stylesheet.registerLookAndFeel ("LookAndFeel_V4", std::make_unique<JuceLookAndFeel_V4>());
    stylesheet.registerLookAndFeel ("FoleysFinest", std::make_unique<LookAndFeel>());
    stylesheet.registerLookAndFeel ("Skeuomorphic", std::make_unique<Skeuomorphic>());
}

juce::var MagicGUIBuilder::getStyleProperty (const juce::Identifier& name, const juce::ValueTree& node) const
{
    return stylesheet.getStyleProperty (name, node);
}

void MagicGUIBuilder::removeStyleClassReferences (juce::ValueTree tree, const juce::String& name)
{
    if (tree.hasProperty (IDs::styleClass))
    {
        const auto separator = " ";
        auto strings = juce::StringArray::fromTokens (tree.getProperty (IDs::styleClass).toString(), separator, "");
        strings.removeEmptyStrings (true);
        strings.removeString (name);
        tree.setProperty (IDs::styleClass, strings.joinIntoString (separator), &undo);
    }

    for (auto child : tree)
        removeStyleClassReferences (child, name);
}

juce::StringArray MagicGUIBuilder::getColourNames (juce::Identifier type)
{
    juce::ValueTree node (type);
    if (auto item = createGuiItem (node))
        return item->getColourNames();

    return {};
}

std::function<void(juce::ComboBox&)> MagicGUIBuilder::createChoicesMenuLambda (juce::StringArray choices) const
{
    return [choices](juce::ComboBox& combo)
    {
        int index = 0;
        for (auto& choice : choices)
            combo.addItem (choice, ++index);
    };
}

std::function<void(juce::ComboBox&)> MagicGUIBuilder::createParameterMenuLambda() const
{
    return [this](juce::ComboBox& combo)
    {
        *combo.getRootMenu() = magicState.createParameterMenu();
    };
}

std::function<void(juce::ComboBox&)> MagicGUIBuilder::createPropertiesMenuLambda() const
{
    return [this](juce::ComboBox& combo)
    {
        magicState.populatePropertiesMenu (combo);
    };
}

std::function<void(juce::ComboBox&)> MagicGUIBuilder::createTriggerMenuLambda() const
{
    return [this](juce::ComboBox& combo)
    {
        *combo.getRootMenu() = magicState.createTriggerMenu();
    };
}

juce::var MagicGUIBuilder::getPropertyDefaultValue (juce::Identifier property) const
{
    // flexbox
    if (property == IDs::flexDirection) return IDs::flexDirRow;
    if (property == IDs::flexWrap)      return IDs::flexNoWrap;
    if (property == IDs::flexAlignContent) return IDs::flexStretch;
    if (property == IDs::flexAlignItems) return IDs::flexStretch;
    if (property == IDs::flexJustifyContent) return IDs::flexStart;
    if (property == IDs::flexAlignSelf) return IDs::flexStretch;
    if (property == IDs::flexOrder) return 0;
    if (property == IDs::flexGrow) return 1.0;
    if (property == IDs::flexShrink) return 1.0;
    if (property == IDs::minWidth) return 0.0;
    if (property == IDs::minHeight) return 0.0;
    if (property == IDs::display) return IDs::flexbox;

    if (property == IDs::captionPlacement) return "centred-top";
    // BEGIN JOS: no fake per-node lookAndFeel default -- unset means "inherit
    // from the parent component"; the ROOT GuiItem falls back to FoleysFinest
    // (see GuiItem::updateInternal). The old blanket default made every item
    // set its own LnF, so one set on the root could never cascade, and the GUI
    // editor showed "FoleysFinest" as if explicitly set on every node.
    // END JOS.

    if (property == juce::Identifier ("font-size")) return 12.0;

    return {};
}

#if JOS_ALLOW_RADIO_BUTTONS == 1 // JOS temp workaround
RadioButtonManager& MagicGUIBuilder::getRadioButtonManager()
{
    return radioButtonManager;
}
#endif

void MagicGUIBuilder::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (root.get() != nullptr)
        root->updateInternal();

    updateLayout();
}

void MagicGUIBuilder::valueTreeRedirected (juce::ValueTree& treeWhichHasBeenChanged)
{
    juce::ignoreUnused (treeWhichHasBeenChanged);
    updateComponents();
}

MagicGUIState& MagicGUIBuilder::getMagicState()
{
    return magicState;
}

juce::UndoManager& MagicGUIBuilder::getUndoManager()
{
    return undo;
}

#if FOLEYS_SHOW_GUI_EDITOR_PALLETTE

void MagicGUIBuilder::setEditMode (bool shouldEdit)
{
    editMode = shouldEdit;

    if (parent == nullptr)
        return;

    if (root.get() != nullptr)
        root->setEditMode (shouldEdit);

    if (shouldEdit == false)
        setSelectedNode (juce::ValueTree());

    parent->repaint();
}

bool MagicGUIBuilder::isEditModeOn() const
{
    return editMode;
}

void MagicGUIBuilder::setSelectedNode (const juce::ValueTree& node)
{
    if (selectedNode != node)
    {
        if (auto* item = findGuiItem (selectedNode))
            item->setDraggable (false);

        selectedNode = node;
        if (magicToolBox.get() != nullptr)
            magicToolBox->setSelectedNode (selectedNode);

        if (auto* item = findGuiItem (selectedNode))
            item->setDraggable (true);

        if (parent != nullptr)
            parent->repaint();
    }
}

const juce::ValueTree& MagicGUIBuilder::getSelectedNode() const
{
    return selectedNode;
}

void MagicGUIBuilder::draggedItemOnto (juce::ValueTree dragged, juce::ValueTree target, int index)
{
    if (dragged == target)
        return;

    undo.beginNewTransaction();

    auto targetParent  = target.getParent();
    auto draggedParent = dragged.getParent();

    if (draggedParent.isValid())
        draggedParent.removeChild (dragged, &undo);

    // BEGIN JOS (from Nick, shared/JUCE 2ff13a55f7): the default index is
    // worked out PER BRANCH.  Stock foleys computed it once, as the TARGET's
    // position inside ITS parent, and then used it for a drop ONTO a View too
    // - so an item dropped on a View that is its parent's first child landed
    // at child 0 of that View (under everything drawn after it) instead of
    // being appended.
    if (target.getType() == IDs::view)
    {
        // Dropped ONTO a View: index < 0 appends (ValueTree::addChild).
        target.addChild (dragged, index, &undo);
    }
    else
    {
        // Dropped onto a sibling item: insert AT that item's position.
        if (targetParent.isValid() && index < 0)
            index = targetParent.indexOf (target);
        targetParent.addChild (dragged, index, &undo);
    }
    // END JOS
}

void MagicGUIBuilder::attachToolboxToWindow (juce::Component& window)
{
    juce::Component::SafePointer<juce::Component> reference (&window);

    juce::MessageManager::callAsync ([&, reference]
                                     {
                                         if (reference != nullptr)
                                         {
                                             magicToolBox = std::make_unique<ToolBox>(reference->getTopLevelComponent(), *this);
                                             magicToolBox->setLastLocation (magicState.getResourcesFolder());
                                         }
                                     });
}

ToolBox& MagicGUIBuilder::getMagicToolBox()
{
    // The magicToolBox should always be present!
    // This method wouldn't be included, if
    // FOLEYS_SHOW_GUI_EDITOR_PALLETTE was 0
    jassert (magicToolBox.get() != nullptr);

    return *magicToolBox;
}

#endif


} // namespace foleys
